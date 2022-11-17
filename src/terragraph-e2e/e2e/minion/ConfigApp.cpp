/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ConfigApp.h"

#include <cstdio>
#include <regex>
#include <sys/stat.h>
#include <time.h>

#include <boost/filesystem.hpp>
#include <curl/curl.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/MapUtil.h>
#include <folly/String.h>
#include <folly/Uri.h>
#include <folly/dynamic.h>

#include "SharedObjects.h"
#include "e2e/common/ConfigUtil.h"
#include "e2e/common/Consts.h"
#include "e2e/common/JsonUtils.h"
#include "e2e/common/MacUtils.h"
#include "e2e/common/SysUtils.h"
#include "e2e/if/gen-cpp2/DriverMessage_types.h"
#include "e2e/if/gen-cpp2/PassThru_types.h"

using apache::thrift::detail::TEnumMapFactory;

using namespace fbzmq;

DEFINE_string(
    base_config_dir,
    "/etc/e2e_config/base_versions/",
    "Directory with base config JSON files");
DEFINE_string(
    hw_base_config_dir,
    "/etc/e2e_config/base_versions/hw_versions/",
    "Directory with hardware base config JSON files");
DEFINE_string(
    hw_config_types_file,
    "/etc/e2e_config/base_versions/hw_versions/hw_types.json",
    "JSON file mapping hardware config types to hardware board IDs");

namespace {
const int kOk{0}; // Script execution success

// Time to wait before triggering delayed node actions
const std::chrono::seconds kDelayedActionTime{2};

// Empty MAC address (expected by firmware when setting radio config)
const std::string kEmptyMacAddress{"00:00:00:00:00:00"};

// MAX_FW_RUNTIME_CFG_POOL_SIZE
const int kMaxFwRuntimeCfgPoolSize{2};

// Commands
const std::string kRebootCmd{"reboot"};
const std::string kRestartMinionCmd{"sv restart e2e_minion"};
const std::string kRestartStatsAgentCmd{"sv restart stats_agent"};
const std::string kRestartLogtailCmd{"sv restart logtail"};
const std::string kRestartOpenrCmd{"sv restart openr"};
const std::string kRestartPopConfigCmd{"sv restart pop_config"};
const std::string kRestartFibNssCmd{"sv restart fib_nss"};
const std::string kRestartSquireLinuxCmd{"sv restart squire_linux"};
const std::string kRestartSquireNssCmd{"sv restart squire_nss"};
const std::string kRestartKeaCmd{"sv restart kea"};
const std::string kRestartUdpPingServer{"sv restart udp_ping_server"};
const std::string kUpdateFirewallCmd{"update_firewall"};
const std::string kRestartFluentbitCmd{"/etc/init.d/fluent-bit restart"};

// Restart sshd to update config files
// NOTE: Rewriting config file is hooked into sshd init
const std::string kRestartSshdCmd{"/etc/init.d/sshd restart"};

// Restart chronyd to update config files
// NOTE: Rewriting config file is hooked into init scripts
const std::string kRestartChronydCmd{"/etc/init.d/chronyd restart"};

// Reload resolvconf config files
// NOTE: Rewriting config file is hooked into resolvconf init
const std::string kReloadResolvconfCmd{"/etc/init.d/resolvconf reload"};

// Reload rsyslogd config files
// NOTE: The normal method (HUP) doesn't work in our environment, so restart.
const std::string kReloadRsyslogConfigCmd{"/etc/init.d/syslog.rsyslog restart"};

// rsyslogd supplementary configuration file directory
const std::string kRsyslogConfigDir{"/var/run/rsyslog.d"};

// rsyslogd supplementary configuration file name
const std::string kRsyslogConfigFile{"tg.conf"};

// Restart SNMP daemon to update config files
// NOTE: Rewriting config file is hooked into snmp init
const std::string kRestartSnmpCmd("/etc/init.d/snmpd restart");
// Restart TG SNMP agent daemon
const std::string kRestartSnmpAgentCmd("sv restart snmp_agent");

// Restart WebUI daemon
const std::string kRestartWebUiCmd("sv restart webui");

// Restart SLIP
const std::string kRestartSlipCmd("sv restart slip");

// Reload tunnel configuration
const std::string kReloadTunnelCmd("/usr/sbin/config_tunnel.sh");

// Run vpp_chaperone
const std::string kRunVppChaperoneCmd(
  "/usr/sbin/run_vpp_chaperone_and_monitor.sh");

// Reload topology names
const std::string kReloadTopologyNamesCmd("/usr/sbin/reload_topology_names.sh");

}

