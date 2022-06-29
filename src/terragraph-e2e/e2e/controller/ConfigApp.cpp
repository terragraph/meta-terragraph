/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ConfigApp.h"

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <folly/Conv.h>

#include "GraphHelper.h"
#include "SharedObjects.h"
#include "e2e/common/GpsClock.h"
#include "e2e/common/Md5Utils.h"
#include "e2e/common/TimeUtils.h"
#include "e2e/common/UuidUtils.h"
#include "algorithms/PolarityHelper.h"
#include "algorithms/ControlSuperframeHelper.h"
#include "algorithms/GolayHelper.h"
#include "algorithms/ChannelHelper.h"

using apache::thrift::detail::TEnumMapFactory;
using std::string;

using namespace fbzmq;

DEFINE_bool(
    config_staged_rollout_enabled,
    false,
    "Whether to roll out config changes in a staged manner");

DEFINE_int32(
    config_batch_timeout_s,
    180,
    "Time period in seconds for a config change to be successfully applied to "
    "a node");

DEFINE_int32(
    consecutive_reconfig_delay_s,
    120,
    "The minimum time period in seconds between consecutive failed node "
    "configs");

DEFINE_int32(
    config_actions_req_timeout_s,
    60,
    "Time period in seconds for controller to aggregate node responses to "
    "config actions requests to minions");

DEFINE_string(
    stats_agent_input_sock_url,
    "tcp://localhost:4231",
    "ZMQ URL on which the local stats agent listens for input messages");

DEFINE_int32(
    firmware_parameter_update_delay,
    200,  // 1 BWGD = 25.6ms, so 200 BWGD intervals = ~5 seconds
    "Number of BWGD intervals to delay firmware updates to allow for "
    "synchronization between nodes");

DEFINE_bool(
    unknown_hw_queries_enabled,
    true,
    "Whether to request base configs and metadata from nodes running unknown "
    "hardware. If this is disabled, config management will just be disabled "
    "for those nodes.");

DEFINE_int32(
    unknown_hw_config_req_delay_s,
    15,
    "The minimum time period in seconds between consecutive requests for base "
    "configs from nodes running unknown hardware with the same board ID");

namespace facebook {
namespace terragraph {

ConfigApp::ConfigApp(
    fbzmq::Context& zmqContext,
    const std::string& routerSockUrl,
    const std::string& monitorSockUrl,
    std::chrono::seconds statusReportsInterval,
    std::chrono::seconds topologyInfoSyncInterval,
    pid_t controllerPid)
    : CtrlApp(
          zmqContext,
          routerSockUrl,
          monitorSockUrl,
          E2EConsts::kConfigAppCtrlId),
      agentSock_(
          zmqContext, fbzmq::IdentityString{E2EConsts::kConfigAppCtrlId}),
      controllerPid_(controllerPid) {
  // Periodic status sync
  statusReportsSyncTimeout_ =
      ZmqTimeout::make(this, [this]() noexcept { syncWithStatusReports(); });
  statusReportsSyncTimeout_->scheduleTimeout(statusReportsInterval, true);

  // Periodic topology info sync to update info about neighbor node.
  topologyInfoSyncTimeout_ = ZmqTimeout::make(this, [this]() noexcept {
    syncTopologyInfo();
  });
  topologyInfoSyncTimeout_->scheduleTimeout(topologyInfoSyncInterval, true);

  // Load controller config metadata file
  // On errors, default initialize to prevent segfaults
  try {
    controllerConfigMeta_ = std::make_unique<ConfigMetadata>(
        FLAGS_controller_config_metadata_file,
        true /* keepFullMetadata */,
        true /* hasFlags */);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Error loading controller config metadata: "
               << folly::exceptionStr(ex);
    folly::dynamic obj = folly::dynamic::object;
    controllerConfigMeta_ = std::make_unique<ConfigMetadata>(
        obj, true /* keepFullMetadata */, true /* hasFlags */);
  }

  // Create stats agent socket connection
  VLOG(2) << "Connecting to stats agent at " << FLAGS_stats_agent_input_sock_url
          << "...";
  const auto res = agentSock_.connect(
      fbzmq::SocketUrl{std::string(FLAGS_stats_agent_input_sock_url)});
  if (res.hasError()) {
    LOG(ERROR) << "Failed to connect to "
               << FLAGS_stats_agent_input_sock_url << ": " << res.error();
  }
}

void
ConfigApp::processMessage(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::GET_CTRL_CONFIG_REQ:
      processGetConfigReq(senderApp, message);
      break;
    case thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_REQ:
      processGetConfigNodeOverridesReq(senderApp, message);
      break;
    case thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_REQ:
      processGetConfigAutoNodeOverridesReq(senderApp, message);
      break;
    case thrift::MessageType::GET_CTRL_CONFIG_BASE_REQ:
      processGetConfigBaseReq(senderApp, message);
      break;
    case thrift::MessageType::GET_CTRL_CONFIG_FIRMWARE_BASE_REQ:
      processGetConfigFirmwareBaseReq(senderApp, message);
      break;
    case thrift::MessageType::GET_CTRL_CONFIG_HARDWARE_BASE_REQ:
      processGetConfigHardwareBaseReq(senderApp, message);
      break;
    case thrift::MessageType::GET_CTRL_CONFIG_NETWORK_OVERRIDES_REQ:
      processGetConfigNetworkOverridesReq(senderApp);
      break;
    case thrift::MessageType::SET_CTRL_CONFIG_NODE_OVERRIDES_REQ:
      processSetConfigNodeOverridesReq(senderApp, message);
      break;
    case thrift::MessageType::MODIFY_CTRL_CONFIG_NODE_OVERRIDES_REQ:
      processModifyConfigNodeOverridesReq(senderApp, message);
      break;
    case thrift::MessageType::SET_CTRL_CONFIG_NETWORK_OVERRIDES_REQ:
      processSetConfigNetworkOverridesReq(senderApp, message);
      break;
    case thrift::MessageType::MODIFY_CTRL_CONFIG_NETWORK_OVERRIDES_REQ:
      processModifyConfigNetworkOverridesReq(senderApp, message);
      break;
    case thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_ACTIONS_REQ:
      processGetConfigNodeOverridesActionsReq(senderApp, message);
      break;
    case thrift::MessageType::GET_CTRL_CONFIG_NETWORK_OVERRIDES_ACTIONS_REQ:
      processGetConfigNetworkOverridesActionsReq(senderApp, message);
      break;
    case thrift::MessageType::GET_MINION_CONFIG_ACTIONS_RESP:
      processGetMinionConfigActionsResp(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_CTRL_CONFIG_ACTIONS_RESULTS_REQ:
      processGetConfigActionsResultsReq(senderApp, message);
      break;
    case thrift::MessageType::GET_CTRL_CONFIG_METADATA_REQ:
      processGetConfigMetadata(senderApp);
      break;
    case thrift::MessageType::GET_CTRL_CONFIG_CONTROLLER_REQ:
      processGetControllerConfigReq(senderApp);
      break;
    case thrift::MessageType::SET_CTRL_CONFIG_CONTROLLER_REQ:
      processSetControllerConfigReq(senderApp, message);
      break;
    case thrift::MessageType::GET_CTRL_CONFIG_CONTROLLER_METADATA_REQ:
      processGetControllerConfigMetadata(senderApp);
      break;
    case thrift::MessageType::EDIT_NODE:
      processNodeChanged(senderApp, message);
      break;
    case thrift::MessageType::BSTAR_GET_APP_DATA:
      processBstarGetAppData(senderApp, message);
      break;
    case thrift::MessageType::BSTAR_APP_DATA:
      processBstarAppData(senderApp, message);
      break;
    case thrift::MessageType::TRIGGER_CHANNEL_OPTIMIZATION:
      processTriggerChannelOptimization(senderApp, message);
      break;
    case thrift::MessageType::TRIGGER_POLARITY_OPTIMIZATION:
      processTriggerPolarityOptimization(senderApp, message);
      break;
    case thrift::MessageType::TRIGGER_CONTROL_SUPERFRAME_OPTIMIZATION:
      processTriggerControlSuperframeOptimization(senderApp, message);
      break;
    case thrift::MessageType::TRIGGER_GOLAY_OPTIMIZATION:
      processTriggerGolayOptimization(senderApp, message);
      break;
    case thrift::MessageType::GET_CTRL_CONFIG_PATHS_REQ:
      processGetCtrlConfigPathsReq(senderApp, message);
      break;
    case thrift::MessageType::FW_SET_LOG_CONFIG:
      processSetFwLogConfig(senderApp, message);
      break;
    case thrift::MessageType::CLEAR_AUTO_NODE_OVERRIDES_CONFIG:
      processClearAutoNodeOverridesConfig(senderApp, message);
      break;
    case thrift::MessageType::MINION_BASE_CONFIG:
      processMinionBaseConfig(minion, senderApp, message);
      break;
    case thrift::MessageType::UPDATE_TUNNEL_CONFIG:
      processUpdateTunnelConfig(senderApp, message);
      break;
    default:
      LOG(ERROR)
          << "Wrong type of message ("
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                 message.mType, "UNKNOWN")
          << ") received from " << minion << ":" << senderApp;
      break;
  }
}

void
ConfigApp::syncWithStatusReports() {
  auto statusReports = SharedObjects::getStatusReports()->copy();

  if (FLAGS_config_staged_rollout_enabled && !currBatch_.empty()) {
    // Check if the current batch has timed out
    auto now = TimeUtils::getSteadyTimestamp();
    if (now - batchStartTime_ > FLAGS_config_batch_timeout_s) {
      LOG(INFO) << "Config monitoring timeout! Node(s) failed to report "
                   "status after config: "
                << folly::join(", ", currBatch_);
    } else {
      // There is still a batch processing. Monitor config changes on the
      // current batch
      monitorCurrentBatchRollout(statusReports);
      return;
    }
  }

  // Get the next batch of nodes (if any) to be configured
  currBatch_ = getNodesPendingConfig(statusReports);
  if (currBatch_.empty()) {
    VLOG(4) << "Config is up to date on all reachable nodes";
    return;
  }

  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();

  // If config rollout is enabled, narrow down a batch of nodes that can be
  // configured together without isolating any nodes in the network
  if (FLAGS_config_staged_rollout_enabled) {
    GraphHelper graphHelper;
    currBatch_ = graphHelper.getBatch(*lockedTopologyW, currBatch_, 0);
  }

  // Get the mac addresses for the nodes in currBatch_
  std::unordered_map<string, string> name2MacMap;
  folly::dynamic currBatchArray = folly::dynamic::array;
  for (const auto& nodeName : currBatch_) {
    auto maybeMacAddr = lockedTopologyW->getMacByNodeName(nodeName);
    if (maybeMacAddr) {
      name2MacMap[nodeName] = maybeMacAddr.value();
    }
    currBatchArray.push_back(nodeName);
  }

  // Release lock to avoid holding multiple locks simultaneously
  lockedTopologyW.unlock();  // lockedTopologyW -> NULL

  // Determine the current BWGD index based on GPS time. The BWGD index is used
  // to schedule firmware changes simultaneously across nodes.
  auto now = GpsClock::now();
  uint64_t bwgdIdx;
  if (now.time_since_epoch().count() != 0) {
    // Prefer to use GPS time to calculate BWGD.
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch());
    bwgdIdx = TimeUtils::gpsTimeToBwgd(seconds.count());
  } else {
    // Fall back to system clock if GPS time not available.
    LOG(INFO) << "GPS time not available, using system clock for scheduling";
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch());
    bwgdIdx = TimeUtils::unixTimeToBwgd(seconds.count());
  }

  // Add a few seconds to the current BWGD index, so config has a chance to
  // reach all appropriate nodes before being simultaneously scheduled.
  bwgdIdx += FLAGS_firmware_parameter_update_delay;

  LOG(INFO) << "Setting config for " << currBatch_.size()
            << " node(s) at BWGD index " << bwgdIdx << ": "
            << folly::join(", ", currBatch_);

  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();

  // Send new config to minion for each node
  batchStartTime_ = TimeUtils::getSteadyTimestamp();
  for (const auto& mapIt : name2MacMap) {
    const std::string& nodeName = mapIt.first;
    const std::string& nodeMac = mapIt.second;

    // Look up our computed config state
    auto configState = lockedConfigHelper->getConfigState(nodeName);
    if (!configState) {
      LOG(ERROR) << "No config state for node " << nodeName << ", skipping...";
      currBatch_.erase(nodeName);
      continue;
    }

    // Don't send config to unknown hardware types!!
    // Instead, request hardware base configs and metadata. We'll apply configs
    // after we receive this response.
    if (configState->isUnknownHardware) {
      VLOG(2) << "Skipping config for " << nodeName << " (unknown hardware)";
      if (!FLAGS_unknown_hw_queries_enabled) {
        continue;
      }

      auto iter = unknownHardwareReqMap_.find(configState->hwBoardId);
      if (iter != unknownHardwareReqMap_.end()) {
        if (batchStartTime_ - iter->second.second <
            FLAGS_unknown_hw_config_req_delay_s) {
          // Limit 1 outgoing base config request per hwBoardId (until timeout)
          continue;
        }
      }
      if (batchStartTime_ - configState->baseConfigRequestedTime <
          FLAGS_consecutive_reconfig_delay_s) {
        continue;  // don't request from same node too frequently
      }

      unknownHardwareReqMap_[configState->hwBoardId] =
          std::make_pair(nodeMac, batchStartTime_);
      lockedConfigHelper->setNodeBaseConfigRequestedTime(
          nodeName, batchStartTime_);

      VLOG(2) << "Requesting base configs for unknown hardware board ID '"
              << configState->hwBoardId << "' from " << nodeName;
      thrift::GetMinionBaseConfig req;
      req.getBaseConfigs = true;
      req.getHwConfigs = true;
      req.getMetadata = true;
      sendToMinionApp(
          nodeMac,
          E2EConsts::kConfigAppMinionId,
          thrift::MessageType::GET_MINION_BASE_CONFIG,
          req);
      continue;
    }

    // Send new config
    lockedConfigHelper->setNodeConfigTime(nodeName, batchStartTime_);
    thrift::SetMinionConfigReq setMinionConfigReq;
    setMinionConfigReq.config = configState->configJson;
    setMinionConfigReq.bwgdIdx = bwgdIdx;
    sendToMinionApp(
        nodeMac,
        E2EConsts::kConfigAppMinionId,
        thrift::MessageType::SET_MINION_CONFIG_REQ,
        setMinionConfigReq);
    eventClient_->logEvent(
        thrift::EventCategory::CONFIG,
        thrift::EventId::SET_CONFIG,
        thrift::EventLevel::INFO,
        "Sending new config",
        "",
        std::make_optional(nodeMac),
        std::make_optional(nodeMac),
        std::make_optional(nodeName));
  }
}

