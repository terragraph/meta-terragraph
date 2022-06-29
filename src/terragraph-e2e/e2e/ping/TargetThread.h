/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <sys/socket.h>

#include <memory>
#include <optional>
#include <string>

#include <folly/MPMCQueue.h>

#include "Probe.h"

namespace facebook {
namespace terragraph {

const int kSockFdInvalid = -1;

/**
 * Contents/metadata of a probe message from the pinging agent.
 */
struct Probe {
  union {
    char data[kProbeDataLen];
    ProbeBody probeBody;
  };
  struct sockaddr_storage clientAddr;
  socklen_t clientAddrLen;
};

/**
 * Creates and binds UDP/IPv6 server socket to 'host' and 'port'.
 *
 * @return socket descriptor of bound socket.
 *
 * @throw std::runtime_error if cannot bind any UDP/IPv6 socket to 'host' and
 * 'port'.
 */
int initUdpServer(
    const std::string& host, int port, std::optional<int32_t> timeout_m);

/**
 * A listening thread that receives UDP probes from pinging agents.
 * Such probes are enqueued for processing/response by the sender thread.
 */
class TargetReceiverThread {
 public:
  TargetReceiverThread(
      int sockFd,
      std::shared_ptr<folly::MPMCQueue<std::unique_ptr<Probe>>> probeQueue);

  /**
   * Listens on socket 'sockFd_' and enqueues incoming probes.
   *
   * @throw std::runtime_error if unable to setup socket.
   */
  void run();

 private:
  /**
   * Receives UDP probe on sockFd_ from some pinging agent. Blocks until a probe
   * is received or recvfrom() fails (e.g., if sender thread closes the socket).
   *
   * @param probe Where to store the result.
   *
   * @throw std::runtime_error if recvfrom() fails.
   */
  bool receiveProbe(Probe* probe);

  /**
   * Enqueues probe for echoing by sender thread. Blocks until the queue has
   * room for the probe to be written.
   *
   * @param probe Probe to enqueue
   */
  void enqueueProbe(std::unique_ptr<Probe>&& probe);

  int sockFd_ = kSockFdInvalid;
  std::shared_ptr<folly::MPMCQueue<std::unique_ptr<Probe>>> probeQueue_;
};

/**
 * A sending thread that dequeues and echoes probes back to pinging agents.
 */
class TargetSenderThread {
 public:
  TargetSenderThread(
      int sockFd,
      std::shared_ptr<folly::MPMCQueue<std::unique_ptr<Probe>>> probeQueue);

  /**
   * Dequeues received probes and echoes them back to the client.
   */
  void run(std::optional<int32_t> timeout_m);

 private:
  /**
   * Dequeues a probe from the queue. Blocks until a probe arrives on the queue
   * if no timeout is provided. Waits for timeout_m minutes, otherwise.
   *
   */
  std::optional<std::unique_ptr<Probe>> dequeueProbe(
      std::optional<int32_t> timeout_m);

  /**
   * Echoes a probe back to pinging agent. Blocks until able to write to
   * socket's send buffer.
   */
  void echoProbe(Probe* probe);

  int sockFd_ = kSockFdInvalid;
  std::shared_ptr<folly::MPMCQueue<std::unique_ptr<Probe>>> probeQueue_;
};

} // namespace terragraph
} // namespace facebook
