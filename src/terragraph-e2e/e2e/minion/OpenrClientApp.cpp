/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "OpenrClientApp.h"

#include <chrono>
#include <regex>

#include <folly/Conv.h>
#include <folly/dynamic.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/json.h>
#include <folly/MapUtil.h>
#include <openr/common/Constants.h>
#include <openr/common/Util.h>
#include <openr/common/NetworkUtil.h>
#include <openr/if/gen-cpp2/KvStore_constants.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "SharedObjects.h"
#include "e2e/common/IpUtil.h"
#include "e2e/common/OpenrUtils.h"
#include "e2e/common/SysUtils.h"

using namespace fbzmq;
using apache::thrift::detail::TEnumMapFactory;

// KvStore sync mynetworkinfo entries
DEFINE_bool(
    disable_network_info_sync,
    false,
    "Disable syncing mynetworkinfo with KvStore");
DEFINE_int32(
    network_info_sync_interval_s,
    10,
    "The network info sync interval (in seconds)");
DEFINE_string(
    kvstore_ctrl_url_key, "e2e-ctrl-url", "E2E controller URL key");
DEFINE_string(
    kvstore_ctrl_url_backup_key,
    "e2e-ctrl-url-backup",
    "E2E controller backup URL key");
DEFINE_string(
    kvstore_aggr_url_key, "e2e-aggr-url", "Aggregator URL key");
DEFINE_string(
    kvstore_network_prefix_key, "e2e-network-prefix", "Network prefix key");

// LinkMonitor sync
DEFINE_int32(
    link_monitor_sync_interval_s,
    60,
    "The LinkMonitor sync interval (in seconds)");

// Deprecated prefix sync
DEFINE_int32(
    deprecated_prefix_sync_interval_s,
    5,
    "Add backwards-compatible prefix sync interval (in seconds)");

// Inject KvStore keys interval
DEFINE_int32(
    inject_kvstore_keys_interval_s,
    10,
    "The interval at which to inject KvStore keys from config (in seconds)");

namespace {
// The link metric to use when soft draining an interface
const int32_t kLinkMetricSoftDrained{100000};

// Commands
const std::string kRestartKeaCmd{"sv restart kea"};
const std::string kUpdateFirewallCmd{"update_firewall"};

// TTL for the static prefixes (openr::Constants::kStaticPrefixAllocParamKey)
// in milliseconds
const int64_t kStaticPrefixTtl{300000};

// Terragraph E2E only uses kDefaultArea
const std::string kDefaultArea{openr::Constants::kDefaultArea};

// String used to identify keys in KvStore injected by e2e-minion
const std::string kMinionOriginatorSuffix{":minion"};
}