void
ConfigApp::syncTopologyInfo() {
  LOCK_TOPOLOGY_AND_CONFIG(lockedTopologyW, rlock, lockedConfigHelper, wlock);

  // Get node names from list of nodes.
  std::vector<std::string> nodeNames =
      folly::gen::from(lockedTopologyW->getAllNodes()) |
      folly::gen::field(&thrift::Node::name) |
      folly::gen::as<std::vector<std::string>>();

  // Update each node's topology info with latest neighbor params.
  std::string errorMsg;
  bool success = lockedConfigHelper->setNodeTopologyInfo(
      nodeNames, *lockedTopologyW, errorMsg);
  if (!success) {
    LOG(ERROR) << "Failed to set node topology info: " << errorMsg;
  }
}

std::unordered_set<string>
ConfigApp::getNodesPendingConfig(
    const std::unordered_map<string, StatusApp::StatusReport>& statusReports) {
  // Copy the full mac2NodeMap to avoid acquiring multiple locks
  auto mac2NodeName =
      SharedObjects::getTopologyWrapper()->rlock()->getMac2NodeNameMap();
  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();

  std::unordered_set<string> nodesPendingConfig;
  for (const auto& statusIt : statusReports) {
    string macAddr = statusIt.first;
    const thrift::StatusReport& report = statusIt.second.report;

    auto nodeNameIt = mac2NodeName.find(macAddr);
    if (nodeNameIt == mac2NodeName.end()) {
      continue;  // not in topology
    }
    string nodeName = nodeNameIt->second;

    // Skip if node reported empty MD5
    // This happens when a node is running a SW version older than RELEASE_M17
    if (report.configMd5.empty()) {
      VLOG(3) << "Skipping config for " << nodeName
              << " (node reported empty config MD5)";
      continue;
    }

    auto configState = lockedConfigHelper->getConfigState(nodeName);
    if (!configState ||
        configState->swVersion != report.version ||
        configState->hwBoardId != report.hardwareBoardId) {
      VLOG(3) << "Initializing config state for " << nodeName
              << " on SW version \"" << report.version << "\" / HW board ID \""
              << report.hardwareBoardId << "\"";
      configState = lockedConfigHelper->initConfigState(
          nodeName,
          report.version,
          report.firmwareVersion,
          report.hardwareBoardId);
      if (!configState) {
        LOG(ERROR) << "Could not set config state for " << nodeName;
        continue;
      }
    } else {
      // Update "unknown hardware" state
      if (configState->isUnknownHardware) {
        lockedConfigHelper->updateNodeIsUnknownHardware(nodeName);
      }
    }

    // Ignore old status
    auto steadyTsSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        statusIt.second.steadyTs.time_since_epoch()).count();
    if (configState->statusTime == steadyTsSeconds) {
      continue;
    }
    lockedConfigHelper->setNodeStatusTime(nodeName, steadyTsSeconds);

    // Skip config if unmanaged
    if (!configState->isManaged) {
      VLOG(3) << "Skipping config for " << nodeName << " (node is not managed)";
      continue;
    }

    // Skip config if MD5 matches
    if (configState->md5 == report.configMd5) {
      VLOG(5) << "Skipping config for " << nodeName << " (MD5 match)";
      continue;
    }

    // Avoid sending multiple configs too soon
    auto now = TimeUtils::getSteadyTimestamp();
    if (now - configState->configTime < FLAGS_consecutive_reconfig_delay_s) {
      LOG(INFO) << "Skipping config for " << nodeName
                << " (avoid back-to-back config: configTime="
                << configState->configTime << ")";
      continue;
    }

    nodesPendingConfig.insert(nodeName);
  }
  return nodesPendingConfig;
}

void
ConfigApp::monitorCurrentBatchRollout(
    const std::unordered_map<string, StatusApp::StatusReport>& statusReports) {
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();

  // Get the mac addresses for the nodes in currBatch_
  std::unordered_map<string, string> name2MacMap;
  for (const auto& nodeName : currBatch_) {
    auto maybeMacAddr = lockedTopologyW->getMacByNodeName(nodeName);
    if (maybeMacAddr) {
      name2MacMap[nodeName] = maybeMacAddr.value();
    }
  }

  // Release lock to avoid holding multiple locks simultaneously
  lockedTopologyW.unlock();  // lockedTopologyW -> NULL

  // Check on the progress of the current batch
  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  for (const auto& mapIt : name2MacMap) {
    auto nodeName = mapIt.first;

    auto statusIt = statusReports.find(mapIt.second);
    if (statusIt == statusReports.end()) {
      continue;  // status reports should persist
    }
    const thrift::StatusReport& report = statusIt->second.report;

    // Check if MD5 of current node matches the report's MD5
    auto configState = lockedConfigHelper->getConfigState(nodeName);
    if (!configState) {
      LOG(ERROR) << "No config state for node " << nodeName << ", skipping...";
      currBatch_.erase(nodeName);
    } else if (configState->md5 == report.configMd5) {
      LOG(INFO) << "Config update for " << nodeName << " is complete";
      currBatch_.erase(nodeName);
    }
  }
}

