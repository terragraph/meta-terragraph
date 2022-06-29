/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "AccelerometerCounters.h"

#include <folly/Format.h>
#include <folly/ScopeGuard.h>
#include <folly/String.h>

#include <fcntl.h>
#include <math.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
const uint16_t kAccelerometerType = 3;
const double kRadToDeg = (180.0 / 3.14159265);
const uint kMaxNumEvents = 64;
const std::string kAccelerometerEventFile =
    "/dev/input/by-path/platform-lis3lv02d-event";
const int kInvalidValue = 10000000;
// Timeout for select(2) in microseconds, must be < 1 sec for high-freq stats
// 0.5 seconds
const long kSelectTimeout = 500000;
} // namespace

namespace facebook {
namespace terragraph {
namespace stats {

struct accEvent {
  uint32_t sec;
  uint32_t usec;
  uint16_t type;
  uint16_t code;
  int32_t value;
};

AccelerometerCounters::AccelerometerCounters() {
  lastGravity_.resize(axisNames_.size(), kInvalidValue);
}

std::unordered_map<std::string, fbzmq::thrift::Counter>
AccelerometerCounters::fetchStats() {

  std::unordered_map<std::string, fbzmq::thrift::Counter> ret;
  // read events from accelerometer driver
  int fd = open(kAccelerometerEventFile.c_str(), O_RDONLY);
  if (fd == -1) {
    PLOG(ERROR) << folly::format(
        "Cannot read accelerometer file: {}", kAccelerometerEventFile);
    return ret;
  }

  // Ensure fd is closed when fetchStats() exits
  SCOPE_EXIT {
    if (close(fd) != 0) {
      PLOG(ERROR) << folly::format(
          "Closing accelerometer event file ({}) failed",
          kAccelerometerEventFile);
    }
  };

  struct accEvent events[kMaxNumEvents];
  fd_set rdfs;
  FD_ZERO(&rdfs);
  FD_SET(fd, &rdfs);

  /* Wait up to 0.5 seconds */
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = kSelectTimeout;

  auto retVal = select(fd + 1, &rdfs, nullptr, nullptr, &tv);
  if (retVal == -1) {
    PLOG(ERROR) << "select failed";
    return ret;
  } else if (retVal == 0) {
    VLOG(4) << "select timed out: no data within 0.3 seconds";
    return ret;
  }

  auto numEvents = read(fd, events, sizeof(events)) / sizeof(struct accEvent);

  // Convert accelerometer events to stats
  for (uint i = 0; i < numEvents; i++) {
    struct accEvent* ev = &events[i];
    if (ev->type == kAccelerometerType && ev->code < axisNames_.size()) {
      // Ignore timestamp from actual events
      // Ignore multiple values (of same axis) per read
      auto axis = ev->code;
      auto acceleration = ev->value;
      auto axisName = axisNames_[axis];
      lastGravity_[axis] = acceleration;
      ret["accelerometer." + axisName] = createGauge(acceleration);
      auto gravityAngle = getGravityAngle(axis);
      if (gravityAngle != kInvalidValue) {
        ret["gravityAngle." + axisName] = createGauge(gravityAngle);
      }
    }
  }

  return ret;
}

double
AccelerometerCounters::getGravityAngle(uint axis) {
  double h = 0;
  for (auto& lastGravity : lastGravity_) {
    if (lastGravity == kInvalidValue) {
      return kInvalidValue;
    }
    h += lastGravity * lastGravity;
  }
  h = sqrt(h);
  return acos(lastGravity_[axis] / h) * kRadToDeg;
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
