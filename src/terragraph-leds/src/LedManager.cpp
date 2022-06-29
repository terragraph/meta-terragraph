/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "LedManager.h"

#include <fcntl.h>
#include <folly/Format.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace facebook {
namespace terragraph {

// Interval at which LEDs will turn on or off in the "BLINK" state
static const std::chrono::milliseconds kBlinkInterval =
    std::chrono::milliseconds(500);

struct LedStaticInfo {
  const char* name;
  int gpio;
};

// Should be in the same order as enum LedId
static const LedStaticInfo kLedStaticInfo[LED_COUNT] = {
  {"A", 505}, {"B", 506}, {"C", 502}
};

// Should be in the same order as enum LedState
static const char* const kLedStateString[3] = {"OFF", "ON", "BLINK"};

const char*
LedManager::getLedIdStr(LedId ledId) {
  return kLedStaticInfo[(int)ledId].name;
}

const char*
LedManager::getLedStateStr(LedState ledState) {
  return kLedStateString[(int)ledState];
}

LedManager::LedManager(fbzmq::ZmqEventLoop& loop) : loop_(loop) {
  // Open and prepare GPIO files in /sys
  for (int i = 0; i < LED_COUNT; i++) {
    ledData_[i].gpioFd = prepareGpioFile(kLedStaticInfo[i].gpio);
  }

  // Turn all LEDs off
  for (int i = 0; i < LED_COUNT; i++) {
    ledData_[i].state = LedState::OFF;
    actuateLed((LedId)i, false);
  }

  // Set up timer callback for blinking LEDs. Actual timer will be turned on
  // if and when at least one LED starts blinking
  blinkTimer_ = fbzmq::ZmqTimeout::make(
      &loop, [this]() noexcept { blinkTimerTimeout(); });

  // Initialization finished, turn on LED A
  setLedState(LedId::A, LedState::ON);
}

LedManager::~LedManager() {
  // Turn off all LEDs
  for (int i = 0; i < LED_COUNT; i++) {
    actuateLed((LedId)i, false);
  }

  // Close GPIO fds
  for (int i = 0; i < LED_COUNT; i++) {
    if (ledData_[i].gpioFd != -1) {
      close(ledData_[i].gpioFd);
    }
  }
}

int
LedManager::prepareGpioFile(int gpio) const {
  std::string errStr = folly::sformat("GPIO {} unusable", gpio);

  // Set GPIO direction to 'out'
  std::string filename =
      folly::sformat("/sys/class/gpio/gpio{}/direction", gpio);
  int fd = open(filename.c_str(), O_WRONLY | O_TRUNC);
  if (fd < 0) {
    // Try exporting GPIO first
    fd = open("/sys/class/gpio/export", O_WRONLY | O_TRUNC);
    if (fd < 0) {
      PLOG(ERROR) << "Can't export GPIO, " << errStr;
      return -1;
    }
    std::string gpioStr = folly::sformat("{}\n", gpio);
    if (write(fd, gpioStr.c_str(), gpioStr.size()) !=
        (ssize_t)gpioStr.size()) {
      PLOG(ERROR) << "Can't write to export file, " << errStr;
      close(fd);
      return -1;
    }
    close(fd);

    // Try again
    fd = open(filename.c_str(), O_WRONLY | O_TRUNC);
    if (fd < 0) {
      PLOG(ERROR) << "Can't open direction file, " << errStr;
      return -1;
    }
  }
  if (write(fd, "out\n", 4) != 4) {
    PLOG(ERROR) << "Can't set direction to out, " << errStr;
    close(fd);
    return -1;
  }
  close(fd);

  // Open value file
  filename = folly::sformat("/sys/class/gpio/gpio{}/value", gpio);
  fd = open(filename.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
  if (fd < 0) {
    PLOG(ERROR) << "Can't open value file, " << errStr;
    return -1;
  }

  return fd;
}

void
LedManager::setLedState(LedId ledId, LedState state) {
  LedState oldState = ledData_[(int)ledId].state;
  if (oldState == state) {
    return;
  }
  ledData_[(int)ledId].state = state;
  LOG(INFO) << "LED " << getLedIdStr(ledId) << " changed "
            << getLedStateStr(oldState) << " -> " << getLedStateStr(state);

  // If new state is ON or OFF, actuate LED now.
  // If BLINK, timer will handle it.
  if (state == LedState::OFF || state == LedState::ON) {
    actuateLed(ledId, state == LedState::ON);
  }

  // Adjust number of blinking LEDs, potentially starting/stopping blink
  // timer. (We stop the timer when no LEDs are blinking to save CPU/power;
  // correctness is maintained regardless) Note that old and new state can't
  // both be BLINK due to the check at the top of the function
  if (oldState == LedState::BLINK) {
    blinkingLedCount_--;
    if (blinkingLedCount_ == 0) {
      blinkTimer_->cancelTimeout();
      blinkStateIsOn_ = false;
    }
  } else if (state == LedState::BLINK) {
    blinkingLedCount_++;
    if (blinkingLedCount_ == 1) {
      blinkTimer_->scheduleTimeout(kBlinkInterval, true /* periodic */);
    }
  }
}

LedState
LedManager::getLedState(LedId ledId) const {
  return ledData_[(int)ledId].state;
}

void
LedManager::actuateLed(LedId ledId, bool turnOn) {
  VLOG(4) << "Turning LED " << getLedIdStr(ledId) << " "
          << (turnOn ? "ON" : "OFF");
  int fd = ledData_[(int)ledId].gpioFd;
  if (fd == -1) {
    VLOG(4) << "GPIO file not open for LED " << getLedIdStr(ledId);
    return;
  }
  if (write(fd, turnOn ? "1\n" : "0\n", 2) != 2) {
    PLOG(ERROR) << "Can't actuate LED " << getLedIdStr(ledId);
  }
}

void
LedManager::blinkTimerTimeout() {
  blinkStateIsOn_ = !blinkStateIsOn_;
  for (int i = 0; i < LED_COUNT; i++) {
    if (ledData_[i].state == LedState::BLINK) {
      actuateLed((LedId)i, blinkStateIsOn_);
    }
  }
}

} // namespace terragraph
} // namespace facebook
