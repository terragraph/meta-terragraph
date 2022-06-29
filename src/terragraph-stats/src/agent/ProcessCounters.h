/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "BaseCounters.h"

#include <map>
#include <sigar.h>

namespace facebook {
namespace terragraph {
namespace stats {

class ProcessCounters : public BaseCounters {
 public:
  ProcessCounters();
  ~ProcessCounters();

  std::unordered_map<std::string, fbzmq::thrift::Counter> fetchStats() override;

 private:
  sigar_t* sigar_;
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
