/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "LocalStatsFetcher.h"

#include <future>

#include <fbzmq/zmq/Zmq.h>
#include <folly/Format.h>
#include <folly/MapUtil.h>
#include <folly/json.h>

#include "../common/Consts.h"
#include "e2e/clients/MinionClient.h"

using namespace fbzmq;
using std::string;

namespace facebook {
namespace terragraph {
namespace stats {

LocalStatsFetcher::LocalStatsFetcher(
    fbzmq::Context& context,
    const std::string& macAddr,
    std::chrono::seconds statsReportInterval,
    const std::string& monitorSubmitUrl,
    bool fetchVppCounters,
    bool fetchOpenrCounters)
    : context_(context), macAddr_(macAddr) {
  zmqMonitorClient_ = std::make_unique<fbzmq::ZmqMonitorClient>(
      context, monitorSubmitUrl, "zmq_monitor_local_stats" /* zmq id */);

  // Change mac format to match that of kvstore
  std::replace(macAddr_.begin(), macAddr_.end(), ':', '.');

  // Enable hardware-specific counters
  if (fetchVppCounters) {
    vppCounters_ = std::make_unique<VppCounters>();
  }
  if (fetchOpenrCounters) {
    openrCounters_ = std::make_unique<OpenrCounters>();
  }

  // Create stats-report timer
  VLOG(2) << "Collecting system stats at " << statsReportInterval.count()
          << "s intervals";
  const bool makePeriodic = true;
  statsReportTimer_ = ZmqTimeout::make(
      this, [this]() noexcept { fetchAndStoreCounterStats(); });
  statsReportTimer_->scheduleTimeout(statsReportInterval, makePeriodic);
}

bool
LocalStatsFetcher::openrConnect() {
  // Do not create new client if one exists already
  if (!openrCtrlClient_) {
    try {
      openrCtrlClient_ =
          openr::getOpenrCtrlPlainTextClient(evb_, folly::IPAddress("::1"));
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Failed to connect to Open/R. Exception: "
                 << folly::exceptionStr(ex);
      openrCtrlClient_ = nullptr;
      return false;
    }
  }
  return true;
}

void
LocalStatsFetcher::getMinionLinks() {
  facebook::terragraph::MinionClient minionClient(context_);
  auto linkStatusDumpResp = minionClient.getLinkStatusDump();
  if (!linkStatusDumpResp) {
    LOG(WARNING) << "Failed to fetch LinkStatusDump from minion for interface "
                    "to responder mapping.";
    return;
  }
  for (const auto& responderToLinkStatus : linkStatusDumpResp->linkStatusDump) {
    if (responderToLinkStatus.second.radioMac_ref().has_value() &&
        responderToLinkStatus.second.ifname_ref().has_value()) {
      interfaceToRadio_[responderToLinkStatus.second.ifname_ref().value()]
          = std::make_pair( responderToLinkStatus.second.radioMac_ref().value(),
                            responderToLinkStatus.first);
    }
  }
}

void
LocalStatsFetcher::fetchAndStoreCounterStats() {
  // fetch interface -> responder mapping from minion
  getMinionLinks();

  fbzmq::CounterMap tCounters;
  if (openrConnect()) {
    if (openrCtrlClient_ && openrCounters_) {
      auto openrCounters = openrCounters_->fetchStats(openrCtrlClient_.get());
      tCounters.insert(openrCounters.begin(), openrCounters.end());
    }
  }

  // update sensor stats
  auto sensorCounters = sensorCounters_.fetchStats();
  tCounters.insert(sensorCounters.begin(), sensorCounters.end());
  // update system stats
  auto systemCounters = systemCounters_.fetchStats();
  tCounters.insert(systemCounters.begin(), systemCounters.end());
  // update network stats
  auto networkCounters = networkCounters_.fetchStats(interfaceToRadio_);
  tCounters.insert(networkCounters.begin(), networkCounters.end());
  // update process stats
  auto processCounters = processCounters_.fetchStats();
  tCounters.insert(processCounters.begin(), processCounters.end());
  // update vpp stats
  if (vppCounters_) {
    auto vppCounters = vppCounters_->fetchStats(interfaceToRadio_);
    tCounters.insert(vppCounters.begin(), vppCounters.end());
    auto vppHqosCounters = vppCounters_->fetchHqosStats(interfaceToRadio_);
    tCounters.insert(vppHqosCounters.begin(), vppHqosCounters.end());
  }

  // push data to monitor
  zmqMonitorClient_->setCounters(tCounters);
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