namespace facebook {
namespace terragraph {
namespace minion {

ConfigApp::ConfigApp(
    fbzmq::Context& zmqContext,
    const std::string& brokerRouterUrl,
    const std::string& monitorSockUrl,
    const std::string& macAddr)
    : MinionApp(
          zmqContext,
          brokerRouterUrl,
          monitorSockUrl,
          macAddr,
          E2EConsts::kConfigAppMinionId) {
  // Load config metadata file
  // On errors, default initialize to prevent segfaults
  try {
    configMeta_ = std::make_unique<ConfigMetadata>(
        FLAGS_node_config_metadata_file,
        true /* keepFullMetadata */,
        false /* hasFlags */);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Error loading node config metadata: "
               << folly::exceptionStr(ex);
    folly::dynamic obj = folly::dynamic::object;
    configMeta_ = std::make_unique<ConfigMetadata>(
        obj, true /* keepFullMetadata */, false /* hasFlags */);
  }

  // Write the initial rsyslogd config and restart the daemon
  // (Only do this if the feature is enabled.)
  if (writeRsyslogConfig(kRsyslogConfigDir, kRsyslogConfigFile)) {
    LOG(INFO) << "Reloading rsyslog configuration...";
    SysUtils::system(kReloadRsyslogConfigCmd);
  }
}

void
ConfigApp::processMessage(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::GET_MINION_CONFIG_REQ:
      processConfigGetReq(senderApp);
      break;
    case thrift::MessageType::SET_MINION_CONFIG_REQ:
      processConfigSetReq(senderApp, message);
      break;
    case thrift::MessageType::GET_MINION_CONFIG_ACTIONS_REQ:
      processGetMinionConfigActionsReq(senderApp, message);
      break;
    case thrift::MessageType::FW_SET_LOG_CONFIG:
      processSetFwLogConfig(senderApp, message);
      break;
    case thrift::MessageType::GET_MINION_BASE_CONFIG:
      processGetMinionBaseConfig(senderApp, message);
      break;
    default: {
      LOG(ERROR)
          << "Wrong type of message ("
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                 message.mType, "UNKNOWN")
          << ") received from " << senderApp;
    }
  }
}

void
ConfigApp::processConfigGetReq(const std::string& senderApp) noexcept {
  LOG(INFO) << "Received GET_MINION_CONFIG_REQ from " << senderApp;

  thrift::GetMinionConfigResp getMinionConfigResp;
  getMinionConfigResp.config =
      SharedObjects::getNodeConfigWrapper()->rlock()->getNodeConfigJson();
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_MINION_CONFIG_RESP,
      getMinionConfigResp);
}

void
ConfigApp::processConfigSetReq(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  LOG(INFO) << "Received SET_MINION_CONFIG_REQ from " << senderApp;

  auto request = maybeReadThrift<thrift::SetMinionConfigReq>(message);
  if (!request) {
    handleInvalidMessage("SetMinionConfigReq", senderApp);
    return;
  }

  auto lockedNodeConfigWrapper = SharedObjects::getNodeConfigWrapper()->wlock();

  // Copy the old config
  folly::dynamic oldNodeConfig = folly::dynamic::object;
  try {
    std::string oldNodeConfigJson =
        lockedNodeConfigWrapper->getNodeConfigJson();
    oldNodeConfig = folly::parseJson(oldNodeConfigJson);
  } catch (const std::exception& ex) {/* shouldn't happen */}

  // Write the new node config
  bool success = lockedNodeConfigWrapper->setNodeConfig(request->config);
  if (!success) {
    auto err = "Unable to set node config";
    LOG(ERROR) << err;
    eventClient_->logEvent(
        thrift::EventCategory::CONFIG,
        thrift::EventId::MINION_SET_CONFIG,
        thrift::EventLevel::ERROR,
        err);
    return;
  }

  // Determine what actions to perform by comparing the old vs. new config
  folly::dynamic newNodeConfig = folly::dynamic::object;
  try {
    std::string newNodeConfigJson =
        lockedNodeConfigWrapper->getNodeConfigJson();
    newNodeConfig = folly::parseJson(newNodeConfigJson);
  } catch (const std::exception& ex) {/* shouldn't happen */}
  folly::dynamic configDiff =
      JsonUtils::dynamicObjectFullDifference(oldNodeConfig, newNodeConfig);
  if (configDiff.empty()) {
    return;  // new config is identical
  }
  VLOG(1) << "Changed or removed config values:\n" << folly::toJson(configDiff);
  auto nodeActions = configMeta_->getActions(configDiff);

  // Log an event
  folly::dynamic nodeActionsArray = folly::dynamic::array;
  for (const auto& kv : nodeActions) {
    const thrift::CfgAction& nodeAction = kv.first;
    auto map = TEnumMapFactory<thrift::CfgAction>::makeValuesToNamesMap();
    if (map.count(nodeAction)) {
      nodeActionsArray.push_back(map.at(nodeAction));
    } else {
      nodeActionsArray.push_back(static_cast<int>(nodeAction));
    }
  }
  eventClient_->logEventDynamic(
      thrift::EventCategory::CONFIG,
      thrift::EventId::MINION_SET_CONFIG,
      thrift::EventLevel::INFO,
      "Applying new node config",
      folly::dynamic::object("actions", nodeActionsArray));

  // Save the old config MD5 if the node will reboot or restart minion. We want
  // to avoid a race condition where the updated hash is sent prematurely in a
  // minion status report.
  for (const auto& kv : nodeActions) {
    const thrift::CfgAction& nodeAction = kv.first;
    if (nodeAction == thrift::CfgAction::REBOOT ||
        nodeAction == thrift::CfgAction::RESTART_MINION) {
      lockedNodeConfigWrapper->usePreviousConfigMd5(true);
      break;
    }
  }

  // Release NodeConfigWrapper lock before performing node actions since these
  // may acquire the same lock.
  lockedNodeConfigWrapper.unlock();  // lockedNodeConfigWrapper -> NULL
  performNodeActions(nodeActions, request->bwgdIdx);
}

