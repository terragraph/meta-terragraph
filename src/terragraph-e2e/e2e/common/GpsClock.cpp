/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "GpsClock.h"

namespace facebook {
namespace terragraph {

std::chrono::microseconds GpsClock::epoch;
std::chrono::steady_clock::time_point
    GpsClock::epochUpdated;

// Update the base time by setting the epoch. For example, if a
// more accurate GPS time is received, set the epoch to
// allow now() to return a more accurate time.
void
GpsClock::setEpoch(duration e) {
  GpsClock::epoch = e;
  GpsClock::epochUpdated = std::chrono::steady_clock::now();
}

std::time_t
GpsClock::to_time_t(const std::chrono::time_point<GpsClock>& t) noexcept {
  return std::time_t(std::chrono::duration_cast<std::chrono::seconds>(
      t.time_since_epoch()).count());
}

std::chrono::time_point<GpsClock>
GpsClock::now() noexcept {
  if (GpsClock::epoch.count() == 0) {
    // If the epoch has not been set, return a value of 0 to indicate
    // the GPS clock has not been initialized and should not be used.
    return std::chrono::time_point<GpsClock>();
  }

  auto timeSinceUpdate =
      std::chrono::steady_clock::now() - GpsClock::epochUpdated;

  return std::chrono::time_point<GpsClock>(
      std::chrono::duration_cast<duration>(GpsClock::epoch + timeSinceUpdate));
}

} // namespace terragraph
} // namespace facebook