void
ConfigApp::processGetMinionConfigActionsResp(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  VLOG(4) << "Received response for config actions from " << minion;
  auto configActionsResp =
      maybeReadThrift<thrift::GetMinionConfigActionsResp>(message);
  if (!configActionsResp) {
    handleInvalidMessage(
        "GetMinionConfigActionsResp", senderApp, minion, false);
    return;
  }

  // Record the response
  auto iter = configActionsResults_.find(configActionsResp->id);
  if (iter == configActionsResults_.end()) {
    VLOG(2) << "Discarding config actions report from " << minion
            << " for unknown or expired ID: " << configActionsResp->id;
    return;  // unknown ID
  }
  auto maybeNodeName =
      SharedObjects::getTopologyWrapper()->rlock()->getNodeNameByMac(minion);
  if (!maybeNodeName) {
    LOG(ERROR) << "Discarding config actions report from " << minion
               << " (not in topology)";
    return;  // unknown minion MAC address (shouldn't happen)
  }
  auto& entry = iter->second[maybeNodeName.value()];
  entry.actions = configActionsResp->actions;
}

void
ConfigApp::processGetConfigActionsResultsReq(
    const string& senderApp, const thrift::Message& message) {
  auto request =
      maybeReadThrift<thrift::GetCtrlConfigActionsResultsReq>(message);
  if (!request) {
    handleInvalidMessage("GetCtrlConfigActionsResultsReq", senderApp);
    return;
  }

  // Find results for the given ID
  auto iter = configActionsResults_.find(request->id);
  if (iter == configActionsResults_.end()) {
    sendE2EAck(
        senderApp,
        false,
        "No results for unknown or expired ID: " + request->id);
    return;
  }
  thrift::GetCtrlConfigActionsResultsResp getCtrlConfigActionResultsResp;
  getCtrlConfigActionResultsResp.results = iter->second;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_CTRL_CONFIG_ACTIONS_RESULTS_RESP,
      getCtrlConfigActionResultsResp);
}

void
ConfigApp::processMinionBaseConfig(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  VLOG(2) << "Received base configs from " << minion;
  auto baseConfigResp = maybeReadThrift<thrift::MinionBaseConfig>(message);
  if (!baseConfigResp) {
    handleInvalidMessage("MinionBaseConfig", senderApp, minion, false);
    return;
  }

  if (!FLAGS_unknown_hw_queries_enabled) {
    LOG(ERROR) << "Received MINION_BASE_CONFIG from " << minion
               << " but feature is disabled!";
    return;
  }

  // Validity checks:
  // - Node and config state exist
  // - We actually sent a request to this node
  auto maybeNodeName =
      SharedObjects::getTopologyWrapper()->rlock()->getNodeNameByMac(minion);
  if (!maybeNodeName) {
    LOG(ERROR) << "Discarding base config response from " << minion
               << " (not in topology)";
    return;
  }
  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  auto configState = lockedConfigHelper->getConfigState(maybeNodeName.value());
  if (!configState) {
    LOG(ERROR) << "Discarding base config response from "
               << maybeNodeName.value() << " (config state not found)";
    return;
  }
  auto reqMapIter = unknownHardwareReqMap_.find(configState->hwBoardId);
  if (reqMapIter == unknownHardwareReqMap_.end()) {
    LOG(ERROR) << "Discarding base config response from "
               << maybeNodeName.value() << " (no pending requests for board ID "
               << configState->hwBoardId << ")";
    return;
  }
  if (reqMapIter->second.first != minion) {
    LOG(ERROR) << "Discarding base config response from "
               << maybeNodeName.value() << " (pending request is for "
               << reqMapIter->second.first << ", not " << minion << ")";
    return;
  }

  // Modify our internal config structures based on received data
  // NOTE: For received base and hardware base config, assume we only get one
  //       entry (i.e. the latest config), which is what minion code sends.
  //       We drop the reported "release version" for the config, and instead
  //       use the node's actual software version string to be safe.
  const std::string& swVersion = configState->swVersion;
  const std::string& hwBoardId = configState->hwBoardId;
  if (baseConfigResp->baseConfigs_ref().has_value()) {
    if (!baseConfigResp->baseConfigs_ref().value().empty()) {
      // Add base config
      const auto& configJson =
          baseConfigResp->baseConfigs_ref().value().rbegin()->second;
      lockedConfigHelper->addBaseConfig(swVersion, configJson);
    }
  }
  if (baseConfigResp->hwConfigs_ref().has_value()) {
    // For simplicity, use fake hwType = hwBoardId
    const std::string& hwType = hwBoardId;
    bool configAdded = false;
    if (!baseConfigResp->hwConfigs_ref().value().empty()) {
      // Add hardware base config
      const auto& configJson =
          baseConfigResp->hwConfigs_ref().value().rbegin()->second;
      configAdded = lockedConfigHelper->addHardwareBaseConfig(
          hwType, swVersion, configJson);
    }
    // Add hardware type mapping
    if (configAdded && !lockedConfigHelper->getHardwareTypeMapping(hwBoardId)) {
      lockedConfigHelper->addHardwareTypeMapping(hwBoardId, hwType);
    }
  }
  if (baseConfigResp->metadata_ref().has_value()) {
    // Don't validate thrift::CfgAction enums here; there may be custom values
    // implemented on this hardware
    if (lockedConfigHelper->mergeConfigMetadata(
            baseConfigResp->metadata_ref().value(), false)) {
      LOG(INFO) << "Merged node config metadata for hardware board ID '"
                << hwBoardId << "' from " << maybeNodeName.value();
    }
  }
  lockedConfigHelper.unlock();  // lockedConfigHelper -> NULL

  eventClient_->logEventThrift(
      thrift::EventCategory::CONFIG,
      thrift::EventId::MINION_BASE_CONFIG_RECEIVED,
      thrift::EventLevel::INFO,
      "Received base config from minion running unknown hardware",
      baseConfigResp.value(),
      std::make_optional(minion),
      std::make_optional(minion));
}

void
ConfigApp::processUpdateTunnelConfig(
    const string& senderApp, const thrift::Message& message) {
  auto request = maybeReadThrift<thrift::UpdateTunnelConfig>(message);
  if (!request) {
    handleInvalidMessage("UpdateTunnelConfig", senderApp);
    return;
  }
  SharedObjects::getConfigHelper()->wlock()->onIpAddrChange(
      request->node, request->ipv6Address);
}

void
ConfigApp::processGetConfigReq(
    const string& senderApp, const thrift::Message& message) {
  auto request = maybeReadThrift<thrift::GetCtrlConfigReq>(message);
  if (!request) {
    handleInvalidMessage("GetCtrlConfigReq", senderApp);
    return;
  }

  // Check if node exists in topology
  auto maybeNode =
      SharedObjects::getTopologyWrapper()->rlock()->getNode(request->node);
  if (!maybeNode) {
    sendE2EAck(senderApp, false, "Node " + request->node + " does not exist");
    return;
  }

  // Try to auto-fill some fields if omitted
  string swVersion = request->swVersion_ref().value_or("");
  string hwBoardId = request->hwBoardId_ref().value_or("");
  string fwVersion = request->fwVersion_ref().value_or("");
  if (!request->swVersion_ref().has_value() ||
      !request->hwBoardId_ref().has_value() ||
      !request->fwVersion_ref().has_value()) {
    auto lockedStatusReports = SharedObjects::getStatusReports()->rlock();
    auto iter = lockedStatusReports->find(maybeNode->mac_addr);
    if (iter == lockedStatusReports->end()) {
      // allow omitting fwVersion (optional layer)
      if (!request->swVersion_ref().has_value() ||
          !request->hwBoardId_ref().has_value()) {
        sendE2EAck(
            senderApp,
            false,
            "No known node status. "
            "Please pass the software version and hardware board ID directly.");
          return;
      }
    } else {
      if (!request->swVersion_ref().has_value()) {
        swVersion = iter->second.report.version;
      }
      if (!request->hwBoardId_ref().has_value()) {
        hwBoardId = iter->second.report.hardwareBoardId;
      }
      if (!request->fwVersion_ref().has_value()) {
        fwVersion = iter->second.report.firmwareVersion;
      }
    }
  }

  // Send full node config
  thrift::GetCtrlConfigResp response;
  try {
    auto configObj = SharedObjects::getConfigHelper()->wlock()->buildNodeConfig(
        request->node_ref().value(),
        swVersion,
        fwVersion,
        hwBoardId,
        std::nullopt,
        std::nullopt,
        std::nullopt);
    response.config = folly::toJson(configObj);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not get node config for "
               << request->node_ref().value() << " " << folly::exceptionStr(ex);
    sendE2EAck(
        senderApp, false, "Could not get node config for " +
            request->node_ref().value());
    return;
  }
  sendToCtrlApp(senderApp, thrift::MessageType::GET_CTRL_CONFIG_RESP, response);
}

