/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TrafficApp.h"

#include <fbzmq/zmq/Zmq.h>
#include <folly/MapUtil.h>

#include "SharedObjects.h"
#include "e2e/common/Consts.h"
#include "e2e/common/MacUtils.h"
#include "e2e/common/OpenrUtils.h"
#include "e2e/common/UuidUtils.h"

using apache::thrift::detail::TEnumMapFactory;

using namespace fbzmq;

namespace facebook {
namespace terragraph {

TrafficApp::TrafficApp(
    fbzmq::Context& zmqContext,
    const std::string& routerSockUrl,
    const std::string& monitorSockUrl)
    : CtrlApp(
          zmqContext,
          routerSockUrl,
          monitorSockUrl,
          E2EConsts::kTrafficAppCtrlId) {}

void
TrafficApp::processMessage(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::START_IPERF:
      processStartIperfServer(minion, senderApp, message);
      break;
    case thrift::MessageType::START_IPERF_SERVER_RESP:
      processStartIperfClient(minion, senderApp, message);
      break;
    case thrift::MessageType::STOP_IPERF:
      processStopIperf(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_IPERF_STATUS:
      processGetIperfStatus(minion, senderApp, message);
      break;
    case thrift::MessageType::IPERF_OUTPUT:
      processIperfOutput(minion, senderApp, message);
      break;
    case thrift::MessageType::START_PING:
      processStartPing(minion, senderApp, message);
      break;
    case thrift::MessageType::STOP_PING:
      processStopPing(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_PING_STATUS:
      processGetPingStatus(minion, senderApp, message);
      break;
    case thrift::MessageType::PING_OUTPUT:
      processPingOutput(minion, senderApp, message);
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
TrafficApp::processStartIperfServer(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto startIperf = maybeReadThrift<thrift::StartIperf>(message);
  if (!startIperf) {
    handleInvalidMessage("StartIperf", senderApp, minion);
    return;
  }

  // Standardize node ids
  if (!startIperf->srcNodeId.empty()) {
    try {
      startIperf->srcNodeId = MacUtils::standardizeMac(startIperf->srcNodeId);
    } catch (const std::invalid_argument& ex) {
      sendE2EAck(
            senderApp,
            false,
            folly::sformat(
                "Invalid srcNodeId: {}: {}",
                startIperf->srcNodeId,
                folly::exceptionStr(ex)));
        return;
    }
  }
  try {
    startIperf->dstNodeId = MacUtils::standardizeMac(startIperf->dstNodeId);
  } catch (const std::invalid_argument& ex) {
    sendE2EAck(
        senderApp,
        false,
        folly::sformat(
            "Invalid dstNodeId: {}: {}",
            startIperf->dstNodeId,
            folly::exceptionStr(ex)));
    return;
  }

  // Basic validation
  if (startIperf->srcNodeId == startIperf->dstNodeId) {
    sendE2EAck(
        senderApp,
        false,
        "Must specify different source and destination nodes.");
    return;
  }
  auto maybeDstNodeName = SharedObjects::getTopologyWrapper()->rlock()
      ->getNodeNameByMac(startIperf->dstNodeId);
  if (!maybeDstNodeName) {
    sendE2EAck(senderApp, false, "Destination node does not exist.");
    return;
  }

  // Fill in link-local address information (if requested)
  std::optional<std::string> iface;
  if (startIperf->useLinkLocal_ref().value_or(false)) {
    auto adj = getAdjacency(startIperf->srcNodeId, startIperf->dstNodeId);
    if (!adj) {
      sendE2EAck(
          senderApp,
          false,
          "Unable to determine link-local address information.");
      return;
    }
    startIperf->dstNodeIpv6_ref() =
        OpenrUtils::binaryAddressToString(adj->nextHopV6_ref().value());
    iface = adj->ifName_ref().value();
  }

  // Fill in destination IPv6 address (if empty)
  if (!startIperf->dstNodeIpv6_ref().has_value() ||
      startIperf->dstNodeIpv6_ref().value().empty()) {
    auto dstNodeIpv6 = getNodeIpv6(startIperf->dstNodeId);
    if (!dstNodeIpv6) {
      sendE2EAck(
          senderApp,
          false,
          "Unable to determine destination node's IPv6 address.");
      return;
    }
    startIperf->dstNodeIpv6_ref() = dstNodeIpv6.value();
  }

  // Generate a random session ID
  std::string id = UuidUtils::genUuid();

  std::string startMsg =
      folly::sformat("Starting iperf server with session ID: {}", id);
  VLOG(2) << startMsg;
  eventClient_->logEventThrift(
      thrift::EventCategory::TRAFFIC,
      thrift::EventId::IPERF_INFO,
      thrift::EventLevel::INFO,
      startMsg,
      startIperf.value(),
      std::make_optional(startIperf->dstNodeId),
      std::make_optional(startIperf->dstNodeId),
      maybeDstNodeName);

  // Send to server node
  thrift::StartMinionIperf startMinionIperf;
  startMinionIperf.iperfConfig = startIperf.value();
  startMinionIperf.id = id;
  startMinionIperf.senderApp = senderApp;
  if (iface.has_value()) {
    startMinionIperf.iface_ref() = iface.value();
  }
  sendToMinionApp(
      startIperf->dstNodeId,
      E2EConsts::kTrafficAppMinionId,
      thrift::MessageType::START_IPERF_SERVER,
      startMinionIperf);

  // Return session ID to sender
  thrift::StartIperfResp startIperfResp;
  startIperfResp.id = id;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::START_IPERF_RESP,
      startIperfResp);
}

void
TrafficApp::processStartIperfClient(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  VLOG(2) << "StartMinionIperf received from " << minion << ":" << senderApp;

  auto startIperfClient = maybeReadThrift<thrift::StartMinionIperf>(message);
  if (!startIperfClient) {
    handleInvalidMessage("StartMinionIperf", senderApp, minion);
    return;
  }

  // Keep track of this session now that server has started
  iperfSessions_[startIperfClient->id] = startIperfClient.value();

  // Skip client if the srcNodeId is omitted
  if (startIperfClient->iperfConfig.srcNodeId.empty()) {
    VLOG(3) << "Skipping iperf client (empty node ID)";
    return;
  }

  // Basic validation
  if (minion != startIperfClient->iperfConfig.dstNodeId) {
    LOG(ERROR) << "Non-server minion " << minion
               << " trying to start an iperf client to server "
               << startIperfClient->iperfConfig.dstNodeId;
    return;
  }
  auto maybeSrcNodeName = SharedObjects::getTopologyWrapper()->rlock()
      ->getNodeNameByMac(startIperfClient->iperfConfig.srcNodeId);
  if (!maybeSrcNodeName) {
    LOG(ERROR) << "iperf client node "
               << startIperfClient->iperfConfig.srcNodeId << " does not exist";
    return;
  }

  std::string startMsg = folly::sformat(
      "Starting iperf client with session ID: {}", startIperfClient->id);
  VLOG(2) << startMsg;
  eventClient_->logEventThrift(
      thrift::EventCategory::TRAFFIC,
      thrift::EventId::IPERF_INFO,
      thrift::EventLevel::INFO,
      startMsg,
      startIperfClient.value(),
      std::make_optional(startIperfClient->iperfConfig.srcNodeId),
      std::make_optional(startIperfClient->iperfConfig.srcNodeId),
      maybeSrcNodeName);

  // Send to client node
  sendToMinionApp(
      startIperfClient->iperfConfig.srcNodeId,
      E2EConsts::kTrafficAppMinionId,
      thrift::MessageType::START_IPERF_CLIENT,
      startIperfClient.value());
}

void
TrafficApp::processStopIperf(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto stopIperf = maybeReadThrift<thrift::StopIperf>(message);
  if (!stopIperf) {
    handleInvalidMessage("StopIperf", senderApp, minion);
    return;
  }

  // Check if session ID exists
  auto iter = iperfSessions_.find(stopIperf->id);
  if (iter == iperfSessions_.end()) {
    sendE2EAck(senderApp, false, "iperf session ID not found (possibly ended)");
    return;
  }
  std::string srcNodeId = iter->second.iperfConfig.srcNodeId;
  std::string dstNodeId = iter->second.iperfConfig.dstNodeId;
  if (!srcNodeId.empty()) {
    // Send to client node
    sendToMinionApp(
        srcNodeId,
        E2EConsts::kTrafficAppMinionId,
        thrift::MessageType::STOP_IPERF,
        stopIperf.value());
  }

  // Send to server node
  sendToMinionApp(
      dstNodeId,
      E2EConsts::kTrafficAppMinionId,
      thrift::MessageType::STOP_IPERF,
      stopIperf.value());

  auto maybeDstNodeName = SharedObjects::getTopologyWrapper()->rlock()
      ->getNodeNameByMac(dstNodeId);

  std::string stopMsg =
      folly::sformat("Stopping iperf for session ID: {}", stopIperf->id);
  VLOG(2) << stopMsg;
  eventClient_->logEventThrift(
      thrift::EventCategory::TRAFFIC,
      thrift::EventId::IPERF_INFO,
      thrift::EventLevel::INFO,
      stopMsg,
      stopIperf.value(),
      std::make_optional(dstNodeId),
      std::make_optional(dstNodeId),
      maybeDstNodeName);

  iperfSessions_.erase(stopIperf->id);
  sendE2EAck(senderApp, true, "Stopped iperf measurements.");
}

void
TrafficApp::processGetIperfStatus(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto getIperfStatus = maybeReadThrift<thrift::GetIperfStatus>(message);
  if (!getIperfStatus) {
    handleInvalidMessage("GetIperfStatus", senderApp, minion);
    return;
  }

  VLOG(4) << "GetIperfStatus received from " << minion << ":" << senderApp;

  thrift::IperfStatus iperfStatus;
  iperfStatus.sessions = iperfSessions_;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::IPERF_STATUS,
      iperfStatus);
}

void
TrafficApp::processIperfOutput(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto iperfOutput = maybeReadThrift<thrift::IperfOutput>(message);
  if (!iperfOutput) {
    handleInvalidMessage("IperfOutput", senderApp, minion, false);
    return;
  }

  VLOG(2) << "Received iperf output ("
          << (iperfOutput->isServer ? "server" : "client")
          << ") for session ID " << iperfOutput->startIperf.id << " from "
          << minion;
  VLOG(4) << iperfOutput->output;

  // Remove completed session (client/server doesn't matter)
  iperfSessions_.erase(iperfOutput->startIperf.id);

  // Send back results to iperf initiator
  std::string sender = iperfOutput->startIperf.senderApp;
  iperfOutput->startIperf.senderApp.clear();  // remove unneeded ZMQ details
  sendToCtrlApp(sender, thrift::MessageType::IPERF_OUTPUT, iperfOutput.value());

  // Record the full iperf results
  eventClient_->sendData(
      JsonUtils::serializeToJson(iperfOutput.value()),
      E2EConsts::kEventIperfResultCategory);
}

void
TrafficApp::processStartPing(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto startPing = maybeReadThrift<thrift::StartPing>(message);
  if (!startPing) {
    handleInvalidMessage("StartPing", senderApp, minion);
    return;
  }

  // Standardize node ids
  try {
    startPing->srcNodeId = MacUtils::standardizeMac(startPing->srcNodeId);
  } catch (const std::invalid_argument& ex) {
    sendE2EAck(
        senderApp,
        false,
        folly::sformat(
            "Invalid srcNodeId: {}: {}",
            startPing->srcNodeId,
            folly::exceptionStr(ex)));
    return;
  }
  if (startPing->dstNodeId_ref().has_value()) {
    try {
      startPing->dstNodeId_ref() =
          MacUtils::standardizeMac(startPing->dstNodeId_ref().value());
    } catch (const std::invalid_argument& ex) {
      sendE2EAck(
          senderApp,
          false,
          folly::sformat(
              "Invalid dstNodeId: {}: {}",
              startPing->dstNodeId_ref().value(),
              folly::exceptionStr(ex)));
      return;
    }
  }

  // Basic validation
  if (startPing->srcNodeId == startPing->dstNodeId_ref().value()) {
    sendE2EAck(
        senderApp,
        false,
        "Must specify different source and destination nodes.");
    return;
  }
  if (!startPing->dstNodeId_ref().has_value() &&
      (!startPing->dstNodeIpv6_ref().has_value() ||
      startPing->dstNodeIpv6_ref().value().empty())) {
    sendE2EAck(senderApp, false, "Must specify a destination.");
    return;
  }
  auto maybeSrcNodeName = SharedObjects::getTopologyWrapper()->rlock()
      ->getNodeNameByMac(startPing->srcNodeId);
  if (!maybeSrcNodeName) {
    sendE2EAck(senderApp, false, "Source node does not exist.");
    return;
  }

  // Fill in link-local address information (if requested)
  std::optional<std::string> iface;
  if (startPing->useLinkLocal_ref().value_or(false)) {
    if (!startPing->dstNodeId_ref().has_value()) {
      sendE2EAck(
          senderApp,
          false,
          "Must specify destination node if using link local address.");
      return;
    }
    auto adj =
        getAdjacency(startPing->srcNodeId, startPing->dstNodeId_ref().value());
    if (!adj) {
      sendE2EAck(
          senderApp,
          false,
          "Unable to determine link-local address information.");
      return;
    }
    startPing->dstNodeIpv6_ref() =
        OpenrUtils::binaryAddressToString(adj->nextHopV6_ref().value());
    iface = adj->ifName_ref().value();
  }

  // Fill in destination IPv6 address (if empty)
  if (!startPing->dstNodeIpv6_ref().has_value() ||
      startPing->dstNodeIpv6_ref().value().empty()) {
    auto dstNodeIpv6 = getNodeIpv6(startPing->dstNodeId_ref().value());
    if (!dstNodeIpv6) {
      sendE2EAck(
          senderApp,
          false,
          "Unable to determine destination node's IPv6 address.");
      return;
    }
    startPing->dstNodeIpv6_ref() = dstNodeIpv6.value();
  }

  // Generate a random session ID
  std::string id = UuidUtils::genUuid();

  std::string startMsg =
      folly::sformat("Starting ping with session ID: {}", id);
  VLOG(2) << startMsg;
  eventClient_->logEventThrift(
      thrift::EventCategory::TRAFFIC,
      thrift::EventId::PING_INFO,
      thrift::EventLevel::INFO,
      startMsg,
      startPing.value(),
      std::make_optional(startPing->srcNodeId),
      std::make_optional(startPing->srcNodeId),
      maybeSrcNodeName);

  // Send to node
  thrift::StartMinionPing startMinionPing;
  startMinionPing.pingConfig = startPing.value();
  startMinionPing.id = id;
  startMinionPing.senderApp = senderApp;
  if (iface.has_value())  {
    startMinionPing.iface_ref() = iface.value();
  }
  sendToMinionApp(
      startPing->srcNodeId,
      E2EConsts::kTrafficAppMinionId,
      thrift::MessageType::START_PING,
      startMinionPing);

  // Return session ID to sender
  thrift::StartPingResp startPingResp;
  startPingResp.id = id;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::START_PING_RESP,
      startPingResp);

  // Record this session
  pingSessions_[id] = startMinionPing;
}

void
TrafficApp::processStopPing(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto stopPing = maybeReadThrift<thrift::StopPing>(message);
  if (!stopPing) {
    handleInvalidMessage("StopPing", senderApp, minion);
    return;
  }

  // Check if session ID exists
  auto iter = pingSessions_.find(stopPing->id);
  if (iter == pingSessions_.end()) {
    sendE2EAck(senderApp, false, "ping session ID not found (possibly ended)");
    return;
  }
  std::string nodeId = iter->second.pingConfig.srcNodeId;
  auto maybeNodeName = SharedObjects::getTopologyWrapper()->rlock()
      ->getNodeNameByMac(nodeId);

  std::string stopMsg =
      folly::sformat("Stopping ping for session ID: {}", stopPing->id);
  VLOG(2) << stopMsg;
  eventClient_->logEventThrift(
      thrift::EventCategory::TRAFFIC,
      thrift::EventId::PING_INFO,
      thrift::EventLevel::INFO,
      stopMsg,
      stopPing.value(),
      std::make_optional(nodeId),
      std::make_optional(nodeId),
      maybeNodeName);

  // Send to node
  sendToMinionApp(
      nodeId,
      E2EConsts::kTrafficAppMinionId,
      thrift::MessageType::STOP_PING,
      stopPing.value());

  pingSessions_.erase(stopPing->id);
  sendE2EAck(senderApp, true, "Stopped ping measurements.");
}

void
TrafficApp::processGetPingStatus(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto getPingStatus = maybeReadThrift<thrift::GetPingStatus>(message);
  if (!getPingStatus) {
    handleInvalidMessage("GetPingStatus", senderApp, minion);
    return;
  }

  VLOG(4) << "GetPingStatus received from " << minion << ":" << senderApp;

  thrift::PingStatus pingStatus;
  pingStatus.sessions = pingSessions_;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::PING_STATUS,
      pingStatus);
}

void
TrafficApp::processPingOutput(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto pingOutput = maybeReadThrift<thrift::PingOutput>(message);
  if (!pingOutput) {
    handleInvalidMessage("PingOutput", senderApp, minion, false);
    return;
  }

  VLOG(2) << "Received ping output for session ID " << pingOutput->startPing.id
          << " from " << minion;
  VLOG(4) << pingOutput->output;

  // Remove completed session
  pingSessions_.erase(pingOutput->startPing.id);

  // Send back results to ping initiator
  std::string sender = pingOutput->startPing.senderApp;
  pingOutput->startPing.senderApp.clear();  // remove unneeded ZMQ details
  sendToCtrlApp(sender, thrift::MessageType::PING_OUTPUT, pingOutput.value());

  // Record the full ping results
  eventClient_->sendData(
      JsonUtils::serializeToJson(pingOutput.value()),
      E2EConsts::kEventPingResultCategory);
}

std::optional<std::string>
TrafficApp::getNodeIpv6(const std::string& nodeId) {
  auto lockedStatusReports = SharedObjects::getStatusReports()->rlock();
  auto iter = lockedStatusReports->find(nodeId);
  if (iter == lockedStatusReports->end()) {
    return std::nullopt;
  }
  return iter->second.report.ipv6Address;
}

std::optional<openr::thrift::Adjacency>
TrafficApp::getAdjacency(
    const std::string& srcNodeId, const std::string& dstNodeId) {
  auto lockedRoutingAdj = SharedObjects::getRoutingAdjacencies()->rlock();
  auto adjDatabase = lockedRoutingAdj->adjacencyMap.find(
      OpenrUtils::toOpenrNodeName(srcNodeId));
  if (adjDatabase == lockedRoutingAdj->adjacencyMap.end()) {
    return std::nullopt;  // no adjacency info
  }
  std::string dst = OpenrUtils::toOpenrNodeName(dstNodeId);
  for (const openr::thrift::Adjacency& adj :
      adjDatabase->second.adjacencies_ref().value()) {
    if (adj.otherNodeName_ref().value() == dst) {
      return adj;
    }
  }
  return std::nullopt;  // adjacency not found
}

} // namespace terragraph
} // namespace facebook
