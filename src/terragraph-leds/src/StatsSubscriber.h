/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqTimeout.h>
#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>
#include <fbzmq/zmq/Zmq.h>

#include <thrift/lib/cpp2/protocol/Serializer.h>

namespace facebook {
namespace terragraph {

/**
 * Subscriber for driver-if stats via ZMQ socket.
 *
 * Processes incoming driver-if stats.
 */
class StatsSubscriber final : public fbzmq::ZmqEventLoop {
 public:
  StatsSubscriber(
      fbzmq::Context& context,
      fbzmq::ZmqEventLoop& loop,
      std::function<void(const fbzmq::thrift::CounterMap&)> callback);

 private:
  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> driverIfSock_;
  apache::thrift::CompactSerializer serializer_;
  std::function<void(const fbzmq::thrift::CounterMap&)> callback_;
};

} // namespace terragraph
} // namespace facebook