void
ConfigApp::processGetConfigAutoNodeOverridesReq(
    const string& senderApp, const thrift::Message& message) {
  VLOG(4) << "Request for automated node config overrides from " << senderApp;
  auto request =
      maybeReadThrift<thrift::GetCtrlConfigAutoNodeOverridesReq>(message);
  if (!request) {
    handleInvalidMessage("GetCtrlConfigAutoNodeOverridesReq", senderApp);
    return;
  }

  auto nodeOverrides =
      SharedObjects::getConfigHelper()->rlock()->getAutoNodeOverridesJson(
          request->nodes);

  if (!nodeOverrides) {
    sendE2EAck(senderApp, false, "Could not parse automated node overrides");
    return;
  }

  thrift::GetCtrlConfigAutoNodeOverridesResp getCtrlConfigAutoNodeOverridesResp;
  getCtrlConfigAutoNodeOverridesResp.overrides = *nodeOverrides;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_RESP,
      getCtrlConfigAutoNodeOverridesResp);
}

void
ConfigApp::processGetConfigNodeOverridesReq(
    const string& senderApp, const thrift::Message& message) {
  VLOG(4) << "Request for node config overrides from " << senderApp;
  auto request =
      maybeReadThrift<thrift::GetCtrlConfigNodeOverridesReq>(message);
  if (!request) {
    handleInvalidMessage("GetCtrlConfigNodeOverridesReq", senderApp);
    return;
  }

  auto nodeOverrides =
      SharedObjects::getConfigHelper()->rlock()->getNodeOverridesJson(
          request->nodes);

  if (!nodeOverrides) {
    sendE2EAck(senderApp, false, "Could not parse node overrides");
    return;
  }

  thrift::GetCtrlConfigNodeOverridesResp getCtrlConfigNodeOverridesResp;
  getCtrlConfigNodeOverridesResp.overrides = *nodeOverrides;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_RESP,
      getCtrlConfigNodeOverridesResp);
}

void
ConfigApp::processSetConfigNodeOverridesReq(
    const string& senderApp, const thrift::Message& message) {
  LOG(INFO) << "Request to set nodes config overrides from " << senderApp;
  auto request =
      maybeReadThrift<thrift::SetCtrlConfigNodeOverridesReq>(message);
  if (!request) {
    handleInvalidMessage("SetCtrlConfigNodeOverridesReq", senderApp);
    return;
  }

  if (setNodeOverrides(request->overrides, senderApp, true)) {
    eventClient_->logEventThrift(
        thrift::EventCategory::CONFIG,
        thrift::EventId::CONFIG_MODIFIED,
        thrift::EventLevel::INFO,
        "Set new node overrides",
        request.value());
  }
}

void
ConfigApp::processModifyConfigNodeOverridesReq(
    const string& senderApp, const thrift::Message& message) {
  LOG(INFO) << "Request to modify nodes config overrides from " << senderApp;
  auto request =
      maybeReadThrift<thrift::ModifyCtrlConfigNodeOverridesReq>(message);
  if (!request) {
    handleInvalidMessage("ModifyCtrlConfigNodeOverridesReq", senderApp);
    return;
  }

  if (setNodeOverrides(request->overrides, senderApp, false)) {
    eventClient_->logEventThrift(
        thrift::EventCategory::CONFIG,
        thrift::EventId::CONFIG_MODIFIED,
        thrift::EventLevel::INFO,
        "Modify node overrides",
        request.value());
  }
}

void
ConfigApp::processConfigHooks(
    const std::string& nodeName, const folly::dynamic& newOverrides) {
  // Process node config changes that need a corresponding auto config layer
  // change.
  folly::dynamic newTunnelConfig = folly::dynamic::object;
  // handle config hooks for a single node
  auto tunnelConfigIt = newOverrides.find("tunnelConfig");
  if (tunnelConfigIt != newOverrides.items().end()) {
    // clear all non-matching tunnel names in auto layer
    newTunnelConfig = tunnelConfigIt->second;
    processTunnelConfigHook(nodeName, tunnelConfigIt->second);
  }
  // clear any removed tunnelConfig objects in the auto layer
  std::string errorMsg;
  SharedObjects::getConfigHelper()
      ->wlock()
      ->updateTunnelConfigAutoLayerForNodeOverride(
          nodeName, newTunnelConfig, errorMsg);
}

void
ConfigApp::processTunnelConfigHook(
    const std::string& nodeName,
    const folly::dynamic& newOverrides,
    const std::optional<std::string> ipv6Address) {
  apache::thrift::SimpleJSONSerializer jsonSerializer;
  std::string errorMsg;

  // copy all status reports to avoid lock, this may not be the best way
  auto statusReports = SharedObjects::getStatusReports()->copy();

  std::unordered_map<std::string, std::optional<std::string>>
      tunnelConfigUpdates;
  // hold lock on topology wrapper to map node name -> node mac addr
  auto topologyWrapperLock = SharedObjects::getTopologyWrapper()->rlock();
  for (const auto& tunnelConfigMap : newOverrides.items()) {
    thrift::TunnelConfig tunnelConfig;
    jsonSerializer.deserialize(
        folly::toJson(tunnelConfigMap.second), tunnelConfig);
    std::optional<std::string> dstIp = std::nullopt;
    if (tunnelConfig.dstNodeName_ref().has_value() &&
        !tunnelConfig.dstNodeName_ref().value().empty()) {
      if (ipv6Address) {
        // use optional IPv6 address specified
        dstIp = *ipv6Address;
      } else {
        // map dst node name to node mac
        auto dstNode = topologyWrapperLock->getNode(tunnelConfig
            .dstNodeName_ref().value());
        if (dstNode) {
          // find destination node ip address
          auto statusReportIt = statusReports.find(dstNode->mac_addr);
          if (statusReportIt != statusReports.end()) {
            dstIp = statusReportIt->second.report.ipv6Address;
          }
        } else {
          LOG(WARNING) << "Unable to find given node name \""
                       << tunnelConfig.dstNodeName_ref().value()
                       << "\" for tunnel \""
                       << tunnelConfigMap.first << "\" on \"" << nodeName
                       << "\"";
        }
      }
    }
    tunnelConfigUpdates[tunnelConfigMap.first.asString()] = dstIp;
  }
  topologyWrapperLock.unlock();

  if (!tunnelConfigUpdates.empty()) {
    // bulk update auto config layer even if dstIp is empty (unsets dstIp)
    SharedObjects::getConfigHelper()
        ->wlock()
        ->setAutoNodeSpecificTunnelConfigIp(
            nodeName, tunnelConfigUpdates, errorMsg);
  }
}

bool
ConfigApp::setNodeOverrides(
    const std::string& overrides,
    const std::string& senderApp,
    bool overwrite) {
  // Get all nodes in topology
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  auto topologyNodeNames = folly::gen::from(lockedTopologyW->getAllNodes()) |
                           folly::gen::field(&thrift::Node::name) |
                           folly::gen::as<std::unordered_set<string>>();
  lockedTopologyW.unlock();  // lockedTopologyW -> NULL

  string errMsg;
  folly::dynamic newNodesOverrides = folly::dynamic::object;
  if (!SharedObjects::getConfigHelper()
           ->wlock()
           ->validateAndSetNewNodeOverrides(
               overrides,
               topologyNodeNames,
               newNodesOverrides,
               overwrite,
               errMsg)) {
    LOG(ERROR) << "Setting new node overrides failed: " << errMsg;
    sendE2EAck(senderApp, false, errMsg);
    return false;
  }

  // errMsg might have warning message in case of success
  sendE2EAck(senderApp, true, "Success. " + errMsg);

  folly::dynamic requestOverrides = folly::dynamic::object;
  // parse config overrides string
  try {
    requestOverrides = folly::parseJson(overrides);
  } catch (const std::exception& ex) {
    errMsg = "Could not parse config overrides";
    LOG(ERROR) << errMsg << ": " << folly::exceptionStr(ex);
    return false;
  }
  // process config hooks after validating node configs
  for (const auto& nodeOverrides : requestOverrides.items()) {
    processConfigHooks(nodeOverrides.first.asString(), nodeOverrides.second);
  }

  // Update BinaryStar data with current config
  auto nodeOverrides =
      SharedObjects::getConfigHelper()->rlock()->getNodeOverridesJson({});
  if (!nodeOverrides) {
    LOG(ERROR) << "Error parsing node overrides";
    return true;
  }
  SharedObjects::getSyncedAppData()->wlock()->setNodeOverrides(
      nodeOverrides.value());
  return true;
}

void
ConfigApp::processGetConfigNodeOverridesActionsReq(
    const string& senderApp, const thrift::Message& message) {
  LOG(INFO) << "Request to get nodes config overrides actions from "
            << senderApp;
  auto request =
      maybeReadThrift<thrift::GetCtrlConfigNodeOverridesActionsReq>(message);
  if (!request) {
    handleInvalidMessage("GetCtrlConfigNodeOverridesActionsReq", senderApp);
    return;
  }

  // Get all nodes in topology
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  auto topologyNodeNames = folly::gen::from(lockedTopologyW->getAllNodes()) |
                           folly::gen::field(&thrift::Node::name) |
                           folly::gen::as<std::unordered_set<string>>();
  lockedTopologyW.unlock();  // lockedTopologyW -> NULL

  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  // Validate the new node overrides
  string errMsg;
  folly::dynamic newNodesOverrides = folly::dynamic::object;
  if (!lockedConfigHelper->validateNewNodeOverrides(
          request->overrides, topologyNodeNames, newNodesOverrides, errMsg)) {
    LOG(ERROR) << "Validation of new node overrides failed: " << errMsg;
    sendE2EAck(senderApp, false, errMsg);
    return;
  }

  // Return ack with a unique ID
  auto resp = generateGetConfigActionsResp(request->overrides);
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_CTRL_CONFIG_OVERRIDES_ACTIONS_RESP,
      resp);

  // Send to all affected nodes that we have config state for
  // NOTE: If a SET command was recently processed and a node hasn't reported
  // its status yet, we won't send them a request.
  std::unordered_map<
      string,
      std::pair<folly::dynamic, ConfigHelper::NodeConfigState>>
      newNodeConfigs;
  for (const auto& pair : newNodesOverrides.items()) {
    string nodeName = pair.first.asString();
    auto configState = lockedConfigHelper->getConfigState(nodeName);

    if (!configState || !configState->isManaged) {
      continue;  // skip unknown or unmanaged nodes
    }

    folly::dynamic nodeConfig = lockedConfigHelper->buildNodeConfig(
        nodeName,
        configState->swVersion,
        configState->fwVersion,
        configState->hwBoardId,
        std::nullopt,
        std::nullopt,
        std::make_optional(newNodesOverrides));

    newNodeConfigs[nodeName] = std::make_pair(nodeConfig, configState.value());
  }
  lockedConfigHelper.unlock();  // lockedConfigHelper -> NULL

  for (const auto& kv : newNodeConfigs) {
    sendConfigActionsRequestToMinion(
        resp.id, kv.first, kv.second.second, kv.second.first);
  }
}

