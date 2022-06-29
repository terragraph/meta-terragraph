/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "StatusApp.h"

#include <cmath>
#include <fbzmq/service/logging/LogSample.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/FileUtil.h>
#include <folly/MapUtil.h>
#include <folly/Random.h>
#include <folly/String.h>

#include "SharedObjects.h"
#include "e2e/common/Consts.h"
#include "e2e/common/GpsClock.h"
#include "e2e/common/TimeUtils.h"
#include "e2e/common/UuidUtils.h"

using apache::thrift::detail::TEnumMapFactory;

using namespace fbzmq;

DEFINE_string(
    sysdump_fileserver_url,
    "",
    "File server URL for pushing sysdump archives");

DEFINE_int32(
    min_gps_timestamp_queue_size,
    6,
    "The minimum number of elements in the GPS timestamp queue to consider "
    "outlier status of data points");

DEFINE_int32(
    max_gps_timestamp_queue_size,
    20,
    "The maximum number of elements to store in the GPS timestamp queue");

DEFINE_bool(
    allow_minion_connections_with_wlan_macs,
    true,
    "If a topology node has empty 'mac_addr' and non-empty 'wlan_mac_addrs', "
    "allow minion connections with a matching radio MAC.");

namespace {
// Elapsed time allowed between receiving a status report ACK and
// the next status report from a node.
const std::chrono::seconds kStatusReportAllowedTime(30);
}

