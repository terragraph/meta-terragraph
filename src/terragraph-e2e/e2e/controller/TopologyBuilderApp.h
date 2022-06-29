/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <ctime>
#include <fbzmq/async/ZmqTimeout.h>

#include "CtrlApp.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "topology/TopologyBuilder.h"

namespace facebook {
namespace terragraph {

/**
 * App that manages the topology building process via topology scans.
 */
class TopologyBuilderApp final : public CtrlApp {
 public:
  /**
   * Constructor.
   *
   * @param zmqContext the ZMQ context
   * @param routerSockUrl the broker address for the E2E controller
   * @param monitorSockUrl the ZmqMonitor address for the E2E controller
   */
  TopologyBuilderApp(
      fbzmq::Context& zmqContext,
      const std::string& routerSockUrl,
      const std::string& monitorSockUrl);

 private:
  // from CtrlApp
  void processMessage(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

  /** Process a request to start a topology scan on a single node. */
  void processStartTopologyScan(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request to find potential links for a node. */
  void processStartLinkDiscoveryScan(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a status query for the link discovery scan. */
  void processGetLinkDiscoveryScanStatus(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a StartScan response from ScanApp. */
  void processStartScanResp(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process topology scan results from ScanApp. */
  void processTopologyScanResult(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request to start a network-wide topology scan. */
  void processStartNetworkTopologyScan(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request to stop a network-wide topology scan. */
  void processStopNetworkTopologyScan(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a status query for the network-wide topology scan. */
  void processGetNetworkTopologyScanStatus(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request to start a continuous topology scan on a single node. */
  void processStartContinuousTopoScan(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process an E2EAck message. */
  void processE2EAck(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a BulkAddResult message. */
  void processBulkAddResult(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Forward a StartScan request from senderApp to ScanApp. */
  void sendStartTopologyScan(
      const std::string& senderApp,
      const std::string& txNode,  // radio MAC or node name (will be deprecated)
      std::optional<int16_t> txPwrIndex = std::nullopt);

  /**
   * Advance the network-wide topology scan.
   *
   * NOTE: This function will acquire three read locks:
   * - SharedObjects::getStatusReports()->rlock()
   * - SharedObjects::getConfigHelper()->rlock()
   * - SharedObjects::getTopologyWrapper()->rlock()
   */
  void runNetworkTopologyScanLoop();

  /**
   * Returns whether any scans are active and sends rejection reason back to
   * the sender.
   */
  bool rejectScanRequest(const std::string& senderApp);

  /** The topology builder instance. */
  TopologyBuilder builder_;

  /** The senderApp initiating the current topology scan, if any. */
  std::string topoScanSenderApp_;

  /**
   * The UNIX time (in seconds) when the pending topology scan was initiated.
   */
  time_t topoScanRequestTime_{0};

  /**
   * The UNIX time (in milliseconds) when the last topology scan response was
   * received.
   */
  int64_t lastTopoScanResponseTime_{0};

  /** Current scan tokens awaiting results. */
  std::unordered_map<int /* token */, std::string /* senderApp */>
      topoScanTokenMap_;

  /** Timeout to advance the network-wide topology scan. */
  std::unique_ptr<fbzmq::ZmqTimeout> networkTopologyScanTimeout_{nullptr};
};

} // namespace terragraph
} // namespace facebook