void
ConfigApp::processGetMinionConfigActionsReq(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  LOG(INFO) << "Received GET_MINION_CONFIG_ACTIONS_REQ from " << senderApp;

  auto request = maybeReadThrift<thrift::GetMinionConfigActionsReq>(message);
  if (!request) {
    handleInvalidMessage("GetMinionConfigActionsReq", senderApp);
    return;
  }

  // Parse the new node config
  folly::dynamic newNodeConfig = folly::dynamic::object;
  try {
    newNodeConfig = folly::parseJson(request->config);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Unable to parse new node config";
    return;
  }

  // Determine what actions to perform by comparing the old vs. new config
  folly::dynamic oldNodeConfig = folly::dynamic::object;
  try {
    std::string oldNodeConfigJson =
        SharedObjects::getNodeConfigWrapper()->rlock()->getNodeConfigJson();
    oldNodeConfig = folly::parseJson(oldNodeConfigJson);
  } catch (const std::exception& ex) {/* shouldn't happen */}
  folly::dynamic configDiff =
      JsonUtils::dynamicObjectFullDifference(oldNodeConfig, newNodeConfig);
  auto nodeActions = configMeta_->getActions(configDiff);

  // Send response to controller
  std::unordered_set<thrift::CfgAction> nodeActionsSet;
  for (const auto& kv : nodeActions) {
    nodeActionsSet.insert(kv.first);
  }
  thrift::GetMinionConfigActionsResp getMinionConfigActionsResp;
  getMinionConfigActionsResp.actions = nodeActionsSet;
  getMinionConfigActionsResp.id = request->id;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_MINION_CONFIG_ACTIONS_RESP,
      getMinionConfigActionsResp);
}