namespace facebook {
namespace terragraph {
namespace minion {

OpenrClientApp::OpenrClientApp(
    fbzmq::Context& zmqContext,
    const std::string& brokerRouterUrl,
    const std::string& monitorSockUrl,
    const std::string& macAddr,
    const std::string& myNetworkInfoFile)
    : MinionApp(
          zmqContext,
          brokerRouterUrl,
          monitorSockUrl,
          macAddr,
          E2EConsts::kOpenrClientAppMinionId),
      myNetworkInfoFile_{myNetworkInfoFile} {
  // If Open/R is disabled, then disable most of this class's functionality
  openrEnabled_ = SharedObjects::getNodeConfigWrapper()->rlock()
      ->getEnvConfig()->OPENR_ENABLED_ref().value_or("") == "1";
  if (!openrEnabled_) {
    VLOG(2) << "Open/R is disabled!";
    return;
  }

  // Create periodic timer to sync mynetworkinfo with KvStore
  if (!FLAGS_disable_network_info_sync) {
    networkInfoSyncTimer_ = ZmqTimeout::make(
        this, [&]() noexcept { networkInfoSync(); });
    networkInfoSyncTimer_->scheduleTimeout(
        std::chrono::seconds(FLAGS_network_info_sync_interval_s),
        true /* periodic */);
    scheduleTimeout(std::chrono::milliseconds::zero(), [&]() noexcept {
      networkInfoSync();  // sync network info with KvStore immediately
    });
  }

  // Create periodic timer to sync with LinkMonitor
  linkMonitorSyncTimer_ = ZmqTimeout::make(
      this, [&]() noexcept { linkMonitorSync(); });
  linkMonitorSyncTimer_->scheduleTimeout(
      std::chrono::seconds(FLAGS_link_monitor_sync_interval_s),
      true /* periodic */);
  scheduleTimeout(std::chrono::milliseconds::zero(), [&]() noexcept {
    linkMonitorSync();  // sync with LinkMonitor immediately
  });

  // XXX HACK: Create periodic timer to check for deprecated prefix keys for
  // compatibility with versions before M80
  auto enablePrefixSync =
      SharedObjects::getNodeConfigWrapper()->rlock()
      ->getEnvConfig()->OPENR_ENABLE_DEPRECATED_PREFIXES_ref().value_or("0");

  if (enablePrefixSync == "1") {
    deprecatedPrefixSyncTimer_ = ZmqTimeout::make(
        this, [&]() noexcept { deprecatedPrefixSync(); });
    deprecatedPrefixSyncTimer_->scheduleTimeout(
        std::chrono::seconds(FLAGS_deprecated_prefix_sync_interval_s),
        true /* periodic */);
  }

  // Create periodic timer to inject KvStore keys from config in case they are
  // modified/deleted from KvStore
  injectKvStoreKeysTimer_ = ZmqTimeout::make(
      this, [&]() noexcept { injectKvStoreKeys(); });
  injectKvStoreKeysTimer_->scheduleTimeout(
      std::chrono::seconds(FLAGS_inject_kvstore_keys_interval_s),
      true /* periodic */);
}

void
OpenrClientApp::initOpenrCtrlClient() {
  // Do not create new client if one exists already
  if (openrCtrlClient_) {
    return;
  }

  try {
    openrCtrlClient_ = openr::getOpenrCtrlPlainTextClient(
        evb_, folly::IPAddress("::1"));
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Failed to connect to Open/R. Exception: "
               << folly::exceptionStr(ex);
    openrCtrlClient_ = nullptr;
  }
}

void
OpenrClientApp::processMessage(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::GET_ROUTING_ADJACENCIES:
      processGetRoutingAdjacencies(senderApp);
      break;
    case thrift::MessageType::SET_LINK_METRIC:
      processSetLinkMetric(senderApp, message);
      break;
    case thrift::MessageType::SYNC_LINK_MONITOR:
      processSyncLinkMonitor();
      break;
    case thrift::MessageType::INJECT_KVSTORE_KEYS:
      processInjectKvstoreKeys();
      break;
    case thrift::MessageType::SET_PREFIXES:
      processSetPrefixes(senderApp, message);
      break;
    case thrift::MessageType::FW_ADJ_REQ:
      processFwAdjReq(senderApp, message);
      break;
    default:
      LOG(ERROR)
          << "Wrong type of message ("
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                     message.mType, "UNKNOWN")
          << ") received from " << senderApp;
  }
}

void
OpenrClientApp::processGetRoutingAdjacencies(const std::string& senderApp) {
  VLOG(2) << "Received request for routing adjacencies from " << senderApp;
  thrift::RoutingAdjacencies adj;

  if (openrEnabled_) {
    // Fetch all AdjacencyDatabase entries from KvStore
    for (const auto& kv : kvStoreDumpKeys(
          openr::Constants::kAdjDbMarker.toString())) {
      // Parse AdjacencyDatabase struct
      openr::thrift::AdjacencyDatabase db;
      try {
        db = fbzmq::util::readThriftObjStr<openr::thrift::AdjacencyDatabase>(
            kv.second, serializer_);
      } catch (std::exception const& e) {
        LOG(ERROR) << "Failed to deserialize AdjacencyDatabase: "
                   << folly::exceptionStr(e);
        continue;
      }

      adj.adjacencyMap[db.thisNodeName_ref().value()] = db;
    }

    // Fetch all PrefixDatabase entries from KvStore
    for (const auto& kv : kvStoreDumpKeys(
          openr::Constants::kPrefixDbMarker.toString())) {
      // Parse PrefixDatabase struct
      openr::thrift::PrefixDatabase db;
      try {
        db = fbzmq::util::readThriftObjStr<openr::thrift::PrefixDatabase>(
            kv.second, serializer_);
      } catch (std::exception const& e) {
        LOG(ERROR) << "Failed to deserialize PrefixDatabase: "
                   << folly::exceptionStr(e);
        continue;
      }
      auto nodeName = db.thisNodeName_ref().value();

      auto iter = adj.prefixMap.find(nodeName);
      if (iter == adj.prefixMap.end()) {
        adj.prefixMap[nodeName] = db;
      } else {
        auto newEntries = db.prefixEntries_ref().value();
        std::copy(newEntries.begin(), newEntries.end(),
            std::back_inserter(
            iter->second.prefixEntries_ref().value()));
      }
    }

    // e2e-network-prefix
    auto networkInfo = createNetworkInfo();
    if (networkInfo) {
      adj.network = networkInfo->network;
    }
  }

  // Send to controller
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::ROUTING_ADJACENCIES,
      adj,
      true /* compress */);
}

void
OpenrClientApp::processSetLinkMetric(
    const std::string& senderApp, const thrift::Message& message) {
  VLOG(2) << "Received request to set link metrics from " << senderApp;
  auto request = maybeReadThrift<thrift::SetLinkMetric>(message);
  if (!request) {
    handleInvalidMessage("SetLinkMetric", senderApp);
    return;
  }
  if (!openrEnabled_) {
    VLOG(3) << "Dropping SetLinkMetric request (Open/R is disabled)";
    return;
  }

  // Store the new metrics
  for (const auto& kv : request->linkMetricMap) {
    if (kv.second > 0) {
      linkMetricMap_[kv.first] = kv.second;
    } else {
      linkMetricMap_.erase(kv.first);
    }
  }

  // Sync with LinkMonitor
  linkMonitorSync();
}

