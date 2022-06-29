/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/zmq/Zmq.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {

/**
 * ZMQ message broker for the E2E controller.
 */
class Broker final : public fbzmq::ZmqEventLoop {
 public:
  /**
   * Constructor.
   *
   * This will bind or connect all sockets.
   *
   * @param zmqContext the ZMQ context
   * @param minionsSockBindUrl the minion socket address, i.e. the ZMQ `ROUTER`
   *                           socket URL on which minionsSock_ binds
   * @param appsSockBindUrl the controller app address, i.e. ZMQ `ROUTER` socket
   *                        URL on which appsSock_ binds
   * @param pubSockBindUrl the event streaming address, i.e. the ZMQ `PUB`
   *                       socket URL to which eventPubSock_ binds
   * @param isAppsSockZapEnabled whether the ZMQ Authentication Protocol (ZAP)
   *                             handler is enabled for appsSock_
   * @param isMinionsSockZapEnabled whether the ZMQ Authentication Protocol
   *                               (ZAP) handler is enabled for minionsSock_
   * @param isBstarEnabled whether to enable the "Binary Star" high availability
   *                       (HA) feature
   * @param isBstarPrimary whether this controller is the "primary" in the high
   *                       availability (HA) configuration
   */
  Broker(
      fbzmq::Context& zmqContext,
      const std::string& minionsSockBindUrl,
      const std::string& appsSockBindUrl,
      const std::string& pubSockBindUrl,
      bool isAppsSockZapEnabled,
      bool isMinionsSockZapEnabled,
      bool isBstarEnabled,
      bool isBstarPrimary = true);

 private:
  // non-copyable
  /** \{ */
  Broker(Broker const&) = delete;
  Broker& operator=(Broker const&) = delete;
  /** \} */

  /** Function invoked when any message is available for the broker. */
  void processMessage(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message) noexcept;

  /** Process a FSM (finite-state machine) change from BinaryStarApp. */
  void processBstarFsm(
      const std::string& senderApp, const thrift::Message& message);

  /** The ZMQ `ROUTER` socket to talk to all E2E minion instances. */
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> minionsSock_;

  /** The ZMQ `ROUTER` socket to talk to all controller apps. */
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> appsSock_;

  /** The ZMQ `PUB` socket to stream events. */
  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> eventPubSock_;

  /** All messages are serialized using this serializer. */
  apache::thrift::CompactSerializer serializer_{};

  /**
   * Whether the ZMQ Authentication Protocol (ZAP) handler is enabled for
   * appsSock_.
   */
  bool isAppsSockZapEnabled_{false};
  /**
   * Whether the ZMQ Authentication Protocol (ZAP) handler is enabled for
   * minionsSock_.
   */
  bool isMinionsSockZapEnabled_{false};

  /** Whether "Binary Star" replication is enabled. */
  bool isBstarEnabled_{false};

  /** The current "Binary Star" FSM (finite-state machine). */
  thrift::BinaryStar bstarFsm_;
};

}
} // namespace facebook::terragraph