void
ConfigApp::processGetConfigNetworkOverridesReq(const string& senderApp) {
  VLOG(4) << "Request for network config overrides from " << senderApp;

  auto networkOverrides =
      SharedObjects::getConfigHelper()->rlock()->getNetworkOverridesJson();

  if (!networkOverrides) {
    sendE2EAck(senderApp, false, "Could not parse network overrides");
    return;
  }

  thrift::GetCtrlConfigNetworkOverridesResp getCtrlConfigNetworkOverridesResp;
  getCtrlConfigNetworkOverridesResp.overrides = *networkOverrides;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_CTRL_CONFIG_NETWORK_OVERRIDES_RESP,
      getCtrlConfigNetworkOverridesResp);
}

void
ConfigApp::processSetConfigNetworkOverridesReq(
    const string& senderApp, const thrift::Message& message) {
  LOG(INFO) << "Request to set network config overrides from " << senderApp;
  auto request =
      maybeReadThrift<thrift::SetCtrlConfigNetworkOverridesReq>(message);
  if (!request) {
    handleInvalidMessage("SetCtrlConfigNetworkOverridesReq", senderApp);
    return;
  }

  if (setNetworkOverrides(request->overrides, senderApp, true)) {
    eventClient_->logEventThrift(
        thrift::EventCategory::CONFIG,
        thrift::EventId::CONFIG_MODIFIED,
        thrift::EventLevel::INFO,
        "Set new network overrides",
        request.value());
  }
}

void
ConfigApp::processModifyConfigNetworkOverridesReq(
    const string& senderApp, const thrift::Message& message) {
  LOG(INFO) << "Request to modify network config overrides from " << senderApp;
  auto request =
      maybeReadThrift<thrift::ModifyCtrlConfigNetworkOverridesReq>(message);
  if (!request) {
    handleInvalidMessage("ModifyCtrlConfigNetworkOverridesReq", senderApp);
    return;
  }

  if (setNetworkOverrides(request->overrides, senderApp, false)) {
    eventClient_->logEventThrift(
        thrift::EventCategory::CONFIG,
        thrift::EventId::CONFIG_MODIFIED,
        thrift::EventLevel::INFO,
        "Modify network overrides",
        request.value());
  }
}

bool
ConfigApp::setNetworkOverrides(
    const std::string& overrides,
    const std::string& senderApp,
    bool overwrite) {
  string errMsg;
  folly::dynamic newNetworkOverrides = folly::dynamic::object;
  if (!SharedObjects::getConfigHelper()
           ->wlock()
           ->validateAndSetNewNetworkOverrides(
               overrides, newNetworkOverrides, overwrite, errMsg)) {
    LOG(ERROR) << "Setting new network overrides failed: " << errMsg;
    sendE2EAck(senderApp, false, errMsg);
    return false;
  }

  // errMsg might have warning message in case of success
  sendE2EAck(senderApp, true, "Success. " + errMsg);

  // Update BinaryStar data with current config
  auto networkOverrides =
      SharedObjects::getConfigHelper()->rlock()->getNetworkOverridesJson();
  if (!networkOverrides) {
    LOG(ERROR) << "Error parsing network overrides";
    return true;
  }
  SharedObjects::getSyncedAppData()->wlock()->setNetworkOverrides(
      networkOverrides.value());
  return true;
}

void
ConfigApp::processGetConfigNetworkOverridesActionsReq(
    const string& senderApp, const thrift::Message& message) {
  LOG(INFO) << "Request to get network config overrides actions from "
            << senderApp;
  auto request =
      maybeReadThrift<thrift::GetCtrlConfigNetworkOverridesActionsReq>(message);
  if (!request) {
    handleInvalidMessage("GetCtrlConfigNetworkOverridesActionsReq", senderApp);
    return;
  }

  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  // Validate the new node overrides
  string errMsg;
  folly::dynamic newNetworkOverrides = folly::dynamic::object;
  if (!lockedConfigHelper->validateNewNetworkOverrides(
          request->overrides, newNetworkOverrides, errMsg)) {
    LOG(ERROR) << "Validation of new network overrides failed: " << errMsg;
    sendE2EAck(senderApp, false, errMsg);
    return;
  }

  // Return ack with a unique ID
  auto resp = generateGetConfigActionsResp(request->overrides);
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_CTRL_CONFIG_OVERRIDES_ACTIONS_RESP,
      resp);

  // Send to all nodes that we have config state for
  // NOTE: If a SET command was recently processed and a node hasn't reported
  // its status yet, we won't send them a request.
  auto configStateMap = lockedConfigHelper->getAllConfigStates();
  std::unordered_map<
      string,
      std::pair<folly::dynamic, ConfigHelper::NodeConfigState>>
      newNodeConfigs;
  for (const auto& kv : configStateMap) {
    auto nodeName = kv.first;
    auto configState = kv.second;

    if (!configState.isManaged) {
      continue;  // skip unmanaged nodes
    }

    folly::dynamic nodeConfig = lockedConfigHelper->buildNodeConfig(
        nodeName,
        configState.swVersion,
        configState.fwVersion,
        configState.hwBoardId,
        std::nullopt,
        std::make_optional(newNetworkOverrides),
        std::nullopt);

    newNodeConfigs[nodeName] = {nodeConfig, configState};
  }
  lockedConfigHelper.unlock();  // lockedConfigHelper -> NULL

  for (const auto& kv : newNodeConfigs) {
    sendConfigActionsRequestToMinion(
        resp.id, kv.first, kv.second.second, kv.second.first);
  }
}

void
ConfigApp::processGetConfigBaseReq(
    const string& senderApp, const thrift::Message& message) {
  VLOG(4) << "Request for base config from " << senderApp;

  auto request = maybeReadThrift<thrift::GetCtrlConfigBaseReq>(message);
  if (!request) {
    handleInvalidMessage("GetCtrlConfigBaseReq", senderApp);
    return;
  }

  string baseConfigs =
      SharedObjects::getConfigHelper()->wlock()->getBaseConfigJson(
          request->swVersions);

  thrift::GetCtrlConfigBaseResp getCtrlConfigBaseResp;
  getCtrlConfigBaseResp.config = baseConfigs;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_CTRL_CONFIG_BASE_RESP,
     getCtrlConfigBaseResp);
}

void
ConfigApp::processGetConfigFirmwareBaseReq(
    const string& senderApp, const thrift::Message& message) {
  VLOG(4) << "Request for firmware base config from " << senderApp;

  auto request = maybeReadThrift<thrift::GetCtrlConfigFirmwareBaseReq>(message);
  if (!request) {
    handleInvalidMessage("GetCtrlConfigFirmwareBaseReq", senderApp);
    return;
  }

  string fwBaseConfigs =
      SharedObjects::getConfigHelper()->wlock()->getFirmwareBaseConfigJson(
          request->fwVersions);

  thrift::GetCtrlConfigFirmwareBaseResp getCtrlConfigFirmwareBaseResp;
  getCtrlConfigFirmwareBaseResp.config = fwBaseConfigs;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_CTRL_CONFIG_FIRMWARE_BASE_RESP,
      getCtrlConfigFirmwareBaseResp);
}

void
ConfigApp::processGetConfigHardwareBaseReq(
    const string& senderApp, const thrift::Message& message) {
  VLOG(4) << "Request for hardware base config from " << senderApp;

  auto request = maybeReadThrift<thrift::GetCtrlConfigHardwareBaseReq>(message);
  if (!request) {
    handleInvalidMessage("GetCtrlConfigHardwareBaseReq", senderApp);
    return;
  }

  string hwBaseConfigs =
      SharedObjects::getConfigHelper()->wlock()->getHardwareBaseConfigJson(
          request->hwBoardIds, request->swVersions);

  thrift::GetCtrlConfigHardwareBaseResp getCtrlConfigHardwareBaseResp;
  getCtrlConfigHardwareBaseResp.config = hwBaseConfigs;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_CTRL_CONFIG_HARDWARE_BASE_RESP,
      getCtrlConfigHardwareBaseResp);
}

void
ConfigApp::processGetConfigMetadata(const string& senderApp) {
  VLOG(4) << "Request for config metadata from " << senderApp;

  auto configMeta =
      SharedObjects::getConfigHelper()->rlock()->getConfigMetadata();

  if (!configMeta) {
    sendE2EAck(senderApp, false, "Could not parse config metadata");
    return;
  }

  thrift::GetCtrlConfigMetadataResp getCtrlConfigMetadataResp;
  getCtrlConfigMetadataResp.metadata = configMeta.value();
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_CTRL_CONFIG_METADATA_RESP,
      getCtrlConfigMetadataResp);
}

