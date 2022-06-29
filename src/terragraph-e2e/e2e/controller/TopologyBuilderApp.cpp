/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TopologyBuilderApp.h"

#include <chrono>
#include <fbzmq/zmq/Zmq.h>
#include <folly/MapUtil.h>
#include <folly/String.h>

#include "SharedObjects.h"
#include "e2e/common/Consts.h"
#include "e2e/common/SysUtils.h"
#include "e2e/if/gen-cpp2/Topology_types.h"

using apache::thrift::detail::TEnumMapFactory;

using namespace fbzmq;

namespace {
// Timeout duration for START_SCAN_RESP from ScanApp (should be near-immediate)
const std::chrono::seconds kStartScanTimeout{2};

// Timeout duration for TOPOLOGY_SCAN_RESULT from ScanApp
// Estimated topology scan time with QTI firmware:
// - under 4s with ibfNumberOfBeams=31
// - under 12s with ibfNumberOfBeams=61
const std::chrono::seconds kTopologyScanTimeout{15};

// Cooldown duration between successive topology scans, to allow responders to
// reset their stations lists (in firmware code).
const std::chrono::milliseconds kTopologyScanCooldown{2500};

// Amount of time to wait before invoking runNetworkTopologyScanLoop() again in
// response to a WAIT action
const std::chrono::seconds kNetworkTopologyScanWaitTime{5};
}

