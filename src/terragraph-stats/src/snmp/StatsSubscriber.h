/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "StatCache.h"

#include <vector>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqTimeout.h>
#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/Synchronized.h>
#include <folly/synchronization/MicroSpinLock.h>

#include <folly/io/async/EventBase.h>
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
  StatsSubscriber(fbzmq::Context& context);
  ~StatsSubscriber(){};

 private:
  /**
   * Initialize stats subscriber socket to driver-if.
   */
  void prepare() noexcept;

  /*
   * Process CounterValuesResponse from driver-if socket.
   *
   * Match stat names to a radio interface via StatCache.
   */
  void processCountersMessage(fbzmq::thrift::CounterValuesResponse& counters);

  fbzmq::Context& context_;
  std::vector<fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT>> csSubSockList_;
  apache::thrift::CompactSerializer serializer_{};
  std::shared_ptr<folly::Synchronized<
      std::unordered_map<std::string /* raw metric name */, LinkMetric>>>
      keyNameCache_;
};

} // namespace terragraph
} // namespace facebook
