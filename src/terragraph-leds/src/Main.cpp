/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <memory>

#include <signal.h>

#include <fbzmq/async/StopEventLoopSignalHandler.h>
#include <folly/init/Init.h>
#include <gflags/gflags.h>

#include "LedManager.h"
#include "StatsSubscriber.h"

DEFINE_int32(
    dump_status_interval_s,
    5 * 60,
    "LED status dump interval (only if -v=1 or higher)");

DEFINE_int32(
    mcs_low_threshold,
    8,
    "A link MCS is considered low if it's <= this threshold");

namespace {
// Interval for timer that checks quality of links
const std::chrono::seconds kLinkQualityCheckInterval = std::chrono::seconds(5);
}

namespace facebook {
namespace terragraph {

// Calls an std::function upon receiving a signal
class SignalHandler final : public fbzmq::AsyncSignalHandler {
 public:
  SignalHandler(fbzmq::ZmqEventLoop& loop, std::function<void(int)> handler)
      : fbzmq::AsyncSignalHandler(&loop), handler_(std::move(handler)) {}

  void
  signalReceived(int sig) noexcept override {
    handler_(sig);
  }

 private:
  std::function<void(int)> handler_;
};

// Main event loop. Receives link status and firmware stats and control LEDs
class LedEventLoop : public fbzmq::ZmqEventLoop {
 public:
  LedEventLoop(fbzmq::Context& context)
      : ledManager_(*this),
        sigUsr1Handler_(*this, [this](int) { dumpAllLedStatus(); }),
        statsClient_(
            context, *this, [this](const fbzmq::thrift::CounterMap& stats) {
              handleStats(stats);
            }) {
    // Install signal handler
    sigUsr1Handler_.registerSignalHandler(SIGUSR1);

    // Initialize link quality check timer
    linkQualityTimer_ = fbzmq::ZmqTimeout::make(
        this, [this]() noexcept { linkQualityTimerProc(); });
    linkQualityTimer_->scheduleTimeout(
        kLinkQualityCheckInterval, true /* periodic */);

    // Initialize debug print timer
    if (VLOG_IS_ON(1)) {
      logLedStatusDumpTimer_ = fbzmq::ZmqTimeout::make(
          this, [this]() noexcept { dumpAllLedStatus(); });
      logLedStatusDumpTimer_->scheduleTimeout(
          std::chrono::seconds(FLAGS_dump_status_interval_s),
          true /* periodic */);
    }
  }

 private:
  // Control and get LED states
  LedManager ledManager_;

  // Signal handler (prints all LED states)
  SignalHandler sigUsr1Handler_;

  // Timer that periodically prints the state of all LEDs
  std::unique_ptr<fbzmq::ZmqTimeout> logLedStatusDumpTimer_;

  // Clients to receive firmware stats
  StatsSubscriber statsClient_;

  // Map [MCS stat key (includes radio MAC)]->[MCS value]
  std::unordered_map<std::string, int> mcsMap_;

  // Timer that checks quality of all links and updates LED state
  std::unique_ptr<fbzmq::ZmqTimeout> linkQualityTimer_;

  // Called upon receiving stats from the firmware.
  // Extracts and saves per-radio MCS values.
  void
  handleStats(const fbzmq::thrift::CounterMap& stats) {
    for (const auto& it : stats) {
      const std::string& key = it.first;
      int value = (int)it.second.value_ref().value();

      // We're only interested in MCS value
      if (key.find(".staPkt.mcs") == std::string::npos) {
        continue;
      }
      mcsMap_[key] = value;

      // Stat format is: [counterName]\0[ourRadioMac]
      // For logging, reformat key as: [ourRadioMac]-[counterName]
      // (counterName includes the peer MAC)
      size_t pos = key.find('\0');
      std::string logStr = (pos != std::string::npos)
          ? key.substr(pos + 1) + "-" + key.substr(0, pos)
          : key;
      VLOG(2) << "MCS for " << logStr << " is " << value;
    }
  }

  // Called periodically. Checks last known link quality for links (as received
  // by handleStats) and sets state of LED C accordingly.
  void
  linkQualityTimerProc() {
    int linkCount = mcsMap_.size();

    // Calculate minimal MCS over all links
    int minMcs = INT_MAX;
    for (const auto& it : mcsMap_) {
      if (it.second < minMcs) {
        minMcs = it.second;
      }
    }

    // Clear MCS map to purge old values
    // This assumes our desired stat frequency > than this timer frequency
    mcsMap_.clear();

    // Set LED state:
    // - LED B: ON if at least one link is associated.
    // - LED C: ON if all links are above the minimum MCS,
    //          BLINK if at least one link is bad.
    if (linkCount > 0) {
      ledManager_.setLedState(LedId::B, LedState::ON);
      if (minMcs == INT_MAX) {
        // shouldn't happen
        ledManager_.setLedState(LedId::C, LedState::OFF);
      } else if (minMcs <= FLAGS_mcs_low_threshold) {
        ledManager_.setLedState(LedId::C, LedState::BLINK);
      } else {
        ledManager_.setLedState(LedId::C, LedState::ON);
      }
    } else {
      ledManager_.setLedState(LedId::B, LedState::OFF);
      ledManager_.setLedState(LedId::C, LedState::OFF);
    }
  }

  // Prints current LED state for all LEDs
  void
  dumpAllLedStatus() {
    LOG(INFO) << "--- Start LED status dump ---";
    for (int i = 0; i < LED_COUNT; i++) {
      LOG(INFO)
          << "Led " << LedManager::getLedIdStr((LedId)i) << " is "
          << LedManager::getLedStateStr(ledManager_.getLedState((LedId)i));
    }
    LOG(INFO) << "--- End LED status dump ---";
  }
};

} // namespace terragraph
} // namespace facebook

int
main(int argc, char** argv) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;

  fbzmq::Context context;

  LOG(INFO) << "Starting LED agent";

  // start signal handler before any thread
  facebook::terragraph::LedEventLoop mainEventLoop(context);
  fbzmq::StopEventLoopSignalHandler handler(&mainEventLoop);
  handler.registerSignalHandler(SIGINT);
  handler.registerSignalHandler(SIGQUIT);
  handler.registerSignalHandler(SIGTERM);

  // main loop
  LOG(INFO) << "Starting main event loop...";
  mainEventLoop.run();
  LOG(INFO) << "Main event loop got stopped";
}