namespace facebook {
namespace terragraph {

TopologyBuilderApp::TopologyBuilderApp(
    fbzmq::Context& zmqContext,
    const std::string& routerSockUrl,
    const std::string& monitorSockUrl)
    : CtrlApp(
          zmqContext,
          routerSockUrl,
          monitorSockUrl,
          E2EConsts::kTopologyBuilderAppCtrlId) {
  networkTopologyScanTimeout_ = ZmqTimeout::make(this, [this]() noexcept {
    runNetworkTopologyScanLoop();
  });
}

void
TopologyBuilderApp::processMessage(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::START_TOPOLOGY_SCAN:
      processStartTopologyScan(minion, senderApp, message);
      break;
    case thrift::MessageType::START_LINK_DISCOVERY_SCAN:
      processStartLinkDiscoveryScan(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_LINK_DISCOVERY_SCAN_STATUS:
      processGetLinkDiscoveryScanStatus(minion, senderApp, message);
      break;
    case thrift::MessageType::START_SCAN_RESP:
      processStartScanResp(minion, senderApp, message);
      break;
    case thrift::MessageType::TOPOLOGY_SCAN_RESULT:
      processTopologyScanResult(minion, senderApp, message);
      break;
    case thrift::MessageType::START_NETWORK_TOPOLOGY_SCAN:
      processStartNetworkTopologyScan(minion, senderApp, message);
      break;
    case thrift::MessageType::STOP_NETWORK_TOPOLOGY_SCAN:
      processStopNetworkTopologyScan(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_NETWORK_TOPOLOGY_SCAN_STATUS:
      processGetNetworkTopologyScanStatus(minion, senderApp, message);
      break;
    case thrift::MessageType::START_CONTINUOUS_TOPO_SCAN:
      processStartContinuousTopoScan(minion, senderApp, message);
      break;
    case thrift::MessageType::E2E_ACK:
      processE2EAck(minion, senderApp, message);
      break;
    case thrift::MessageType::BULK_ADD_RESULT:
      processBulkAddResult(minion, senderApp, message);
      break;
    default:
      LOG(ERROR)
          << "Wrong type of message ("
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                 message.mType, "UNKNOWN")
          << ") received from " << minion << ":" << senderApp;
  }
}

void
TopologyBuilderApp::processStartTopologyScan(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto startTopologyScan = maybeReadThrift<thrift::StartTopologyScan>(message);
  if (!startTopologyScan) {
    handleInvalidMessage("StartTopologyScan", senderApp, minion);
    return;
  }
  if (builder_.isRunningNetworkTopologyScan()) {
    sendE2EAck(
        senderApp,
        false,
        "A network-wide topology scan is currently running. "
        "Please stop it or wait for it to finish.");
    return;
  }

  // Check if tx node is valid
  std::string txNodeMac;
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  auto txNode = lockedTopologyW->getNodeByMac(startTopologyScan->txNode);
  if (txNode) {
    txNodeMac = startTopologyScan->txNode;
  } else {
    // Backwards compatibility: look up by node name
    txNode = lockedTopologyW->getNode(startTopologyScan->txNode);
    if (txNode) {
      txNodeMac = txNode->wlan_mac_addrs.empty()
        ? txNode->mac_addr
        : txNode->wlan_mac_addrs[0];
    } else {
      sendE2EAck(senderApp, false, "The given Tx node does not exist.");
      return;
    }
  }
  lockedTopologyW.unlock();  // lockedTopologyW -> NULL
  if (txNode->status == thrift::NodeStatusType::OFFLINE) {
    sendE2EAck(senderApp, false, "The given Tx node is offline.");
    return;
  }

  // Reject if there's a pending request
  if (!topoScanSenderApp_.empty()) {
    auto now = std::time(nullptr);
    if (difftime(now, topoScanRequestTime_) < kStartScanTimeout.count()) {
      sendE2EAck(
          senderApp,
          false,
          "A scan request is currently pending. "
          "Please try again in a few seconds.");
      return;
    }

    // We've waited long enough - disregard the previous request
    topoScanSenderApp_.clear();
  }

  VLOG(2) << "Received StartTopologyScan from " << senderApp << " for txNode "
          << txNodeMac << " (" << txNode->name << ")";

  auto txPwrIdx = startTopologyScan->txPwrIndex_ref().has_value() ?
      std::make_optional<int16_t>(startTopologyScan->txPwrIndex_ref().value())
      : std::nullopt;
  sendStartTopologyScan(senderApp, txNodeMac, txPwrIdx);

  // Don't send a reply to senderApp - wait until we receive the scan results
}

void
TopologyBuilderApp::processStartLinkDiscoveryScan(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto startLinkDiscoveryScan =
    maybeReadThrift<thrift::StartLinkDiscoveryScan>(message);
  if (!startLinkDiscoveryScan) {
    handleInvalidMessage("StartLinkDiscoveryScan", senderApp, minion);
    return;
  }
  if (rejectScanRequest(senderApp)) {
    return;
  }

  // Initialize scans
  try {
    auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
    builder_.initLinkDiscoveryScan(*lockedTopologyW, *startLinkDiscoveryScan);
  } catch (const std::exception& e) {
    sendE2EAck(senderApp, false, folly::exceptionStr(e).toStdString());
    return;
  }

  sendE2EAck(senderApp, true, "Link discovery scans started");

  // Start scan loop
  runNetworkTopologyScanLoop();
}

void
TopologyBuilderApp::processGetLinkDiscoveryScanStatus(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  if (!maybeReadThrift<thrift::GetLinkDiscoveryScanStatus>(message)) {
    handleInvalidMessage("GetLinkDiscoveryScanStatus", senderApp, minion);
    return;
  }

  sendToCtrlApp(
      senderApp,
      thrift::MessageType::LINK_DISCOVERY_SCAN_STATUS,
      builder_.getLinkDiscoveryScanStatus());
}

void
TopologyBuilderApp::sendStartTopologyScan(
    const std::string& senderApp,
    const std::string& txNode,
    std::optional<int16_t> txPwrIndex) {
  // Store senderApp
  topoScanSenderApp_ = senderApp;
  topoScanRequestTime_ = std::time(nullptr);

  // Send scan request to ScanApp
  thrift::StartScan startScan;
  startScan.scanType = thrift::ScanType::TOPO;
  startScan.txNode_ref() = txNode;
  startScan.startTime = 0 /* immediate */;
  if (txPwrIndex.has_value()) {
    startScan.txPwrIndex_ref() = txPwrIndex.value();
  }
  sendToCtrlApp(
      E2EConsts::kScanAppCtrlId, thrift::MessageType::START_SCAN, startScan);
}

void
TopologyBuilderApp::processStartScanResp(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto startScanResp = maybeReadThrift<thrift::StartScanResp>(message);
  if (!startScanResp) {
    handleInvalidMessage("StartScanResp", senderApp, minion);
    return;
  }

  // Make sure response is from ScanApp
  if (senderApp != E2EConsts::kScanAppCtrlId) {
    LOG(ERROR) << "Ignoring StartScanResp message from " << senderApp
               << " (expected ScanApp)";
    return;
  }

  // Check if we're expecting this reply
  if (topoScanSenderApp_.empty()) {
    LOG(ERROR) << "Ignoring StartScanResp message (no pending request)";
    return;
  }

  // Response is valid - clear the pending request
  const std::string originalSenderApp = topoScanSenderApp_;
  topoScanSenderApp_.clear();
  topoScanRequestTime_ = 0;

  // Check if scan was started successfully
  if (!startScanResp->success) {
    std::string error = "Failed to start scan: " + startScanResp->message;
    LOG(ERROR) << error;
    sendE2EAck(originalSenderApp, false, error);
    return;
  }
  if (!startScanResp->token_ref().has_value()) {
    std::string error = "No token returned in StartScanResp";
    LOG(ERROR) << error;
    sendE2EAck(originalSenderApp, false, error);
    return;
  }
  const int token = startScanResp->token_ref().value();

  VLOG(2) << "Received StartScanResp for " << originalSenderApp
          << " with token=" << token;

  // Associate the token with original senderApp
  topoScanTokenMap_[token] = originalSenderApp;
  scheduleTimeout(kTopologyScanTimeout, [&, token]() noexcept {
    // Check if the scan finished - assume tokens aren't reused
    auto iter = topoScanTokenMap_.find(token);
    if (iter == topoScanTokenMap_.end()) {
      return;
    }

    // After too much time passes, send a failure to original senderApp
    LOG(ERROR) << "Topology scan timed out for " << iter->second << " (token="
               << token << ")";
    if (iter->second != E2EConsts::kTopologyBuilderAppCtrlId) {
      sendE2EAck(iter->second, false, "Topology scan timed out");
    }
    topoScanTokenMap_.erase(iter);
  });
}

void
TopologyBuilderApp::processTopologyScanResult(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto scanResult = maybeReadThrift<thrift::ScanResult>(message);
  if (!scanResult) {
    handleInvalidMessage("ScanResult", senderApp, minion);
    return;
  }

  // Make sure response is from ScanApp
  if (senderApp != E2EConsts::kScanAppCtrlId) {
    LOG(ERROR) << "Ignoring ScanResult message from " << senderApp
               << " (expected ScanApp)";
    return;
  }

  // Make sure this is a topology scan
  if (scanResult->data.type != thrift::ScanType::TOPO) {
    LOG(ERROR) << "Ignoring ScanResult message (unexpected scan type "
               << static_cast<int>(scanResult->data.type) << ")";
    return;
  }

  // Check if we're expecting this reply
  // (The topology scan could have been initiated by another entity)
  auto iter = topoScanTokenMap_.find(scanResult->token);
  if (iter == topoScanTokenMap_.end()) {
    VLOG(3) << "Ignoring ScanResult message (unknown token "
            << scanResult->token << ")";
    return;
  }

  // Record response time
  lastTopoScanResponseTime_ =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();

  // Get some info about the scan initiator
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  auto maybeTxNode = lockedTopologyW->getNodeByMac(scanResult->data.txNode);
  if (!maybeTxNode) {
    // Backwards compatibility: look up by node name
    maybeTxNode = lockedTopologyW->getNode(scanResult->data.txNode);
  }
  lockedTopologyW.unlock();  // lockedTopologyW -> NULL

  // Process results
  lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  thrift::StartTopologyScanResp response = builder_.processTopologyScanResults(
      *lockedTopologyW, scanResult->data);
  lockedTopologyW.unlock();  // lockedTopologyW -> NULL

  VLOG(2) << "Received ScanResult for txNode " << response.txNode
          << " with " << response.responders.size() << " responders (token="
          << scanResult->token << ")";

  if (iter->second == E2EConsts::kTopologyBuilderAppCtrlId) {
    // Part of network-wide topology scan - handle results
    builder_.handleScanResult(response);
    runNetworkTopologyScanLoop();
  } else {
    // Return response to original senderApp
    sendToCtrlApp(
        iter->second, thrift::MessageType::START_TOPOLOGY_SCAN_RESP, response);
  }
  topoScanTokenMap_.erase(iter);
}

void
TopologyBuilderApp::processStartNetworkTopologyScan(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto req = maybeReadThrift<thrift::StartNetworkTopologyScan>(message);
  if (!req) {
    handleInvalidMessage("StartNetworkTopologyScan", senderApp, minion);
    return;
  }
  if (rejectScanRequest(senderApp)) {
    return;
  }

  // Initialize scans
  try {
    auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
    builder_.initNetworkTopologyScan(*lockedTopologyW, *req);
  } catch (const std::exception& e) {
    sendE2EAck(senderApp, false, folly::exceptionStr(e).toStdString());
    return;
  }

  sendE2EAck(senderApp, true, "Network-wide topology scans started");

  // Start main scan loop
  runNetworkTopologyScanLoop();
}

void
TopologyBuilderApp::processStopNetworkTopologyScan(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto req = maybeReadThrift<thrift::StopNetworkTopologyScan>(message);
  if (!req) {
    handleInvalidMessage("StopNetworkTopologyScan", senderApp, minion);
    return;
  }
  if (!builder_.isRunningNetworkTopologyScan()) {
    sendE2EAck(
        senderApp,
        false,
        "No network-wide topology scan is running");
    return;
  }

  // Stop scans
  VLOG(2) << "Stopping network-wide topology scans...";
  builder_.resetNetworkTopologyScan();
  sendE2EAck(senderApp, true, "Network-wide topology scans stopped");
}

void
TopologyBuilderApp::processGetNetworkTopologyScanStatus(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  if (!maybeReadThrift<thrift::GetNetworkTopologyScanStatus>(message)) {
    handleInvalidMessage("GetNetworkTopologyScanStatus", senderApp, minion);
    return;
  }

  sendToCtrlApp(
      senderApp,
      thrift::MessageType::NETWORK_TOPOLOGY_SCAN_STATUS,
      builder_.getNetworkTopologyScanStatus());
}

void
TopologyBuilderApp::runNetworkTopologyScanLoop() {
  if (networkTopologyScanTimeout_->isScheduled()) {
    networkTopologyScanTimeout_->cancelTimeout();
  }

  // Build map of last status report times (using "steadyTs")
  std::unordered_map<std::string, int64_t> lastStatusReportMap;
  auto lockedStatusReports = SharedObjects::getStatusReports()->rlock();
  for (const auto& status : *lockedStatusReports) {
    auto steadyTsSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        status.second.steadyTs.time_since_epoch()).count();
    lastStatusReportMap[status.first] = steadyTsSeconds;
  }
  lockedStatusReports.unlock();  // lockedStatusReports -> NULL