void
OpenrClientApp::processSyncLinkMonitor() {
  linkMonitorSync();
}

void
OpenrClientApp::processInjectKvstoreKeys() {
  injectKvStoreKeys();
}

void
OpenrClientApp::processFwAdjReq(
    const std::string& senderApp, const thrift::Message& message) {
  VLOG(3) << "Received FW_ADJ_REQ from " << senderApp;
  const auto& driverMsg = maybeReadThrift<thrift::DriverMessage>(message);
  if (!driverMsg) {
    LOG(ERROR) << "Failed to unwrap message of type: "
               << folly::get_default(
                  TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                  message.mType,
                  "UNKNOWN");
    return;
  }

  std::unordered_map<std::string, std::string> nodeIfaceMap;
  if (openrEnabled_) {
    // Get local adj info from KvStore
    const auto& db = getRoutingAdjacencies();
    if (!db) {
      LOG(ERROR) << "Failed to fetch adjacency database from Open/R";
      return;
    }

    // Construct neighbor MAC -> interface mapping
    // NOTE: Before forwarding, PassThru.cpp filters in only "nic" interfaces
    for (const auto& adj : db->adjacencies_ref().value()) {
      const std::string& mac =
          OpenrUtils::fromOpenrNodeName(adj.otherNodeName_ref().value());
      if (mac != driverMsg->radioMac) {
        nodeIfaceMap[mac] = adj.ifName_ref().value();
      }
    }
  }

  // Add local radio MACs
  auto radioMacToBusId =
      SharedObjects::getNodeInfoWrapper()->rlock()->getRadioMacToBusId();
  for (const auto& pair : radioMacToBusId) {
    if (pair.first != driverMsg->radioMac) {
      // Set empty interface to get accepted by "nic" filter in PassThru.cpp
      nodeIfaceMap[pair.first] = "";
    }
  }

  thrift::FwAdjResp fwAdjResp;
  fwAdjResp.adjs = std::move(nodeIfaceMap);
  sendToDriverApp(
      driverMsg->radioMac,
      thrift::MessageType::FW_ADJ_RESP,
      fwAdjResp);
}

void
OpenrClientApp::injectKvStoreKeys() {
  if (!openrEnabled_) {
    VLOG(3) << "Not injecting KvStore keys (Open/R is disabled)";
    return;
  }

  // Get KvStore key-value pairs from config
  std::map<std::string, std::string> kvstoreParams;
  {
    auto lockedNodeConfigW = SharedObjects::getNodeConfigWrapper()->rlock();
    kvstoreParams = *lockedNodeConfigW->getKvstoreParams();
  }

  // Set all keys (if necessary)
  for (const auto& kv : kvstoreParams) {
    std::string key = kv.first;
    std::string value = kv.second;

    // TODO: Delete this check after some time, once the default empty values
    // have been removed from deployed configs.
    if (value.empty()) {
      continue;
    }

    // Check against current value
    if (checkKvPairExists(key, value)) {
      continue;
    }

    auto keySetParams = makeKeySetParams(key,
        OpenrUtils::toOpenrNodeName(macAddr_) + kMinionOriginatorSuffix,
        value.c_str());

    // Set new value
    initOpenrCtrlClient();
    try {
      openrCtrlClient_->sync_setKvStoreKeyVals(keySetParams, kDefaultArea);
      LOG(INFO) << "Set KvStore key " << key << " = " << value;
      eventClient_->logEventDynamic(
          thrift::EventCategory::OPENR,
          thrift::EventId::OPENR_KVSTORE_MODIFIED,
          thrift::EventLevel::INFO,
          folly::sformat("Set key '{}' in KvStore", key),
          folly::dynamic::object("key", key)("value", value));
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Error setting KvStore key " << key << " to "
                 << value << ". Exception: " << folly::exceptionStr(ex);
    }
  }
}

void
OpenrClientApp::networkInfoSync() {
  // Read values from KvStore
  std::optional<thrift::NetworkInfo> networkInfo = createNetworkInfo();
  if (!networkInfo) {
    LOG(ERROR) << "Failed to generate new NetworkInfo, skipping sync...";
    return;
  }

  // Compare with existing mynetworkinfo file
  std::optional<thrift::NetworkInfo> oldNetworkInfo = readMyNetworkInfoFile();
  if (!oldNetworkInfo ||
      (*oldNetworkInfo != *networkInfo &&
       *networkInfo != thrift::NetworkInfo())) {
    // Overwrite file if values changed
    writeMyNetworkInfoFile(*networkInfo);

    // Also Restart Kea(dhcpd)
    LOG(INFO) << "Restarting kea(dhcpd)...";
    SysUtils::system(kRestartKeaCmd);

    // Update firewall rules with new network config
    LOG(INFO) << "Updating firewall";
    SysUtils::system(kUpdateFirewallCmd);
  }
}

