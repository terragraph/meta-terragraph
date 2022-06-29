/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqTimeout.h>
#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>

#include "CtrlApp.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "prefix-allocators/BasePrefixAllocator.h"
#include "topology/RoutesHelper.h"
#include "topology/TopologyWrapper.h"

namespace facebook {
namespace terragraph {

/**
 * App that manages the network topology.
 */
class TopologyApp final : public CtrlApp {
 public:
  /**
   * Constructor.
   *
   * This will perform or schedule many topology- or config-related actions.
   *
   * @param zmqContext the ZMQ context
   * @param routerSockUrl the broker address for the E2E controller
   * @param monitorSockUrl the ZmqMonitor address for the E2E controller
   * @param statusReportsInterval the interval at which to sync topology status
   *                              with minion status reports
   * @param topologyReportInterval the interval at which to push topology stats
   * @param routingAdjacenciesDumpInterval the interval at which to request
   *                                       routing adjacencies from a node
   * @param nodeAliveTimeOut the period at which to determine node liveness
   * @param airtimeAllocUpdateInterval the minimum interval at which to
   *                                   recompute airtime allocations
   * @param centralizedPrefixUpdateInterval the interval at which allocated
   *                                        prefixes are sent to a POP node
   * @param topologyFile the topology file
   * @param topologyDir the topology directory
   * @param autoAirtimeAllocEnabled whether automatic fair airtime allocation is
   *                                enabled
   * @param centalizedPrefixAllocEnabled whether centralized prefix allocation
   *                                     (CPA) is enabled
   * @param deterministicPrefixAllocEnabled whether deterministic prefix
   *                                        allocation (DPA) is enabled
   * @param createIntrasiteLinks whether to automatically create wired
   *                             intra-site links in the topology
   */
  TopologyApp(
      fbzmq::Context& zmqContext,
      const std::string& routerSockUrl,
      const std::string& monitorSockUrl,
      std::chrono::seconds statusReportsInterval,
      std::chrono::seconds topologyReportInterval,
      std::chrono::seconds routingAdjacenciesDumpInterval,
      std::chrono::seconds nodeAliveTimeOut,
      std::chrono::seconds airtimeAllocUpdateInterval,
      std::chrono::seconds centralizedPrefixUpdateInterval,
      const std::string& topologyFile,
      const std::string& topologyDir = "",
      bool autoAirtimeAllocEnabled = false,
      bool centalizedPrefixAllocEnabled = false,
      bool deterministicPrefixAllocEnabled = false,
      bool createIntrasiteLinks = false);

