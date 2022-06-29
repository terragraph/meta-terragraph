/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "e2e/if/gen-cpp2/Controller_types.h"

#include "CtrlApp.h"

namespace facebook {
namespace terragraph {

/**
 * App that initiates iperf and ping sessions.
 *
 * This app primarily communicates with a separate TrafficApp on the E2E minion.
 */
class TrafficApp final : public CtrlApp {
 public:
  /**
   * Constructor.
   *
   * @param zmqContext the ZMQ context
   * @param routerSockUrl the broker address for the E2E controller
   * @param monitorSockUrl the ZmqMonitor address for the E2E controller
   */
  TrafficApp(
      fbzmq::Context& zmqContext,
      const std::string& routerSockUrl,
      const std::string& monitorSockUrl);

 private:
  // from CtrlApp
  void processMessage(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

  /** Process a request to start an iperf server. */
  void processStartIperfServer(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process a request to start an iperf client. */
  void processStartIperfClient(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process a request to stop an iperf session. */
  void processStopIperf(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process an iperf status request. */
  void processGetIperfStatus(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process iperf session output. */
  void processIperfOutput(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request to start a ping session. */
  void processStartPing(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process a request to stop a ping session. */
  void processStopPing(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process a ping status request. */
  void processGetPingStatus(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process ping session output. */
  void processPingOutput(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /**
   * Returns the IPv6 address for the given node, or std::nullopt if not found.
   */
  std::optional<std::string> getNodeIpv6(const std::string& nodeId);

  /**
   * Returns the adjacency struct for the given source -> dest node, or
   * std::nullopt if not found.
   */
  std::optional<openr::thrift::Adjacency> getAdjacency(
      const std::string& srcNodeId, const std::string& dstNodeId);

  /** Current iperf sessions. */
  std::unordered_map<std::string /* id */, thrift::StartMinionIperf>
      iperfSessions_;

  /** Current ping sessions. */
  std::unordered_map<std::string /* id */, thrift::StartMinionPing>
      pingSessions_;
};

} // namespace terragraph
} // namespace facebook