namespace facebook {
namespace terragraph {

StatusApp::StatusApp(
    fbzmq::Context& zmqContext,
    const std::string& routerSockUrl,
    const std::string& monitorSockUrl,
    std::chrono::seconds statusReportThrottleInterval,
    std::chrono::seconds fullStatusReportInterval,
    const std::string& versionFile)
    : CtrlApp(
          zmqContext,
          routerSockUrl,
          monitorSockUrl,
          E2EConsts::kStatusAppCtrlId),
      statusReportThrottleInterval_(statusReportThrottleInterval),
      fullStatusReportInterval_(fullStatusReportInterval) {
  // Get controller version string
  if (!folly::readFile(versionFile.c_str(), version_)) {
    LOG(ERROR) << "Could not read controller version file";
  } else {
    version_ = folly::trimWhitespace(version_).str();
    LOG(INFO) << "Current Controller Version: " << version_;
  }
}

void
StatusApp::processMessage(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) noexcept {

  switch (message.mType) {
    case thrift::MessageType::GET_STATUS_DUMP:
      processGetStatusDump(senderApp, message);
      break;
    case thrift::MessageType::STATUS_REPORT:
      processStatusReport(minion, senderApp, message);
      break;
    case thrift::MessageType::REBOOT_REQUEST:
      processRebootRequest(senderApp, message);
      break;
    case thrift::MessageType::RESTART_MINION_REQUEST:
      processRestartMinionRequest(senderApp, message);
      break;
    case thrift::MessageType::GET_GPS_TIME:
      processGetGpsTime(senderApp, message);
      break;
    case thrift::MessageType::GET_CTRL_NEIGHBORS_REQ:
      processGetCtrlNeighborsReq(senderApp, message);
      break;
    case thrift::MessageType::GET_MINION_NEIGHBORS_RESP:
      processGetMinionNeighborsResp(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_SYSDUMP:
      processGetSysdump(senderApp, message);
      break;
    case thrift::MessageType::MINION_SYSDUMP_STARTED:
      processMinionSysdumpStarted(senderApp, message);
      break;
    case thrift::MessageType::MINION_SYSDUMP_COMPLETE:
      processMinionSysdumpComplete(senderApp, message);
      break;
    case thrift::MessageType::IS_ALIVE:
      processIsAlive(senderApp);
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
StatusApp::processGetSysdump(
    const std::string& senderApp, const thrift::Message& message) {
  VLOG(4) << "Received GetSysdump request from " << senderApp;
  auto request = maybeReadThrift<thrift::GetSysdump>(message);
  if (!request) {
    handleInvalidMessage("GetSysdump", senderApp);
    return;
  }
  std::string url = FLAGS_sysdump_fileserver_url;
  if (url.empty()) {
    sendE2EAck(
        senderApp, false, "Empty controller flag 'sysdump_fileserver_url'");
    return;
  }
  auto nodeMacAddr = SharedObjects::getTopologyWrapper()->rlock()
      ->getMacByNodeName(request->node);
  if (!nodeMacAddr) {
    LOG(WARNING) << "Node not found in topology, dropping sysdump request for: "
                 << request->node;
    sendE2EAck(senderApp, false, "Node not found in topology");
    return;
  }
  thrift::GetMinionSysdump getMinionSysdump;
  getMinionSysdump.url = url;
  getMinionSysdump.sender = senderApp;
  sendToMinionApp(
      nodeMacAddr.value(),
      E2EConsts::kStatusAppMinionId,
      thrift::MessageType::GET_MINION_SYSDUMP,
      getMinionSysdump);
}

void
StatusApp::processMinionSysdumpComplete(
    const std::string& senderApp, const thrift::Message& message) {
  auto notif = maybeReadThrift<thrift::MinionSysdumpComplete>(message);
  if (!notif) {
    handleInvalidMessage("MinionSysdumpComplete", senderApp);
    return;
  }
  if (!notif->error.empty()) {
    sendE2EAck(notif->sender, false, notif->error);
    return;
  }
  sendE2EAck(notif->sender, true, "Minion sysdump complete.");
}

void
StatusApp::processMinionSysdumpStarted(
    const std::string& senderApp, const thrift::Message& message) {
  auto notif = maybeReadThrift<thrift::MinionSysdumpStarted>(message);
  if (!notif) {
    handleInvalidMessage("MinionSysdumpStarted", senderApp);
    return;
  }
  thrift::GetSysdumpResp response;
  response.success = notif->error.empty() ? true : false;
  response.filename = notif->filename;
  response.error = notif->error;

  sendToCtrlApp(
      notif->sender, thrift::MessageType::GET_SYSDUMP_RESP, response);
}

void
StatusApp::processStatusReport(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  VLOG(4) << "Status Report received from " << minion << ":" << senderApp;
  auto statusReport = maybeReadThrift<thrift::StatusReport>(message);
  if (!statusReport) {
    handleInvalidMessage("StatusReport", senderApp, minion, false);
    return;
  }

  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();

  // verify node exists in topology before recording status report
  auto node = lockedTopologyW->getNodeByMac(minion);
  if (!node) {
    if (FLAGS_allow_minion_connections_with_wlan_macs) {
      // If this node ID isn't in the topology, check if any reported radio MACs
      // are associated with an existing node that has an empty node ID.
      for (const auto& kv : statusReport->radioStatus) {
        const std::string& radioMac = kv.first;
        node = lockedTopologyW->getNodeByMac(radioMac);
        if (node && node->mac_addr.empty()) {
          VLOG(2) << "Received status report from '" << minion
                  << "' with radio MAC '" << radioMac
                  << "' associated with existing node '" << node->name
                  << "'. Updating topology with this new MAC address...";

          // Tell TopologyApp to update the node ID according to this request.
          thrift::SetNodeMac setNodeMac;
          setNodeMac.nodeName = node->name;
          setNodeMac.nodeMac = minion;
          setNodeMac.force = true;
          sendToCtrlApp(
              E2EConsts::kTopologyAppCtrlId,
              thrift::MessageType::SET_NODE_MAC,
              setNodeMac);

          // Drop the status report for now, while we wait for TopologyApp to
          // asynchronously update the topology. This will help avoid any edge
          // cases where we record a status report for an unknown node ID.
          return;
        }
      }
    }

    LOG(WARNING) << "Node not found in topology, dropping status report for: "
                 << minion;
    eventClient_->logEvent(
        thrift::EventCategory::STATUS,
        thrift::EventId::UNKNOWN_NODE,
        thrift::EventLevel::WARNING,
        folly::sformat("Dropped status report from unknown node {}", minion),
        "",
        std::make_optional(minion));
    return;
  }

  // find all links for this node
  std::vector<std::string> wirelessNeighborMacs;
  std::vector<std::string> wiredNeighborMacs;
  auto links = lockedTopologyW->getLinksByNodeName(node->name);
  for (const auto& link : links) {
    std::string nbrMac = (link.a_node_name == node->name)
        ? link.z_node_mac : link.a_node_mac;
    if (!nbrMac.empty()) {
      if (link.link_type == thrift::LinkType::ETHERNET) {
        wiredNeighborMacs.push_back(nbrMac);
      } else {
        wirelessNeighborMacs.push_back(nbrMac);
      }
    }
  }

  lockedTopologyW.unlock();  // lockedTopologyW -> NULL

  // record this status report
  auto now = TimeUtils::getSteadyTimePoint();
  statusReport->timeStamp = std::time(nullptr);
  bool throttleReport = false;
  bool requestFullStatusReport = false;
  bool ipv6AddressChanged = false;
  {
    auto lockedStatusReports = SharedObjects::getStatusReports()->wlock();
    auto it = lockedStatusReports->find(minion);
    if (it != lockedStatusReports->end()) {
      // we've seen a status report from this node before
      if (it->second.steadyTs.time_since_epoch() +
          statusReportThrottleInterval_ > now.time_since_epoch()) {
        throttleReport = true;
      }

      updateGpsTime(statusReport.value(), it->second, minion);

      it->second.steadyTs = now;

      // the minion may omit static version fields on subsequent reports,
      // so fill the struct with our cached data
      if (statusReport->version.empty()) {
        statusReport->version = it->second.report.version;
        statusReport->ubootVersion = it->second.report.ubootVersion;
        statusReport->hardwareModel = it->second.report.hardwareModel;
        statusReport->hardwareBoardId = it->second.report.hardwareBoardId;
        if (it->second.report.networkInterfaceMacs_ref().has_value()) {
          statusReport->networkInterfaceMacs_ref() =
              it->second.report.networkInterfaceMacs_ref().value();
        } else {
          statusReport->networkInterfaceMacs_ref().reset();
        }
        statusReport->firmwareVersion = it->second.report.firmwareVersion;

        // re-request the full status report when:
        // - we thought the node was offline
        // - the last full status report was too long ago (as a safeguard only)
        if (node->status == thrift::NodeStatusType::OFFLINE ||
            now.time_since_epoch() -
            it->second.lastFullReportTs.time_since_epoch() >=
                fullStatusReportInterval_) {
          requestFullStatusReport = true;
        }
      } else {
        it->second.lastFullReportTs = now;
      }

      // check if ipv6Address changed
      if (statusReport->ipv6Address != it->second.report.ipv6Address) {
        ipv6AddressChanged = true;
      }
      it->second.report = statusReport.value();
    } else if (statusReport->version.empty()) {
      // received a partial report from a new node: request the full report
      requestFullStatusReport = true;
    } else {
      // received a fully-formed report from a new node: store it
      (*lockedStatusReports)[minion] = StatusReport(now, statusReport.value());
      ipv6AddressChanged = true;
    }
  }

  if (ipv6AddressChanged) {
    VLOG(4) << "IP address changed for node \"" << node->name << "\" to \""
            << statusReport->ipv6Address << "\"";
    // inform ConfigApp to update tunnel configuration(s) for this node
    thrift::UpdateTunnelConfig updateTunnelConfig;
    updateTunnelConfig.node = *node;
    updateTunnelConfig.ipv6Address = statusReport->ipv6Address;
    sendToCtrlApp(
        E2EConsts::kConfigAppCtrlId,
        thrift::MessageType::UPDATE_TUNNEL_CONFIG,
        updateTunnelConfig);
  }

  // send ACK to minion
  thrift::StatusReportAck statusReportAck;
  statusReportAck.requestFullStatusReport =
      requestFullStatusReport && !throttleReport;
  sendToMinionApp(
      minion,
      E2EConsts::kStatusAppMinionId,
      thrift::MessageType::STATUS_REPORT_ACK,
      statusReportAck);

  // don't process too frequent status reports
  // (e.g. buffered by ZMQ socket and sent on a re-connect)
  if (throttleReport) {
    return;
  }

  std::string minionToStatusKey =
      folly::sformat("e2e_controller.statusReport_rcvd.{}", minion);
  if (statusReport->status == thrift::NodeStatusType::OFFLINE) {
    // minion is first time online or it just did a restart
    // set counter = 0
    setCounter(minionToStatusKey, 0, fbzmq::thrift::CounterValueType::COUNTER);

    // Notify TopologyApp to send NodeParams
    thrift::SetNodeParamsReq setNodeParamsReq;
    setNodeParamsReq.nodeMac_ref() = minion;
    sendToCtrlApp(
        E2EConsts::kTopologyAppCtrlId,
        thrift::MessageType::SET_NODE_PARAMS_REQ,
        setNodeParamsReq);

    // Notify TopologyApp to mark the node as online
    thrift::SetNodeStatus setNodeStatus;
    setNodeStatus.nodeMac = minion;
    setNodeStatus.nodeStatus = thrift::NodeStatusType::ONLINE;
    sendToCtrlApp(
        E2EConsts::kTopologyAppCtrlId,
        thrift::MessageType::SET_NODE_STATUS,
        setNodeStatus);
  } else {
    bumpCounter(minionToStatusKey);
  }

  // Request link status on all of this node's links if either is true:
  // - The node just connected (and reported itself as previously-offline)
  // - The controller just started (and wrongly viewed the node as offline)
  if (statusReport->status == thrift::NodeStatusType::OFFLINE ||
      node->status == thrift::NodeStatusType::OFFLINE) {
    for (const auto& mac : wirelessNeighborMacs) {
      thrift::GetLinkStatus getLinkStatus;
      getLinkStatus.responderMac = mac;
      sendToMinionApp(
          minion,
          E2EConsts::kIgnitionAppMinionId,
          thrift::MessageType::GET_LINK_STATUS,
          getLinkStatus);
    }
  }

  // Calculate wired link status for this node's
  // wired neighbors and send them to TopologyApp
  if (statusReport->neighborConnectionStatus_ref().has_value()) {
    std::unordered_map<std::string, bool> wiredLinkStatus;
    auto lockedStatusReports = SharedObjects::getStatusReports()->rlock();
    for (const auto& nbr : wiredNeighborMacs) {
      auto it = lockedStatusReports->find(nbr);
      if (it == lockedStatusReports->end()) {
        // We don't have a status report for this neighbor, ignore
        continue;
      }
      // Search neighbor's network interface macs to find the one the primary
      // node's switch is connected to
      if (!it->second.report.networkInterfaceMacs_ref().has_value()) {
        continue;
      }
      // Assume status is down unless we find connection status on one of
      // neighbor's wired interfaces
      wiredLinkStatus[nbr] = false;
      for (const auto& interface :
        it->second.report.networkInterfaceMacs_ref().value()) {
        auto interfaceStatus =
            statusReport->neighborConnectionStatus_ref().value()
            .find(interface.second);
        if (interfaceStatus !=
            statusReport->neighborConnectionStatus_ref().value().end()) {
          wiredLinkStatus[nbr] = interfaceStatus->second;
          break;
        }
      }
    }
    lockedStatusReports.unlock();

    // Notify TopologyApp to update wired links for this node
    thrift::SetWiredLinkStatus setWiredLinkStatus;
    setWiredLinkStatus.nodeMac = minion;
    setWiredLinkStatus.linkStatus = wiredLinkStatus;
    sendToCtrlApp(
        E2EConsts::kTopologyAppCtrlId,
        thrift::MessageType::SET_WIRED_LINK_STATUS,
        setWiredLinkStatus);
  }

  // If a self-reported node type differs from what's in the topology,
  // instruct the minion to restart to re-initialize the firmware state
  if (statusReport->nodeType_ref().has_value() &&
      statusReport->nodeType_ref().value() != node->node_type) {
    // Check that the node type is valid (to be safe)
    auto nodeTypeMap =
        TEnumMapFactory<thrift::NodeType>::makeValuesToNamesMap();
    auto iter = nodeTypeMap.find(
        statusReport->nodeType_ref().value());
    if (iter != nodeTypeMap.end())
    {
      LOG(INFO) << "Node " << node->name << " reported a node type ("
                << iter->second << ") that differs from the topology ("
                << nodeTypeMap.at(node->node_type)
                << "). Restarting minion on the node...";
      thrift::RestartMinion restartMinion;
      restartMinion.secondsToRestart = 1;
      sendToMinionApp(
          minion,
          E2EConsts::kStatusAppMinionId,
          thrift::MessageType::RESTART_MINION,
          restartMinion);
    }
  }

  // If the node reported status on radios that are not present in the topology,
  // add them to our list of WLAN MACs
  if (!statusReport->radioStatus.empty()) {
    std::vector<std::string> newWlanMacs;
    for (const auto& kv : statusReport->radioStatus) {
      const auto& v = node->wlan_mac_addrs;
      if (std::find(v.begin(), v.end(), kv.first) == v.end()) {
        newWlanMacs.push_back(kv.first);
      }
    }
    if (!newWlanMacs.empty()) {
      LOG(INFO) << "Adding self-reported WLAN MAC addresses for node "
                << node->name << "...";
      thrift::AddNodeWlanMacs addNodeWlanMacs;
      addNodeWlanMacs.nodeName = node->name;
      addNodeWlanMacs.wlanMacs = newWlanMacs;
      sendToCtrlApp(
          E2EConsts::kTopologyAppCtrlId,
          thrift::MessageType::ADD_NODE_WLAN_MACS,
          addNodeWlanMacs);
    }
  }
}

void
StatusApp::processGetStatusDump(
    const std::string& senderApp, const thrift::Message& message) {
  VLOG(5) << "Request for status dump from " << senderApp;
  if (!maybeReadThrift<thrift::GetStatusDump>(message)) {
    handleInvalidMessage("GetStatusDump", senderApp);
    return;
  }

  std::unordered_map<std::string, thrift::StatusReport> statusReports;
  {
    auto lockedStatusReports = SharedObjects::getStatusReports()->rlock();
    for (const auto& status : *lockedStatusReports) {
      statusReports[status.first] = status.second.report;
    }
  }

  thrift::StatusDump statusDump;
  statusDump.timeStamp = std::time(nullptr);
  statusDump.statusReports = statusReports;
  statusDump.version_ref() = version_;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::STATUS_DUMP,
      statusDump);
}

std::optional<std::vector<thrift::Node>>
restartRequestHelper(
    int delay,
    const std::vector<std::string>& nodes,
    std::string& errorMessage) {
  if (delay < 1 || delay > 60) {
    errorMessage = "Delay should be between 1 and 60";
    return std::nullopt;
  }

  std::vector<thrift::Node> nodesToReboot;
  if (nodes.size() == 0) {
    nodesToReboot = SharedObjects::getTopologyWrapper()->rlock()->getAllNodes();
  } else {
    auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
    for (const std::string& nodeName : nodes) {
      auto maybeNode = lockedTopologyW->getNode(nodeName);
      if (!maybeNode) {
        errorMessage = "Could not find node: " + nodeName;
        return std::nullopt;
      }
      nodesToReboot.push_back(*maybeNode);
    }
  }
  return nodesToReboot;
}

void
StatusApp::processRebootRequest(
    const std::string& senderApp, const thrift::Message& message) {
  VLOG(5) << "Reboot request from " << senderApp;
  auto request = maybeReadThrift<thrift::RebootReq>(message);
  if (!request) {
    handleInvalidMessage("RebootReq", senderApp);
    return;
  }
  std::string resp = "Reboot requests sent to nodes";
  auto nodesToReboot = restartRequestHelper(
      request->secondsToReboot, request->nodes, resp);
  if (nodesToReboot) {
    for (const auto& node : *nodesToReboot) {
      if (node.mac_addr.empty()) {
        continue;
      }

      thrift::RebootNode rebootNode;
      rebootNode.force = request->force;
      rebootNode.secondsToReboot_ref() = request->secondsToReboot;
      sendToMinionApp(
          node.mac_addr,
          E2EConsts::kStatusAppMinionId,
          thrift::MessageType::REBOOT_NODE,
          rebootNode);

      eventClient_->logEventDynamic(
          thrift::EventCategory::STATUS,
          thrift::EventId::REBOOT_NODE_REQ,
          thrift::EventLevel::INFO,
          "Sent reboot request",
          folly::dynamic::object("secondsToReboot", request->secondsToReboot)(
              "force", request->force),
          std::make_optional(node.mac_addr),
          std::make_optional(node.mac_addr),
          std::make_optional(node.name));
    }
  }
  sendE2EAck(senderApp, nodesToReboot.has_value(), resp);
}

void
StatusApp::processRestartMinionRequest(
    const std::string& senderApp, const thrift::Message& message) {
  VLOG(5) << "Restart Minion request from " << senderApp;
  auto request = maybeReadThrift<thrift::RestartMinionRequest>(message);
  if (!request) {
    handleInvalidMessage("RestartMinionRequest", senderApp);
    return;
  }
  std::string resp = "Restart requests sent to minions";
  auto nodesToRestart = restartRequestHelper(
      request->secondsToRestart, request->nodes, resp);
  if (nodesToRestart) {
    for (const auto& node : *nodesToRestart) {
      if (node.mac_addr.empty()) {
        continue;
      }

      thrift::RestartMinion restartMinion;
      restartMinion.secondsToRestart = request->secondsToRestart;
      sendToMinionApp(
          node.mac_addr,
          E2EConsts::kStatusAppMinionId,
          thrift::MessageType::RESTART_MINION,
          restartMinion);

      eventClient_->logEventDynamic(
          thrift::EventCategory::STATUS,
          thrift::EventId::RESTART_MINION_REQ,
          thrift::EventLevel::INFO,
          "Sent minion restart request",
          folly::dynamic::object("secondsToRestart", request->secondsToRestart),
          std::make_optional(node.mac_addr),
          std::make_optional(node.mac_addr),
          std::make_optional(node.name));
    }
  }
  sendE2EAck(senderApp, nodesToRestart.has_value(), resp);
}

void
StatusApp::processGetGpsTime(
    const std::string& senderApp, const thrift::Message& message) {
  VLOG(5) << "GPS time request from " << senderApp;
  auto request = maybeReadThrift<thrift::GetGpsTime>(message);
  if (!request) {
    handleInvalidMessage("GetGpsTime", senderApp);
    return;
  }

  thrift::GpsTime gpsTime;
  gpsTime.unixTimeNanoseconds =
      std::chrono::system_clock::now().time_since_epoch().count();
  gpsTime.gpsTimeMicroseconds = GpsClock::now().time_since_epoch().count();
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GPS_TIME,
      gpsTime);
}

void
StatusApp::processGetCtrlNeighborsReq(
    const std::string& senderApp, const thrift::Message& message) {
  VLOG(5) << "Request to get neighbors from all minions from " << senderApp;
  auto request = maybeReadThrift<thrift::GetCtrlNeighborsReq>(message);
  if (!request) {
    handleInvalidMessage("GetCtrlNeighborsReq", senderApp);
    return;
  }

  if (request->devices.size() < 1) {
    sendE2EAck(senderApp, false, "Must provide at least one device");
    return;
  }

  // Return ack with a unique ID
  std::string reqId = UuidUtils::genUuid();
  thrift::GetCtrlNeighborsResp getCtrlNeighborsResp;
  getCtrlNeighborsResp.devices = request->devices;
  getCtrlNeighborsResp.reqId = reqId;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_CTRL_NEIGHBORS_RESP,
      getCtrlNeighborsResp);

  // Send request to get neighbors to all nodes
  {
    auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
    for (const auto& node : lockedTopologyW->getAllNodes()) {
      if (!node.mac_addr.empty() &&
          node.status != thrift::NodeStatusType::OFFLINE) {
        thrift::GetMinionNeighborsReq getMinionNeighborsReq;
        getMinionNeighborsReq.devices = request->devices;
        getMinionNeighborsReq.reqId = reqId;
        getMinionNeighborsReq.senderApp = senderApp;
        sendToMinionApp(
            node.mac_addr,
            E2EConsts::kStatusAppMinionId,
            thrift::MessageType::GET_MINION_NEIGHBORS_REQ,
            getMinionNeighborsReq);
      }
    }
  }
}

void
StatusApp::processGetMinionNeighborsResp(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  VLOG(5) << "Received response for get neighbors from " << minion;
  auto response = maybeReadThrift<thrift::GetMinionNeighborsResp>(message);
  if (!response) {
    handleInvalidMessage("GetMinionNeighborsResp", senderApp, minion, false);
  }

  // Forward response to original senderApp
  thrift::GetNeighborsResp getNeighborsResp;
  getNeighborsResp.deviceNeighborsMap = response->deviceNeighborsMap;
  getNeighborsResp.reqId = response->reqId;
  getNeighborsResp.minion =  minion;
  sendToCtrlApp(
      response->senderApp,
      thrift::MessageType::GET_NEIGHBORS_RESP,
      getNeighborsResp);
}

void
StatusApp::processIsAlive(const std::string& senderApp) {
  VLOG(4) << "Received isAlive message from " << senderApp;
  sendE2EAck(senderApp, true, "Up");
}

void
StatusApp::updateGpsTime(
    const thrift::StatusReport& statusReport,
    const StatusReport& lastStatusReport,
    const std::string& minion) {
  // A node in the ONLINE_INITIATOR state must by definition have GPS
  // enabled. When the controller receives a status report from a node
  // without this status, don't update GPS epoch.
  if (lastStatusReport.report.status !=
      thrift::NodeStatusType::ONLINE_INITIATOR) {
    return;
  }

  auto statusReportAckMicroseconds =
      std::chrono::duration_cast<std::chrono::microseconds>(
          lastStatusReport.steadyTs.time_since_epoch());
  auto lastAckGpsTimestamp = std::chrono::microseconds(
      (uint64_t) statusReport.lastAckGpsTimestamp);
  auto sentGpsTimestamp = std::chrono::microseconds(
      (uint64_t) statusReport.sentGpsTimestamp);
  auto nowMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch());