void
ConfigApp::performNodeActions(
    const std::unordered_map<thrift::CfgAction, std::vector<std::string>>&
        nodeActions,
    int64_t bwgdIdx) {
  // Keep track of actions to be invoked after a delay
  std::unordered_set<thrift::CfgAction> delayedNodeActions;

  for (const auto& kv : nodeActions) {
    thrift::CfgAction action = kv.first;
    switch (action) {
      // Delayed actions
      case thrift::CfgAction::REBOOT:
      case thrift::CfgAction::RESTART_MINION:
        delayedNodeActions.insert(action);
        break;
      case thrift::CfgAction::RESTART_ROUTING:
        // POP config needs to be delayed until Open/R restarts first.
        LOG(INFO) << "Restarting Open/R first...";
        SysUtils::system(kRestartOpenrCmd);
        delayedNodeActions.insert(action);
        break;

      // Immediate actions
      case thrift::CfgAction::RESTART_STATS_AGENT:
        LOG(INFO) << "Restarting stats agent...";
        SysUtils::system(kRestartStatsAgentCmd);
        break;
      case thrift::CfgAction::RESTART_SQUIRE:
        LOG(INFO) << "Restarting squire...";
        SysUtils::system(kRestartSquireLinuxCmd);
        SysUtils::system(kRestartSquireNssCmd);
        break;
      case thrift::CfgAction::RESTART_LOGTAIL:
        LOG(INFO) << "Restarting logtail...";
        SysUtils::system(kRestartLogtailCmd);
        break;
      case thrift::CfgAction::REDO_POP_CONFIG:
        LOG(INFO) << "Restarting pop config and fib nss...";
        SysUtils::system(kRestartPopConfigCmd);
        SysUtils::system(kRestartFibNssCmd);
        break;
      case thrift::CfgAction::RELOAD_RSYSLOG_CONFIG:
        LOG(INFO) << "Reloading rsyslog configuration...";
        writeRsyslogConfig(kRsyslogConfigDir, kRsyslogConfigFile);
        SysUtils::system(kReloadRsyslogConfigCmd);
        break;
      case thrift::CfgAction::RESTART_KEA:
        LOG(INFO) << "Restarting kea (dhcpd)...";
        SysUtils::system(kRestartKeaCmd);
        break;
      case thrift::CfgAction::UPDATE_FIREWALL:
        LOG(INFO) << "Updating firewall...";
        SysUtils::system(kUpdateFirewallCmd);
        break;
      case thrift::CfgAction::SYNC_LINK_MONITOR:
        LOG(INFO) << "Syncing with LinkMonitor...";
        sendToMinionApp(
            E2EConsts::kOpenrClientAppMinionId,
            thrift::MessageType::SYNC_LINK_MONITOR,
            thrift::SyncLinkMonitor());
        break;
      case thrift::CfgAction::INJECT_KVSTORE_KEYS:
        LOG(INFO) << "Injecting KvStore keys...";
        sendToMinionApp(
            E2EConsts::kOpenrClientAppMinionId,
            thrift::MessageType::INJECT_KVSTORE_KEYS,
            thrift::InjectKvStoreKeys());
        break;
      case thrift::CfgAction::UPDATE_LINK_METRICS:
        LOG(INFO) << "Updating link metrics...";
        sendToMinionApp(
            E2EConsts::kStatusAppMinionId,
            thrift::MessageType::UPDATE_LINK_METRICS,
            thrift::UpdateLinkMetrics());
        break;
      case thrift::CfgAction::UPDATE_GLOG_LEVEL:
        LOG(INFO) << "Updating VLOG level...";
        updateVlogLevel();
        break;
      case thrift::CfgAction::SYMLINK_TIMEZONE:
        LOG(INFO) << "Linking in configured timezone...";
        symlinkTimezone();
        break;
      case thrift::CfgAction::RELOAD_SSHD_CA_KEYS:
        LOG(INFO) << "Updating sshd trusted CA keys...";
        SysUtils::system(kRestartSshdCmd);
        break;
      case thrift::CfgAction::RELOAD_NTP_CONFIG:
        LOG(INFO) << "Updating NTP servers...";
        SysUtils::system(kRestartChronydCmd);
        break;
      case thrift::CfgAction::RELOAD_FIRMWARE:
        if (affectsLinksOrRadios(kv.second)) {
          LOG(INFO) << "Firmware will be reloaded...";
          delayedNodeActions.insert(thrift::CfgAction::RESTART_MINION);
        } else {
          VLOG(2) << "Config change does not require reloading firmware.";
        }
        break;
      case thrift::CfgAction::SET_FW_PARAMS:
        if (affectsLinksOrRadios(kv.second)) {
          LOG(INFO) << "Changing runtime firmware parameters...";
          setFwParams(kv.second, std::nullopt);
        } else {
          VLOG(2)
              << "Config change does not require a firmware parameter update.";
        }
        break;
      case thrift::CfgAction::SET_FW_PARAMS_SYNC_OR_RELOAD_FIRMWARE:
        if (affectsLinksOrRadios(kv.second)) {
          LOG(INFO) << "Synchronizing runtime firmware parameter change...";
          if (!setFwParams(kv.second, std::make_optional(bwgdIdx))) {
            // If this fails, reload firmware instead
            LOG(WARNING)
                << "Unable to synchronize runtime firmware parameter changes. "
                   "Reloading firmware instead.";
            delayedNodeActions.insert(thrift::CfgAction::RESTART_MINION);
          }
        } else {
          VLOG(2)
              << "Config change does not require a firmware parameter update.";
        }
        break;
      case thrift::CfgAction::SET_AIRTIME_PARAMS:
        LOG(INFO) << "Changing airtime allocation...";
        setAirtimeParams();
        break;
      case thrift::CfgAction::SET_FW_STATS_CONFIG:
        LOG(INFO) << "Changing firmware stats config...";
        sendToMinionApp(
            E2EConsts::kStatusAppMinionId,
            thrift::MessageType::UPDATE_FW_STATS_CONFIG,
            thrift::Empty());
        break;
      case thrift::CfgAction::RESTART_UDP_PING_SERVER:
        LOG(INFO) << "Restarting UDP ping server...";
        SysUtils::system(kRestartUdpPingServer);
        break;
      case thrift::CfgAction::RELOAD_SSHD:
        LOG(INFO) << "Restarting SSH daemon...";
        SysUtils::system(kRestartSshdCmd);
        break;
      case thrift::CfgAction::RESTART_FLUENTD_AGENT:
        LOG(INFO) << "Restarting fluent-bit...";
        SysUtils::system(kRestartFluentbitCmd);
        break;
      case thrift::CfgAction::RELOAD_DNS_SERVERS:
        LOG(INFO) << "Reloading DNS resolution config...";
        SysUtils::system(kReloadResolvconfCmd);
        break;
      case thrift::CfgAction::RESTART_SNMP:
        LOG(INFO) << "Updating and restarting SNMP...";
        SysUtils::system(kRestartSnmpCmd);
        SysUtils::system(kRestartSnmpAgentCmd);
        break;
      case thrift::CfgAction::RESTART_WEBUI:
        LOG(INFO) << "Updating and restarting WebUI...";
        SysUtils::system(kRestartWebUiCmd);
        break;
      case thrift::CfgAction::RESTART_SLIP:
        LOG(INFO) << "Restarting SLIP...";
        SysUtils::system(kRestartSlipCmd);
        break;
      case thrift::CfgAction::RELOAD_TUNNEL_CONFIG:
        LOG(INFO) << "Reloading tunnel configuration...";
        SysUtils::system(kReloadTunnelCmd);
        break;
      case thrift::CfgAction::RELOAD_VPP_CONFIG_AND_MONITOR:
        LOG(INFO) << "Running VPP Chaperone...";
        SysUtils::system(kRunVppChaperoneCmd);
        break;
      case thrift::CfgAction::RELOAD_TOPOLOGY_NAMES:
        LOG(INFO) << "Reload topology names...";
        SysUtils::system(kReloadTopologyNamesCmd);
        break;
      case thrift::CfgAction::UPDATE_ZONE: {
        auto bgpParams = SharedObjects::getNodeConfigWrapper()->rlock()
          ->getBgpParams();
        // If CPE Prefix auto advertisement is enabled, redo POP config
        // only if it is not present already in the current update.
        if (!nodeActions.count(thrift::CfgAction::REDO_POP_CONFIG)
          && bgpParams->cpePrefixesAutoAdvertisement) {
            LOG(INFO) << "Restarting pop config due to zone update...";
            SysUtils::system(kRestartPopConfigCmd);
            SysUtils::system(kRestartFibNssCmd);
          }
          break;
      }
      default:
        break;
    }
  }

  // Return if no delayed actions to schedule
  if (delayedNodeActions.empty()) {
    return;
  }

  // Schedule delayed actions
  delayedActionTimer_ = ZmqTimeout::make(this, [delayedNodeActions]() noexcept {
    if (delayedNodeActions.count(thrift::CfgAction::REBOOT)) {
      // Schedule reboot and ignore other actions
      LOG(INFO) << "Rebooting...";
      if (!SysUtils::system(kRebootCmd, kOk)) {
        LOG(ERROR) << kRebootCmd << " failed";
      }
      return;
    }

    if (delayedNodeActions.count(thrift::CfgAction::RESTART_ROUTING)) {
      // Open/R restart should be complete. Restart POP config.
      LOG(INFO) << "Restarting POP config...";
      SysUtils::system(kRestartPopConfigCmd);
    }

    // Restart minion last
    if (delayedNodeActions.count(thrift::CfgAction::RESTART_MINION)) {
      LOG(INFO) << "Restarting minion...";
      SysUtils::system(kRestartMinionCmd);
    }
  });
  delayedActionTimer_->scheduleTimeout(kDelayedActionTime);
}

