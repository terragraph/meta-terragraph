/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqTimeout.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/io/async/EventBase.h>
#include <folly/Synchronized.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "e2e/common/Consts.h"
#include "e2e/if/gen-cpp2/Controller_types.h"

#include "StreamRequestHandler.h"

namespace facebook {
namespace terragraph {
namespace api {

class Streamer final : public fbzmq::ZmqEventLoop {
 public:
  Streamer(
      fbzmq::Context& zmqContext,
      const std::string& zmqId,
      const std::string& ctrlPubUrl,
      const std::chrono::seconds& ctrlSockTimeout,
      StreamRequestHandler::StreamClients& streamClients);

 private:
  void processMessage(
      const std::string& senderApp,
      const thrift::Message& message) noexcept;

  // Sends event and data to all connected clients
  void sendToClients(
      const thrift::MessageType& event,
      const std::string& data);

  // Tries to connect to the controller
  void connectToCtrl();

  // Dealer socket to connect to the router socket on the controller
  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> subSock_;

  // Timeout for control socket if we haven't received any messages
  std::chrono::seconds ctrlSockTimeout_{};

  // Timer for timing out controller socket (disconnect + reconnect)
  std::unique_ptr<fbzmq::ZmqTimeout> ctrlSockTimeoutTimer_;

  // All messages are serialized using this serializer
  apache::thrift::CompactSerializer serializer_{};

  // The zmq url that subSock_ is connected to
  std::string ctrlPubUrl_{};

  // Map stream requests used to send controller events to clients
  StreamRequestHandler::StreamClients& streamClients_;
};

} // namespace api
} // namespace terragraph
} // namespace facebook
