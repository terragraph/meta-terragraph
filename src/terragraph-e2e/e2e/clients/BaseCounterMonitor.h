/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <vector>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqTimeout.h>
#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>
#include <fbzmq/zmq/Zmq.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "StatCache.h"
#include "stats/common/StatInfo.h"

namespace facebook {
namespace terragraph {

/**
 * Subscriber for driver-if stats via ZMQ socket.
 *
 * Processes incoming counters.
 */
class BaseCounterMonitor : public fbzmq::ZmqEventLoop {
 public:
  BaseCounterMonitor(const std::vector<StatFormat>& statFormat);

 protected:
  /** Process radio stats */
  virtual void processStats(const std::vector<RadioStat>& radioStats) = 0;

  /** Mapping of raw key name to meta-data about the link key */
  std::unordered_map<std::string /* raw key name */, RadioMetric> linkKeys_{};

 private:
  /** Initialize stats subscriber socket to driver-if. */
  void prepare() noexcept;

  /** Process CounterValuesResponse from ZMQ monitor socket. */
  void processCountersMessage(fbzmq::thrift::CounterValuesResponse& counters);

  /**
   * Refresh mapping of link key short names (snr, rssi, etc) based on active
   * links.
   */
  void refreshLinkKeys();

  /** ZMQ context for subscriber socket(s) */
  fbzmq::Context context_{};
  /** Key/stat format */
  std::vector<StatFormat> statFormat_;
  /** Timer for refreshing link key mappings based on LinkDump */
  std::unique_ptr<fbzmq::ZmqTimeout> linkKeysTimer_{nullptr};
  /** List of ZMQ sockets to subscribe to for counters */
  std::vector<fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT>> csSubSockList_;
  /** Thrift message serializer */
  apache::thrift::CompactSerializer serializer_{};
};

} // namespace terragraph
} // namespace facebook