bool
ConfigApp::setFwParams(
    const std::vector<std::string>& keys, std::optional<int64_t> bwgdIdx) {
  VLOG(2) << "Processing runtime firmware config for keys"
          << (bwgdIdx
                 ? folly::sformat(" at BWGD index {}", bwgdIdx.value())
                 : "")
          << ": " << folly::join(", ", keys);

  // Build firmware config requests for each radio
  auto reqs = getFwParamsForConfig(keys, bwgdIdx);

  // Passing a BWGD index causes firmware to schedule these requests.
  // The firmware scheduler's buffer size is small, so if we can't actually
  // schedule everything, we need to restart minion instead.
  // NOTE: Assumes we aren't receiving back-to-back config changes scheduled
  //       before the previous operations are run (otherwise this will fail).
  if (bwgdIdx.has_value()) {
    for (const auto& kv : reqs) {
      if (kv.second.passThruMsgs.size() > kMaxFwRuntimeCfgPoolSize) {
        return false;
      }
    }
  }

  // Send requests to driver
  // TODO: This can interfere with concurrent pending requests sent through
  // StatusApp (incorrect ACK may get forwarded). Should be OK / non-critical,
  // since responses are quick and all requests are manual (through CLI).
  for (const auto& kv : reqs) {
    sendToDriverApp(kv.first, thrift::MessageType::FW_CONFIG_REQ, kv.second);
  }
  return true;
}

