/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqTimeout.h>

namespace facebook {
namespace terragraph {

enum { LED_COUNT = 3 };
enum class LedId { A, B, C };
enum class LedState { OFF, ON, BLINK };

class LedManager {
 public:
  // Convert LED id to a string
  static const char* getLedIdStr(LedId ledId);
  // Convert LED state to a string
  static const char* getLedStateStr(LedState ledState);

  LedManager(fbzmq::ZmqEventLoop& loop);
  ~LedManager();

  // Set LED state. This handles states that require continuous managing, such
  // as BLINK
  void setLedState(LedId ledId, LedState state);

  // Get current LED state
  LedState getLedState(LedId ledId) const;

 private:
  // LED fields
  struct LedData {
    // Current LED state
    LedState state = LedState::OFF;

    // GPIO "value" file descriptor
    int gpioFd = -1;
  };

  // Prepare the GPIO files in /sys for the given GPIO pin.
  // Returns an open file descriptor to the "value" file, or -1 upon any error.
  int prepareGpioFile(int gpio) const;

  // Physically turn a led on/off (write to /sys)
  void actuateLed(LedId ledId, bool turnOn);

  // Runs every half blinking interval (500ms) to turn on or off
  // LEDs that should be blinking
  void blinkTimerTimeout();

  // The ZMQ event loop
  fbzmq::ZmqEventLoop& loop_;

  // For each LED: LED state and additional dynamic data
  LedData ledData_[LED_COUNT];

  // Timer and state for blinking LEDs
  std::unique_ptr<fbzmq::ZmqTimeout> blinkTimer_;
  bool blinkStateIsOn_ = false;
  int blinkingLedCount_ = 0;
};

} // namespace terragraph
} // namespace facebook
