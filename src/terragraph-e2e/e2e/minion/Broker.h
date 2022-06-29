/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqTimeout.h>
#include <fbzmq/zmq/Zmq.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "e2e/common/Consts.h"
#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {
namespace minion {

/**
 * ZMQ message broker for the E2E minion.
 */
class Broker final : public fbzmq::ZmqEventLoop {
 public:
  /**
   * Constructor.
   *
   * This will bind or connect all sockets.
   *
   * @param zmqContext the ZMQ context
   * @param macAddr our MAC address
   * @param controllerRouterUrl the controller address, i.e. the ZMQ `DEALER`
   *                            socket URL to which controllerSock_ connects
   * @param appsSockBindUrl the minion app address, i.e. ZMQ `ROUTER` socket URL
   *                        on which appsSock_ binds
   * @param broadcastPubSockBindUrl the minion broadcast address, i.e. the ZMQ
   *                                `PUB` socket URL to which broadcastPubSock_
   *                                binds
   * @param ctrlSockTimeout the timeout on the controller socket
   *                        (controllerSock_) if no messages are received
   * @param myNetworkInfoFile the network information file
   */
  Broker(
      fbzmq::Context& zmqContext,
      const std::string& macAddr,
      const std::string& controllerRouterUrl,
      const std::string& appsSockBindUrl,
      const std::string& broadcastPubSockBindUrl,
      const std::chrono::seconds& ctrlSockTimeout,
      const std::string& myNetworkInfoFile);

 private:
  /** Function invoked when any message is available for the broker. */
  void processMessage(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /** Process a command to change between primary/backup controllers. */
  void processBstarSwitchController(
      const std::string& senderApp, const thrift::Message& message);

  /**
   * Connect controllerSock_ to given url:
   * 1. Disconnect from previous controller if necessary.
   * 2. Only update currentCtrlUrl_ if current connection succeeds.
   * 3. Try rollback if current connection fails.
   */
  void connectToCtrl(const std::string& ctrlUrl);

  /** Read the controller URL from the network information file. */
  void getCtrlUrl();

  /** Initialize controllerSock_ and set up the message callback function. */
  void initControllerSock();

  /**
   * Record a ZMQ send error to a given destination.
   *
   * Returns true if the error should be logged, or false to throttle.
   */
  bool recordZmqSendError(const std::string& dstZmqId);

  /** The ZMQ context. */
  fbzmq::Context& zmqContext_;

  /** Our MAC address. */
  const std::string macAddr_;

  /**
   * The ZMQ URL on which the ZMQ `ROUTER` port in the primary controller
   * listens.
   */
  std::string controllerPrimaryRouterUrl_{};

  /**
   * The ZMQ URL on which the ZMQ `ROUTER` port in the backup controller
   * listens.
   */
  std::string controllerBackupRouterUrl_{};

  /**
   * The ZMQ `DEALER` socket to connect to the ZMQ `ROUTER` socket on the
   * controller.
   */
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> controllerSock_;

  /**
   * The ZMQ `ROUTER` socket on which the minion broker talks to all minion
   * apps.
   */
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> appsSock_;

  /** The ZMQ `PUB` socket to broadcast asynchronous messages. */
  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> broadcastPubSock_;

  /**
   * The ZMQ `PUB` socket bind URL for broadcastPubSock_ (or empty string if
   * disabled).
   */
  std::string broadcastPubSockBindUrl_{};

  /** Timeout for the controller socket if we haven't received any messages. */
  std::chrono::seconds ctrlSockTimeout_{};

  /** Timer for timing out the controller socket (disconnect + reconnect). */
  std::unique_ptr<fbzmq::ZmqTimeout> ctrlSockTimeoutTimer_;

  /**
   * Timer to periodically read the controller URL from the network information
   * file.
   */
  std::unique_ptr<fbzmq::ZmqTimeout> getCtrlUrlTimer_;

  /** The ZMQ URL that controllerSock_ is currently connected to. */
  std::string currentCtrlUrl_{};

  /**
   * true if we are currently connected to the primary controller URL, or
   * false if currently using the backup URL.
   */
  bool usingPrimaryCtrlUrl_{true};

  /** The network information filename. */
  const std::string myNetworkInfoFile_{};

  /** Occurrences/timestamps of per-destination ZMQ send errors. */
  std::unordered_map<
      std::string /* dst zmq id */,
      std::pair<uint64_t /* total errors */, int64_t /* last log time */>>
          zmqSendErrors_{};

  /** All messages are serialized using this serializer. */
  apache::thrift::CompactSerializer serializer_{};
};

} // namespace minion
} // namespace terragraph
} // namespace facebook
