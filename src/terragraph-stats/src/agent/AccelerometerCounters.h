/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "BaseCounters.h"

namespace facebook {
namespace terragraph {
namespace stats {

class AccelerometerCounters : public BaseCounters {

 public:
  AccelerometerCounters();

  std::unordered_map<std::string, fbzmq::thrift::Counter> fetchStats() override;

 private:
  // calculate angle which gravity makes with specific axis
  double getGravityAngle(uint axis);

  std::vector<std::string> axisNames_{"x", "y", "z"};
  // store last known 3d gravity value
  std::vector<int> lastGravity_;
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
