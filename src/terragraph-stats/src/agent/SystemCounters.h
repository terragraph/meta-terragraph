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

extern "C"
{
  #include <sigar_format.h>
}

namespace facebook {
namespace terragraph {
namespace stats {

class SystemCounters : public BaseCounters {
 public:
  SystemCounters();
  ~SystemCounters();

  std::unordered_map<std::string, fbzmq::thrift::Counter> fetchStats() override;

 private:
  void populateDiskStats(
      std::unordered_map<std::string, fbzmq::thrift::Counter>& stats);
  void populateSysStats(
      std::unordered_map<std::string, fbzmq::thrift::Counter>& stats);

  // Helper function to add cpu stats from individual core to total cpu stats
  void updateCpuSum(sigar_cpu_t& cpu);

  // Helper function to return current cpu usage and reset sum for next
  // calculation
  sigar_cpu_t rollCurrentCpuSum();

  sigar_t* sigar_;
  sigar_cpu_list_t cpuList_{}; // List of cores for calculating cpu stats on
  sigar_cpu_t cpuSum_{}; // Rolling sum of cpu usage for all cores used to
                         // calculate overall cpu usage
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