void
ConfigApp::processGetControllerConfigReq(const string& senderApp) {
  VLOG(4) << "Request for controller config from " << senderApp;
  thrift::GetCtrlControllerConfigResp getCtrlControllerConfigResp;
  getCtrlControllerConfigResp.config =
      SharedObjects::getE2EConfigWrapper()->rlock()->getConfigJson();
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_CTRL_CONFIG_CONTROLLER_RESP,
      getCtrlControllerConfigResp);
}

void
ConfigApp::processSetControllerConfigReq(
    const string& senderApp, const thrift::Message& message) {
  LOG(INFO) << "Request to set controller config from " << senderApp;
  auto request = maybeReadThrift<thrift::SetCtrlControllerConfigReq>(message);
  if (!request) {
    handleInvalidMessage("SetCtrlControllerConfigReq", senderApp);
    return;
  }

  string errorMsg;
  if (!setControllerConfig(request->config, false, errorMsg)) {
    sendE2EAck(senderApp, false, errorMsg);
    return;
  }

  eventClient_->logEventThrift(
      thrift::EventCategory::CONFIG,
      thrift::EventId::CONFIG_MODIFIED,
      thrift::EventLevel::INFO,
      "Set new controller config",
      request.value());

  sendE2EAck(senderApp, true, "Success.");

  // Update BinaryStar data with current config
  string controllerConfig =
      SharedObjects::getE2EConfigWrapper()->rlock()->getConfigJson();
  SharedObjects::getSyncedAppData()->wlock()->setControllerConfig(
      controllerConfig);
}

bool
ConfigApp::setControllerConfig(
    const string& controllerConfig, bool fromBstarSync, string& errorMsg) {
  // Try to set the config (wrapper performs all validation)
  auto maybeConfigDiff =
      SharedObjects::getE2EConfigWrapper()->wlock()->setConfig(
          controllerConfig, controllerConfigMeta_.get(), fromBstarSync);
  if (maybeConfigDiff.hasError()) {
    errorMsg = "Could not set controller config: " + maybeConfigDiff.error();
    return false;
  }

  VLOG(1) << "Changed or removed config values:\n"
          << folly::toJson(maybeConfigDiff.value());

  // Determine what actions to perform based on the config diff
  auto controllerActions =
      controllerConfigMeta_->getActions(maybeConfigDiff.value());

  // Perform controller actions
  performControllerConfigActions(controllerActions);

  return true;
}

void
ConfigApp::processGetControllerConfigMetadata(const string& senderApp) {
  VLOG(4) << "Request for controller config metadata from " << senderApp;

  thrift::GetCtrlControllerConfigMetadataResp response;
  try {
    response.metadata = folly::toJson(controllerConfigMeta_->get());
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not parse controller config metadata: "
               << folly::exceptionStr(ex);
    sendE2EAck(senderApp, false, "Could not parse controller config metadata");
    return;
  }

  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_CTRL_CONFIG_CONTROLLER_METADATA_RESP,
      response);
}

void
ConfigApp::processNodeChanged(
    const string& senderApp, const thrift::Message& message) {
  VLOG(3) << "Request to edit node from " << senderApp;
  auto editNode = maybeReadThrift<thrift::EditNode>(message);
  if (!editNode) {
    handleInvalidMessage("EditNode", senderApp, "", false);
    return;
  }

  // Basic validity checks
  string oldNodeName = editNode->nodeName;
  string newNodeName = editNode->newNode.name;
  if (senderApp != E2EConsts::kTopologyAppCtrlId) {
    LOG(INFO) << "Ignoring EditNode message from " << senderApp
              << " (expecting " << E2EConsts::kTopologyAppCtrlId << ")";
    return;  // invalid sender app
  }
  if (oldNodeName.empty() || newNodeName.empty()) {
    LOG(INFO) << "Ignoring EditNode message from " << senderApp
              << " (empty node name field)";
    return;  // empty node name(s)
  }
  if (oldNodeName == newNodeName) {
    LOG(INFO) << "Ignoring EditNode message from " << senderApp
              << " (node name didn't change)";
    return;  // no name change
  }

  if (!SharedObjects::getConfigHelper()->wlock()->migrateNodeOverrides(
          oldNodeName, newNodeName)) {
    LOG(INFO) << "Unable to migrate config from " << oldNodeName << " to "
              << newNodeName;
    return;  // failed name change
  }

  // Update BinaryStar data with current config
  auto nodeOverrides =
      SharedObjects::getConfigHelper()->rlock()->getNodeOverridesJson({});
  if (!nodeOverrides) {
    LOG(ERROR) << "Error parsing node overrides";
    return;
  }
  SharedObjects::getSyncedAppData()->wlock()->setNodeOverrides(
      nodeOverrides.value());
}

void
ConfigApp::processBstarGetAppData(
    const string& senderApp, const thrift::Message& message) {
  if (!maybeReadThrift<thrift::BinaryStarGetAppData>(message)) {
    handleInvalidMessage("BinaryStarGetAppData", senderApp);
    return;
  }

  // Get current config
  auto lockedConfigHelper = SharedObjects::getConfigHelper()->rlock();
  auto networkOverrides = lockedConfigHelper->getNetworkOverridesJson();
  if (!networkOverrides) {
    LOG(ERROR) << "Error parsing network overrides";
    return;
  }
  auto nodeOverrides = lockedConfigHelper->getNodeOverridesJson({});
  if (!nodeOverrides) {
    LOG(ERROR) << "Error parsing node overrides";
    return;
  }
  auto autoNodeOverrides = lockedConfigHelper->getAutoNodeOverridesJson({});
  if (!autoNodeOverrides) {
    LOG(ERROR) << "Error parsing auto node overrides";
    return;
  }
  lockedConfigHelper.unlock();  // lockedConfigHelper -> NULL
  string controllerConfig =
      SharedObjects::getE2EConfigWrapper()->rlock()->getConfigJson();

  // Update BinaryStar data with current config
  auto lockedSyncedAppData = SharedObjects::getSyncedAppData()->wlock();
  lockedSyncedAppData->setNetworkOverrides(networkOverrides.value());
  lockedSyncedAppData->setNodeOverrides(nodeOverrides.value());
  lockedSyncedAppData->setAutoNodeOverrides(autoNodeOverrides.value());
  lockedSyncedAppData->setControllerConfig(controllerConfig);
}

void
ConfigApp::processBstarAppData(
    const string& senderApp, const thrift::Message& message) {
  auto data = maybeReadThrift<thrift::BinaryStarAppData>(message);
  if (!data) {
    handleInvalidMessage("BinaryStarAppData", senderApp);
    return;
  }

  // Received new config from active controller, so replace ours
  // NOTE: Assume these are validated already, so don't validate here
  LOG(INFO) << "Received new config from " << senderApp
            << ", overwriting existing config...";

  // Replace config overrides
  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  if (data->configNetworkOverrides_ref().has_value()) {
    LOG(INFO) << "> Overwriting network overrides...";
    string errMsg;
    if (!lockedConfigHelper->setNewNetworkOverrides(
            data->configNetworkOverrides_ref().value(), errMsg)) {
      LOG(ERROR) << "Unable to write network config overrides: " << errMsg;
    }
  }
  if (data->configNodeOverrides_ref().has_value()) {
    LOG(INFO) << "> Overwriting node overrides...";
    string errMsg;
    if (!lockedConfigHelper->setNewNodeOverrides(
            data->configNodeOverrides_ref().value(), errMsg)) {
      LOG(ERROR) << "Unable to write node config overrides: " << errMsg;
    }
  }
  if (data->configAutoNodeOverrides_ref().has_value()) {
    LOG(INFO) << "> Overwriting auto node overrides...";
    string errMsg;
    if (!lockedConfigHelper->setNewAutoNodeOverrides(
            data->configAutoNodeOverrides_ref().value(), errMsg)) {
      LOG(ERROR) << "Unable to write auto node config overrides: " << errMsg;
    }
  }
  lockedConfigHelper.unlock();  // lockedConfigHelper -> NULL

  // Replace controller config, except for unsynced keys
  if (data->configController_ref().has_value()) {
    LOG(INFO) << "> Overwriting controller config...";
    string errMsg;
    if (!setControllerConfig(data->configController_ref().value(), true,
        errMsg)) {
      LOG(ERROR) << "Unable to write controller config: " << errMsg;
    }
  }
}

bool
ConfigApp::sendConfigActionsRequestToMinion(
    const string& id,
    const string& nodeName,
    const ConfigHelper::NodeConfigState& configState,
    const folly::dynamic& nodeConfig) {

  string configJson = JsonUtils::toSortedPrettyJson(nodeConfig);
  string configMd5 = Md5Utils::computeMd5(configJson);

  // If our config didn't change, don't send request
  if (configMd5 == configState.md5) {
    return false;
  }

  // Get the minion MAC address
  auto maybeMacAddr =
      SharedObjects::getTopologyWrapper()->rlock()->getMacByNodeName(nodeName);
  if (!maybeMacAddr) {
    return false;  // not in topology
  }

  // Send request message to minion
  VLOG(4) << "Sending config actions request to " << nodeName << " (id=" << id
          << ")";
  thrift::GetMinionConfigActionsReq getMinionConfigActionsReq;
  getMinionConfigActionsReq.config = configJson;
  getMinionConfigActionsReq.id = id;
  sendToMinionApp(
      maybeMacAddr.value(),
      E2EConsts::kConfigAppMinionId,
      thrift::MessageType::GET_MINION_CONFIG_ACTIONS_REQ,
      getMinionConfigActionsReq);

  return true;
}