std::optional<thrift::NetworkInfo>
OpenrClientApp::createNetworkInfo() {
  thrift::NetworkInfo networkInfo;

  // e2e-ctrl-url
  auto ctrlUrl_expected =
      kvStoreGetValueString(FLAGS_kvstore_ctrl_url_key);
  if (ctrlUrl_expected.hasValue()) {
    auto ctrlUrl = ctrlUrl_expected.value();
    if (IpUtil::isValidZmqUri(ctrlUrl)) {
      networkInfo.e2eCtrlUrl = ctrlUrl;
    }
  } else if (ctrlUrl_expected.error() ==
        OpenrClientApp::KvStoreError::CONNECTION_ERROR) {
    return std::nullopt;
  }

  // e2e-ctrl-url-backup
  auto ctrlUrlBackup_expected =
      kvStoreGetValueString(FLAGS_kvstore_ctrl_url_backup_key);
  if (ctrlUrlBackup_expected.hasValue()) {
    auto ctrlUrlBackup = ctrlUrlBackup_expected.value();
    if (IpUtil::isValidZmqUri(ctrlUrlBackup)) {
      networkInfo.e2eCtrlUrlBackup = ctrlUrlBackup;
    }
  } else if (ctrlUrlBackup_expected.error() ==
                 OpenrClientApp::KvStoreError::CONNECTION_ERROR) {
    return std::nullopt;
  }

  // e2e-aggr-url
  for (const auto& kv : kvStoreDumpKeys(FLAGS_kvstore_aggr_url_key)) {
    if (IpUtil::isValidZmqUri(kv.second)) {
      networkInfo.aggrUrl.push_back(kv.second);
    }
  }

  // e2e-network-prefix
  // seed-prefix is of the form "fc00:cafe:babe::/56,64"
  auto networkPrefix_expected =
      kvStoreGetValueString(FLAGS_kvstore_network_prefix_key);
  if (networkPrefix_expected.hasValue()) {
    auto networkPrefix = networkPrefix_expected.value();
    size_t idx = networkPrefix.find(',');
    if (idx != std::string::npos) {
      networkInfo.network = networkPrefix.substr(0, idx);
    }
  } else if (networkPrefix_expected.error() ==
                 OpenrClientApp::KvStoreError::CONNECTION_ERROR) {
    return std::nullopt;
  }

  return networkInfo;
}