std::unordered_map<std::string, thrift::FwConfigParams>
ConfigApp::getFwParamsForConfig(
    const std::vector<std::string>& keys, std::optional<int64_t> bwgdIdx) {
  auto peerNodeTypeMap = SharedObjects::getPeerNodeTypeMap()->copy();
  auto radioMacToBusId =
    SharedObjects::getNodeInfoWrapper()->rlock()->getRadioMacToBusId();
  if (radioMacToBusId.empty()) {
    radioMacToBusId[""] = "";  // compatibility
  }
  auto lockedNodeConfig = SharedObjects::getNodeConfigWrapper()->rlock();

  // Fill out config objects for the given keys
  std::unordered_map<std::string /* mac */, folly::dynamic> radioFwParamMap;
  for (const auto& kv : radioMacToBusId) {
    radioFwParamMap[kv.first] = folly::dynamic::object;
  }
  std::unordered_map<std::string /* mac */, folly::dynamic> linkFwParamMap;

  for (const std::string& fullKey : keys) {
    // Split full key into tokens
    std::vector<std::string> tokens;
    folly::split(".", fullKey, tokens);
    std::string cfg = tokens.front();
    std::string key = tokens.back();
    std::string mac = "";
    if (cfg == "radioParamsOverride" || cfg == "linkParamsOverride") {
      if (tokens.size() < 4) {
        // "radioParamsOverride.<mac>.fwParams.<key>"
        // "linkParamsOverride.<mac>.fwParams.<key>"
        continue;  // malformed/unsupported
      }

      // Standardize MAC address
      try {
        mac = MacUtils::standardizeMac(tokens[1]);
      } catch (std::invalid_argument& ex) {
        LOG(ERROR) << "Failed to parse MAC '" << tokens[1] << "': "
                   << folly::exceptionStr(ex);
        continue;  // bad MAC address
      }
    } else {
      if (tokens.size() < 3) {
        // "radioParamsBase.fwParams.<key>"
        // "linkParamsBase.fwParams.<key>"
        continue;  // malformed/unsupported
      }
    }

    // Fill out config objects
    // NOTE - Link params will be removed from radioParams in the future, so
    // we currently won't support changing link-related parameters within
    // radioParams (i.e. the config used by responders) dynamically. To make
    // dynamic changes at the responder, use linkParams instead (and set the
    // same value in radioParams to persist this).
    if (cfg == "radioParamsBase") {
      // Set config on all radios (since base changed)
      for (const auto& kv : radioFwParamMap) {
        auto fwParams = lockedNodeConfig->getRadioParams(kv.first).fwParams;
        if (auto val = getFwParam(key, fwParams)) {
          radioFwParamMap[kv.first][key] = *val;
        }
      }
    } else if (cfg == "radioParamsOverride") {
      // Set config on specific radio
      if (!radioFwParamMap.count(mac)) {
        LOG(ERROR) << "Ignoring radioParamsOverride for unknown MAC " << mac;
        continue;
      }
      auto fwParams = lockedNodeConfig->getRadioParams(mac).fwParams;
      if (auto val = getFwParam(key, fwParams)) {
        radioFwParamMap[mac][key] = *val;
      }
    } else if (cfg == "linkParamsBase") {
      // Set config on all links (since base changed)
      for (const auto& kv : peerNodeTypeMap) {
        auto fwParams = lockedNodeConfig->getLinkParams(kv.first).fwParams;
        if (auto val = getFwParam(key, fwParams)) {
          if (!linkFwParamMap.count(kv.first)) {
            linkFwParamMap[kv.first] = folly::dynamic::object;
          }
          linkFwParamMap[kv.first][key] = *val;
        }
      }
    } else if (cfg == "linkParamsOverride") {
      // Set config on specific link
      auto fwParams = lockedNodeConfig->getLinkParams(mac).fwParams;
      if (auto val = getFwParam(key, fwParams)) {
        if (!linkFwParamMap.count(mac)) {
          linkFwParamMap[mac] = folly::dynamic::object;
        }
        linkFwParamMap[mac][key] = *val;
      }
    }
  }

  lockedNodeConfig.unlock();  // lockedNodeConfig -> NULL

  // Build requests
  // TODO - We send link params to every radio... should be harmless.
  std::unordered_map<std::string /* radioMac */, thrift::FwConfigParams> reqs;
  for (const auto& radioPair : radioFwParamMap) {
    // Radio config uses a special MAC address (kEmptyMacAddress)
    // If no radio params are changing, don't need to send message to self
    if (radioPair.second.size() > 0) {
      linkFwParamMap[kEmptyMacAddress] = radioPair.second;
    }

    // Add message for each neighbor and self (as needed)
    thrift::FwConfigParams fwConfigParams;
    for (const auto& linkPair : linkFwParamMap) {
      // Deserialize JSON -> Thrift
      thrift::FwOptParams fwParams;
      try {
        apache::thrift::SimpleJSONSerializer::deserialize(
            folly::toJson(linkPair.second), fwParams);
      } catch (const std::exception& ex) {
        LOG(ERROR) << "JSON deserialization to FwOptParams failed: "
                   << folly::exceptionStr(ex);
        continue;  // shouldn't happen
      }

      // Construct PassThru message
      thrift::PassThruMsg passThruMsg;
      passThruMsg.msgType = thrift::PtMsgTypes::SB_SET_FW_PARAMS;
      passThruMsg.dest = thrift::PtMsgDest::SB;
      passThruMsg.setfwParamsReq.addr = linkPair.first;
      if (bwgdIdx.has_value()) {
        passThruMsg.setfwParamsReq.bwgdIdx_ref() = bwgdIdx.value();
      }
      passThruMsg.setfwParamsReq.optionalParams = fwParams;

      // Add message to list
      fwConfigParams.passThruMsgs.push_back(passThruMsg);
    }

    reqs[radioPair.first] = std::move(fwConfigParams);
  }
  return reqs;
}

std::optional<int64_t>
ConfigApp::getFwParam(
    const std::string& key, const thrift::FwOptParams& fwParams) {
  folly::dynamic obj = folly::parseJson(JsonUtils::serializeToJson(fwParams));
  auto iter = obj.find(key);
  if (iter == obj.items().end()) {
    return std::nullopt;  // key not found
  }
  return iter->second.asInt();
}