void
ConfigApp::performControllerConfigActions(
    const std::unordered_map<thrift::CfgAction, std::vector<std::string>>&
        controllerActions) {
  for (const auto& kv : controllerActions) {
    thrift::CfgAction action = kv.first;
    switch (action) {
      case thrift::CfgAction::UPDATE_GLOG_LEVEL: {
        // Update glog's VLOG level
        const auto& flags =
            SharedObjects::getE2EConfigWrapper()->rlock()->getConfig()->flags;
        auto iter = flags.find("v");
        if (iter != flags.end()) {
          auto v = folly::tryTo<int32_t>(iter->second);
          if (v.hasValue() && FLAGS_v != v.value()) {
            LOG(INFO) << "Updating VLOG level to " << v.value() << "...";
            FLAGS_v = v.value();
          }
        }
        break;
      }
      case thrift::CfgAction::UPDATE_GFLAG: {
        // Reload gflag values
        const auto& flags =
            SharedObjects::getE2EConfigWrapper()->rlock()->getConfig()->flags;
        for (const std::string& key : kv.second) {
          // Basic validation
          std::vector<std::string> k = ConfigMetadata::splitFullKey(key);
          if (k.size() != 2 || k[0] != "flags") {
            LOG(ERROR) << "UPDATE_GFLAG: Unsupported key: " << key;
            continue;
          }
          const std::string& flagName = k[1];
          google::CommandLineFlagInfo flagInfo;
          if (!google::GetCommandLineFlagInfo(flagName.c_str(), &flagInfo)) {
            LOG(ERROR) << "UPDATE_GFLAG: Unknown flag: " << flagName;
            continue;
          }

          // Retrieve and set new flag value
          std::string val;
          auto iter = flags.find(flagName);
          if (iter != flags.end()) {
            val = iter->second;
          } else {
            // value was deleted, revert to default
            val = flagInfo.default_value;
          }
          std::string result =
              google::SetCommandLineOption(flagName.c_str(), val.c_str());
          if (!result.empty()) {
            LOG(INFO) << "Set gflag '" << flagName << "' value to: " << val;
          } else {
            LOG(ERROR) << "UPDATE_GFLAG: Failed to update gflag '" << flagName
                       << "' value to: " << val;
          }
        }
        break;
      }
      case thrift::CfgAction::UPDATE_SCAN_CONFIG: {
        // Notify ScanApp to update scan timers
        LOG(INFO) << "Updating scan config...";
        sendToCtrlApp(
            E2EConsts::kScanAppCtrlId,
            thrift::MessageType::SCAN_SCHEDULE_UPDATED,
            thrift::ScanScheduleUpdated());
        break;
      }
      case thrift::CfgAction::UPDATE_PREFIX_ALLOC_PARAMS: {
        // Notify TopologyApp to update network prefix
        LOG(INFO) << "Updating network prefix...";
        sendToCtrlApp(
            E2EConsts::kTopologyAppCtrlId,
            thrift::MessageType::PREFIX_ALLOC_PARAMS_UPDATED,
            thrift::PrefixAllocParamsUpdated());
        break;
      }
      case thrift::CfgAction::RESTART_STATS_AGENT: {
        // Restart stats agent
        LOG(INFO) << "Restarting local stats agent...";
        thrift::Message msg;
        msg.mType = thrift::MessageType::RESTART;
        msg.value =
            fbzmq::util::writeThriftObjStr(thrift::Empty(), serializer_);
        const auto sendRet = agentSock_.sendOne(
            fbzmq::Message::fromThriftObj(msg, serializer_).value());
        if (sendRet.hasError()) {
          LOG(ERROR) << "Error sending to stats agent: " << sendRet.error();
        }
        break;
      }
      case thrift::CfgAction::REBOOT: {
        // Schedule reboot
        LOG(INFO) << "Controller process rebooting...";
        scheduleTimeout(std::chrono::seconds(2), [&]() noexcept {
          kill(controllerPid_, SIGTERM);
        });
        break;
      }
      case thrift::CfgAction::UPDATE_CHANNEL_ASSIGNMENT: {
        // Reassign channels if necessary
        LOG(INFO) << "Updating channel assignment (if needed)...";
        LOCK_TOPOLOGY_AND_CONFIG(
            lockedTopologyW, rlock, lockedConfigHelper, wlock);
        ChannelHelper::validateTopologyChannels(
            *lockedTopologyW, *lockedConfigHelper);
        break;
      }
      default:
        break;
    }
  }
}

thrift::GetCtrlConfigOverridesActionsResp
ConfigApp::generateGetConfigActionsResp(const string& overrides) {
  // Create response
  thrift::GetCtrlConfigOverridesActionsResp resp;
  resp.overrides = overrides;
  resp.id = UuidUtils::genUuid();
  resp.availableUntil = std::time(nullptr) + FLAGS_config_actions_req_timeout_s;

  // Create entry for this ID
  configActionsResults_.emplace(resp.id, ConfigActionsResults());

  // Schedule timeout to discard these results
  scheduleTimeout(
      std::chrono::seconds(FLAGS_config_actions_req_timeout_s),
      [&, resp ]() noexcept { configActionsResults_.erase(resp.id); });

  return resp;
}

bool
ConfigApp::performPolarityOptimization(
    bool clearUserPolarityConfig, std::vector<std::string>& errMsgs) {
  std::vector<thrift::Node> allNodes =
      SharedObjects::getTopologyWrapper()->rlock()->getAllNodes();

  // Get existing user-configured and automatically assigned polarities
  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  auto macToUserPolarity = lockedConfigHelper->getPolarities(allNodes, true);
  auto macToOldPolarity = lockedConfigHelper->getPolarities(allNodes, false);
  lockedConfigHelper.unlock(); // lockedConfigHelper -> NULL

  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  std::unordered_map<std::string, thrift::PolarityType> macToNewPolarity;
  std::unordered_map<std::string, thrift::PolarityType> emptyMacToPolarityMap;

  // Invoke polarity optimization
  bool ret = PolarityHelper::optimizePolarity(
      *lockedTopologyW,
      clearUserPolarityConfig ? emptyMacToPolarityMap : macToUserPolarity,
      macToOldPolarity,
      macToNewPolarity,
      errMsgs);
  if (!ret) {
    return false;
  }

  // Copy mac2NodeName before releasing the lock
  auto mac2NodeName = lockedTopologyW->getMac2NodeNameMap();
  lockedTopologyW.unlock(); // lockedTopologyW -> NULL

  lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  if (clearUserPolarityConfig) {
    // Clear user-configured polarities
    auto clearPolaritiesMap =
        folly::gen::from(macToUserPolarity) |
        folly::gen::eachAs<std::pair<std::string, thrift::PolarityType>&>() |
        folly::gen::map([](const auto& iter) {
          return std::make_pair(iter.first, std::nullopt);
        }) |
        folly::gen::as<std::unordered_map<
            std::string, std::optional<thrift::PolarityType>>>();
    std::string errMsg;
    if (!lockedConfigHelper->setNodePolarities(
            clearPolaritiesMap, mac2NodeName, true, errMsg)) {
      errMsgs.push_back(errMsg);
      return false;
    }
  } else {
    // Check if we had to override any of the user-configured polarities
    std::vector<std::string> conflictingPolarityMacs;
    for (const auto& newPolarityIt : macToNewPolarity) {
      auto userPolarityIt = macToUserPolarity.find(newPolarityIt.first);
      if (userPolarityIt != macToUserPolarity.end()) {
        if (userPolarityIt->second != newPolarityIt.second) {
          VLOG(3) << folly::format(
              "Mismatch with user-set polarity: mac {} User {} New {}",
              newPolarityIt.first,
              static_cast<int>(userPolarityIt->second),
              static_cast<int>(newPolarityIt.second));
          conflictingPolarityMacs.push_back(newPolarityIt.first);
        }
      }
    }

    if (!conflictingPolarityMacs.empty()) {
      errMsgs.push_back(folly::sformat(
          "Unable to respect user polarity config at: {}",
          folly::join(", ", conflictingPolarityMacs)));
      return false;
    }
  }

  // Apply the new auto polarity assignment
  // Convert first to the appropriate format
  auto autoNodePolarities =
      folly::gen::from(macToNewPolarity) |
      folly::gen::eachAs<std::pair<std::string, thrift::PolarityType>&>() |
      folly::gen::map([](const auto& iter) {
        return std::make_pair(iter.first, std::make_optional(iter.second));
      }) |
      folly::gen::as<std::unordered_map<
          std::string, std::optional<thrift::PolarityType>>>();

  std::string errMsg;
  if (!lockedConfigHelper->setNodePolarities(
          autoNodePolarities, mac2NodeName, false, errMsg)) {
    errMsgs.push_back(errMsg);
    return false;
  }

  return true;
}

void
ConfigApp::processTriggerPolarityOptimization(
    const string& senderApp, const thrift::Message& message) {
  auto request = maybeReadThrift<thrift::TriggerPolarityOptimization>(message);
  if (!request) {
    handleInvalidMessage("TriggerPolarityOptimization", senderApp);
    return;
  }

  LOG(INFO) << folly::format(
      "Received a request to perform network-wide polarity optimization "
      "from {} with `clearUserPolarityConfig` flag set to {}",
      senderApp,
      request->clearUserPolarityConfig);

  // Perform optimization
  std::vector<std::string> errMsgs;
  if (!performPolarityOptimization(request->clearUserPolarityConfig, errMsgs)) {
    std::string errorMsg =
        "Polarity optimization failed. " + folly::join(", ", errMsgs);
    LOG(ERROR) << errorMsg;
    eventClient_->logEvent(
        thrift::EventCategory::CONFIG,
        thrift::EventId::CONFIG_MODIFIED,
        thrift::EventLevel::ERROR,
        errorMsg);
    sendE2EAck(senderApp, false, errorMsg);
  }

  eventClient_->logEventThrift(
      thrift::EventCategory::CONFIG,
      thrift::EventId::CONFIG_MODIFIED,
      thrift::EventLevel::INFO,
      "Performed network-wide polarity optimization",
      request.value());

  sendE2EAck(senderApp, true, "Success");
}