  // Make sure GPS time is working correctly on node.
  if (lastAckGpsTimestamp.count() == 0) {
    return;
  }

  // Make sure status report came back in a reasonable amount of time since
  // node last ACKed, say 30 seconds.
  if (nowMicroseconds - statusReportAckMicroseconds >=
      kStatusReportAllowedTime) {
    return;
  }

  // Use Cristian's algorithm to subtract network latency to determine
  // the actual time.
  auto delta = (nowMicroseconds - statusReportAckMicroseconds) -
      (sentGpsTimestamp - lastAckGpsTimestamp);
  auto gpsTime = std::chrono::microseconds(sentGpsTimestamp + delta / 2);

  // Figure out if the time reported by the node is accurate or if it is an
  // outlier. If the time is an outlier, discard it. Otherwise, update the
  // controller's GPS clock.
  bool outlier = gpsTimestampIsOutlier(gpsTime);
  if (outlier) {
    VLOG(5) << "Received outlier timestamp: " << gpsTime.count() << " from "
            << minion;
    return;
  }

  VLOG(5) << "Updating GPS time to " << gpsTime.count()
          << " microseconds from " << minion;
  GpsClock::setEpoch(gpsTime);
}

bool
StatusApp::gpsTimestampIsOutlier(std::chrono::microseconds gpsTime) {
  // Calculate the delta between the time from the node and the system's current
  // time. A list of these deltas will be used to calculate a probability that
  // the time from the node is an outlier.
  std::chrono::microseconds systemMicroseconds;
  auto now = GpsClock::now();
  if (now.time_since_epoch().count() != 0) {
    systemMicroseconds = now.time_since_epoch();
  } else {
    // Unix time has an epoch of Jan 1, 1970, whereas GPS time has an epoch of
    // Jan 6, 1980. When using the system clock as a fallback, must convert to
    // GPS time.
    systemMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch() -
        E2EConsts::kUnixGpsEpochDifference);
  }
  auto systemDelta = (systemMicroseconds - gpsTime).count();

