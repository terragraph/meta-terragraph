/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>
#include <ctime>

namespace facebook {
namespace terragraph {

/**
 * Time-related utilities, mainly for working with UTC and GPS time references.
 */
class TimeUtils {
 public:
  /** Returns a time point representing steady_clock's current point in time. */
  static std::chrono::steady_clock::time_point getSteadyTimePoint();

  /** Returns a monotonic timestamp in seconds. */
  static int64_t getSteadyTimestamp();

  /**
   * Convert UNIX time (seconds since epoch 1/1/1970, minus leap seconds) to GPS
   * time (seconds since epoch 1/6/1980).
   */
  static uint64_t unixTimeToGpsTime(std::time_t unixTime);

  /** Convert GPS time (seconds since epoch 1/6/1980) to BWGD index. */
  static uint64_t gpsTimeToBwgd(std::time_t gpsTime);

  /** Convert UNIX time (seconds since epoch 1/1/1970) to BWGD index. */
  static uint64_t unixTimeToBwgd(std::time_t unixTime);

  /** Convert BWGD index to UNIX time (millisecond resolution). */
  static std::chrono::system_clock::time_point bwgdToUnixTime(uint64_t bwgd);

  /**
   * Return the current BWGD index, optionally adding a given offset in seconds.
   *
   * This uses the GPS time if available (via GpsClock), otherwise the system
   * UNIX time.
   */
  static uint64_t currentBwgdIdx(const std::chrono::seconds& offset);
};

} // namespace terragraph
} // namespace facebook