  // Build map of last config time (using "steadyTs")
  std::unordered_map<std::string, int64_t> lastConfigTimeMap;
  auto lockedConfigHelper = SharedObjects::getConfigHelper()->rlock();
  for (const auto& kv : lockedConfigHelper->getAllConfigStates()) {
    lastConfigTimeMap[kv.first] = kv.second.configTime;
  }
  lockedConfigHelper.unlock();  // lockedConfigHelper -> NULL

  // Invoke scan loop
  // NOTE: call getNetworkTopologyScanReq() before networkTopologyScanLoop()
  //       since the latter may reset all data when the procedure is complete
  const bool dryRun = builder_.getNetworkTopologyScanReq().dryRun;
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  auto action = builder_.networkTopologyScanLoop(
      *lockedTopologyW, lastStatusReportMap, lastConfigTimeMap);
  lockedTopologyW.unlock();  // lockedTopologyW -> NULL

  // Process actions
  if (!action.newNodes.empty() || !action.newLinks.empty()) {
    // Add new nodes/links
    thrift::BulkAdd bulkAdd;
    for (const auto& kv : action.newNodes) {
      bulkAdd.nodes.push_back(kv.second);
    }
    bulkAdd.links = action.newLinks;
    if (dryRun) {
      VLOG(2) << folly::format(
          "[Dry Run] Not adding {} node(s) and {} link(s)",
          bulkAdd.nodes.size(),
          bulkAdd.links.size());
    } else {
      sendToCtrlApp(
          E2EConsts::kTopologyAppCtrlId,
          thrift::MessageType::BULK_ADD,
          bulkAdd);
    }
  }
  switch (action.type) {
    case TopologyBuilder::ActionType::SCAN: {
      const std::string txNode = action.txNode;

      // Start a topology scan
      // We must wait at least 2.5s for responders to remove the last txNode
      // from their stations lists - otherwise they won't respond
      int64_t nowInMs = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
      std::chrono::milliseconds timeSinceResp(
          nowInMs - lastTopoScanResponseTime_);
      std::chrono::milliseconds scanReqTimeout(
          kStartScanTimeout + kTopologyScanTimeout);
      if (timeSinceResp.count() < kTopologyScanCooldown.count()) {
        // Schedule scan request after cooldown
        auto startScanDelay = kTopologyScanCooldown - timeSinceResp;
        VLOG(2) << "Waiting " << startScanDelay.count()
                << "ms before scanning...";
        scheduleTimeout(startScanDelay, [&, txNode]() noexcept {
          sendStartTopologyScan(E2EConsts::kTopologyBuilderAppCtrlId, txNode);
        });
        scanReqTimeout += startScanDelay;
      } else {
        // Send scan request now
        sendStartTopologyScan(
            E2EConsts::kTopologyBuilderAppCtrlId, action.txNode);
      }
      networkTopologyScanTimeout_->scheduleTimeout(scanReqTimeout);
      break;
    }
    case TopologyBuilder::ActionType::WAIT:
      // Wait for nodes to come online
      networkTopologyScanTimeout_->scheduleTimeout(
          kNetworkTopologyScanWaitTime);
      break;
    case TopologyBuilder::ActionType::FINISH:
      // We're done, nothing to do
      break;
  }
}

void
TopologyBuilderApp::processStartContinuousTopoScan(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto req = maybeReadThrift<thrift::StartContinuousTopoScan>(message);
  if (!req) {
    handleInvalidMessage("StartContinuousTopoScan", senderApp, minion);
    return;
  }

  // validate MAC address
  auto maybeNode = SharedObjects::getTopologyWrapper()->rlock()
      ->getNodeByMac(req->radioMac);
  if (!maybeNode) {
    sendE2EAck(
        senderApp, false, "The given MAC address does not belong to any node.");
    return;
  }
  if (maybeNode->mac_addr.empty()) {
    sendE2EAck(senderApp, false, "The given node has no MAC address assigned.");
    return;
  }

  sendToMinionApp(
      maybeNode->mac_addr,
      E2EConsts::kIgnitionAppMinionId,
      thrift::MessageType::START_CONTINUOUS_TOPO_SCAN,
      req.value());

  sendE2EAck(senderApp, true, "Request sent to node: " + maybeNode->name);
}

void
TopologyBuilderApp::processE2EAck(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto ack = maybeReadThrift<thrift::E2EAck>(message);
  if (!ack) {
    handleInvalidMessage("E2EAck", senderApp, minion);
    return;
  }

  // Log the message
  std::string s = folly::sformat(
      "[{}] {}", ack->success ? "Success" : "Failure", ack->message);
  if (!minion.empty()) {
    LOG(ERROR) << "Received unexpected E2EAck from " << minion << ":"
               << senderApp << ": " << s;
  } else {
    VLOG(2) << "Received E2EAck from " << senderApp << ": " << s;
  }
}

void
TopologyBuilderApp::processBulkAddResult(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto result = maybeReadThrift<thrift::BulkAddResult>(message);
  if (!result) {
    handleInvalidMessage("BulkAddResult", senderApp, minion);
    return;
  }

  // Log the message
  std::string s = folly::sformat(
      "[{}] {}", result->success ? "Success" : "Failure", result->message);
  VLOG(2) << "Received E2EAck from " << minion << ":" << senderApp << ": " << s;
}

bool
TopologyBuilderApp::rejectScanRequest(const std::string& senderApp) {
  if (builder_.isRunningNetworkTopologyScan()) {
    sendE2EAck(
        senderApp,
        false,
        "A network-wide topology scan is currently running. "
        "Please stop it or wait for it to finish.");
    return true;
  }

  // Reject if there's a pending request
  if (!topoScanSenderApp_.empty()) {
    auto now = std::time(nullptr);
    if (difftime(now, topoScanRequestTime_) < kStartScanTimeout.count()) {
      sendE2EAck(
          senderApp,
          false,
          "A scan request is currently pending. "
          "Please try again in a few seconds.");
      return true;
    }

    // We've waited long enough - disregard the previous request
    topoScanSenderApp_.clear();
  }

  return false;
}

} // namespace terragraph
} // namespace facebook
