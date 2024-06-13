/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unistd.h>

#include <fbzmq/async/ZmqTimeout.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/Synchronized.h>

#include "MinionApp.h"
#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {
namespace minion {

/**
 * App that initiates iperf and ping sessions.
 *
 * This app primarily communicates with a separate TrafficApp on the E2E
 * controller.
 */
class TrafficApp final : public MinionApp {
 public:
  /**
   * Constructor.
   *
   * @param zmqContext the ZMQ context
   * @param brokerRouterUrl the broker address for the E2E minion
   * @param monitorSockUrl the ZmqMonitor address for the E2E minion
   * @param macAddr our MAC address
   */
  TrafficApp(
      fbzmq::Context& zmqContext,
      const std::string& brokerRouterUrl,
      const std::string& monitorSockUrl,
      const std::string& macAddr);

  void processMessage(
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

 private:
  /** Process a request to start an iperf server. */
  void processStartIperfServer(
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process a request to start an iperf client. */
  void processStartIperfClient(
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process a request to stop an iperf session. */
  void processStopIperf(
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request to start a ping session. */
  void processStartPing(
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process a request to stop a ping session. */
  void processStopPing(
      const std::string& senderApp,
      const thrift::Message& message);

  /** Append the iperf CLI args for the options struct to the command vector. */
  void getIperfCliOptionsString(
      std::vector<std::string>& command,
      const thrift::IperfOptions& options,
      bool isServer);

  /** Append the ping CLI args for the options struct to the command vector. */
  void getPingCliOptionsString(
      std::vector<std::string>& command,
      const thrift::PingOptions& options);

  /**
   * Fork a child process, and exec the given command in the child process.
   *
   * The parent process will read the child's stdout, wait for the child to
   * exit, and then return the full output as a string.
   *
   * Returns std::nullopt if fork() failed.
   *
   * @param command the command vector to execute
   * @param pidCallback optional function called with the child pid immediately
   *                    after forking
   * @param initialDataCallback optional function called when the first byte of
   *                            output is read
   */
  std::optional<std::string> forkCommand(
      const std::vector<std::string>& command,
      std::optional<std::function<void(pid_t)>> pidCallback = std::nullopt,
      std::optional<std::function<void()>> initialDataCallback = std::nullopt);

  /** Running iperf processes. */
  folly::Synchronized<std::unordered_map<std::string /* id */, pid_t>> iperfProcesses_;

  /** Running ping processes. */
  folly::Synchronized<std::unordered_map<std::string /* id */, pid_t>> pingProcesses_;

  /** List of unused ports available for iperf. */
  folly::Synchronized<std::unordered_set<int32_t>> iperfAvailablePorts_;
};

} // namespace minion
} // namespace terragraph
} // namespace facebook