bool ConfigApp::affectsLinksOrRadios(const std::vector<std::string>& keys) {
  auto peerNodeTypeMap = SharedObjects::getPeerNodeTypeMap()->copy();
  auto radioMacToBusId =
    SharedObjects::getNodeInfoWrapper()->rlock()->getRadioMacToBusId();

  for (const std::string& fullKey : keys) {
    // Split full key into tokens
    std::vector<std::string> tokens;
    folly::split(".", fullKey, tokens);
    if (tokens.empty()) {
      continue;
    }
    std::string cfg = tokens.front();

    if (cfg == "linkParamsBase") {
      if (!peerNodeTypeMap.empty()) {
        return true;
      }
    } else if (cfg == "radioParamsBase") {
      if (!radioMacToBusId.empty()) {
        return true;
      }
    } else if (cfg == "radioParamsOverride" || cfg == "linkParamsOverride") {
      if (tokens.size() < 2) {
        continue;
      }
      // Standardize MAC address
      std::string mac = "";
      try {
        mac = MacUtils::standardizeMac(tokens[1]);
      } catch (std::invalid_argument& ex) {
        LOG(ERROR) << "Failed to parse MAC '" << tokens[1] << "': "
                   << folly::exceptionStr(ex);
        continue;  // bad MAC address
      }

      if (cfg == "linkParamsOverride") {
        if (peerNodeTypeMap.count(mac)) {
          return true;
        }
      } else {
        if (radioMacToBusId.count(mac)) {
          return true;
        }
      }
    }
  }

  return false;
}

void
ConfigApp::setAirtimeParams() {
  thrift::NodeParams nodeParams;
  nodeParams.type = thrift::NodeParamsType::NETWORK;
  nodeParams.airtimeAllocMap_ref() =
      SharedObjects::getNodeConfigWrapper()->rlock()->getNodeAirtime();
  sendToMinionApp(
      E2EConsts::kStatusAppMinionId,
      thrift::MessageType::SET_NODE_PARAMS,
      nodeParams);
}

bool
ConfigApp::writeRsyslogConfig(
    const std::string& configDir,
    const std::string& configFile) {
  auto syslogParams = SharedObjects::getNodeConfigWrapper()->rlock()
      ->getNodeConfig()->syslogParams;

  std::string outputFile = folly::sformat("{}/{}",
      configDir,
      configFile);

  if (!syslogParams.enabled || syslogParams.remoteHost.empty()) {
    // Delete the existing config file (if one exists)
    return (std::remove(outputFile.c_str()) == 0);
  }

  // Construct an rsyslog rule to forward with 'omfwd'
  std::string rule = folly::sformat(
      "{} action(type=\"omfwd\" target=\"{}\" port=\"{}\" protocol=\"{}\" "
      "action.resumeRetryCount=\"100\" "
      "queue.type=\"linkedList\" queue.size=\"1000\")",
      syslogParams.selector,
      syslogParams.remoteHost,
      syslogParams.remotePort,
      syslogParams.protocol);

  // Check if current file contents are the same
  std::string contents;
  if (folly::readFile(outputFile.c_str(), contents) && contents == rule) {
    return false;  // no changes to make
  }

  // Create target directory, if not present
  boost::filesystem::path path(configDir);
  boost::system::error_code errorCode;

  if (!boost::filesystem::create_directories(path, errorCode)) {
    LOG(ERROR) << folly::sformat(
        "Failed to create rsyslog config dir '{}': {}",
        outputFile,
        errorCode.message());
    return false;  // mkdir failed
  }

  // Write new config file
  if (!folly::writeFile(rule, outputFile.c_str())) {
    LOG(ERROR) << "Failed to write rsyslog config to " << outputFile;
    return false;  // write failed
  }

  return true;
}

void
ConfigApp::updateVlogLevel() {
  auto verbose = SharedObjects::getNodeConfigWrapper()->rlock()
      ->getEnvConfig()->MINION_VERBOSE_ref();
  if (verbose.has_value()) {
    auto v = folly::tryTo<int32_t>(verbose.value());
    if (v.hasValue()) {
      FLAGS_v = v.value();
    }
  }
}

void
ConfigApp::symlinkTimezone() {
  const std::string kRunSettz{"/etc/init.d/settz"};
  int settzStatus = SysUtils::system(kRunSettz);
  if (settzStatus != kOk) {
    LOG(ERROR)  << "Running " << kRunSettz << " failed (" << settzStatus
                << ") - Refer to /tmp/settz.log on the node";
  }
}

void
ConfigApp::processSetFwLogConfig(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  auto request = maybeReadThrift<thrift::SetLogConfig>(message);
  if (!request) {
    handleInvalidMessage("SetLogConfig", senderApp);
    return;
  }

  VLOG(2) << "Received FW_SET_LOG_CONFIG from " << senderApp;

  // TODO support per-radio config
  auto radioMacToBusId =
    SharedObjects::getNodeInfoWrapper()->rlock()->getRadioMacToBusId();
  if (radioMacToBusId.empty()) {
    radioMacToBusId[""] = "";  // compatibility
  }
  for (const auto& kv : radioMacToBusId) {
    sendToDriverApp(
        kv.first, thrift::MessageType::FW_SET_LOG_CONFIG, request.value());
  }
}

