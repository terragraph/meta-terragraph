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

namespace facebook {
namespace terragraph {
namespace stats {

class Broker final : public fbzmq::ZmqEventLoop {

 public:
  Broker(
      fbzmq::Context& context,
      const std::string& agentsSockBindUrl,
      const std::string& appsSockBindUrl);

 private:
  // Initializes ZMQ sockets
  void prepare() noexcept;

  // zmq url on which the agentsSock listens on
  const std::string agentsSockBindUrl_;

  // zmq url on which the appsSock listens on
  const std::string appsSockBindUrl_;

  // a router socket to talk to the e2e agents
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> agentsSock_;

  // a router socket to talk to the apps
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> appsSock_;

  // all messages are serialized using this serializer
  apache::thrift::CompactSerializer serializer_;
};
} // namesapce stats
} // namespace terragraph
} // namespace facebook