std::optional<thrift::NetworkInfo>
OpenrClientApp::readMyNetworkInfoFile() {
  // Read file
  std::string contents;
  if (!folly::readFile(myNetworkInfoFile_.c_str(), contents)) {
    return std::nullopt;
  }

  // Deserialize from JSON
  try {
    return apache::thrift::SimpleJSONSerializer::deserialize<
        thrift::NetworkInfo>(contents);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Failed to deserialize NetworkInfo from file ("
               << myNetworkInfoFile_ << "): " << folly::exceptionStr(ex);
    return std::nullopt;
  }
}

bool
OpenrClientApp::writeMyNetworkInfoFile(
    const thrift::NetworkInfo& networkInfo) {
  // Serialize to JSON
  std::string json = apache::thrift::SimpleJSONSerializer::serialize<
      std::string>(networkInfo);

  // Pretty-print JSON
  folly::json::serialization_opts opts;
  opts.pretty_formatting = true;
  try {
    json = folly::json::serialize(folly::parseJson(json), opts);
  } catch (const std::exception& ex) {/* doesn't matter */}

  // Write file
  if (!folly::writeFile(json, myNetworkInfoFile_.c_str())) {
    LOG(ERROR) << "Failed to write NetworkInfo to file: " << myNetworkInfoFile_;
    return false;
  }
  VLOG(2) << "Wrote new NetworkInfo to: " << myNetworkInfoFile_ << ": " << json;

  return true;
}

folly::Expected<openr::thrift::Value, OpenrClientApp::KvStoreError>
OpenrClientApp::kvStoreGetValue(const std::string& key) {
  initOpenrCtrlClient();
  if (!openrCtrlClient_) {
    LOG(ERROR) << "Can't init OpenrCtrl client";
    return folly::makeUnexpected(
        OpenrClientApp::KvStoreError::CONNECTION_ERROR);
  }
  openr::thrift::Publication publication;
  try {
    openrCtrlClient_->sync_getKvStoreKeyVals(publication, {key});
  } catch (const std::exception &ex) {
    openrCtrlClient_.reset();
    openrCtrlClient_ = nullptr;
    LOG(ERROR) << "Failed to get KeyVals from Open/R"
               << ", exception: " << folly::exceptionStr(ex);
    return folly::makeUnexpected(
        OpenrClientApp::KvStoreError::CONNECTION_ERROR);
  }
  auto keyVals = publication.keyVals_ref().value();
  auto keyIter = keyVals.find(key);
  if (keyIter == keyVals.end()) {
    // Actual value is nonexistent for TTL updates (not expected here)
    return folly::makeUnexpected(OpenrClientApp::KvStoreError::KEY_NOT_FOUND);
  }
  auto value = *keyIter;
  if (!value.second.value_ref().has_value()) {
    return folly::makeUnexpected(OpenrClientApp::KvStoreError::EMPTY_VALUE);
  }
  return value.second;
}

folly::Expected<std::string, OpenrClientApp::KvStoreError>
OpenrClientApp::kvStoreGetValueString(const std::string& key) {
  auto value_expected = kvStoreGetValue(key);
  if (value_expected.hasError()) {
    return folly::makeUnexpected(value_expected.error());
  }
  if (!value_expected.hasValue()) {
    return folly::makeUnexpected(OpenrClientApp::KvStoreError::EMPTY_VALUE);
  }
  return value_expected.value().value_ref().value();
}

int64_t
OpenrClientApp::kvStoreGetVersion(const std::string key) {
  auto kvPair_expected = kvStoreGetValue(key);
  if (kvPair_expected) {
    auto kvPair = kvPair_expected.value(); // Unwrap folly::Expected
    return kvPair.version_ref().value(); // Get version of openr::thrift::Value
  }
  return 0;
}

bool
OpenrClientApp::checkKvPairExists(const std::string key,
    const std::string value) {
  auto kvPair_expected = kvStoreGetValueString(key);
  if (kvPair_expected) {
    if (value == kvPair_expected.value()) { // Compare new and old value
      return true;
    }
  }
  return false;
}

openr::thrift::KeySetParams
OpenrClientApp::makeKeySetParams(const std::string key,
    const std::string originator, std::string value, int64_t ttl,
    int64_t ttlVersion, std::optional<int64_t> hash) {
  int64_t version = kvStoreGetVersion(key);
  auto val = openr::createThriftValue(version + 1, originator, value, ttl,
      ttlVersion, hash);
  openr::thrift::KeyVals keyVals ({{key, val}});
  auto keySetParams = openr::thrift::KeySetParams();
  keySetParams.keyVals_ref().value() = keyVals;
  return keySetParams;
}

std::map<std::string, openr::thrift::Value>
OpenrClientApp::kvStoreDumpKeysRaw(const std::string& prefix) {
  std::map<std::string, openr::thrift::Value> keyThriftVals;

  // Init openrCtrlClient to talk to kvStore
  initOpenrCtrlClient();

  if (!openrCtrlClient_) {
    LOG(ERROR) << "Can't init OpenrCtrl client";
    return keyThriftVals;
  }

  openr::thrift::Publication pub;
  try {
    openr::thrift::KeyDumpParams keyDumpParams;
    keyDumpParams.prefix_ref().value() = prefix;
    openrCtrlClient_->sync_getKvStoreKeyValsFiltered(pub, keyDumpParams);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Failed to retrieve KvStore keyVals for prefix: " << prefix
               << "Exception: " << folly::exceptionStr(ex);
    openrCtrlClient_ = nullptr;
    return keyThriftVals;
  }

  // Iterate through KeyVals of openr::thrift::Publication
  for (const auto& [k, v] : pub.keyVals_ref().value()) {
    keyThriftVals[k] = v;
  }
  return keyThriftVals;
}

std::map<std::string, std::string>
OpenrClientApp::kvStoreDumpKeys(const std::string& prefix) {
  std::map<std::string, std::string> keyVals;

  std::map<std::string, openr::thrift::Value> keyThriftVals =
      kvStoreDumpKeysRaw(prefix);

  // Iterate through KeyVals of openr::thrift::Publication
  for (const auto& [k, v] : keyThriftVals) {
    if (v.value_ref().has_value()) {
      keyVals[k] = v.value_ref().value();
    }
  }
  return keyVals;
}

void OpenrClientApp::deprecatedPrefixSync() {
  if (!openrEnabled_) {
    return;
  }

  initOpenrCtrlClient();
  if (!openrCtrlClient_) {
    LOG(ERROR) << "Can't init OpenrCtrl client";
    return;
  }

  // Decompose old KvStore prefix entries into individual KvStore entries and
  // inject them into KvStore. M80 and newer Terragraph releases need KvStore
  // prefix entries to have keys with the new prefix key format. Nodes use these
  // prefix keys for routing via nodes' allocated prefixes.
  std::regex deprecatedPrefixKeyRegex {folly::sformat("{}(.*)",
    openr::Constants::kPrefixDbMarkerDeprecated.toString())};
  auto deprecatedPrefixMap =
      kvStoreDumpKeysRaw(
      openr::Constants::kPrefixDbMarkerDeprecated.toString());
  bool foundDeprecatedPrefix = false;
  for (const auto& kv : deprecatedPrefixMap) {
    openr::thrift::Value thriftValue = kv.second;
    if (thriftValue.originatorId_ref().value().find(kMinionOriginatorSuffix) !=
        std::string::npos) {
      continue; // Ignore prefixes injected by minion.
    }
    foundDeprecatedPrefix = true;
    VLOG(3) << "Unpacking deprecated KvStore prefix with key: " << kv.first;
    std::cmatch m;
    std::regex_match(kv.first.c_str(), m, deprecatedPrefixKeyRegex);
    if (m.empty()) {
      // This should never happen. deprecatedPrefixMap should only contain
      // well-formed deprecated prefix keys.
      VLOG(3) << "Invalid deprecated prefix key found: " << kv.first;
      continue;
    }
    std::string node = m[1].str();

    openr::thrift::PrefixDatabase db;
    try {
      db = fbzmq::util::readThriftObjStr<openr::thrift::PrefixDatabase>(
          thriftValue.value_ref().value(), serializer_);
    } catch (std::exception const& e) {
      LOG(ERROR) << "Failed to deserialize PrefixDatabase: "
                 << folly::exceptionStr(e);
      continue;
    }
    for (const auto& prefixEntry : db.prefixEntries_ref().value()) {
      openr::thrift::PrefixDatabase newDb;
      newDb.thisNodeName_ref() = db.thisNodeName_ref().value();
      newDb.prefixEntries_ref() =
          std::vector<openr::thrift::PrefixEntry>{prefixEntry};
      newDb.deletePrefix_ref() = db.deletePrefix_ref().value();
      std::string prefix = openr::toString(prefixEntry.prefix_ref().value());
      std::string newKey = folly::sformat("{}{}:[{}]",
          openr::Constants::kPrefixDbMarker.toString(), node, prefix);
      openr::thrift::KeySetParams keySetParams = makeKeySetParams(newKey,
          db.thisNodeName_ref().value() + kMinionOriginatorSuffix,
          fbzmq::util::writeThriftObjStr(newDb, serializer_),
          thriftValue.ttl_ref().value() /* copy TTL */ );
      VLOG(3) << "Setting prefix: " << newKey;
      try {
        openrCtrlClient_->sync_setKvStoreKeyVals(keySetParams, kDefaultArea);
      } catch (const std::exception& ex) {
        LOG(ERROR) << "Error setting deprecated prefixes in KvStore";
        openrCtrlClient_ = nullptr;
        return;
      }
    }
  }

  if (!foundDeprecatedPrefix) {
    // don't inject deprecated prefixes if no nodes in the network use them.
    return;
  }

  // Add prefix entries in KvStore with the deprecated prefix key format. Older
  // Terragraph releases (M79 and prior) need these KvStore entries to route to
  // all other nodes in the network via their allocated prefixes.
  std::string nodePrefix = folly::sformat("{}{}",
      openr::Constants::kPrefixDbMarker.toString(),
      OpenrUtils::toOpenrNodeName(macAddr_));
  std::vector<openr::thrift::PrefixEntry> newEntries;
  openr::thrift::PrefixDatabase newDb;
  newDb.thisNodeName_ref() = OpenrUtils::toOpenrNodeName(macAddr_);
  newDb.deletePrefix_ref() = false;
  newDb.area_ref() = kDefaultArea;
  for (const auto& kv : kvStoreDumpKeysRaw(nodePrefix)) {
    openr::thrift::Value thriftValue = kv.second;
    if (thriftValue.originatorId_ref().value().find(kMinionOriginatorSuffix) !=
        std::string::npos) {
      continue; // Ignore prefixes injected by minion.
    }
    openr::thrift::PrefixDatabase db;
    try {
      db = fbzmq::util::readThriftObjStr<openr::thrift::PrefixDatabase>(
          thriftValue.value_ref().value(), serializer_);
    } catch (std::exception const& e) {
      LOG(ERROR) << "Failed to deserialize PrefixDatabase: "
                 << folly::exceptionStr(e);
      continue;
    }
    auto prefixEntries = db.prefixEntries_ref().value();
    newEntries.insert(newEntries.begin(), prefixEntries.begin(),
        prefixEntries.end());
  }
  newDb.prefixEntries_ref() = newEntries;
  std::string newKey = folly::sformat("{}{}",
      openr::Constants::kPrefixDbMarkerDeprecated.toString(),
      OpenrUtils::toOpenrNodeName(macAddr_));
  // Note that the TTL must be longer than the KvStore sync request period.
  openr::thrift::KeySetParams keySetParams = makeKeySetParams(newKey,
      newDb.thisNodeName_ref().value() + kMinionOriginatorSuffix,
      fbzmq::util::writeThriftObjStr(newDb, serializer_),
      kStaticPrefixTtl);
  VLOG(3) << "Emplacing deprecated prefix: " << newKey;
  try {
    openrCtrlClient_->sync_setKvStoreKeyVals(keySetParams, kDefaultArea);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Error setting deprecated prefixes in KvStore";
    openrCtrlClient_ = nullptr;
    return;
  }
}

void
OpenrClientApp::linkMonitorSync() {
  if (!openrEnabled_) {
    VLOG(3) << "Not syncing link metrics (Open/R is disabled)";
    return;
  }

  // Dump links from LinkMonitor
  auto linkDump = linkMonitorDumpLinks();
  if (!linkDump) {
    VLOG(2) << "Could not fetch DumpLinksReply from LinkMonitor";
    return;
  }

  // Construct interface -> neighbor MAC mapping
  std::unordered_map<std::string, std::string> ifaceMap;
  auto lockedIgnitedNeighbors = SharedObjects::getIgnitedNeighbors()->rlock();
  for (const auto& kv : *lockedIgnitedNeighbors) {
    if (kv.second.ifname_ref().has_value()) {
      ifaceMap[kv.second.ifname_ref().value()] = kv.first;
    }
  }
  lockedIgnitedNeighbors.unlock();  // lockedIgnitedNeighbors -> NULL

  auto lockedNodeConfigW = SharedObjects::getNodeConfigWrapper()->rlock();

  // Check the link metric on all interfaces against values in config
  // (If no config entry, assume the metric should be unset)
  for (const auto& kv : linkDump->interfaceDetails_ref().value()) {
    std::string iface = kv.first;
    openr::thrift::InterfaceDetails details = kv.second;
    bool isDrained = (details.metricOverride_ref().has_value() &&
        details.metricOverride_ref().value() == kLinkMetricSoftDrained);
    bool shouldDrain = false;

    // Find the neighbor associated with this interface
    auto macIter = ifaceMap.find(iface);
    if (macIter == ifaceMap.end()) {
      continue;  // shouldn't happen on ignited wireless links
    }

    // Look up the expected metric
    std::optional<int32_t> expectedMetric;
    auto linkMetricIter = linkMetricMap_.find(macIter->second);
    if (linkMetricIter != linkMetricMap_.end()) {
      expectedMetric = linkMetricIter->second;
    }

    // Get link params
    // "softDisable" takes precedence over "fixedMetric"
    auto openrLinkParams =
        lockedNodeConfigW->getLinkParams(macIter->second).openrLinkParams;
    if (openrLinkParams.softDisable_ref().has_value() &&
        openrLinkParams.softDisable_ref().value()) {
      shouldDrain = true;
      expectedMetric = kLinkMetricSoftDrained;
    } else if (openrLinkParams.fixedMetric_ref().has_value()) {
      expectedMetric = openrLinkParams.fixedMetric_ref().value();
    }

    // On mismatch, change the link metric
    if ((details.metricOverride_ref().has_value() != expectedMetric.has_value())
        || (expectedMetric && details.metricOverride_ref().value() !=
        expectedMetric.value())) {
      if (shouldDrain != isDrained) {
        LOG(INFO) << "Open/R link metric mismatch to " << macIter->second
                  << " on interface " << iface << ". Now "
                  << (shouldDrain ? "draining..." : "undraining...");
      } else {
        LOG(INFO) << "Changing Open/R link metric on interface " << iface
                  << " to " << (expectedMetric ?
                      std::to_string(expectedMetric.value()) : "<unset>");
      }
      setLinkMetric(expectedMetric, iface);
    }
  }
}

void
OpenrClientApp::setLinkMetric(
    std::optional<int32_t> metric, const std::string& interface) {
  // Init openrCtrlClient to talk to linkMonitor
  initOpenrCtrlClient();

  if (!openrCtrlClient_) {
    LOG(ERROR) << "Can't init OpenrCtrl client";
    return;
  }

  try {
    if (metric) {
      openrCtrlClient_->sync_setInterfaceMetric(interface, metric.value());
    } else {
      openrCtrlClient_->sync_unsetInterfaceMetric(interface);
    }
  } catch (const std::exception& ex) {
    // make sure next time we will connect with new one
    openrCtrlClient_.reset();
    openrCtrlClient_ = nullptr;
    LOG(ERROR) << "Failed to " << (metric ? "set" : "unset") << " link metric"
               << ", exception: " << folly::exceptionStr(ex);
    return;
  }

  folly::dynamic details = folly::dynamic::object;
  details["interfaceName"] = interface;
  if (metric) {
    details["cmd"] = "SET_LINK_METRIC";
    details["overrideMetric"] = metric.value();
  } else {
    details["cmd"] = "UNSET_LINK_METRIC";
  }
  // Log an event
  std::string msg = metric
      ? folly::sformat(
            "Set link metric on interface {} to {}", interface, metric.value())
      : folly::sformat("Unset link metric on interface {}", interface);
  eventClient_->logEventDynamic(
      thrift::EventCategory::OPENR,
      thrift::EventId::OPENR_LINK_MONITOR_MODIFIED,
      thrift::EventLevel::INFO,
      msg,
      details);
}

std::optional<openr::thrift::DumpLinksReply>
OpenrClientApp::linkMonitorDumpLinks() {
  // Init openrCtrlClient to talk to linkMonitor
  initOpenrCtrlClient();

  if (!openrCtrlClient_) {
    LOG(ERROR) << "Can't init OpenrCtrl client";
    return std::nullopt;
  }

  try {
    // Receive DumpLinksReply struct
    openr::thrift::DumpLinksReply reply;
    openrCtrlClient_->sync_getInterfaces(reply);
    return reply;
  } catch (const std::exception& ex) {
    // make sure next time we will connect with new one
    openrCtrlClient_.reset();
    openrCtrlClient_ = nullptr;
    LOG(ERROR) << "Failed to dump links from Open/R"
               << ", exception: " << folly::exceptionStr(ex);
  }
  return std::nullopt;
}

std::optional<openr::thrift::AdjacencyDatabase>
OpenrClientApp::getRoutingAdjacencies() {
  // Keys have format: adj:[openr_node_name]
  // ex. "adj:node-00.00.00.10.0b.40"
  std::string myAdjKey = openr::Constants::kAdjDbMarker.toString() +
    OpenrUtils::toOpenrNodeName(macAddr_);

  // Fetch AdjacencyDatabase from KvStore
  auto maybeAdjDb = kvStoreGetValueString(myAdjKey);
  if (!maybeAdjDb.hasValue()) {
    return std::nullopt;
  }

  // Parse AdjacencyDatabase struct
  openr::thrift::AdjacencyDatabase db;
  try {
    auto adjDb = maybeAdjDb.value();
    db = fbzmq::util::readThriftObjStr<openr::thrift::AdjacencyDatabase>(
        adjDb, serializer_);
  } catch (std::exception const& e) {
    LOG(ERROR) << "Failed to deserialize AdjacencyDatabase: "
               << folly::exceptionStr(e);
    return std::nullopt;
  }
  db.area_ref() = openr::Constants::kDefaultArea;
  return db;
}

void
OpenrClientApp::processSetPrefixes(
    const std::string& senderApp, const thrift::Message& message) {
  VLOG(2) << "Received request to set prefixes from " << senderApp;
  auto newPrefixes = maybeReadThrift<openr::thrift::StaticAllocation>(message);
  if (!newPrefixes) {
    handleInvalidMessage("StaticAllocation", senderApp);
    return;
  }
  if (!openrEnabled_) {
    VLOG(3) << "Dropping StaticAllocation request (Open/R is disabled)";
    return;
  }

  // Initially true so prefixes are set by default
  bool shouldSetPrefixes = true;

  // shouldSetPrefixes should be false iff the prefixes in KvStore are the same
  // as the ones received from controller
  openr::thrift::StaticAllocation oldPrefixes;
  auto maybeOldPrefixes = kvStoreGetValue(
      openr::Constants::kStaticPrefixAllocParamKey.toString());
  if (maybeOldPrefixes.hasValue()) {
    auto oldPrefixesValue = maybeOldPrefixes.value().value_ref().value();
    try {
       oldPrefixes =
          fbzmq::util::readThriftObjStr<openr::thrift::StaticAllocation> (
              oldPrefixesValue, serializer_);
    } catch (std::exception const& e) {
      LOG(ERROR) << "Failed to deserialize StaticAllocation prefixes: "
                 << folly::exceptionStr(e);
    }
    // At this point, newPrefixes will not be std::nullopt
    shouldSetPrefixes = (oldPrefixes != newPrefixes);
  }
  if (!shouldSetPrefixes) {
    return;
  }

  // TODO: No need to use fbzmq here after Open/R Nov 14 2020 revision
  LOG(INFO) << "Setting static prefixes in KvStore...";
  openr::thrift::KeySetParams keySetParams =
      makeKeySetParams(openr::Constants::kStaticPrefixAllocParamKey.toString(),
          OpenrUtils::toOpenrNodeName(macAddr_) + kMinionOriginatorSuffix,
          fbzmq::util::writeThriftObjStr(*newPrefixes, serializer_),
          kStaticPrefixTtl);
  initOpenrCtrlClient();
  if (!openrCtrlClient_) {
    LOG(ERROR) << "Can't init OpenrCtrl client";
    return;
  }
  try {
    openrCtrlClient_->sync_setKvStoreKeyVals(keySetParams, kDefaultArea);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Error setting static prefixes in KvStore";
    openrCtrlClient_ = nullptr;
    return;
  }

  // Log an event
  eventClient_->logEventThrift(
      thrift::EventCategory::OPENR,
      thrift::EventId::OPENR_KVSTORE_MODIFIED,
      thrift::EventLevel::INFO,
      "Set static prefixes in KvStore",
      newPrefixes.value());
}

} // namespace minion
} // namespace terragraph
} // namespace facebook
