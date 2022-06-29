/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "BaseCounters.h"

#include <map>
#include <sensors/sensors.h>

namespace facebook {
namespace terragraph {
namespace stats {

class SensorCounters : public BaseCounters {
 public:
  SensorCounters();
  ~SensorCounters();

  std::unordered_map<std::string, fbzmq::thrift::Counter> fetchStats() override;

 private:
  std::string parseChipName(const sensors_chip_name* chip);
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
