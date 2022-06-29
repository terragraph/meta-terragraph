/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "MarvellStatsFetcher.h"
#include "ZmqCounterUtils.h"

#include "e2e/common/SysUtils.h"
#include "e2e/common/MarvellUtils.h"
#include <fbzmq/zmq/Zmq.h>
#include <regex>

#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/String.h>

using namespace fbzmq;
using std::map;
using std::string;

namespace facebook {
namespace terragraph {
namespace stats {

// total # of interfaces requested from the marvell switch scripts above
// used for parsing command output
const static int NUM_INTERFACES = 8;

// zmq id of marvell stats fetcher
const std::string kMarvellStatsFetcherId{"zmq_monitor_marvell_stats"};

// counter setting operations
#define SET_COUNTER(INPUT, TXDIR, DEST, NAME)                   \
  if (TXDIR) {                                                  \
    txSwitchCounters_[portName].DEST += folly::to<long>(INPUT); \
  } else {                                                      \
    rxSwitchCounters_[portName].DEST += folly::to<long>(INPUT); \
  }                                                             \
  retMap.insert(std::make_pair(                                 \
      portName + "." + (TXDIR ? "tx_" : "rx_") + NAME,          \
      ZmqCounterUtils::createCounter(                           \
          TXDIR ? txSwitchCounters_[portName].DEST              \
                : rxSwitchCounters_[portName].DEST)));

MarvellStatsFetcher::MarvellStatsFetcher(
    fbzmq::Context& context,
    std::chrono::seconds statsReportInterval,
    const std::string& monitorSubmitUrl)
    : context_(context) {

  zmqMonitorClient_ = std::make_shared<fbzmq::ZmqMonitorClient>(
      context, monitorSubmitUrl, kMarvellStatsFetcherId);

  eventClient_ = std::make_unique<EventClient>(
      kMarvellStatsFetcherId, zmqMonitorClient_);

  // Create stats-report timer
  const bool makePeriodic = true;
  statsReportTimer_ = ZmqTimeout::make(
      this, [this]() noexcept { fetchAndStoreCounterStats(); });
  statsReportTimer_->scheduleTimeout(statsReportInterval, makePeriodic);
}

void
MarvellStatsFetcher::fetchAndStoreCounterStats() {
  bool prevMarvellStatus = marvellSwitchStatus_;
  // Fetch stats and check if any of the fetching fails
  fbzmq::CounterMap tCounters;
  marvellSwitchStatus_ =
      fetchInterfaceCounters(tCounters) & fetchInterfaceStatus(tCounters);

  // send counters to zmq subscriber(s)
  zmqMonitorClient_->setCounters(tCounters);

  // If marvell switch went from down to up, send info event, if it went from up
  // to down, send error event
  if (marvellSwitchStatus_ != prevMarvellStatus) {
    eventClient_->logEventDynamic(
        thrift::EventCategory::STATUS,
        thrift::EventId::MARVELL_SWITCH_STATUS,
        marvellSwitchStatus_ ? thrift::EventLevel::INFO
                             : thrift::EventLevel::ERROR,
        folly::sformat(
            "Marvell switch is {}sending stats",
            marvellSwitchStatus_ ? "" : "not "),
        folly::dynamic::object("sending_stats", marvellSwitchStatus_));
  }
}

bool
MarvellStatsFetcher::fetchInterfaceCounters(fbzmq::CounterMap& retMap) {

  VLOG(3) << "Requesting Marvell interface counters";
  if (!MarvellUtils::updateInterfaceCounters()) {
    LOG(ERROR) << "Failed to update Marvell interface counters";
    return false;
  }
  auto interfaceCounters = MarvellUtils::getInterfaceCounters();
  if (!interfaceCounters) {
    LOG(ERROR) << "Failed to fetch Marvell interface counters";
    return false;
  }
  // The output format lists each interface on its own line for the receive
  // stats, then transmit stats. Use a counter to determine if we're on the
  // receive or transmit section
  int outLinesMatched = 0;
  for (const auto& counterReMatch : *interfaceCounters) {
    VLOG(3) << "Marvell port matched: " << counterReMatch[1]
            << " Unicast: " << counterReMatch[2]
            << " Multicast: " << counterReMatch[3]
            << " Broadcast: " << counterReMatch[4]
            << " Bits: " << counterReMatch[5];
    std::string portName =
        MarvellUtils::formatPortIdToName(counterReMatch[1]);
    // determine port counter direction (rx/tx) based on # of lines matched
    bool isTxDir = outLinesMatched >= NUM_INTERFACES ? true : false;
    // each time we execute the counters command, the counter is reset to 0
    // so we need to keep track of the value over time so we don't just use
    // a single 1-second data point for 30-second collection windows
    SET_COUNTER(counterReMatch[2], isTxDir, unicastPkts, "unicast");
    SET_COUNTER(counterReMatch[3], isTxDir, multicastPkts, "multicast");
    SET_COUNTER(counterReMatch[4], isTxDir, broadcastPkts, "broadcast");
    SET_COUNTER(counterReMatch[5], isTxDir, octets, "bps");
    outLinesMatched++;
  }
  return true;
}

bool
MarvellStatsFetcher::fetchInterfaceStatus(fbzmq::CounterMap& retMap) {

  VLOG(3) << "Requesting Marvell interface status";
  if (!MarvellUtils::updateInterfaceStatus()) {
    LOG(ERROR) << "Failed to update Marvell interface status";
    return false;
  }
  auto interfaceStatus = MarvellUtils::getInterfaceStatus();
  if (!interfaceStatus) {
    LOG(ERROR) << "Failed to fetch Marvell interface status";
    return false;
  }
  /*
   * Example console match:
   *
   * 0/4             SGMII        Down   2.5G    Full    None
   * 0/12            SGMII        Down   2.5G    Full    None
   * 0/20            SGMII        Down   2.5G    Full    None
   * 0/24            RXAUI         Up     10G    Full    None
   * 0/27            SR_LR        Down    10G    Full    None
   */
  for (const auto& statusReMatch : *interfaceStatus) {
    VLOG(3) << "Marvell port matched: " << statusReMatch[1]
            << " Type: " << statusReMatch[2] << " State: " << statusReMatch[3]
            << " Speed: " << statusReMatch[4]
            << " Duplex: " << statusReMatch[6];
    std::string portName =
        MarvellUtils::formatPortIdToName(statusReMatch[1]);
    retMap[portName + ".up"] =
        ZmqCounterUtils::createGauge(statusReMatch[3] == "Up" ? 1 : 0);
    retMap[portName + ".speed"] = ZmqCounterUtils::createGauge(
        statusReMatch[5] == "G" ? folly::to<double>(statusReMatch[4])
                                    : 0.0);
    retMap[portName + ".full_duplex"] =
        ZmqCounterUtils::createGauge(statusReMatch[6] == "Full" ? 1 : 0);
  }
  return true;
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
