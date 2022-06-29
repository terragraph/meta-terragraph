/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "SystemCounters.h"

#include <mntent.h>

#include <folly/Format.h>
#include <gflags/gflags.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>

DEFINE_string(
    mounted_filesystems_path, "/etc/mtab", "Mounted file systems path");

using std::map;
using std::string;

namespace facebook {
namespace terragraph {
namespace stats {

static const std::string CORE_NAME_FORMAT = "core_{}.cpu";

SystemCounters::SystemCounters() {
  auto status = sigar_open(&sigar_);
  if (SIGAR_OK != status) {
    LOG(ERROR) << "Unable to open SIGAR instance: error " << status;
    sigar_close(sigar_);
    sigar_ = nullptr;
    return;
  }
  status = sigar_cpu_list_get(sigar_, &cpuList_);
  if (SIGAR_OK != status) {
    LOG(ERROR) << "Unable to fetch cpu info from sigar: error " << status;
    sigar_close(sigar_);
    sigar_ = nullptr;
    return;
  }
  for (unsigned int i = 0; i < cpuList_.number; i++) {
    updateCpuSum(cpuList_.data[i]);
  }
}

SystemCounters::~SystemCounters() {
  if (sigar_) {
    sigar_cpu_list_destroy(sigar_, &cpuList_);
    sigar_close(sigar_);
  }
}

void
SystemCounters::populateDiskStats(
    std::unordered_map<string, fbzmq::thrift::Counter>& stats) {
  FILE* mtab = setmntent(FLAGS_mounted_filesystems_path.c_str(), "r");
  if (mtab == nullptr) {
    return;
  }
  struct mntent mntbuf;
  char buf[4096];
  while (getmntent_r(mtab, &mntbuf, buf, (int)sizeof(buf)) != nullptr) {
    if (mntbuf.mnt_dir == nullptr) {
      continue;
    }
    // get disk usage for the mount path
    struct statvfs64 sfs;
    if (statvfs64(mntbuf.mnt_dir, &sfs) != 0) {
      continue;
    }
    // calculate space from FS struct
    auto total = sfs.f_blocks * sfs.f_frsize;
    auto free = sfs.f_bfree * sfs.f_frsize;
    double util = 0;
    if (total > 0 && free > 0) {
      util = (total - free) / (double)total * 100;
    }
    if (total > 0) {
      // add counter for non-zero disk mounts
      string mountName(mntbuf.mnt_dir);
      stats["mount.total:" + mountName] = createGauge(total);
      stats["mount.free:" + mountName] = createGauge(free);
      stats["mount.util:" + mountName] = createGauge(util);
    }
  }
  // close file handle
  fclose(mtab);
}

void
SystemCounters::updateCpuSum(sigar_cpu_t& cpu) {
  cpuSum_.user += cpu.user;
  cpuSum_.sys += cpu.sys;
  cpuSum_.nice += cpu.nice;
  cpuSum_.idle += cpu.idle;
  cpuSum_.wait += cpu.wait;
  cpuSum_.irq += cpu.irq;
  cpuSum_.soft_irq += cpu.soft_irq;
  cpuSum_.stolen += cpu.stolen;
  cpuSum_.total += cpu.total;
}

sigar_cpu_t
SystemCounters::rollCurrentCpuSum() {
  sigar_cpu_t oldCpu = cpuSum_;
  cpuSum_ = {};
  return oldCpu;
}

void
SystemCounters::populateSysStats(
    std::unordered_map<string, fbzmq::thrift::Counter>& stats) {
  // basic system stats
  struct sysinfo info;
  int sysInfoRet = sysinfo(&info);
  if (sysInfoRet == 0) {
    stats["uptime"] = createGauge(info.uptime);
    uint32_t memUnit = info.mem_unit;
    stats["mem.total"] = createGauge(info.totalram * memUnit);
    stats["mem.free"] = createGauge(info.freeram * memUnit);
    stats["mem.shared"] = createGauge(info.sharedram * memUnit);
    stats["mem.buffer"] = createGauge(info.bufferram * memUnit);
    // free mem %
    stats["mem.util"] = createGauge((int64_t)(
        (double)(info.totalram - info.freeram) / info.totalram * 100.0));
    stats["swap.total"] = createGauge(info.totalswap * memUnit);
    stats["swap.free"] = createGauge(info.freeswap * memUnit);
    // free swap %
    stats["swap.util"] = createGauge((int64_t)(
        (double)(info.totalswap - info.freeswap) / info.totalswap * 100));
    stats["procs.total"] = createGauge(info.procs);
  }
  // load averages
  double loads[3];
  getloadavg(loads, 3);
  stats["load-1"] = createGauge(loads[0]);
  stats["load-5"] = createGauge(loads[1]);
  stats["load-15"] = createGauge(loads[2]);

  // CPU Usage
  if (sigar_) {
    auto oldCpuInfo = cpuList_;
    auto status = sigar_cpu_list_get(sigar_, &cpuList_);
    if (SIGAR_OK != status || cpuList_.number != oldCpuInfo.number) {
      LOG(ERROR) << "Unable to fetch cpu list from sigar: error " << status;
    } else {
      sigar_cpu_t oldCpuSum = rollCurrentCpuSum();
      for (unsigned int i = 0; i < cpuList_.number; i++) {
        sigar_cpu_t oldCpu = oldCpuInfo.data[i];
        sigar_cpu_t curCpu = cpuList_.data[i];
        updateCpuSum(curCpu);
        sigar_cpu_perc_t cpuPerc;
        sigar_cpu_perc_calculate(&oldCpu, &curCpu, &cpuPerc);
        stats[folly::format(CORE_NAME_FORMAT, i).str() + ".util"] =
            createGauge(cpuPerc.combined * 100);
        stats[folly::format(CORE_NAME_FORMAT, i).str() + ".user"] =
            createGauge(cpuPerc.user * 100);
        stats[folly::format(CORE_NAME_FORMAT, i).str() + ".sys"] =
            createGauge(cpuPerc.sys * 100);
        stats[folly::format(CORE_NAME_FORMAT, i).str() + ".nice"] =
            createGauge(cpuPerc.nice * 100);
        stats[folly::format(CORE_NAME_FORMAT, i).str() + ".wait"] =
            createGauge(cpuPerc.wait * 100);
      }
      sigar_cpu_perc_t totalCpuPerc;
      sigar_cpu_perc_calculate(&oldCpuSum, &cpuSum_, &totalCpuPerc);
      stats["cpu.util"] = createGauge(totalCpuPerc.combined * 100);
      stats["cpu.user"] = createGauge(totalCpuPerc.user * 100);
      stats["cpu.sys"] = createGauge(totalCpuPerc.sys * 100);
      stats["cpu.nice"] = createGauge(totalCpuPerc.nice * 100);
      stats["cpu.wait"] = createGauge(totalCpuPerc.wait * 100);
    }
  }
}

std::unordered_map<string, fbzmq::thrift::Counter>
SystemCounters::fetchStats() {
  std::unordered_map<string, fbzmq::thrift::Counter> ret;
  populateDiskStats(ret);
  populateSysStats(ret);
  return ret;
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
