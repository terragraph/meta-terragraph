/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "OpenrCounters.h"

namespace facebook {
namespace terragraph {
namespace stats {

std::unordered_map<std::string, fbzmq::thrift::Counter>
OpenrCounters::fetchStats() {
  return std::unordered_map<std::string, fbzmq::thrift::Counter>{};
}

std::unordered_map<std::string, fbzmq::thrift::Counter>
OpenrCounters::fetchStats(
    openr::thrift::OpenrCtrlAsyncClient* openrCtrlClient) {
  std::unordered_map<std::string, fbzmq::thrift::Counter> ret;

  if (!openrCtrlClient) {
    LOG(WARNING) << "Cannot fetch stats with invalid Open/R client";
    return ret;
  }

  std::map<std::string, int64_t> counters;
  try {
    openrCtrlClient->sync_getCounters(counters);
  } catch (const std::exception& e) {
    LOG(WARNING) << "Failed to get Open/R counters";
  }

  ret.reserve(counters.size());
  for (const auto& c : counters) {
    ret[c.first] = createGauge(c.second);
  }

  return ret;
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
