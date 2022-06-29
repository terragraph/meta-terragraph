/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>

namespace facebook {
namespace terragraph {

/**
 * A GpsClock is an approximation of GPS time. The epoch is equal to time since
 * January 6, 1980, and should be set to be as close to actual GPS time as
 * possible. Then, a steady_clock is used to determine time since the epoch was
 * set. The ideal use case is to update the epoch every so often and rely on
 * steady_clock to approximate the elapsed time since the epoch was set.
 */
class GpsClock {
 public:
  /** \{ */
  typedef std::chrono::microseconds duration;
  typedef duration::rep rep;
  typedef duration::period period;
  typedef std::chrono::time_point<GpsClock> time_point;
  static constexpr bool is_steady = false;
  /** \} */

  /** Update the base time. */
  static void setEpoch(std::chrono::microseconds e);

  /** Convert a GPS time point to number of seconds since epoch. */
  static std::time_t to_time_t(const time_point& t) noexcept;

  /**
   * Get the current GPS time.
   *
   * If setEpoch() has never been called, this returns a time point equivalent
   * to 0 seconds.
   */
  static time_point now() noexcept;

 private:
  /** The epoch time. */
  static std::chrono::microseconds epoch;
  /** The time when the epoch was last updated. */
  static std::chrono::steady_clock::time_point epochUpdated;
};

} // namespace terragraph
} // namespace facebook
