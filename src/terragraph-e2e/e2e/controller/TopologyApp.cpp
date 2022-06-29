/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TopologyApp.h"

#include <algorithm>

#include <fbzmq/zmq/Zmq.h>
#include <folly/FileUtil.h>
#include <folly/MapUtil.h>
#include <folly/String.h>
#include <folly/gen/Base.h>
#include <gflags/gflags.h>

#include "SharedObjects.h"
#include "algorithms/BandwidthAllocationHelper.h"
#include "algorithms/PolarityHelper.h"
#include "e2e/common/Consts.h"
#include "e2e/common/OpenrUtils.h"
#include "prefix-allocators/CentralizedPrefixAllocator.h"
#include "prefix-allocators/DeterministicPrefixAllocator.h"

using apache::thrift::detail::TEnumMapFactory;

using namespace fbzmq;
using namespace std;

DEFINE_int32(
    kvstore_pub_port,
    60001,
    "KvStore publisher port for emitting realtime key-value deltas");
DEFINE_int32(kvstore_router_port, 60002, "The port KvStore replier listens on");
DEFINE_bool(
    enable_pop_prefix_broadcast,
    false,
    "If enabled, broadcast prefix allocations to all POP nodes instead of a "
    "single random POP node (for use when POP nodes cannot reach each other)");
DEFINE_bool(
    send_bf_resp_scan_disable_on_link_down,
    true,
    "If enabled, disable BF responder mode on radios after receiving LINK_DOWN "
    "events on DN-to-DN links with other active P2MP links");