 private:
  // from CtrlApp
  void processMessage(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

  /** Process LinkStatus from a minion. */
  void processLinkStatus(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process LinkAuthorized from a minion. */
  void processLinkAuthorized(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a network topology request. */
  void processGetTopology(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a network airtime request. */
  void processGetNetworkAirtime(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process a routes request. */
  void processGetRoutes(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process a default routes request. */
  void processGetDefaultRoutes(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a routing adjacencies request. */
  void processGetRoutingAdjacencies(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::RoutingAdjacencies. */
  void processRoutingAdjacencies(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process thrift::SetNodeStatus. */
  void processSetNodeStatus(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process thrift::SetNodeParamsReq. */
  void processSetNodeParamsReq(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::SetNetworkParamsReq. */
  void processSetNetworkParamsReq(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process thrift::SetNodeMac. */
  void processSetNodeMac(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::SetNodeMacList. */
  void processSetNodeMacList(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process thrift::AddNodeWlanMacs. */
  void processAddNodeWlanMacs(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::DelNodeWlanMacs. */
  void processDelNodeWlanMacs(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::ChangeNodeWlanMac. */
  void processChangeNodeWlanMac(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process thrift::BumpLinkupAttempt. */
  void processBumpLinkUpAttempts(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process thrift::GetNode. */
  void processGetNode(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::AddNode. */
  void processAddNode(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::DelNode. */
  void processDelNode(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::EditNode. */
  void processEditNode(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process thrift::GetLink. */
  void processGetLink(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::AddLink. */
  void processAddLink(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::DelLink. */
  void processDelLink(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process thrift::GetSite. */
  void processGetSite(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::AddSite. */
  void processAddSite(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::DelSite. */
  void processDelSite(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::EditSite. */
  void processEditSite(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process thrift::ResetTopologyState. */
  void processResetTopologyState(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a GPS position. */
  void processGpsGetPosResp(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process thrift::SetTopologyName. */
  void processSetTopologyName(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process thrift::BulkAdd. */
  void processBulkAdd(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request for app data from BinaryStarApp. */
  void processBstarGetAppData(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process new app data from BinaryStarApp. */
  void processBstarAppData(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process thrift::GetZonePrefixes. */
  void processGetZonePrefixes(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::GetNodePrefixes. */
  void processGetNodePrefixes(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::AllocatePrefixes. */
  void processAllocatePrefixes(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::NetworkPrefixUpdated. */
  void processPrefixAllocParamsUpdated(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process thrift::SetWiredLinksStatus. */
  void processSetWiredLinkStatus(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Update topology status based on minion status reports. */
  void syncWithStatusReports();

  /** Push topology status stats. */
  void reportTopologyStats() const;

  /** Send GetRoutingAdjacencies to a minion (reachablePop_). */
  void sendGetRoutingAdjacencies();

  /** Update fair airtime allocations as needed. */
  void updateAirtimeAlloc();

  /**
   * Add "potentially out-of-sync" links to linksInUnknownStatus_ for a given
   * node.
   *
   * This refers to ignited wireless links with alive neighbors.
   *
   * Reason:
   * - It is possible controller lost the LINK_STATUS message from the
   *   "alive neighbor". We need to send GET_LINK_STATUS to sync link status.
   * - Alive links with dead neighbors will be marked "down" as a part of
   *   syncWithStatusReports().
   */
  void addLinksInUnknownStatus(const thrift::Node& node);

  /** Sanitize link status for linksInUnknownStatus_. */
  void sanitizeLinkStatus();

  /**
   * Send GPS configurations to the node based on its site location accuracy.
   *
   * If the location is accurate in the topology or 'forceGpsDisable' is set,
   * fill out the location and send a GpsEnable command.
   *
   * Otherwise, ask the node for its current GPS position.
   */
  void sendGpsConfigurations(const thrift::Node& node, bool forceGpsDisable);

  /** Perform actions associated with a topology change (logging, etc.). */
  void topologyChanged(
      const thrift::MessageType& messageType,
      const std::string& source,
      const std::string& itemName,
      const thrift::EventId& eventId);
  /** Perform actions associated with a link status change (logging, etc.). */
  void linkStatusChanged(
      const std::string& source,
      const thrift::Link& link,
      const thrift::LinkStatusType& linkStatusType,
      const std::string& reason);
  /** Perform actions associated with a node status change (logging, etc.). */
  void nodeStatusChanged(
      const std::string& messageType,
      const thrift::Node& node,
      const std::string& reason,
      const thrift::NodeStatusType status);
  /**
   * Perform actions associated with a wired link status change (logging,
   * etc.).
   */
  void wiredLinkStatusChanged(
      const thrift::Link& link,
      const bool alive) const;

  /**
   * Send BF_RESP_SCAN_DISABLE for the given sector if it has any wireless links
   * currently ignited, returning true if the command was actually sent.
   */
  bool sendBfRespScanDisable(
      const thrift::Node& node, const std::string& radioMac);

  /**
   * Initialize the prefix allocator using 'prefixAllocParams' from controller
   * config.
   *
   * Returns true if a prefix allocator is created, otherwise false.
   */
  bool initPrefixAllocator(ConfigHelper& configHelper);

  /** Send prefixes to designated POP node(s). */
  void sendSetPrefixes();

  /** Set the auto node config for 'OPENR_STATIC_PREFIX_ALLOC' for all nodes. */
  void setAllAutoNodeOpenrStaticPrefixAlloc(
      const bool openrStaticPrefixAllocEnabled, ConfigHelper& configHelper);

  /**
   * Clear the auto node config for 'bgpParams.specificNetworkPrefixes' for all
   * nodes.
   */
  void clearAutoSpecificNetworkPrefixes(ConfigHelper& configHelper);

  /** Queue an update to auto node overrides within BinaryStar data. */
  void bstarUpdateAutoNodeOverrides() const;

  /** Interval at which status reports are synced (from StatusApp). */
  std::chrono::seconds statusReportsInterval_;

  /** Interval at which link/node aliveness stats are pushed. */
  std::chrono::seconds topologyReportInterval_;

  /** Interval at which GetRoutingAdjacencies is sent to nodes. */
  std::chrono::seconds routingAdjacenciesDumpInterval_;

  /** The network topology (source of truth). */
  std::unique_ptr<TopologyWrapper> topologyW_{nullptr};

  /** The routes helper instance. */
  std::unique_ptr<RoutesHelper> routesHelper_{nullptr};

  /** Timeout interval to determine whether a node is alive or dead. */
  std::chrono::seconds nodeAliveTimeOut_{};

  /** Timer to sync topology status with received status reports. */
  std::unique_ptr<fbzmq::ZmqTimeout> statusReportsSyncTimeout_{nullptr};

  /** Timer to push stats about topology status. */
  std::unique_ptr<fbzmq::ZmqTimeout> topologyReportTimeout_{nullptr};

  /** Timer to periodically send GetRoutingAdjacencies to nodes. */
  std::unique_ptr<fbzmq::ZmqTimeout> getRoutingAdjacenciesTimeout_{nullptr};

  /**
   * The dynamic configuration that holds TX/RX airtime information on every
   * node in the network.
   */
  thrift::NetworkAirtime nwAirtimeAlloc_{};

  /** Whether automatic fair airtime allocation is enabled. */
  bool autoAirtimeAllocEnabled_{true};

  /** Timer to recompute airtime allocations and send new node config. */
  std::unique_ptr<fbzmq::ZmqTimeout> airtimeComputationTimeout_{nullptr};

  /**
   * Whether airtime allocations need to be recomputed (e.g. due to a topology
   * change).
   */
  bool shouldUpdateAirtimeAlloc_{false};

  /** Set of links that need to be queried to sync link state. */
  std::set<std::string> linksInUnknownStatus_{};

  /**
   * The node ID (MAC) of a reachable POP node to use to interact with minion
   * services (such as Open/R).
   */
  std::string reachablePop_{};

  /** Interval at which allocated prefixes are sent to a POP node. */
  std::chrono::seconds centralizedPrefixUpdateInterval_;

  /** Timer to update prefixes in Open/R's KvStore (if needed). */
  std::unique_ptr<fbzmq::ZmqTimeout> centralizedPrefixUpdateTimeout_{nullptr};

  /** Whether or not centralized prefix allocation (CPA) is enabled. */
  bool centralizedPrefixAllocEnabled_{false};

  /** Whether or not to perform deterministic prefix allocation (DPA). */
  bool deterministicPrefixAllocEnabled_{false};

  /** The prefix allocator instance. */
  std::unique_ptr<BasePrefixAllocator> prefixAllocator_{nullptr};

  /** Timer to send updated auto node overrides to BinaryStarApp. */
  std::unique_ptr<fbzmq::ZmqTimeout> haAutoNodeOverridesSyncTimeout_{nullptr};

};

} // namespace terragraph
} // namespace facebook
