/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "StatsSubscriber.h"

#include <folly/dynamic.h>
#include <glog/logging.h>

#include "Consts.h"

namespace facebook {
namespace terragraph {

// subscribe to a stats stream, parse <link_name> to <metric name>
StatsSubscriber::StatsSubscriber(uWS::Loop* loop, uWS::TemplatedApp<false>* app)
    : BaseCounterMonitor(WebUIConsts::kStatsFormat), loop_(loop), app_(app) {}

void
StatsSubscriber::processStats(const std::vector<RadioStat>& radioStats) {
  for (const auto& stat : radioStats) {
    // format stat as dynamic object
    folly::dynamic linkStats =
        folly::dynamic::object("radioMac", stat.radioMac)(
            "responderMac", stat.responderMac)("key", stat.shortName)(
            "value", stat.value)("timestamp", stat.timestamp);
    std::string json = folly::toJson(linkStats);
    // publish JSON to websocket
    loop_->defer([this, json]() {
      app_->publish("broadcast", json, uWS::OpCode::TEXT, true);
    });
  }
}

} // namespace terragraph
} // namespace facebook