  if (latestGpsTimestamps_.size() <
      (decltype(latestGpsTimestamps_)::size_type)
          FLAGS_min_gps_timestamp_queue_size) {
    // Not enough data to reliably calculate outliers. Add this data point and
    // assume it is not an outlier, in order to set the GPS clock as early as
    // possible. If it turns out this data point is an outlier, the GPS clock
    // should be corrected when more data arrives.
    addGpsTimestamp(systemDelta);
    return false;
  }

  // Calculate mean of GPS timestamps. Since the values in the queue are deltas,
  // there shouldn't be any issues with overflow.
  int64_t sum = std::accumulate(
      latestGpsTimestamps_.begin(), latestGpsTimestamps_.end(), 0);
  double mean = static_cast<double>(sum) / latestGpsTimestamps_.size();

  // Calculate standard deviation of GPS timestamps.
  double stdDev = 0;
  for (const auto& gpsTimestamp : latestGpsTimestamps_) {
    stdDev += pow(gpsTimestamp - mean, 2);
  }
  stdDev = std::sqrt(stdDev / latestGpsTimestamps_.size());

  // NOTE: This section uses Chauvenet's criterion to determine if the GPS
  // timestamp received from the node is an outlier. If the queue is full, after
  // determining outlier status the GPS timestamp will be pushed onto the queue
  // and the oldest GPS timestamp will be removed. This changes the data set and
  // also changes the assumptions about the data in the data set. Theoretically,
  // this means data points previously not considered outliers and used to
  // update the controller's GpsClock could be considered outliers in the
  // future. In practice, however, this seems unlikely to occur and should be
  // corrected relatively quickly using data from other nodes sending accurate
  // times.

