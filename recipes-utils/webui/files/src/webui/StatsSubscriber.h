/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

// for uWebSockets
#include <App.h>

#include "e2e/clients/BaseCounterMonitor.h"

namespace facebook {
namespace terragraph {

/**
 * Subscriber for driver-if stats via ZMQ socket.
 *
 * Processes incoming driver-if stats for publishing to websockets connection.
 */
class StatsSubscriber final : public BaseCounterMonitor {
 public:
  StatsSubscriber(uWS::Loop* loop, uWS::TemplatedApp<false>* app);
  ~StatsSubscriber(){};

 protected:
  /**
   * Process CounterValuesResponse from driver-if socket.
   *
   * Match stat names to a radio interface via StatCache.
   */
  void processStats(const std::vector<RadioStat>& radioStats);

 private:
  /** uWebSockets event loop - for publishing to websocket */
  uWS::Loop* loop_;
  /** uWebSockets Non-TLS app - for publishing to websocket */
  uWS::TemplatedApp<false>* app_;
};

} // namespace terragraph
} // namespace facebook
