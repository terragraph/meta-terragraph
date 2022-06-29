/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ProcessCounters.h"

#include <folly/Format.h>
#include <folly/String.h>

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::string;

namespace {
const string kMinionProcName{"e2e_minion"};
const string kOpenRProcName{"openr"};
const string kStatsAgentProcName{"stats_agent"};
const string kLogTailProcName{"logtail"};
const string kControllerProcName{"e2e_controller"};
const string kAggregatorProcName{"nms_aggregator"};
const string kFibNssProcName{"fib_nss"};
const string kSquireNssProcName{"squire_nss"};
const string kQuaggaProcName{"quagga"};
const string kQuaggaSyncProcName{"quagga_sync"};
const string kFluentBitProcName{"fluent-bit"};
const std::set<string> kKnowProcNames{kMinionProcName,
                                      kOpenRProcName,
                                      kStatsAgentProcName,
                                      kLogTailProcName,
                                      kControllerProcName,
                                      kAggregatorProcName,
                                      kFibNssProcName,
                                      kSquireNssProcName,
                                      kQuaggaProcName,
                                      kQuaggaSyncProcName,
                                      kFluentBitProcName};
}

namespace facebook {
namespace terragraph {
namespace stats {

ProcessCounters::ProcessCounters() {
  auto status = sigar_open(&sigar_);
  if (SIGAR_OK != status) {
     LOG(ERROR) << "Unable to open SIGAR instance: error " << status;
     sigar_ = nullptr;
  }
}

ProcessCounters::~ProcessCounters() {
  if (sigar_) {
    sigar_close(sigar_);
  }
}

std::unordered_map<std::string, fbzmq::thrift::Counter>
ProcessCounters::fetchStats() {
  std::unordered_map<std::string, fbzmq::thrift::Counter> retMap;
  // close, but no SIGAR
  if (!sigar_) {
    return retMap;
  }
  sigar_proc_list_t proclist;
  auto status = sigar_proc_list_get(sigar_, &proclist);
  if (SIGAR_OK != status) {
    PLOG(ERROR) << "Error fetching process list using SIGAR";
    return retMap;
  }

  sigar_mem_t sysMem;
  status = sigar_mem_get(sigar_, &sysMem);
  if (SIGAR_OK != status) {
    return retMap;
  }

  for (size_t i = 0; i < proclist.number; i++) {
    sigar_pid_t pid = proclist.data[i];
    sigar_proc_state_t pstate;

    auto status = sigar_proc_state_get(sigar_, pid, &pstate);
    if (status != SIGAR_OK) {
      PLOG(ERROR) << folly::format(
          "Error fetching process state for pid {} using SIGAR", pid);
      continue;
    }

    // skip processes we are not interested in
    if (kKnowProcNames.find(pstate.name) == kKnowProcNames.end()) {
      continue;
    }

    sigar_proc_time_t ptime;
    status = sigar_proc_time_get(sigar_, pid, &ptime);
    if (status != SIGAR_OK) {
      PLOG(ERROR) << folly::format(
          "Error fetching process time for pid {} using SIGAR", pid);
      continue;
    }

    // process uptime
    auto uptime = seconds(std::time(nullptr)).count() - ptime.start_time / 1000;
    retMap.insert(std::make_pair(
        folly::sformat("{}.uptime", pstate.name), createGauge(uptime)));

    sigar_proc_cpu_t cpu;
    status = sigar_proc_cpu_get(sigar_, pid, &cpu);
    if (status != SIGAR_OK) {
      PLOG(ERROR) << folly::format(
          "Error fetching process cpu for pid {} using SIGAR", pid);
      continue;
    }

    // process cpu usage
    retMap.insert(std::make_pair(
        folly::sformat("{}.cpu.util", pstate.name),
        createGauge(100 * cpu.percent)));

    sigar_proc_mem_t mem;
    status = sigar_proc_mem_get(sigar_, pid, &mem);
    if (status != SIGAR_OK) {
      PLOG(ERROR) << folly::format(
          "Error fetching process mem for pid {} using SIGAR", pid);
      continue;
    }

    // process memory usage
    double memPercent = ((double)mem.resident / (double)sysMem.total) * 100;
    retMap.insert(std::make_pair(
        folly::sformat("{}.mem.util", pstate.name), createGauge(memPercent)));

    sigar_proc_fd_t fd;
    status = sigar_proc_fd_get(sigar_, pid, &fd);
    if (status != SIGAR_OK) {
      PLOG(ERROR) << folly::format(
        "Error fetching process file descriptors for pid {} using SIGAR", pid);
      continue;
    }

    // process fd usage
    auto fdTotal = (double)fd.total;
    retMap.insert(std::make_pair(
      folly::sformat("{}.open_files", pstate.name), createGauge(fdTotal)));
  }

  sigar_proc_list_destroy(sigar_, &proclist);
  return retMap;
}

} // stats
} // terragraph
} // facebook