void
ConfigApp::processTriggerControlSuperframeOptimization(
    const string& senderApp, const thrift::Message& message) {
  auto request =
      maybeReadThrift<thrift::TriggerControlSuperframeOptimization>(message);
  if (!request) {
    handleInvalidMessage("TriggerControlSuperframeOptimization", senderApp);
    return;
  }

  LOG(INFO) << folly::format(
      "Received a request to perform network-wide control superframe "
      "reassignment from {} with `clearUserConfig` flag set to {}",
      senderApp,
      request->clearUserConfig);

  // Perform optimization
  try {
    LOCK_TOPOLOGY_AND_CONFIG(lockedTopologyW, rlock, lockedConfigHelper, wlock);
    ControlSuperframeHelper::assignAllControlSuperframes(
        *lockedTopologyW, *lockedConfigHelper, request->clearUserConfig);
  } catch (const std::exception& ex) {
    std::string errorMsg = "Control superframe optimization failed. " +
                           folly::exceptionStr(ex).toStdString();
    LOG(ERROR) << errorMsg;
    eventClient_->logEvent(
        thrift::EventCategory::CONFIG,
        thrift::EventId::CONFIG_MODIFIED,
        thrift::EventLevel::ERROR,
        errorMsg);
    sendE2EAck(senderApp, false, errorMsg);
    return;
  }

  eventClient_->logEventThrift(
      thrift::EventCategory::CONFIG,
      thrift::EventId::CONFIG_MODIFIED,
      thrift::EventLevel::INFO,
      "Performed network-wide control superframe optimization",
      request.value());

  sendE2EAck(senderApp, true, "Success");
}

void
ConfigApp::processTriggerGolayOptimization(
    const string& senderApp, const thrift::Message& message) {
  auto request = maybeReadThrift<thrift::TriggerGolayOptimization>(message);
  if (!request) {
    handleInvalidMessage("TriggerGolayOptimization", senderApp);
    return;
  }

  LOG(INFO) << folly::format(
      "Received a request to perform network-wide Golay reassignment from {} "
      "with `clearUserConfig` flag set to {}",
      senderApp,
      request->clearUserConfig);

  // Perform optimization
  try {
    LOCK_TOPOLOGY_AND_CONFIG(lockedTopologyW, rlock, lockedConfigHelper, wlock);
    GolayHelper::assignNetworkGolay(
        *lockedTopologyW, *lockedConfigHelper, request->clearUserConfig);
  } catch (const std::exception& ex) {
    std::string errorMsg =
        "Golay optimization failed. " + folly::exceptionStr(ex).toStdString();
    LOG(ERROR) << errorMsg;
    eventClient_->logEvent(
        thrift::EventCategory::CONFIG,
        thrift::EventId::CONFIG_MODIFIED,
        thrift::EventLevel::ERROR,
        errorMsg);
    sendE2EAck(senderApp, false, errorMsg);
    return;
  }

  eventClient_->logEventThrift(
      thrift::EventCategory::CONFIG,
      thrift::EventId::CONFIG_MODIFIED,
      thrift::EventLevel::INFO,
      "Performed network-wide Golay optimization",
      request.value());

  sendE2EAck(senderApp, true, "Success");
}

void
ConfigApp::processTriggerChannelOptimization(
    const string& senderApp, const thrift::Message& message) {
  auto request = maybeReadThrift<thrift::TriggerChannelOptimization>(message);
  if (!request) {
    handleInvalidMessage("TriggerChannelOptimization", senderApp);
    return;
  }

  LOG(INFO) << folly::format(
      "Received a request to perform network-wide channel optimization "
      "from {} with `clearUserChannelConfig` flag set to {}",
      senderApp,
      request->clearUserChannelConfig);

  // Perform optimization
  try {
    LOCK_TOPOLOGY_AND_CONFIG(lockedTopologyW, rlock, lockedConfigHelper, wlock);
    ChannelHelper::assignNetworkChannels(
        *lockedTopologyW, *lockedConfigHelper, request->clearUserChannelConfig);
  } catch (const std::exception& ex) {
    std::string errorMsg =
        "Channel optimization failed. " + folly::exceptionStr(ex).toStdString();
    LOG(ERROR) << errorMsg;
    eventClient_->logEvent(
        thrift::EventCategory::CONFIG,
        thrift::EventId::CONFIG_MODIFIED,
        thrift::EventLevel::ERROR,
        errorMsg);
    sendE2EAck(senderApp, false, errorMsg);
    return;
  }

  eventClient_->logEventThrift(
      thrift::EventCategory::CONFIG,
      thrift::EventId::CONFIG_MODIFIED,
      thrift::EventLevel::INFO,
      "Performed network-wide channel optimization",
      request.value());

  sendE2EAck(senderApp, true, "Success");
}

void
ConfigApp::processGetCtrlConfigPathsReq(
    const string& senderApp, const thrift::Message& message) {
  auto request = maybeReadThrift<thrift::GetCtrlConfigPathsReq>(message);
  if (!request) {
    handleInvalidMessage("GetCtrlConfigPathsReq", senderApp);
    return;
  }

  VLOG(4) << folly::format(
      "Received a request for config paths from {} for {} node(s) and {} "
      "path(s)",
      senderApp,
      request->nodeNames.size(),
      request->configPaths.size());

  // Get all nodes in topology
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  auto topologyNodeNames = folly::gen::from(lockedTopologyW->getAllNodes()) |
                           folly::gen::field(&thrift::Node::name) |
                           folly::gen::as<std::unordered_set<string>>();
  lockedTopologyW.unlock(); // lockedTopologyW -> NULL

  std::vector<std::string> nodeNames(request->nodeNames);
  if (request->nodeNames.empty()) {
    nodeNames = folly::gen::from(topologyNodeNames) |
                folly::gen::as<std::vector<string>>();
  }

  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  std::unordered_map<std::string, std::string> configResp;
  for (const auto& nodeName : nodeNames) {
    if (topologyNodeNames.count(nodeName)) {
      std::optional<folly::dynamic> configObj =
          lockedConfigHelper->getNodeConfigPaths(
              nodeName, request->configPaths);
      if (configObj) {
        try {
          configResp[nodeName] = folly::toJson(configObj.value());
        } catch (const std::exception& ex) {
          LOG(ERROR) << folly::format(
              "Error converting config object to json string for node {}",
              nodeName);
        }
      }
    }
  }

  thrift::GetCtrlConfigPathsResp getCtrlConfigPathsResp;
  getCtrlConfigPathsResp.config = configResp;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_CTRL_CONFIG_PATHS_RESP,
      getCtrlConfigPathsResp);
}

void
ConfigApp::processSetFwLogConfig(
    const std::string& senderApp, const thrift::Message& message) {
  auto request = maybeReadThrift<thrift::SetFwLogConfig>(message);
  if (!request) {
    handleInvalidMessage("SetFwLogConfig", senderApp);
    return;
  }

  VLOG(4) << "Received a request to set firmware log configuration from "
          << senderApp;

  // Determine configs to use
  std::unordered_map<thrift::LogModule, thrift::LogLevel> configs;
  if (request->configs_ref().has_value() &&
      !request->configs_ref().value().empty()) {
    configs = request->configs_ref().value();
  } else if (request->level_ref().has_value()) {
    for (const auto& kv :
        TEnumMapFactory<thrift::LogModule>::makeValuesToNamesMap()) {
      configs[kv.first] = request->level_ref().value();
    }
  } else {
    sendE2EAck(senderApp, false, "Invalid request");
    return;
  }

  // Find target nodes
  std::vector<thrift::Node> nodeList;
  if (request->nodes.empty()) {
    nodeList = SharedObjects::getTopologyWrapper()->rlock()->getAllNodes();
  } else {
    auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
    for (const std::string& nodeName : request->nodes) {
      auto maybeNode = lockedTopologyW->getNode(nodeName);
      if (!maybeNode) {
        sendE2EAck(senderApp, false, "Could not find node: " + nodeName);
        return;
      }
      nodeList.push_back(*maybeNode);
    }
  }

  // Send request to nodes
  thrift::SetLogConfig setLogConfig;
  setLogConfig.configs = configs;
  for (const auto& node : nodeList) {
    if (node.mac_addr.empty()) {
      continue;
    }
    sendToMinionApp(
        node.mac_addr,
        E2EConsts::kConfigAppMinionId,
        thrift::MessageType::FW_SET_LOG_CONFIG,
        setLogConfig);
  }

  sendE2EAck(senderApp, true, "Success");
}

void
ConfigApp::processClearAutoNodeOverridesConfig(
    const string& senderApp, const thrift::Message& message) {
  auto request = maybeReadThrift<thrift::ClearAutoNodeOverridesConfig>(message);
  if (!request) {
    handleInvalidMessage("ClearAutoNodeOverridesConfig", senderApp);
    return;
  }

  LOG(INFO) << folly::format(
      "Received a request to clear auto layer configs paths from {} for "
      " {} node(s) and {} path(s)",
      senderApp,
      request->nodeNames.size(),
      request->configPaths.size());

  // Get all nodes in topology
  std::vector<std::string> nodeNames(request->nodeNames);
  if (request->nodeNames.empty()) {
    auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
    nodeNames = folly::gen::from(lockedTopologyW->getAllNodes()) |
                                 folly::gen::field(&thrift::Node::name) |
                                 folly::gen::as<std::vector<string>>();
  }

  std::string errorMsg;
  if (!SharedObjects::getConfigHelper()->wlock()->clearAutoNodeOverrides(
        nodeNames, request->configPaths, errorMsg)) {
    sendE2EAck(senderApp, false, errorMsg);
    return;
  }

  sendE2EAck(senderApp, true, "Success");
}

} // namespace terragraph
} // namespace facebook