namespace facebook {
namespace terragraph {

TopologyApp::TopologyApp(
    fbzmq::Context& zmqContext,
    const std::string& routerSockUrl,
    const std::string& monitorSockUrl,
    chrono::seconds statusReportsInterval,
    chrono::seconds topologyReportInterval,
    chrono::seconds routingAdjacenciesDumpInterval,
    chrono::seconds nodeAliveTimeOut,
    chrono::seconds airtimeAllocUpdateInterval,
    chrono::seconds centralizedPrefixUpdateInterval,
    const std::string& topologyFile,
    const std::string& topologyDir,
    bool autoAirtimeAllocEnabled,
    bool centralizedPrefixAllocEnabled,
    bool deterministicPrefixAllocEnabled,
    bool createIntrasiteLinks)
    : CtrlApp(
          zmqContext,
          routerSockUrl,
          monitorSockUrl,
          E2EConsts::kTopologyAppCtrlId),
      statusReportsInterval_(statusReportsInterval),
      topologyReportInterval_(topologyReportInterval),
      routingAdjacenciesDumpInterval_(routingAdjacenciesDumpInterval),
      nodeAliveTimeOut_(nodeAliveTimeOut),
      centralizedPrefixUpdateInterval_(centralizedPrefixUpdateInterval),
      centralizedPrefixAllocEnabled_(centralizedPrefixAllocEnabled),
      deterministicPrefixAllocEnabled_(deterministicPrefixAllocEnabled) {

  topologyW_ = std::make_unique<TopologyWrapper>(
      topologyFile, topologyDir, createIntrasiteLinks);

  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  bool controllerPrefixAllocationEnabled =
      initPrefixAllocator(*lockedConfigHelper);
  std::string err;
  std::vector<std::string> nodeNames =
      folly::gen::from(topologyW_->getAllNodes()) |
      folly::gen::field(&thrift::Node::name) |
      folly::gen::as<std::vector<std::string>>();
  if (!lockedConfigHelper->setNodeTopologyInfo(nodeNames, *topologyW_, err)) {
    LOG(ERROR) << "Error generating topology info for nodes: " << err;
  }
  lockedConfigHelper.unlock();  // lockedConfigHelper -> NULL

  if (controllerPrefixAllocationEnabled) {
    // set allocated prefixes to kvstore periodically
    centralizedPrefixUpdateTimeout_ =
        ZmqTimeout::make(this, [this]() noexcept { sendSetPrefixes(); });
    centralizedPrefixUpdateTimeout_->scheduleTimeout(
        centralizedPrefixUpdateInterval_, true /* isPeriodic */);
  }

  topologyW_->sanitizeState();

  // Update globally-shared topology wrapper
  // (Some topology parameters could have changed during this constructor.)
  SharedObjects::getTopologyWrapper()->wlock()
      ->setControllerPrefixAllocScheme(
          topologyW_->getControllerPrefixAllocScheme());
  SharedObjects::getTopologyWrapper()->wlock()->setTopology(
      topologyW_->getTopology());
  *(SharedObjects::getTopologyName()->wlock()) = topologyW_->getTopologyName();

  // check all node statuses periodically
  statusReportsSyncTimeout_ =
      ZmqTimeout::make(this, [this]() noexcept { syncWithStatusReports(); });
  statusReportsSyncTimeout_->scheduleTimeout(
      statusReportsInterval_, true /* isPeriodic */);

  // send out request to report topology to stats agent periodically
  topologyReportTimeout_ =
      ZmqTimeout::make(this, [this]() noexcept { reportTopologyStats(); });
  topologyReportTimeout_->scheduleTimeout(
      topologyReportInterval_, true /* isPeriodic */);

  // send out request to get routing adjacencies periodically
  getRoutingAdjacenciesTimeout_ = ZmqTimeout::make(
      this, [this]() noexcept { sendGetRoutingAdjacencies(); });
  getRoutingAdjacenciesTimeout_->scheduleTimeout(
      routingAdjacenciesDumpInterval_, true /* isPeriodic */);

  // send updated auto node overrides to BinaryStarApp
  // (triggered on demand, not periodically)
  // NOTE: we need to do this in a separate invocation due to exclusive locks on
  // the shared ConfigHelper, e.g. in topologyChanged()
  haAutoNodeOverridesSyncTimeout_ = ZmqTimeout::make(this, [this]() noexcept {
    auto autoNodeOverrides =
        SharedObjects::getConfigHelper()->rlock()->getAutoNodeOverridesJson({});
    if (!autoNodeOverrides) {
      LOG(ERROR) << "Error parsing auto node overrides";
      return;
    }
    SharedObjects::getSyncedAppData()->wlock()->setAutoNodeOverrides(
        autoNodeOverrides.value());
  });

  // initialize routes helper
  routesHelper_ = std::make_unique<RoutesHelper>(topologyW_->getTopology());

  // is automatic airtime allocation enabled?
  autoAirtimeAllocEnabled_ = autoAirtimeAllocEnabled;
  if (autoAirtimeAllocEnabled_) {
    // generate initial airtime allocation maps
    nwAirtimeAlloc_ =
        BandwidthAllocationHelper::computeAirtimes(topologyW_.get());

    // enforce a minimum interval for recomputing airtime allocations and
    // updating nodes (to avoid flooding SetNodeParams requests)
    airtimeComputationTimeout_ =
        ZmqTimeout::make(this, [this]() noexcept { updateAirtimeAlloc(); });
    airtimeComputationTimeout_->scheduleTimeout(
        airtimeAllocUpdateInterval, true /* isPeriodic */);
  }
}

bool
TopologyApp::initPrefixAllocator(ConfigHelper& configHelper) {
  bool centralizedPrefixAllocEnabled = centralizedPrefixAllocEnabled_;
  bool deterministicPrefixAllocEnabled = deterministicPrefixAllocEnabled_;
  thrift::PrefixAllocParams prefixAllocParams =
      SharedObjects::getE2EConfigWrapper()
          ->rlock()
          ->getConfig()
          ->prefixAllocParams;

  if (centralizedPrefixAllocEnabled && deterministicPrefixAllocEnabled) {
    LOG(ERROR) << "Cannot have both centralized and deterministic prefix "
               << "allocation enabled. Defaulting to centralized prefix "
               << "allocation...";
    deterministicPrefixAllocEnabled = false;
  }

  bool controllerPrefixAllocationEnabled =
      centralizedPrefixAllocEnabled || deterministicPrefixAllocEnabled;

  // Handle controller-based prefix allocation
  if (controllerPrefixAllocationEnabled &&
      prefixAllocParams == thrift::PrefixAllocParams()) {
    // Turn off controller-based prefix allocation if prefix allocation
    // parameters are missing
    LOG(ERROR) << folly::format(
        "A controller-based (centralized or deterministic) prefix allocation "
        "scheme is enabled, but the prefix allocation parameters are not "
        "set. Seed prefix: {}, Allocation prefix length: {}. Disabling "
        "controller-based prefix allocation...",
        prefixAllocParams.seedPrefix,
        prefixAllocParams.allocPrefixLen);
    centralizedPrefixAllocEnabled = false;
    deterministicPrefixAllocEnabled = false;
    controllerPrefixAllocationEnabled = false;
  }

  if (deterministicPrefixAllocEnabled) {
    try {
      prefixAllocator_ = std::make_unique<DeterministicPrefixAllocator>(
          prefixAllocParams, topologyW_.get(), configHelper);
      LOG(INFO) << "Deterministic prefix allocation enabled";
    } catch (const std::invalid_argument& ex) {
      LOG(ERROR) << folly::format(
          "Deterministic prefix allocation failed: {}. "
          "Reverting to simple centralized prefix allocation.",
          folly::exceptionStr(ex));

      // Revert to centralized prefix allocation
      centralizedPrefixAllocEnabled = true;
      deterministicPrefixAllocEnabled = false;
    }
  }

  if (centralizedPrefixAllocEnabled) {
    try {
      prefixAllocator_ = std::make_unique<CentralizedPrefixAllocator>(
          prefixAllocParams, topologyW_.get(), configHelper);
      LOG(INFO) << "Centralized prefix allocation enabled";
    } catch (const std::invalid_argument& ex) {
      LOG(ERROR) << folly::format(
          "Centralized prefix allocation failed: {}. "
          "Reverting to distributed prefix allocation.",
          folly::exceptionStr(ex));
      centralizedPrefixAllocEnabled = false;
      controllerPrefixAllocationEnabled = false;
    }
  }

  // Set auto node config for OPENR_STATIC_PREFIX_ALLOC for all nodes
  setAllAutoNodeOpenrStaticPrefixAlloc(
      controllerPrefixAllocationEnabled, configHelper);

  if (!controllerPrefixAllocationEnabled) {
    topologyW_->setControllerPrefixAllocScheme(
      ControllerPrefixAllocScheme::NONE);
    // Clear all specific network prefix overrides if DPA isn't enabled.
    clearAutoSpecificNetworkPrefixes(configHelper);
  } else {
    if (centralizedPrefixAllocEnabled) {
      topologyW_->setControllerPrefixAllocScheme(
        ControllerPrefixAllocScheme::CENTRALIZED_PREFIX_ALLOCATION);
    } else {
      topologyW_->setControllerPrefixAllocScheme(
        ControllerPrefixAllocScheme::DETERMINISTIC_PREFIX_ALLOCATION);
    }
  }

  return controllerPrefixAllocationEnabled;
}

void
TopologyApp::setAllAutoNodeOpenrStaticPrefixAlloc(
    const bool openrStaticPrefixAllocEnabled, ConfigHelper& configHelper) {
  std::string errorMsg;
  for (const auto& node : topologyW_->getAllNodes()) {
    if (!configHelper.setAutoNodeOpenrStaticPrefixAlloc(
            node.name, openrStaticPrefixAllocEnabled, errorMsg)) {
      LOG(ERROR) << folly::format(
          "Error setting OPENR_STATIC_PREFIX_ALLOC for {} to {}",
          node.name,
          openrStaticPrefixAllocEnabled);
    }
  }
}

void
TopologyApp::clearAutoSpecificNetworkPrefixes(ConfigHelper& configHelper) {
  std::string errorMsg;
  for (const auto& node : topologyW_->getAllNodes()) {
    if (!configHelper.setAutoNodeSpecificNetworkPrefixes(
            node.name, std::nullopt, errorMsg)) {
      LOG(ERROR) << folly::format(
          "Error clearing specificNetworkPrefixes for {}", node.name);
    }
  }
}

void
TopologyApp::processMessage(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) noexcept {

  switch (message.mType) {
    case thrift::MessageType::LINK_STATUS:
      processLinkStatus(minion, senderApp, message);
      break;
    case thrift::MessageType::LINK_AUTHORIZED:
      processLinkAuthorized(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_TOPOLOGY:
      processGetTopology(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_NETWORK_AIRTIME:
      processGetNetworkAirtime(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_ROUTES:
      processGetRoutes(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_DEFAULT_ROUTES:
      processGetDefaultRoutes(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_ROUTING_ADJACENCIES:
      processGetRoutingAdjacencies(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_ZONE_PREFIXES:
      processGetZonePrefixes(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_NODE_PREFIXES:
      processGetNodePrefixes(minion, senderApp, message);
      break;
    case thrift::MessageType::ALLOCATE_PREFIXES:
      processAllocatePrefixes(minion, senderApp, message);
      break;
    case thrift::MessageType::PREFIX_ALLOC_PARAMS_UPDATED:
      processPrefixAllocParamsUpdated(minion, senderApp, message);
      break;
    case thrift::MessageType::ROUTING_ADJACENCIES:
      processRoutingAdjacencies(minion, senderApp, message);
      break;
    case thrift::MessageType::SET_NODE_STATUS:
      processSetNodeStatus(minion, senderApp, message);
      break;
    case thrift::MessageType::SET_NODE_PARAMS_REQ:
      processSetNodeParamsReq(minion, senderApp, message);
      break;
    case thrift::MessageType::SET_NETWORK_PARAMS_REQ:
      processSetNetworkParamsReq(minion, senderApp, message);
      break;
    case thrift::MessageType::SET_NODE_MAC:
      processSetNodeMac(minion, senderApp, message);
      break;
    case thrift::MessageType::SET_NODE_MAC_LIST:
      processSetNodeMacList(minion, senderApp, message);
      break;
    case thrift::MessageType::ADD_NODE_WLAN_MACS:
      processAddNodeWlanMacs(minion, senderApp, message);
      break;
    case thrift::MessageType::DEL_NODE_WLAN_MACS:
      processDelNodeWlanMacs(minion, senderApp, message);
      break;
    case thrift::MessageType::CHANGE_NODE_WLAN_MAC:
      processChangeNodeWlanMac(minion, senderApp, message);
      break;
    case thrift::MessageType::BUMP_LINKUP_ATTEMPTS:
      processBumpLinkUpAttempts(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_NODE:
      processGetNode(minion, senderApp, message);
      break;
    case thrift::MessageType::ADD_NODE:
      processAddNode(minion, senderApp, message);
      break;
    case thrift::MessageType::DEL_NODE:
      processDelNode(minion, senderApp, message);
      break;
    case thrift::MessageType::EDIT_NODE:
      processEditNode(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_LINK:
      processGetLink(minion, senderApp, message);
      break;
    case thrift::MessageType::ADD_LINK:
      processAddLink(minion, senderApp, message);
      break;
    case thrift::MessageType::DEL_LINK:
      processDelLink(minion, senderApp, message);
      break;
    case thrift::MessageType::GET_SITE:
      processGetSite(minion, senderApp, message);
      break;
    case thrift::MessageType::ADD_SITE:
      processAddSite(minion, senderApp, message);
      break;
    case thrift::MessageType::DEL_SITE:
      processDelSite(minion, senderApp, message);
      break;
    case thrift::MessageType::EDIT_SITE:
      processEditSite(minion, senderApp, message);
      break;
    case thrift::MessageType::RESET_TOPOLOGY_STATE:
      processResetTopologyState(minion, senderApp, message);
      break;
    case thrift::MessageType::GPS_GET_POS_RESP:
      processGpsGetPosResp(minion, senderApp, message);
      break;
    case thrift::MessageType::SET_TOPOLOGY_NAME:
      processSetTopologyName(minion, senderApp, message);
      break;
    case thrift::MessageType::BULK_ADD:
      processBulkAdd(minion, senderApp, message);
      break;
    case thrift::MessageType::BSTAR_GET_APP_DATA:
      processBstarGetAppData(minion, senderApp, message);
      break;
    case thrift::MessageType::BSTAR_APP_DATA:
      processBstarAppData(minion, senderApp, message);
      break;
    case thrift::MessageType::SET_WIRED_LINK_STATUS:
      processSetWiredLinkStatus(minion, senderApp, message);
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
TopologyApp::processLinkStatus(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto linkStatus = maybeReadThrift<thrift::LinkStatus>(message);
  if (!linkStatus) {
    handleInvalidMessage("LinkStatus", senderApp, minion, false);
    return;
  }
  bool alive = (linkStatus->linkStatusType == thrift::LinkStatusType::LINK_UP);
  std::string linkStatusStr = folly::get_default(
      TEnumMapFactory<thrift::LinkStatusType>::makeValuesToNamesMap(),
      linkStatus->linkStatusType,
      "UNKNOWN");

  // NOTE:
  // Here, "responderMac" refers to the other end of the link. It is NOT
  // necessarily the "responder" in the assoc procedure (bad naming...).

  // Existence checks
  auto maybeNode = topologyW_->getNodeByMac(minion);
  if (!maybeNode) {
    LOG(ERROR) << "Received LinkStatus message (" << linkStatusStr
                << ") from unknown node with MAC address " << minion;
    return;
  }
  if (linkStatus->radioMac_ref().has_value()) {
    auto iter = std::find(
        maybeNode->wlan_mac_addrs.begin(),
        maybeNode->wlan_mac_addrs.end(),
        linkStatus->radioMac_ref().value());
    if (iter == maybeNode->wlan_mac_addrs.end() &&
        maybeNode->mac_addr
            != linkStatus->radioMac_ref().value() /* backwards compat */) {
      LOG(ERROR) << "Received LinkStatus message (" << linkStatusStr
                  << ") from node [" << maybeNode->name
                  << "] reporting unknown radio MAC address "
                  << linkStatus->radioMac_ref().value();
      return;
    }
  }
  auto maybeResponderNode = topologyW_->getNodeByMac(linkStatus->responderMac);
  std::string linkName;
  bool isUnrecognizedLink = false;
  if (maybeResponderNode) {
    auto maybeLinkName =
        topologyW_->getLinkName(maybeNode->name, maybeResponderNode->name);
    if (maybeLinkName) {
      linkName = maybeLinkName.value();
    } else {
      LOG(ERROR) << "Received LinkStatus message (" << linkStatusStr
                 << ") from unrecognized link between ["
                 << maybeNode->name << "], [" << maybeResponderNode->name
                 << "] from " << maybeNode->name << ":" << senderApp;
      isUnrecognizedLink = true;
    }
  } else {
    LOG(ERROR) << "Received LinkStatus message (" << linkStatusStr
                << ") to unknown responder node with MAC address "
                << linkStatus->responderMac << " from " << maybeNode->name
                << ":" << senderApp;
    isUnrecognizedLink = true;
  }

  // If we receive LINK_UP but the link should not exist (e.g. unknown/deleted),
  // forcefully dissoc the link now.
  if (isUnrecognizedLink) {
    if (alive) {
      // Notify IgnitionApp to send force dissoc
      thrift::ForceDissocReq forceDissocReq;
      forceDissocReq.initiatorMac = linkStatus->radioMac_ref().value_or(minion);
      forceDissocReq.responderMac = linkStatus->responderMac;
      sendToCtrlApp(
          E2EConsts::kIgnitionAppCtrlId,
          thrift::MessageType::FORCE_DISSOC_REQ,
          forceDissocReq);
    }
  } else {
    // Received LINK_UP/LINK_DOWN from initiator/responder node
    auto link = topologyW_->getLink(linkName);
    LOG(INFO) << "Received " << linkStatusStr << " from " << maybeNode->name
              << ":" << senderApp << " for " << link->name;

    // Remove link from linksInUnknownStatus_ if necessary
    linksInUnknownStatus_.erase(linkName);

    // Update link status DB right away, for the link is truly up/down now
    if (link->is_alive != alive) {
      topologyW_->setLinkStatus(linkName, alive);
      linkStatusChanged(
          maybeNode->name,
          *link,
          linkStatus->linkStatusType,
          "Link status update from minion");

      // Update globally-shared topology wrapper
      SharedObjects::getTopologyWrapper()->wlock()->setTopology(
          topologyW_->getTopology());
    }

    // Notify IgnitionApp
    thrift::LinkStatusEvent linkStatusEvent;
    linkStatusEvent.linkName = linkName;
    linkStatusEvent.linkStatusType = linkStatus->linkStatusType;
    linkStatusEvent.nodeName = maybeNode->name;
    sendToCtrlApp(
        E2EConsts::kIgnitionAppCtrlId,
        thrift::MessageType::LINK_STATUS_EVENT,
        linkStatusEvent);
  }

  // When a DN-DN link goes down, both sectors will enter BF responder mode.
  // For P2MP DNs with other ignited links *and* intact connection to the
  // controller, we should disable responder mode to avoid the associated ~50%
  // throughput loss. If the node is not reachable, the message should be
  // (intentionally) dropped.
  if (FLAGS_send_bf_resp_scan_disable_on_link_down &&
      linkStatus->isEvent && !alive &&
      maybeNode && maybeResponderNode &&
      maybeNode->node_type == thrift::NodeType::DN &&
      maybeResponderNode->node_type == thrift::NodeType::DN) {
    sendBfRespScanDisable(*maybeNode,
        linkStatus->radioMac_ref().value_or(minion));
    sendBfRespScanDisable(*maybeResponderNode, linkStatus->responderMac);
  }
}

bool
TopologyApp::sendBfRespScanDisable(
    const thrift::Node& node, const string& radioMac) {
  int aliveLinkCount = 0;
  for (const thrift::Link& link : topologyW_->getLinksByRadioMac(radioMac)) {
    if (link.is_alive && link.link_type == thrift::LinkType::WIRELESS) {
      aliveLinkCount++;
    }
  }
  if (!aliveLinkCount) {
    VLOG(5) << "Skipping BF_RESP_SCAN_DISABLE to node ["
            << node.name << "] for radio MAC " << radioMac
            << " with no active links";
    return false;
  }
  VLOG(3) << "Sending BF_RESP_SCAN_DISABLE to node ["
          << node.name << "] for radio MAC " << radioMac << " with "
          << aliveLinkCount << " active link(s)";
  thrift::BfRespScan bfRespScan;
  bfRespScan.radioMac = radioMac;
  sendToMinionApp(
      node.mac_addr,
      E2EConsts::kIgnitionAppMinionId,
      thrift::MessageType::BF_RESP_SCAN_DISABLE,
      bfRespScan);
  return true;
}

void
TopologyApp::processLinkAuthorized(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto linkAuthorized = maybeReadThrift<thrift::LinkAuthorized>(message);
  if (!linkAuthorized) {
    handleInvalidMessage("LinkAuthorized", senderApp, minion, false);
    return;
  }

  // Nothing to do
  VLOG(4) << folly::format(
      "Received LINK_AUTHORIZED from {}:{} (wsec={}): "
      "responderMac={}, radioMac={}, ifname={}",
      minion,
      senderApp,
      linkAuthorized->wsecEnable,
      linkAuthorized->responderMac,
      linkAuthorized->radioMac,
      linkAuthorized->ifname);
}

void
TopologyApp::processGetTopology(
    const string& minion,
    const string& senderApp,
    const thrift::Message& /* message */) {
  VLOG(4) << "Received getTopology message from " << minion << ":" << senderApp;
  sendToCtrlApp(
      senderApp, thrift::MessageType::TOPOLOGY, topologyW_->getTopology());
}

void
TopologyApp::processGetNetworkAirtime(
    const string& minion,
    const string& senderApp,
    const thrift::Message& /* message */) {
  VLOG(4) << "Received getNetworkAirtime message from " << minion << ":"
          << senderApp;
  sendToCtrlApp(
      senderApp, thrift::MessageType::NETWORK_AIRTIME, nwAirtimeAlloc_);
}

void
TopologyApp::processGetRoutes(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  VLOG(3) << "Received getRoutes message from " << minion << ":" << senderApp;
  auto getRoutes = maybeReadThrift<thrift::GetRoutes>(message);
  if (!getRoutes) {
    handleInvalidMessage("GetRoutes", senderApp, minion);
    return;
  }

  // Check if src/dst node exist
  if (!topologyW_->getNode(getRoutes->srcNode)) {
    sendE2EAck(senderApp, false, "srcNode does not exist");
    return;
  }
  if (!topologyW_->getNode(getRoutes->dstNode)) {
    sendE2EAck(senderApp, false, "dstNode does not exist");
    return;
  }

  // Compute routes
  auto lockedRoutingAdj = SharedObjects::getRoutingAdjacencies()->rlock();
  auto routes = routesHelper_->computeRoutes(
      getRoutes->srcNode, getRoutes->dstNode, *lockedRoutingAdj);
  lockedRoutingAdj.unlock();  // lockedRoutingAdj -> NULL
  thrift::GetRoutesResp getRoutesResp;
  getRoutesResp.routes = routes;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_ROUTES_RESP,
      getRoutesResp);
}

void
TopologyApp::processGetDefaultRoutes(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  VLOG(3) << "Received getDefaultRoutes message from " << minion << ":"
          << senderApp;
  auto getDefaultRoutes = maybeReadThrift<thrift::GetDefaultRoutes>(message);
  if (!getDefaultRoutes) {
    handleInvalidMessage("GetDefaultRoutes", senderApp, minion);
    return;
  }

  // Compute default routes
  auto lockedRoutingAdj = SharedObjects::getRoutingAdjacencies()->rlock();
  std::unordered_map<string, std::vector<std::vector<string>>> defaultRoutes;
  for (const auto& node : getDefaultRoutes->nodes) {
    // Check if node exists
    if (!topologyW_->getNode(node)) {
      sendE2EAck(
          senderApp, false, folly::sformat("Node {} does not exist", node));
      return;
    }

    // Check if node is already in defaultRoutes
    if (defaultRoutes.count(node)) {
      continue;
    }

    defaultRoutes.insert(
        {node,
         routesHelper_->computeRoutes(node, std::nullopt, *lockedRoutingAdj)});
  }
  lockedRoutingAdj.unlock();  // lockedRoutingAdj -> NULL

  thrift::GetDefaultRoutesResp getDefaultRoutesResp;
  getDefaultRoutesResp.defaultRoutes = defaultRoutes;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_DEFAULT_ROUTES_RESP,
      getDefaultRoutesResp);
}

void
TopologyApp::syncWithStatusReports() {
  bool didTopologyChange = false;
  auto statusReports = SharedObjects::getStatusReports()->copy();

  // time_since_epoch: duration since the start of the clock
  auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::steady_clock::now().time_since_epoch())
                 .count();

  // Check all status reports to determine alive/dead nodes
  int aliveNodesCnt = 0;
  string reachablePop;
  std::vector<thrift::Node> nodesNeedGpsConfigs;
  for (const auto& it : statusReports) {
    string mac = it.first;

    // skip invalid nodes
    auto currNode = topologyW_->getNodeByMac(mac);
    if (!currNode) {
      continue;
    }

    // receive heartbeat within timeout
    if (chrono::seconds(now) -
        it.second.steadyTs.time_since_epoch() < nodeAliveTimeOut_) {
      thrift::NodeStatusType newStatus;
      if (it.second.report.status == thrift::NodeStatusType::OFFLINE) {
        newStatus = thrift::NodeStatusType::ONLINE;
      } else {
        newStatus = it.second.report.status;
      }
      aliveNodesCnt++;
      if (currNode->status != newStatus) {
        topologyW_->setNodeStatus(currNode->name, newStatus);
        nodeStatusChanged(
            "STATUS_DUMP",
            *currNode,
            "Receiving heartbeat from node within timeout, mark it up.",
            it.second.report.status);
        didTopologyChange = true;
      }

      // location and gps_enable needed for ONLINE -> ONLINE_INITIATOR
      if (it.second.report.status != thrift::NodeStatusType::ONLINE_INITIATOR &&
          currNode->node_type == thrift::NodeType::DN) {
        nodesNeedGpsConfigs.push_back(*currNode);
      }

      // Find a reachable POP to request routing adjacencies from
      // Prefer keeping the old node (reachablePop_), if alive
      if (currNode->pop_node &&
          (reachablePop.empty() || reachablePop != reachablePop_)) {
        reachablePop = mac;
      }

      continue;
    }

    // exceed timeout, mark node down
    if (currNode->status != thrift::NodeStatusType::OFFLINE) {
      topologyW_->setNodeStatus(
          currNode->name, thrift::NodeStatusType::OFFLINE);
      nodeStatusChanged(
          "STATUS_DUMP",
          *currNode,
          "Exceeded node alive timeout, marked it down as well as all links "
          "associated to it.",
          thrift::NodeStatusType::OFFLINE);
      addLinksInUnknownStatus(*currNode);
      didTopologyChange = true;
    }
  }

  VLOG(2) << "Number of alive/dead nodes: " << aliveNodesCnt << "/"
          << (statusReports.size() - aliveNodesCnt);

  reachablePop_ = reachablePop;

  // Send GPS configs
  if (!nodesNeedGpsConfigs.empty()) {
    auto lockedConfigHelper = SharedObjects::getConfigHelper()->rlock();
    for (const thrift::Node& node : nodesNeedGpsConfigs) {
      sendGpsConfigurations(
          node, lockedConfigHelper->isForceGpsDisable(node));
    }
  }

  // If no heartbeat was ever heard from a particular node, mark the node down
  // (This can happen when BinaryStarApp syncs the topology from the peer)
  for (const auto& node : topologyW_->getAllNodes()) {
    if (statusReports.count(node.mac_addr)) {
      continue;
    }
    if (node.status != thrift::NodeStatusType::OFFLINE) {
      topologyW_->setNodeStatus(node.name, thrift::NodeStatusType::OFFLINE);
      nodeStatusChanged(
          "STATUS_DUMP",
          node,
          "Marking node without status reports as offline.",
          thrift::NodeStatusType::OFFLINE);
      didTopologyChange = true;
    }
    addLinksInUnknownStatus(node);
  }

  // If both nodes of a link are down, mark the link down
  for (const auto& link : topologyW_->getAllLinks()) {
    if (link.link_type == thrift::LinkType::ETHERNET || !link.is_alive) {
      continue;
    }
    auto aNode = topologyW_->getNode(link.a_node_name);
    if (aNode->status != thrift::NodeStatusType::OFFLINE) {
      continue;  // A-node is still online
    }
    auto zNode = topologyW_->getNode(link.z_node_name);
    if (zNode->status != thrift::NodeStatusType::OFFLINE) {
      continue;  // Z-node is still online
    }

    // mark link down
    topologyW_->setLinkStatus(link.name, false);
    linkStatusChanged(
        "e2e_controller",
        link,
        thrift::LinkStatusType::LINK_DOWN,
        "Marking link as down because both ends are offline");
    didTopologyChange = true;
  }

  // sanitize link in unknown status
  sanitizeLinkStatus();

  if (didTopologyChange) {
    // Update globally-shared topology wrapper
    SharedObjects::getTopologyWrapper()->wlock()->setTopology(
        topologyW_->getTopology());
  }
}

void
TopologyApp::processGetRoutingAdjacencies(
    const string& minion,
    const string& senderApp,
    const thrift::Message& /* message */) {
  VLOG(4) << "Received getRoutingAdjacencies message from " << minion << ":"
          << senderApp;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::ROUTING_ADJACENCIES,
      *SharedObjects::getRoutingAdjacencies()->rlock());
}

void
TopologyApp::processRoutingAdjacencies(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  VLOG(3) << "Received RoutingAdjacencies from " << minion << ":" << senderApp;

  auto routingAdj = maybeReadThrift<thrift::RoutingAdjacencies>(message);
  if (!routingAdj) {
    handleInvalidMessage("RoutingAdjacencies", senderApp, minion, false);
    return;
  }

  // XXX HACK for controller backwards-compatibility with pre-M78 versions
  for (auto& [nodeId, adjacencyDb] : routingAdj.value().adjacencyMap) {
    adjacencyDb.area_ref() = kDefaultArea;
  }

  // store new routing adjacencies
  SharedObjects::getRoutingAdjacencies()->swap(routingAdj.value());

  // notify routes helper that we have new routing adjacencies
  routesHelper_->routingAdjacenciesUpdated();
}

void
TopologyApp::processSetNodeStatus(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  VLOG(4) << "Received SetNodeStatus from " << minion << ":" << senderApp;

  auto setNodeStatus = maybeReadThrift<thrift::SetNodeStatus>(message);
  if (!setNodeStatus) {
    handleInvalidMessage("SetNodeStatus", senderApp, minion, false);
    return;
  }

  // skip invalid nodes
  auto node = topologyW_->getNodeByMac(setNodeStatus->nodeMac);
  if (!node) {
    LOG(ERROR) << "Received SetNodeStatus with invalid node MAC "
               << setNodeStatus->nodeMac;
    return;
  }

  // update node status
  if (setNodeStatus->nodeStatus != node->status) {
    topologyW_->setNodeStatus(node->name, setNodeStatus->nodeStatus);

    nodeStatusChanged(
        "SET_NODE_STATUS",
        *node,
        "Receiving SET_NODE_STATUS",
        setNodeStatus->nodeStatus);

    // Update globally-shared topology wrapper
    SharedObjects::getTopologyWrapper()->wlock()->setTopology(
        topologyW_->getTopology());
  }

  bumpCounter(setNodeStatus->nodeMac + ".setNodeStatus.rcvd");
}

void
TopologyApp::processSetNodeParamsReq(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto setNodeParamsReq = maybeReadThrift<thrift::SetNodeParamsReq>(message);
  if (!setNodeParamsReq) {
    handleInvalidMessage("SetNodeParamsReq", senderApp, minion, false);
    return;
  }

  // skip invalid nodes
  auto node = topologyW_->getNodeByMac(setNodeParamsReq->nodeMac);
  if (!node) {
    LOG(ERROR) << "Received SetNodeParamsReq with invalid node MAC `"
               << setNodeParamsReq->nodeMac << "` from " << minion << ":"
               << senderApp;
    return;
  }

  LOG(INFO) << "Sending SetNodeParams to " << node->name << ".  Requested by "
            << minion << ":" << senderApp;

  thrift::NodeParams nodeParams;
  nodeParams.type = thrift::NodeParamsType::INIT;

  // Check if there's any update requests on airtimeAllocMap
  // if not, config with latest config on topoglogyApp
  if (setNodeParamsReq->nodeAirtime_ref().has_value()) {
    LOG(INFO) << "Updating airtimeAllocMap ... ";
    nwAirtimeAlloc_.nodeAirtimeMap[node->name] =
        setNodeParamsReq->nodeAirtime_ref().value();
  }
  map<string, thrift::NodeAirtime>& nodeAirtimeMap =
      nwAirtimeAlloc_.nodeAirtimeMap;

  // fill out airtime map (overrides node config)
  if (nodeAirtimeMap.find(node->name) == nodeAirtimeMap.end()) {
    VLOG(3) << "No airtime allocation found for node " << node->name;
  } else {
    nodeParams.airtimeAllocMap_ref() = nodeAirtimeMap.at(node->name);
  }

  // Get radio configurations
  std::vector<thrift::NodeParams> nodeParamsPerRadio;
  if (node->wlan_mac_addrs.empty()) {
    // no radios on node (yet?), just send empty INIT response
    nodeParamsPerRadio.push_back(nodeParams);
  } else {
    auto lockedConfigHelper = SharedObjects::getConfigHelper()->rlock();
    for (const std::string& radioMac : node->wlan_mac_addrs) {
      thrift::NodeParams radioNodeParams = nodeParams;
      radioNodeParams.radioMac_ref() = radioMac;

      // Look up polarity/channel
      // NOTE: polarity/channel are set by the minion directly since
      // RELEASE_M42, and these parameters should be ignored.
      // This is here for backwards compatibility OR if managedConfig is
      // disabled.

      auto polarity = lockedConfigHelper->getRadioPolarity(
          node->name, radioMac, false);
      if (polarity.has_value()) {
        radioNodeParams.polarity_ref() = polarity.value();
      } else {
        radioNodeParams.polarity_ref().reset();
      }
      auto channel = lockedConfigHelper->getRadioChannel(
          node->name, radioMac, false, false);
      if (channel.has_value()) {
        radioNodeParams.channel_ref() = channel.value();
      } else {
        radioNodeParams.channel_ref().reset();
      }

      nodeParamsPerRadio.push_back(radioNodeParams);
    }
    lockedConfigHelper.unlock();  // lockedConfigHelper -> NULL
  }

  // Send one message per radioMac
  for (const thrift::NodeParams& radioNodeParams : nodeParamsPerRadio) {
    sendToMinionApp(
        node->mac_addr,
        E2EConsts::kStatusAppMinionId,
        thrift::MessageType::SET_NODE_PARAMS,
        radioNodeParams);
  }
}

void
TopologyApp::processSetNetworkParamsReq(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto setNetworkParamsReq =
      maybeReadThrift<thrift::SetNetworkParamsReq>(message);
  if (!setNetworkParamsReq) {
    handleInvalidMessage("SetNetworkParamsReq", senderApp, minion, false);
    return;
  }

  // Update airtimeAllocMap configuration
  if (setNetworkParamsReq->networkAirtime_ref().has_value()) {
    // Validity check
    for (const auto& nodeParam :
         setNetworkParamsReq->networkAirtime_ref().value().nodeAirtimeMap) {
      auto node = topologyW_->getNode(nodeParam.first);
      if (!node) {
        LOG(ERROR) << "Received SetNetworkParamsReq from " << minion << ":"
                   << senderApp << ": invalid node name in airtimeAllocMap: `"
                   << nodeParam.first << "` ";
        return;
      }
    }

    // Update airtimeAllocMap in topologyApp
    nwAirtimeAlloc_.nodeAirtimeMap_ref() =
        setNetworkParamsReq->networkAirtime_ref().value().nodeAirtimeMap;

    // Send SetNodeParamsReq to each node
    for (const auto& updateNodeParam :
         setNetworkParamsReq->networkAirtime_ref().value().nodeAirtimeMap) {
      auto node = topologyW_->getNode(updateNodeParam.first);
      if (!node || node->mac_addr.empty()) {
        continue;
      }

      LOG(INFO) << "Sending SetNodeParams to " << node->name
                << ".  Requested by " << minion << ":" << senderApp;

      thrift::NodeParams nodeParams;
      nodeParams.airtimeAllocMap_ref() = updateNodeParam.second;

      nodeParams.type = thrift::NodeParamsType::NETWORK;
      sendToMinionApp(
          node->mac_addr,
          E2EConsts::kStatusAppMinionId,
          thrift::MessageType::SET_NODE_PARAMS,
          nodeParams);
    }
  }
}

void
TopologyApp::processSetNodeMac(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto setNodeMac = maybeReadThrift<thrift::SetNodeMac>(message);
  if (!setNodeMac) {
    handleInvalidMessage("SetNodeMac", senderApp, minion);
    return;
  }

  LOG(INFO) << "Received SetNodeMac for " << setNodeMac->nodeName << " to "
            << setNodeMac->nodeMac << " with force flag set to "
            << setNodeMac->force;

  auto node = topologyW_->getNode(setNodeMac->nodeName);
  try {
    topologyW_->setNodeMacByName(
        setNodeMac->nodeName, setNodeMac->nodeMac, setNodeMac->force);
  } catch (exception const& e) {
    sendE2EAck(
        senderApp,
        false,
        "Setting MAC failed because: " + folly::exceptionStr(e).toStdString());
    return;
  }

  // Update config
  SharedObjects::getConfigHelper()->wlock()->onChangeRadioMac(
      *topologyW_, setNodeMac->nodeName, node->mac_addr, setNodeMac->nodeMac);

  topologyChanged(
      thrift::MessageType::SET_NODE_MAC,
      folly::sformat("{}:{}", minion, senderApp),
      setNodeMac->nodeName + "(" + setNodeMac->nodeMac + ")",
      thrift::EventId::TOPOLOGY_NODE_MODIFIED);
  sendE2EAck(senderApp, true, "Setting node MAC successful");
}

void
TopologyApp::processSetNodeMacList(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto setNodeMacList = maybeReadThrift<thrift::SetNodeMacList>(message);
  if (!setNodeMacList) {
    handleInvalidMessage("SetNodeMacList", senderApp, minion);
    return;
  }

  LOG(INFO) << "Received SetNodeMacList with "
            << setNodeMacList->setNodeMacList.size()
            << " node to mac address mappings";

  // set each mac address
  int failedCount = 0;
  for (const auto& setNodeMac : setNodeMacList->setNodeMacList) {
    auto node = topologyW_->getNode(setNodeMac.nodeName);
    try {
      topologyW_->setNodeMacByName(
          setNodeMac.nodeName, setNodeMac.nodeMac, setNodeMac.force);
    } catch (exception const& e) {
      failedCount++;
      continue;
    }

    // Update config
    SharedObjects::getConfigHelper()->wlock()->onChangeRadioMac(
        *topologyW_, setNodeMac.nodeName, node->mac_addr, setNodeMac.nodeMac);

    // log each mac change individually
    topologyChanged(
        thrift::MessageType::SET_NODE_MAC,
        folly::sformat("{}:{}", minion, senderApp),
        setNodeMac.nodeName + "(" + setNodeMac.nodeMac + ")",
        thrift::EventId::TOPOLOGY_NODE_MODIFIED);
  }

  sendE2EAck(
      senderApp,
      failedCount == 0,
      failedCount ? folly::sformat(
                        "Failed setting {}/{} MAC addresses",
                        failedCount,
                        setNodeMacList->setNodeMacList.size())
                  : std::string("Setting node MAC list successful"));
}

void
TopologyApp::processAddNodeWlanMacs(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto addNodeWlanMacs = maybeReadThrift<thrift::AddNodeWlanMacs>(message);
  if (!addNodeWlanMacs) {
    handleInvalidMessage("AddNodeWlanMacs", senderApp, minion);
    return;
  }

  LOG(INFO) << "Received AddNodeWlanMacs for " << addNodeWlanMacs->nodeName
            << " with " << addNodeWlanMacs->wlanMacs.size()
            << " WLAN MAC addresses: "
            << folly::join(", ", addNodeWlanMacs->wlanMacs);

  try {
    topologyW_->addNodeWlanMacs(
        addNodeWlanMacs->nodeName, addNodeWlanMacs->wlanMacs);
  } catch (exception const& e) {
    sendE2EAck(
        senderApp,
        false,
        "Adding WLAN MACs failed because: " +
            folly::exceptionStr(e).toStdString());
    return;
  }

  // Update config
  for (const auto& wlanMac : addNodeWlanMacs->wlanMacs) {
    SharedObjects::getConfigHelper()->wlock()->onChangeRadioMac(
        *topologyW_, addNodeWlanMacs->nodeName, "", wlanMac);
  }

  topologyChanged(
      thrift::MessageType::ADD_NODE_WLAN_MACS,
      folly::sformat("{}:{}", minion, senderApp),
      addNodeWlanMacs->nodeName,
      thrift::EventId::TOPOLOGY_NODE_MODIFIED);
  sendE2EAck(senderApp, true, "Adding node WLAN MACs successful");
}

void
TopologyApp::processDelNodeWlanMacs(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto delNodeWlanMacs = maybeReadThrift<thrift::DelNodeWlanMacs>(message);
  if (!delNodeWlanMacs) {
    handleInvalidMessage("DelNodeWlanMacs", senderApp, minion);
    return;
  }

  LOG(INFO) << "Received DelNodeWlanMacs for " + minion + " with "
            << delNodeWlanMacs->wlanMacs.size() << " WLAN MAC addresses: "
            << folly::join(", ", delNodeWlanMacs->wlanMacs);

  try {
    topologyW_->deleteNodeWlanMacs(
        delNodeWlanMacs->nodeName,
        delNodeWlanMacs->wlanMacs,
        delNodeWlanMacs->force);
  } catch (exception const& e) {
    sendE2EAck(
        senderApp,
        false,
        "Deleting WLAN MACs failed because: " +
            folly::exceptionStr(e).toStdString());
    return;
  }

  topologyChanged(
      thrift::MessageType::DEL_NODE_WLAN_MACS,
      folly::sformat("{}:{}", minion, senderApp),
      delNodeWlanMacs->nodeName,
      thrift::EventId::TOPOLOGY_NODE_MODIFIED);
  sendE2EAck(senderApp, true, "Deleting node WLAN MACs successful");
}

void
TopologyApp::processChangeNodeWlanMac(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto changeNodeWlanMac = maybeReadThrift<thrift::ChangeNodeWlanMac>(message);
  if (!changeNodeWlanMac) {
    handleInvalidMessage("ChangeNodeWlanMac", senderApp, minion);
    return;
  }

  LOG(INFO) << "Received ChangeNodeWlanMac for " + minion
            << ". Changing WLAN MAC `" << changeNodeWlanMac->oldWlanMac
            << "` to `" << changeNodeWlanMac->newWlanMac << "`";

  try {
    topologyW_->changeNodeWlanMac(
        changeNodeWlanMac->nodeName,
        changeNodeWlanMac->oldWlanMac,
        changeNodeWlanMac->newWlanMac,
        changeNodeWlanMac->force);
  } catch (exception const& e) {
    sendE2EAck(
        senderApp,
        false,
        "Changing WLAN MAC failed because: " +
            folly::exceptionStr(e).toStdString());
    return;
  }

  SharedObjects::getConfigHelper()->wlock()->onChangeRadioMac(
      *topologyW_,
      changeNodeWlanMac->nodeName,
      changeNodeWlanMac->oldWlanMac,
      changeNodeWlanMac->newWlanMac);

  topologyChanged(
      thrift::MessageType::CHANGE_NODE_WLAN_MAC,
      folly::sformat("{}:{}", minion, senderApp),
      changeNodeWlanMac->nodeName,
      thrift::EventId::TOPOLOGY_NODE_MODIFIED);
  sendE2EAck(senderApp, true, "Changing node WLAN MAC successful");
}

void
TopologyApp::processSetTopologyName(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto setTopologyName = maybeReadThrift<thrift::SetTopologyName>(message);
  if (!setTopologyName) {
    handleInvalidMessage("SetTopologyName", senderApp, minion);
    return;
  }

  LOG(INFO) << "Received SetTopologyName to: " << setTopologyName->name;

  topologyW_->setTopologyName(setTopologyName->name);
  std::string err;
  std::vector<std::string> nodeNames =
      folly::gen::from(topologyW_->getAllNodes()) |
      folly::gen::field(&thrift::Node::name) |
      folly::gen::as<std::vector<std::string>>();
  if (!SharedObjects::getConfigHelper()->wlock()->setNodeTopologyInfo(
          nodeNames, *topologyW_, err)) {
    LOG(ERROR) << "Error generating topology info for nodes: " << err;
  }

  topologyChanged(
      thrift::MessageType::SET_TOPOLOGY_NAME,
      folly::sformat("{}:{}", minion, senderApp),
      setTopologyName->name,
      thrift::EventId::TOPOLOGY_NAME_MODIFIED);
  sendE2EAck(senderApp, true, "Setting topolgy name successful");

  // Update shared object
  *(SharedObjects::getTopologyName()->wlock()) = topologyW_->getTopologyName();
}

void
TopologyApp::processBulkAdd(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto bulkAdd = maybeReadThrift<thrift::BulkAdd>(message);
  if (!bulkAdd) {
    handleInvalidMessage("BulkAdd", senderApp, minion);
    return;
  }

  LOG(INFO) << "Received BulkAdd with " << bulkAdd->sites.size() << " sites, "
            << bulkAdd->nodes.size() << " nodes, " << bulkAdd->links.size()
            << " links";

  thrift::BulkAddResult result;

  // nothing to add?
  if (bulkAdd->sites.empty() &&
      bulkAdd->nodes.empty() &&
      bulkAdd->links.empty()) {
    result.success = false;
    result.message = "Empty request";
    sendToCtrlApp(senderApp, thrift::MessageType::BULK_ADD_RESULT, result);
    return;
  }

  // add sites, nodes, and links individually
  result.success = true;
  for (const auto& site : bulkAdd->sites) {
    try {
      topologyW_->addSite(site);
    } catch (exception const& e) {
      result.success = false;
      result.failedSites[site.name] = folly::exceptionStr(e).toStdString();
      continue;
    }

    result.addedSites.push_back(site.name);
    topologyChanged(
        thrift::MessageType::ADD_SITE,
        folly::sformat("{}:{}", minion, senderApp),
        site.name,
        thrift::EventId::TOPOLOGY_SITE_ADDED);
  }
  if (bulkAdd->nodes.size() || bulkAdd->links.size()) {
    auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
    for (auto& node : bulkAdd->nodes) {
      try {
        topologyW_->addNode(node);
        if (prefixAllocator_) {
          prefixAllocator_->addNode(node, *lockedConfigHelper);
        }
        std::string err;
        if (!lockedConfigHelper->setNodeTopologyInfo(
                {node.name}, *topologyW_, err)) {
          LOG(ERROR) << "Error generating topology info for node '" << node.name
                     << "': " << err;
        }
      } catch (exception const& e) {
        result.success = false;
        result.failedNodes[node.name] = folly::exceptionStr(e).toStdString();
        continue;
      }

      result.addedNodes.push_back(node.name);
      topologyChanged(
          thrift::MessageType::ADD_NODE,
          folly::sformat("{}:{}", minion, senderApp),
          node.name,
          thrift::EventId::TOPOLOGY_NODE_ADDED);
    }
    for (auto& link : bulkAdd->links) {
      // We expect TopologyWrapper to generate the link name, but in case a
      // failure occurs beforehand, generate it here for logging purposes.
      std::string expectedLinkName =
          TopologyWrapper::buildLinkName(link.a_node_name, link.z_node_name);

      try {
        topologyW_->addLink(link);
        if (prefixAllocator_) {
          prefixAllocator_->addLink(link, *lockedConfigHelper);
        }
      } catch (exception const& e) {
        result.success = false;
        result.failedLinks[expectedLinkName] =
            folly::exceptionStr(e).toStdString();
        continue;
      }

      // Update config
      lockedConfigHelper->onAddLink(*topologyW_, link);
      std::string err;
      if (!lockedConfigHelper->setNodeTopologyInfo(
              {link.a_node_name, link.z_node_name}, *topologyW_, err)) {
        LOG(ERROR) << "Error generating topology info for link between '"
                   << link.a_node_name << "' and '" << link.z_node_name << "': "
                   << err;
      }

      result.addedLinks.push_back(expectedLinkName);
      topologyChanged(
          thrift::MessageType::ADD_LINK,
          folly::sformat("{}:{}", minion, senderApp),
          expectedLinkName,
          thrift::EventId::TOPOLOGY_LINK_ADDED);
    }
  }

  // If links were successfully added, airtime allocation needs to be updated
  if (result.addedLinks.size() > 0) {
    shouldUpdateAirtimeAlloc_ = true;
  }

  result.message = result.success
    ? "Bulk add successful"
    : folly::sformat(
          "Failed adding {}/{} site(s), {}/{} node(s), {}/{} link(s)",
          result.failedSites.size(),
          bulkAdd->sites.size(),
          result.failedNodes.size(),
          bulkAdd->nodes.size(),
          result.failedLinks.size(),
          bulkAdd->links.size());
  sendToCtrlApp(senderApp, thrift::MessageType::BULK_ADD_RESULT, result);
}

void
TopologyApp::reportTopologyStats() const {
  string topoName = topologyW_->getTopologyName();
  for (const auto& link : topologyW_->getAllLinks()) {
    auto aNode = topologyW_->getNode(link.a_node_name);
    auto zNode = topologyW_->getNode(link.z_node_name);
    setCounter(
        folly::sformat(
            "e2e_controller.link_status.{}.{}.{}",
            folly::get_default(
                TEnumMapFactory<thrift::LinkType>::makeValuesToNamesMap(),
                link.link_type, "UNKNOWN"),
            !link.a_node_mac.empty() ? link.a_node_mac
                                     : (aNode ? aNode->mac_addr : aNode->name),
            !link.z_node_mac.empty() ? link.z_node_mac
                                     : (zNode ? zNode->mac_addr : zNode->name)),
        (link.is_alive) ? 1 : 0,
        fbzmq::thrift::CounterValueType::GAUGE);
  }
}

void
TopologyApp::sendGetRoutingAdjacencies() {
  if (reachablePop_.empty()) {
    return;  // no alive/valid node to send to
  }

  sendToMinionApp(
      reachablePop_,
      E2EConsts::kOpenrClientAppMinionId,
      thrift::MessageType::GET_ROUTING_ADJACENCIES,
      thrift::GetRoutingAdjacencies());
}

void
TopologyApp::updateAirtimeAlloc() {
  if (!shouldUpdateAirtimeAlloc_) {
    return;  // nothing needs to be done this interval
  }

  // Recompute airtime allocation maps
  auto airtimeAlloc =
      BandwidthAllocationHelper::computeAirtimes(topologyW_.get());

  // Send updated NodeParams to any nodes whose link airtimes changed
  for (const auto& kv : airtimeAlloc.nodeAirtimeMap) {
    string node = kv.first;
    thrift::NodeAirtime nodeAirtime = kv.second;

    // Check if the node airtime struct didn't change
    auto iter = nwAirtimeAlloc_.nodeAirtimeMap.find(node);
    if (iter != nwAirtimeAlloc_.nodeAirtimeMap.end()) {
      // We don't care about the order of the linkAirtimes lists,
      // so turn them into maps (mac address -> LinkAirtime)
      std::unordered_map<string, thrift::LinkAirtime> prevLinkAirtimes;
      for (const auto& linkAirtime : iter->second.linkAirtimes) {
        prevLinkAirtimes[linkAirtime.macAddress] = linkAirtime;
      }
      std::unordered_map<string, thrift::LinkAirtime> linkAirtimes;
      for (const auto& linkAirtime : nodeAirtime.linkAirtimes) {
        linkAirtimes[linkAirtime.macAddress] = linkAirtime;
      }
      if (linkAirtimes == prevLinkAirtimes) {
        continue;  // no change, so skip this node
      }
    }

    LOG(INFO) << "Updating airtime allocation for node: " << node;

    // Send message to node
    string nodeMac = topologyW_->getNode(node)->mac_addr;
    thrift::NodeParams nodeParams;
    nodeParams.airtimeAllocMap_ref() = nodeAirtime;
    nodeParams.type = thrift::NodeParamsType::NETWORK;
    sendToMinionApp(
        nodeMac,
        E2EConsts::kStatusAppMinionId,
        thrift::MessageType::SET_NODE_PARAMS,
        nodeParams);
  }

  nwAirtimeAlloc_ = airtimeAlloc;
  shouldUpdateAirtimeAlloc_ = false;
}

void
TopologyApp::processBumpLinkUpAttempts(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  VLOG(4) << "Received BumpLinkUpAttempt from " << minion << ":" << senderApp;

  auto bumpLinkUpAttempts =
      maybeReadThrift<thrift::BumpLinkUpAttempts>(message);
  if (!bumpLinkUpAttempts) {
    handleInvalidMessage("BumpLinkUpAttempts", senderApp, minion, false);
    return;
  }

  topologyW_->bumpLinkupAttempts(bumpLinkUpAttempts->linkName);
}

void
TopologyApp::processGetNode(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto getNode = maybeReadThrift<thrift::GetNode>(message);
  if (!getNode) {
    handleInvalidMessage("GetNode", senderApp, minion);
    return;
  }

  auto maybeNode = topologyW_->getNode(getNode->name);
  if (maybeNode) {
    sendToCtrlApp(senderApp, thrift::MessageType::NODE, maybeNode.value());
  } else {
    sendE2EAck(senderApp, false, "Invalid node name");
  }
}

void
TopologyApp::processAddNode(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto addNode = maybeReadThrift<thrift::AddNode>(message);
  if (!addNode) {
    handleInvalidMessage("AddNode", senderApp, minion);
    return;
  }

  LOG(INFO) << "Processing AddNode for " << addNode->node.name;

  try {
    auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
    topologyW_->addNode(addNode->node);
    if (prefixAllocator_) {
      prefixAllocator_->addNode(addNode->node, *lockedConfigHelper);
    }
    std::string err;
    if (!lockedConfigHelper->setNodeTopologyInfo(
            {addNode->node.name}, *topologyW_, err)) {
      LOG(ERROR) << "Error generating topology info for " << addNode->node.name
                 << ": " << err;
    }
  } catch (exception const& e) {
    sendE2EAck(
        senderApp,
        false,
        "Adding node failed because: " + folly::exceptionStr(e).toStdString());
    return;
  }

  topologyChanged(
      thrift::MessageType::ADD_NODE,
      folly::sformat("{}:{}", minion, senderApp),
      addNode->node.name,
      thrift::EventId::TOPOLOGY_NODE_ADDED);
  sendToApiStream(thrift::MessageType::EVENT_ADD_NODE, addNode.value());
  sendE2EAck(senderApp, true, "Adding node successful");
}

void
TopologyApp::processDelNode(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto delNode = maybeReadThrift<thrift::DelNode>(message);
  if (!delNode) {
    handleInvalidMessage("DelNode", senderApp, minion);
    return;
  }

  LOG(INFO) << "Processing DelNode for " << delNode->nodeName;

  try {
    auto oldNode = topologyW_->getNode(delNode->nodeName);
    auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
    topologyW_->delNode(delNode->nodeName, delNode->force);
    if (prefixAllocator_) {
      // oldNode will not be std::nullopt if TopologyWrapper::delNode() does not
      // throw exception.
      prefixAllocator_->delNode(*oldNode, *lockedConfigHelper);
    }
    std::string errorMsg;
    lockedConfigHelper->deleteAllNodeOverrides(delNode->nodeName, errorMsg);
    lockedConfigHelper.unlock();  // lockedConfigHelper -> NULL
    // Delete the node's status report
    if (!oldNode->mac_addr.empty()) {
      SharedObjects::getStatusReports()->wlock()->erase(oldNode->mac_addr);
    }
  } catch (exception const& e) {
    sendE2EAck(
        senderApp,
        false,
        "Deleting node failed because: " +
            folly::exceptionStr(e).toStdString());
    return;
  }

  // If 'force' is set, some links could have been deleted,
  // so trigger an airtime allocation update
  if (delNode->force) {
    shouldUpdateAirtimeAlloc_ = true;
  }

  topologyChanged(
      thrift::MessageType::DEL_NODE,
      folly::sformat("{}:{}", minion, senderApp),
      delNode->nodeName,
      thrift::EventId::TOPOLOGY_NODE_REMOVED);
  sendToApiStream(thrift::MessageType::EVENT_DEL_NODE, delNode.value());
  sendE2EAck(senderApp, true, "Deleting node successful");
}

void
TopologyApp::processEditNode(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto editNode = maybeReadThrift<thrift::EditNode>(message);
  if (!editNode) {
    handleInvalidMessage("EditNode", senderApp, minion);
    return;
  }

  LOG(INFO) << "Processing EditNode for " << editNode->nodeName;

  auto node = topologyW_->getNode(editNode->nodeName);
  string newNodeName = editNode->newNode.name.empty() ? editNode->nodeName
                                                      : editNode->newNode.name;

  try {
    topologyW_->editNode(editNode->nodeName, editNode->newNode);
    if (prefixAllocator_ && node) {
      prefixAllocator_->editNode(*node, editNode->newNode);
    }
    std::string err;
    if (!SharedObjects::getConfigHelper()->wlock()->setNodeTopologyInfo(
            {editNode->nodeName}, *topologyW_, err)) {
      LOG(ERROR) << "Error generating topology info for " << editNode->nodeName
                 << ": " << err;
    }
  } catch (exception const& e) {
    sendE2EAck(
        senderApp,
        false,
        "Editing node failed because: " + folly::exceptionStr(e).toStdString());
    return;
  }

  // If node name changed, tell ConfigApp to migrate the config
  auto newNode = topologyW_->getNode(newNodeName);
  if (node && newNode && node->name != newNode->name) {
    sendToCtrlApp(
        E2EConsts::kConfigAppCtrlId,
        thrift::MessageType::EDIT_NODE,
        editNode.value());
  }

  topologyChanged(
      thrift::MessageType::EDIT_NODE,
      folly::sformat("{}:{}", minion, senderApp),
      folly::sformat("{} -> {}", editNode->nodeName, editNode->newNode.name),
      thrift::EventId::TOPOLOGY_NODE_MODIFIED);
  sendToApiStream(thrift::MessageType::EVENT_EDIT_NODE, editNode.value());
  sendE2EAck(senderApp, true, "Editing node successful");
}

void
TopologyApp::processGetLink(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto getLink = maybeReadThrift<thrift::GetLink>(message);
  if (!getLink) {
    handleInvalidMessage("GetLink", senderApp, minion);
    return;
  }

  auto maybeLink = topologyW_->getLink(getLink->name);
  if (maybeLink) {
    sendToCtrlApp(senderApp, thrift::MessageType::LINK, maybeLink.value());
  } else {
    sendE2EAck(senderApp, false, "Invalid link name");
  }
}

void
TopologyApp::processAddLink(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto addLink = maybeReadThrift<thrift::AddLink>(message);
  if (!addLink) {
    handleInvalidMessage("AddLink", senderApp, minion);
    return;
  }

  LOG(INFO) << "Processing AddLink between a_node_name "
            << addLink->link.a_node_name << " (" << addLink->link.a_node_mac
            << ") and z_node_name " << addLink->link.z_node_name << " ("
            << addLink->link.z_node_mac << ")";

  try {
    auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
    topologyW_->addLink(addLink->link);
    if (prefixAllocator_) {
      prefixAllocator_->addLink(addLink->link, *lockedConfigHelper);
    }

    // Update config
    lockedConfigHelper->onAddLink(*topologyW_, addLink->link);
    std::string err;
    if (!lockedConfigHelper->setNodeTopologyInfo(
            {addLink->link.a_node_name, addLink->link.z_node_name},
            *topologyW_,
            err)) {
      LOG(ERROR) << "Error updating topology info for link between '"
                 << addLink->link.a_node_name << "' and '"
                 << addLink->link.z_node_name << "': " << err;
    }
  } catch (exception const& e) {
    sendE2EAck(
        senderApp,
        false,
        "Adding link failed because: " + folly::exceptionStr(e).toStdString());
    return;
  }

  // Update airtime allocations upon link changes
  shouldUpdateAirtimeAlloc_ = true;

  topologyChanged(
      thrift::MessageType::ADD_LINK,
      folly::sformat("{}:{}", minion, senderApp),
      addLink->link.name,
      thrift::EventId::TOPOLOGY_LINK_ADDED);
  sendToApiStream(thrift::MessageType::EVENT_ADD_LINK, addLink.value());
  sendE2EAck(senderApp, true, "Adding link successful");
}

void
TopologyApp::processDelLink(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto delLink = maybeReadThrift<thrift::DelLink>(message);
  if (!delLink) {
    handleInvalidMessage("DelLink", senderApp, minion);
    return;
  }

  LOG(INFO) << "Processing DelLink between a_node_name " << delLink->aNodeName
            << " and z_node_name " << delLink->zNodeName;

  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  try {
    // First remove this link from the config
    auto link = topologyW_->getLink(delLink->aNodeName, delLink->zNodeName);
    if (link) {
      std::string err;
      if (!lockedConfigHelper->deleteAutoLinkOverrides(link.value(), err)) {
        LOG(ERROR) << folly::format(
            "Error occured while deleting auto link overrides: {}", err);
      }
    } else {
      LOG(ERROR) << folly::format(
          "Could not find link between a_node_name {} and "
          "z_node_name {}",
          delLink->aNodeName,
          delLink->zNodeName);
    }

    // Then delete the link from topology
    topologyW_->delLink(delLink->aNodeName, delLink->zNodeName, delLink->force);

    // Lastly, update topologyInfo on a/z nodes
    std::string err;
    if (!lockedConfigHelper->setNodeTopologyInfo(
            {delLink->aNodeName, delLink->zNodeName}, *topologyW_, err)) {
      LOG(ERROR) << "Error updating topology info for link between '"
                 << delLink->aNodeName << "' and '" << delLink->zNodeName
                 << "': " << err;
    }
  } catch (exception const& e) {
    sendE2EAck(
        senderApp,
        false,
        "Deleting link failed because: " +
            folly::exceptionStr(e).toStdString());
    return;
  }
  lockedConfigHelper.unlock();  // lockedConfigHelper -> NULL

  // Update airtime allocations upon link changes
  shouldUpdateAirtimeAlloc_ = true;

  topologyChanged(
      thrift::MessageType::DEL_LINK,
      folly::sformat("{}:{}", minion, senderApp),
      folly::sformat("link-{}-{}", delLink->aNodeName, delLink->zNodeName),
      thrift::EventId::TOPOLOGY_LINK_REMOVED);
  sendToApiStream(thrift::MessageType::EVENT_DEL_LINK, delLink.value());
  sendE2EAck(senderApp, true, "Deleting link successful");
}

void
TopologyApp::processGetSite(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto getSite = maybeReadThrift<thrift::GetSite>(message);
  if (!getSite) {
    handleInvalidMessage("GetSite", senderApp, minion);
    return;
  }

  auto maybeSite = topologyW_->getSite(getSite->name);
  if (maybeSite) {
    sendToCtrlApp(senderApp, thrift::MessageType::SITE, maybeSite.value());
  } else {
    sendE2EAck(senderApp, false, "Invalid site name");
  }
}

void
TopologyApp::processAddSite(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto addSite = maybeReadThrift<thrift::AddSite>(message);
  if (!addSite) {
    handleInvalidMessage("AddSite", senderApp, minion);
    return;
  }

  LOG(INFO) << "Processing AddSite for " << addSite->site.name;

  try {
    topologyW_->addSite(addSite->site);
  } catch (exception const& e) {
    sendE2EAck(
        senderApp,
        false,
        "Adding site failed. " + folly::exceptionStr(e).toStdString());
    return;
  }
  topologyChanged(
      thrift::MessageType::ADD_SITE,
      folly::sformat("{}:{}", minion, senderApp),
      addSite->site.name,
      thrift::EventId::TOPOLOGY_SITE_ADDED);
  sendToApiStream(thrift::MessageType::EVENT_ADD_SITE, addSite.value());
  sendE2EAck(senderApp, true, "Adding site successful");
}

void
TopologyApp::processDelSite(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto delSite = maybeReadThrift<thrift::DelSite>(message);
  if (!delSite) {
    handleInvalidMessage("DelSite", senderApp, minion);
    return;
  }

  LOG(INFO) << "Processing DelSite for " << delSite->siteName;

  try {
    topologyW_->delSite(delSite->siteName);
  } catch (exception const& e) {
    sendE2EAck(
        senderApp,
        false,
        "Deleting site failed. " + folly::exceptionStr(e).toStdString());
    return;
  }
  topologyChanged(
      thrift::MessageType::DEL_SITE,
      folly::sformat("{}:{}", minion, senderApp),
      delSite->siteName,
      thrift::EventId::TOPOLOGY_SITE_REMOVED);
  sendToApiStream(thrift::MessageType::EVENT_DEL_SITE, delSite.value());
  sendE2EAck(senderApp, true, "Deleting site successful");
}

void
TopologyApp::processEditSite(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto editSite = maybeReadThrift<thrift::EditSite>(message);
  if (!editSite) {
    handleInvalidMessage("EditSite", senderApp, minion);
    return;
  }

  LOG(INFO) << "Processing EditSite for " << editSite->siteName;

  auto site = topologyW_->getSite(editSite->siteName);
  string newSiteName = editSite->newSite.name.empty() ? editSite->siteName
                                                      : editSite->newSite.name;

  try {
    topologyW_->editSite(editSite->siteName, editSite->newSite);
    if (prefixAllocator_) {
      prefixAllocator_->editSite(editSite->siteName, editSite->newSite);
    }
    std::string err;
    std::vector<std::string> nodeNames =
        folly::gen::from(topologyW_->getNodesBySiteName(newSiteName)) |
        folly::gen::field(&thrift::Node::name) |
        folly::gen::as<std::vector<std::string>>();
    if (!SharedObjects::getConfigHelper()->wlock()->setNodeTopologyInfo(
            nodeNames, *topologyW_, err)) {
      LOG(ERROR) << "Error generating topology info for site " << newSiteName
                 << ": " << err;
    }
  } catch (exception const& e) {
    sendE2EAck(
        senderApp,
        false,
        "Editing site failed. " + folly::exceptionStr(e).toStdString());
    return;
  }

  // If location changed, propagate configs to the GPS chip on all site nodes
  auto newSite = topologyW_->getSite(newSiteName);
  if (site && newSite && site->location != newSite->location) {
    auto lockedConfigHelper = SharedObjects::getConfigHelper()->rlock();
    for (const auto& node : topologyW_->getNodesBySiteName(newSiteName)) {
      sendGpsConfigurations(
          node, lockedConfigHelper->isForceGpsDisable(node));
    }
  }

  topologyChanged(
      thrift::MessageType::EDIT_SITE,
      folly::sformat("{}:{}", minion, senderApp),
      folly::sformat("{} -> {}", editSite->siteName, newSiteName),
      thrift::EventId::TOPOLOGY_SITE_MODIFIED);
  sendToApiStream(thrift::MessageType::EVENT_EDIT_SITE, editSite.value());
  sendE2EAck(senderApp, true, "Editing site successful");
}

void
TopologyApp::processResetTopologyState(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  VLOG(4) << "Received ResetTopologyState from " << minion << ":" << senderApp;

  auto resetTopologyState =
      maybeReadThrift<thrift::ResetTopologyState>(message);
  if (!resetTopologyState) {
    handleInvalidMessage("ResetTopologyState", senderApp, minion, false);
    return;
  }

  // reset link up attempts
  if (resetTopologyState->resetLinkupAttempts) {
    for (const auto& link : topologyW_->getAllLinks()) {
      topologyW_->resetLinkupAttempts(link.name);
    }
  }
}

void
TopologyApp::processGpsGetPosResp(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto location = maybeReadThrift<thrift::Location>(message);
  if (!location) {
    handleInvalidMessage("Location", senderApp, minion, false);
    return;
  }

  topologyW_->setLocation(minion, location.value());
}

void
TopologyApp::sendGpsConfigurations(
    const thrift::Node& node, bool forceGpsDisable) {
  if (node.mac_addr.empty()) {
    return;
  }

  // no need to send gps configuration for cns
  if (node.node_type == thrift::NodeType::CN) {
    return;
  }

  // send GpsEnable if site location information is accurate
  thrift::Location location = topologyW_->getSite(node.site_name)->location;
  if (forceGpsDisable) {
    // We want to ignore accuracy-related GPS checks.
    // Send "enableGps" and force accuracy to 0 meters.
    location.accuracy = 0;
  }
  if (location.accuracy <= E2EConsts::kGpsAccuracyThresh) {
    thrift::NodeParams nodeParams;
    // fill out enableGPS
    nodeParams.enableGps_ref() = true;
    // fill out location
    nodeParams.location_ref() = location;
    nodeParams.type = thrift::NodeParamsType::GPS;
    sendToMinionApp(
        node.mac_addr,
        E2EConsts::kStatusAppMinionId,
        thrift::MessageType::SET_NODE_PARAMS,
        nodeParams);
  } else {
    LOG(INFO) << "Bad location accuracy of " << location.accuracy << " for "
              << node.mac_addr << " attempting to get it from node";
    sendToMinionApp(
        node.mac_addr,
        E2EConsts::kStatusAppMinionId,
        thrift::MessageType::GPS_GET_POS_REQ,
        thrift::Empty());
  }
}

void
TopologyApp::topologyChanged(
    const thrift::MessageType& messageType,
    const std::string& source,
    const std::string& itemName,
    const thrift::EventId& eventId) {
  thrift::Topology topology = topologyW_->getTopology();

  // Update globally-shared topology wrapper
  SharedObjects::getTopologyWrapper()->wlock()->setTopology(topology);

  // Update BinaryStar data with current topology
  SharedObjects::getSyncedAppData()->wlock()->setTopology(topology);

  // Auto node overrides may have changed, so update BinaryStar data
  bstarUpdateAutoNodeOverrides();

  if (prefixAllocator_) {
    // Send prefixes to pop node in case they changed
    sendSetPrefixes();
    // Reset timeout
    centralizedPrefixUpdateTimeout_->scheduleTimeout(
        centralizedPrefixUpdateInterval_, true /* isPeriodic */);
  }

  std::string messageTypeStr =
      TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap()
      .at(messageType);
  VLOG(2) << folly::format(
      "topologyChanged: type: {}, item: {}, source: {}",
      messageTypeStr,
      itemName,
      source);

  eventClient_->logEventDynamic(
      thrift::EventCategory::TOPOLOGY,
      eventId,
      thrift::EventLevel::INFO,
      folly::sformat("{} - {}", messageTypeStr, itemName),
      folly::dynamic::object("type", messageTypeStr)("item", itemName));
}

void
TopologyApp::linkStatusChanged(
    const std::string& source,
    const thrift::Link& link,
    const thrift::LinkStatusType& linkStatusType,
    const std::string& reason) {
  auto linkStatusTypeStr = folly::get_default(
      TEnumMapFactory<thrift::LinkStatusType>::makeValuesToNamesMap(),
      linkStatusType,
      folly::sformat("UNKNOWN ({})", static_cast<int>(linkStatusType)).c_str());
  bool alive = (linkStatusType == thrift::LinkStatusType::LINK_UP);
  auto aNode = topologyW_->getNode(link.a_node_name);
  auto zNode = topologyW_->getNode(link.z_node_name);
  bool isCnLink = (aNode && aNode->node_type == thrift::NodeType::CN) ||
                  (zNode && zNode->node_type == thrift::NodeType::CN);

  VLOG(3) << folly::sformat(
      "{} linkStatusChanged: source: {}, status: {}, reason: {}",
      link.name,
      source,
      linkStatusTypeStr,
      reason);

  auto linkTypeMap = TEnumMapFactory<thrift::LinkType>::makeValuesToNamesMap();
  setCounter(
      folly::sformat(
          "e2e_controller.link_status.{}.{}.{}",
          folly::get_default(
              linkTypeMap,
              link.link_type, "UNKNOWN"),
          !link.a_node_mac.empty() ? link.a_node_mac
                                   : (aNode ? aNode->mac_addr : aNode->name),
          !link.z_node_mac.empty() ? link.z_node_mac
                                   : (zNode ? zNode->mac_addr : zNode->name)),
      alive ? 1 : 0,
      fbzmq::thrift::CounterValueType::GAUGE);

  eventClient_->logEventDynamic(
      thrift::EventCategory::STATUS,
      thrift::EventId::LINK_STATUS,
      alive ? thrift::EventLevel::INFO : thrift::EventLevel::ERROR,
      folly::sformat("{} is {}", link.name, alive ? "UP" : "DOWN"),
      folly::dynamic::object("name", link.name)("is_alive", alive)(
          "source", source)("is_cn_link", isCnLink)(
          "link_type", linkTypeMap.at(link.link_type)),
      std::make_optional(link.name));

  sendToApiStream(thrift::MessageType::EVENT_LINK_STATUS, link);
}

void
TopologyApp::nodeStatusChanged(
    const std::string& messageType,
    const thrift::Node& node,
    const std::string& reason,
    const thrift::NodeStatusType status) {
  auto statusStr = folly::get_default(
      TEnumMapFactory<thrift::NodeStatusType>::makeValuesToNamesMap(), status,
      "UNKNOWN");

  VLOG(3) << folly::sformat(
      "{} nodeStatusChanged: type: {}, status: {}, reason: {}",
      node.name,
      messageType,
      statusStr,
      reason);

  eventClient_->logEventDynamic(
      thrift::EventCategory::STATUS,
      thrift::EventId::NODE_STATUS,
      status == thrift::NodeStatusType::OFFLINE ? thrift::EventLevel::ERROR
                                                : thrift::EventLevel::INFO,
      folly::sformat("{} is {}", node.name, statusStr),
      folly::dynamic::object("name", node.name)("status", statusStr)(
          "source", messageType)(
          "node_type", TEnumMapFactory<thrift::NodeType>::
              makeValuesToNamesMap().at(node.node_type)),
      std::make_optional(node.mac_addr),
      std::make_optional(node.mac_addr),
      std::make_optional(node.name));

  sendToApiStream(thrift::MessageType::EVENT_NODE_STATUS, node);
}

void
TopologyApp::addLinksInUnknownStatus(const thrift::Node& node) {
  auto links = topologyW_->getLinksByNodeName(node.name);
  for (const auto& link : links) {
    // skip offline link and ethernet link
    if (!link.is_alive || link.link_type == thrift::LinkType::ETHERNET) {
      continue;
    }
    auto neighbor = topologyW_->getNbrNode(node.name, link);
    // link will be brought down anyway
    if (neighbor->status == thrift::NodeStatusType::OFFLINE) {
      continue;
    }
    // only add alive wireless link with alive neighbor to linksInUnknownStatus_
    linksInUnknownStatus_.insert(link.name);
  }
}

void
TopologyApp::sanitizeLinkStatus() {
  for (auto iter = linksInUnknownStatus_.begin();
       iter != linksInUnknownStatus_.end();) {
    const auto link = topologyW_->getLink(*iter);
    const auto aNode = topologyW_->getNode(link->a_node_name);
    const auto zNode = topologyW_->getNode(link->z_node_name);

    // node has been sanitized
    if (!link->is_alive ||
        (aNode->status == thrift::NodeStatusType::OFFLINE &&
         zNode->status == thrift::NodeStatusType::OFFLINE)) {
      iter = linksInUnknownStatus_.erase(iter);
      continue;
    }
    if (!aNode->mac_addr.empty() &&
        aNode->status != thrift::NodeStatusType::OFFLINE) {
      thrift::GetLinkStatus linkStatus;
      linkStatus.responderMac = link->z_node_mac;
      sendToMinionApp(
          aNode->mac_addr,
          E2EConsts::kIgnitionAppMinionId,
          thrift::MessageType::GET_LINK_STATUS,
          linkStatus);
    }
    if (!zNode->mac_addr.empty() &&
        zNode->status != thrift::NodeStatusType::OFFLINE) {
      thrift::GetLinkStatus linkStatus;
      linkStatus.responderMac = link->a_node_mac;
      sendToMinionApp(
          zNode->mac_addr,
          E2EConsts::kIgnitionAppMinionId,
          thrift::MessageType::GET_LINK_STATUS,
          linkStatus);
    }
    ++iter;
  }
}

void
TopologyApp::processBstarGetAppData(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  if (!maybeReadThrift<thrift::BinaryStarGetAppData>(message)) {
    handleInvalidMessage("BinaryStarGetAppData", senderApp, minion);
    return;
  }

  // Update BinaryStar data with current topology
  SharedObjects::getSyncedAppData()->wlock()->setTopology(
      topologyW_->getTopology());
}

void
TopologyApp::processBstarAppData(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  auto data = maybeReadThrift<thrift::BinaryStarAppData>(message);
  if (!data) {
    handleInvalidMessage("BinaryStarAppData", senderApp, minion);
    return;
  }

  // Received new topology from active controller, so replace ours
  // NOTE: Assume this has been validated already, so don't validate here
  LOG(INFO) << "Received new topology from " << senderApp
            << ", overwriting existing topology...";
  if (data->topology_ref().has_value()) {  // should always be true
    topologyW_->setTopology(data->topology_ref().value());
    topologyW_->writeToTsFile();

    // Update globally-shared topology wrapper
    SharedObjects::getTopologyWrapper()->wlock()->setTopology(
        topologyW_->getTopology());
  }
}

void
TopologyApp::sendSetPrefixes() {
  if (reachablePop_.empty()) {
    return;  // no alive/valid node to send to
  }

  std::map<folly::CIDRNetwork, std::string> prefixes;
  if (prefixAllocator_) {
    prefixes = prefixAllocator_->getAllocatedPrefixes();
  }
  // Don't empty out the static prefixes (makes network unreachable)
  if (prefixes.empty()) {
    LOG(WARNING) << "No allocated prefixes, skipping...";
    return;
  }

  // Format prefixes for kvstore
  openr::thrift::StaticAllocation staticAlloc;
  for (const auto& entry : prefixes) {
    auto prefix = openr::toIpPrefix(entry.first);
    auto node = topologyW_->getNode(entry.second);
    // Skip if node does not exist in topology
    if (!node) {
      LOG(WARNING) << "Node `" << entry.second << "` not in topology";
      continue;
    }
    if (node->mac_addr.empty()) {
      LOG(WARNING) << "Node `" << entry.second << "` has no MAC assigned";
      continue;
    }
    auto openrName = OpenrUtils::toOpenrNodeName(node->mac_addr);
    staticAlloc.nodePrefixes_ref().value()[openrName] = prefix;
  }

  // Send to minion(s)
  std::vector<std::string> popMacAddrs;
  if (FLAGS_enable_pop_prefix_broadcast) {
    for (const auto& popNode : topologyW_->getPopNodes()) {
      if (!popNode.mac_addr.empty() &&
          (popNode.status == thrift::NodeStatusType::ONLINE ||
           popNode.status == thrift::NodeStatusType::ONLINE_INITIATOR)) {
        popMacAddrs.push_back(popNode.mac_addr);
      }
    }
  } else {
    popMacAddrs.push_back(reachablePop_);
  }
  for (const std::string& popMacAddr : popMacAddrs) {
    VLOG(1) << "Sending prefix allocations to POP node: " << popMacAddr;
    sendToMinionApp(
        popMacAddr,
        E2EConsts::kOpenrClientAppMinionId,
        thrift::MessageType::SET_PREFIXES,
        staticAlloc,
        true /* compress */);
  }
}

void
TopologyApp::processGetZonePrefixes(
    const string& minion,
    const string& senderApp,
    const thrift::Message& /* message */) {
  VLOG(4) << "Received GET_ZONE_PREFIXES message from " << minion << ":"
          << senderApp;
  thrift::GetZonePrefixesResp zonePrefixesResp;
  if (prefixAllocator_) {
    zonePrefixesResp.zonePrefixes = prefixAllocator_->getZonePrefixes();
    sendToCtrlApp(
        senderApp,
        thrift::MessageType::GET_ZONE_PREFIXES_RESP,
        zonePrefixesResp);
  } else {
    sendE2EAck(
        senderApp, false, "Centralized prefix allocation is not enabled");
  }
}

void
TopologyApp::processGetNodePrefixes(
    const string& minion,
    const string& senderApp,
    const thrift::Message& /* message */) {
  VLOG(4) << "Received GET_NODE_PREFIXES message from " << minion << ":"
          << senderApp;
  thrift::GetNodePrefixesResp getNodePrefixesResp;
  getNodePrefixesResp.nodePrefixes = topologyW_->getNodePrefixes();
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_NODE_PREFIXES_RESP,
      getNodePrefixesResp);
}

void
TopologyApp::processAllocatePrefixes(
    const string& minion,
    const string& senderApp,
    const thrift::Message& /*message*/) {
  VLOG(4) << "Received ALLOCATE_PREFIXES message from " << minion << ":"
          << senderApp;

  if (prefixAllocator_) {
    auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
    prefixAllocator_->allocate(*lockedConfigHelper);
    lockedConfigHelper.unlock();  // lockedConfigHelper -> NULL

    sendE2EAck(senderApp, true, "Successfully reallocated prefixes!");
    bstarUpdateAutoNodeOverrides();

    // Update globally-shared topology wrapper
    SharedObjects::getTopologyWrapper()->wlock()->setTopology(
        topologyW_->getTopology());
  } else {
    sendE2EAck(
        senderApp, false, "Controller-based prefix allocation is not enabled");
  }
}

void
TopologyApp::processPrefixAllocParamsUpdated(
    const string& minion,
    const string& senderApp,
    const thrift::Message& /*message*/) {
  VLOG(4) << "Received NETWORK_PREFIX_UPDATED message from " << minion << ":"
          << senderApp;

  if (!prefixAllocator_) {
    auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
    if (!initPrefixAllocator(*lockedConfigHelper)) {
      LOG(WARNING) << "Network prefix updated, but no centralized prefix "
                   << "allocation is configured. This change has no effect.";
    } else {
      LOG(INFO) << "Prefix allocation enabled";
    }
    lockedConfigHelper.unlock();  // lockedConfigHelper -> NULL
    SharedObjects::getTopologyWrapper()->wlock()
        ->setControllerPrefixAllocScheme(
            topologyW_->getControllerPrefixAllocScheme());
    return;
  }

  thrift::PrefixAllocParams prefixAllocParams =
      SharedObjects::getE2EConfigWrapper()
          ->rlock()
          ->getConfig()
          ->prefixAllocParams;

  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  try {
    prefixAllocator_->updatePrefixAllocParams(
        prefixAllocParams, *lockedConfigHelper);
  } catch (const std::invalid_argument& ex) {
    LOG(ERROR) << folly::format("Error changing network prefix: {}", ex.what());
    return;
  }
}

void
TopologyApp::bstarUpdateAutoNodeOverrides() const {
  if (!haAutoNodeOverridesSyncTimeout_->isScheduled()) {
    haAutoNodeOverridesSyncTimeout_->scheduleTimeout(
        std::chrono::milliseconds::zero());
  }
}

void
TopologyApp::processSetWiredLinkStatus(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto wiredLinkStatus = maybeReadThrift<thrift::SetWiredLinkStatus>(message);
  if (!wiredLinkStatus) {
    handleInvalidMessage("SetWiredLinkStatus", senderApp, minion, false);
    return;
  }

  // Existence check
  auto aNodeName =
      topologyW_->getNodeNameByMac(wiredLinkStatus->nodeMac);
  if (!aNodeName) {
    LOG(ERROR) << "Could not find node with MAC address: "
               << wiredLinkStatus->nodeMac;
    return;
  }

  bool topologyChanged = false;
  for (const auto& connectionStatus : wiredLinkStatus->linkStatus) {
    auto zNodeName =
        topologyW_->getNodeNameByMac(connectionStatus.first);
    if (!zNodeName) {
      LOG(ERROR) << "Could not find node with MAC address: "
                 << connectionStatus.first;
      continue;
    }

    auto linkName =
        topologyW_->getLinkName(*aNodeName, *zNodeName);
    if (!linkName) {
      LOG(ERROR) << "Unrecognized link between [" << *aNodeName << ", "
                 << *zNodeName << "]";
      continue;
    }

    auto link = topologyW_->getLink(*linkName);
    if (link->link_type != thrift::LinkType::ETHERNET) {
      LOG(ERROR) << *linkName << " is not a wired link";
      continue;
    }

    // Check that both nodes belong to the same site
    auto aNode = topologyW_->getNode(*aNodeName);
    auto zNode = topologyW_->getNode(*zNodeName);
    if (!aNode || !zNode) {
      continue;
    }
    if (aNode->site_name != zNode->site_name) {
      continue;
    }

    bool linkStatusChanged = link->is_alive != connectionStatus.second;
    topologyChanged = topologyChanged || linkStatusChanged;
    if (linkStatusChanged) {
      topologyW_->setLinkStatus(link->name, connectionStatus.second);
      wiredLinkStatusChanged(*link, connectionStatus.second);
    }
  }
  if (topologyChanged) {
    // Update globally-shared topology wrapper
    SharedObjects::getTopologyWrapper()->wlock()->setTopology(
        topologyW_->getTopology());
  }
}

void
TopologyApp::wiredLinkStatusChanged(
    const thrift::Link& link, const bool alive) const {
  VLOG(3) << "Wired link status for " << link.name << " changed from "
          << (link.is_alive ? "UP" : "DOWN") << " to "
          << (alive ? "UP" : "DOWN");

  auto aNode = topologyW_->getNode(link.a_node_name);
  auto zNode = topologyW_->getNode(link.z_node_name);

  auto linkTypeMap = TEnumMapFactory<thrift::LinkType>::makeValuesToNamesMap();
  setCounter(
      folly::sformat(
          "e2e_controller.link_status.{}.{}.{}",
          folly::get_default(
              linkTypeMap,
              link.link_type, "UNKNOWN"),
          !link.a_node_mac.empty() ? link.a_node_mac
                                   : (aNode ? aNode->mac_addr : aNode->name),
          !link.z_node_mac.empty() ? link.z_node_mac
                                   : (zNode ? zNode->mac_addr : zNode->name)),
      alive ? 1 : 0,
      fbzmq::thrift::CounterValueType::GAUGE);

  eventClient_->logEventDynamic(
      thrift::EventCategory::STATUS,
      thrift::EventId::WIRED_LINK_STATUS,
      alive ? thrift::EventLevel::INFO : thrift::EventLevel::ERROR,
      folly::sformat("{} is {}", link.name, alive ? "UP" : "DOWN"),
      folly::dynamic::object("name", link.name)("is_alive", alive)(
          "link_type", linkTypeMap.at(link.link_type)),
      std::make_optional(link.name));
}

}  // namespace terragraph
}  // namespace facebook