  // Calculate the z-value on a standard normal distribution (mean = 0,
  // standard deviation = 1).
  double zValue = -std::abs((systemDelta - mean) / stdDev);
  // Calculate the probability of the GPS timestamp being an outlier based on
  // the current sample data. The equation returns the probability of a value
  // being less than the zValue on a normal distribution with a mean of 0 and
  // a standard deviation of 1.
  double probability = 0.5 * erfc(-zValue * M_SQRT1_2);
  // Use Chauvenet's criterion to determine if the GPS timestamp is an outlier.
  bool outlier = probability * latestGpsTimestamps_.size() < 0.5;

  // Push all GPS timestamps (including outliers) onto data set. This will skew
  // the outlier criteria a little bit, but it solves the problem of a data
  // set consisting of only bad data which prevents good data from ever ousting
  // it.
  addGpsTimestamp(systemDelta);

  return outlier;
}

void
StatusApp::addGpsTimestamp(int64_t gpsTimestamp) {
  latestGpsTimestamps_.push_back(gpsTimestamp);
  // Trim queue to max size.
  while (latestGpsTimestamps_.size() >
      (decltype(latestGpsTimestamps_)::size_type)
          FLAGS_max_gps_timestamp_queue_size) {
    latestGpsTimestamps_.pop_front();
  }
}

} // namespace terragraph
} // namespace facebook