void
ConfigApp::processGetMinionBaseConfig(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  LOG(INFO) << "Received GET_MINION_BASE_CONFIG from " << senderApp;

  auto request = maybeReadThrift<thrift::GetMinionBaseConfig>(message);
  if (!request) {
    handleInvalidMessage("GetMinionBaseConfig", senderApp);
    return;
  }

  // Build response
  thrift::MinionBaseConfig response;
  if (request->getBaseConfigs) {
    // Only send the latest base config
    SwVersion latestSwVer;
    std::string baseConfigPath;
    for (const auto& pair : SysUtils::findFilesInDirectory(
            FLAGS_base_config_dir, E2EConsts::kJsonFileExtension)) {
      // Check software version length
      std::string filename = pair.second.substr(
          0, pair.second.size() - E2EConsts::kJsonFileExtension.size());
      if (filename.empty()) {
        continue;
      }

      auto swVer = SwVersion(filename);
      if (latestSwVer <= swVer) {
        latestSwVer = swVer;
        baseConfigPath = pair.first;
      }
    }

    // Read the config file
    if (baseConfigPath.empty()) {
      LOG(WARNING) << "No base configs found!";
    } else {
      try {
        VLOG(2) << "Reading base config: " << baseConfigPath;
        std::map<std::string, std::string> baseConfigs;
        baseConfigs[latestSwVer.version] =
            JsonUtils::readJsonFile2String(baseConfigPath);
        response.baseConfigs_ref() = baseConfigs;
      } catch (const std::exception& ex) {
        LOG(ERROR) << "Unable to read base config file "
                    << baseConfigPath << ": " << folly::exceptionStr(ex);
      }
    }
  }
  if (request->getHwConfigs) {
    // Only send the latest hardware base config for this hardware type
    auto hwBoardId =
        SharedObjects::getNodeInfoWrapper()->rlock()->getHwBoardId();
    if (!hwBoardId) {
      LOG(ERROR) << "Hardware board ID is undefined";
    } else {
      // Look up hw_versions subdirectory using board ID and the hw types file
      auto hwType =
          getHardwareConfigType(FLAGS_hw_config_types_file, hwBoardId.value());
      if (!hwType) {
        LOG(ERROR) << "Hardware config type not found for our board ID: "
                   << hwBoardId.value();
      } else {
        // Find the latest base version
        std::string hwConfigDir = FLAGS_hw_base_config_dir + hwType.value();
        SwVersion latestSwVer;
        std::string hwConfigPath;
        for (const auto& pair : SysUtils::findFilesInDirectory(
                hwConfigDir, E2EConsts::kJsonFileExtension)) {
          // Check software version length
          std::string filename = pair.second.substr(
              0, pair.second.size() - E2EConsts::kJsonFileExtension.size());
          if (filename.empty()) {
            continue;
          }

          auto swVer = SwVersion(filename);
          if (latestSwVer <= swVer) {
            latestSwVer = swVer;
            hwConfigPath = pair.first;
          }
        }

        // Read the config file
        if (hwConfigPath.empty()) {
          LOG(WARNING) << "No hardware base configs found for our type: "
                       << hwType.value();
        } else {
          try {
            VLOG(2) << "Reading hardware config: " << hwConfigPath;
            std::map<std::string, std::string> hwConfigs;
            hwConfigs[latestSwVer.version] =
                JsonUtils::readJsonFile2String(hwConfigPath);
            response.hwConfigs_ref() = hwConfigs;
          } catch (const std::exception& ex) {
            LOG(ERROR) << "Unable to read hardware config file "
                       << hwConfigPath << ": " << folly::exceptionStr(ex);
          }
        }
      }
    }
  }
  if (request->getMetadata) {
    response.metadata_ref() = folly::toJson(configMeta_->get());
  }
  sendToCtrlApp(
      senderApp, thrift::MessageType::MINION_BASE_CONFIG, response);
}

std::optional<std::string>
ConfigApp::getHardwareConfigType(
    const std::string& hwConfigTypesFile, const std::string& hwBoardId) {
  folly::dynamic hwConfigTypes = folly::dynamic::object;
  try {
    hwConfigTypes = JsonUtils::readJsonFile2DynamicObject(hwConfigTypesFile);
  } catch (const std::invalid_argument& ex) {
    LOG(ERROR) << "Unable to read hardware config types file "
               << hwConfigTypesFile << ": " << folly::exceptionStr(ex);
    return std::nullopt;
  }
  if (!hwConfigTypes.isObject()) {
    LOG(ERROR) << "Hardware config types file is not a JSON object";
    return std::nullopt;
  }
  for (const auto& pair : hwConfigTypes.items()) {
    if (pair.first.isString() && pair.second.isArray()) {
      for (const auto& boardId : pair.second) {
        if (boardId.isString() && boardId.asString() == hwBoardId) {
          return pair.first.asString();
        }
      }
    }
  }
  return std::nullopt;
}

} // namespace minion
} // namespace terragraph
} // namespace facebook
