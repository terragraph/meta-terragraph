/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TimeUtils.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "Consts.h"
#include "GpsClock.h"

DEFINE_int32(
    gps_utc_offset,
    18,
    "The current leap second offset between GPS and UTC time");

namespace facebook {
namespace terragraph {

std::chrono::steady_clock::time_point
TimeUtils::getSteadyTimePoint() {
  return std::chrono::steady_clock::now();
}

int64_t
TimeUtils::getSteadyTimestamp() {
  // time_since_epoch: duration since the start of the clock
  return std::chrono::duration_cast<std::chrono::seconds>(
      getSteadyTimePoint().time_since_epoch()).count();
}

uint64_t
TimeUtils::unixTimeToGpsTime(std::time_t unixTime) {
  // Widen to 64 bits
  uint64_t unixTime64 = (uint64_t)unixTime;

  // Convert to GPS time.
  // GPS time starts at midnight Jan 6, 1980
  // (i.e., the midnight from Jan 5 to Jan 6), while UNIX time starts at
  // midnight Jan 1, 1970. The number of seconds between the two dates is
  // 315964800.
  uint64_t gpsTime = unixTime64 - E2EConsts::kUnixGpsEpochDifference.count();

  // Adjust for leap seconds.
  // GPS time is not adjusted for leap seconds, while UTC is.
  // UNIX time is based on UTC.
  // Leap seconds are usually applied on June 30 or Decmeber 31, and get added
  // on average every 18 months.
  // A GPS device knows the difference between GPS and UTC, so in the future
  // we might want to ask a GPS device for the current difference, rather
  // than hardcode it. For our purposes, we don't mind if our calculations
  // are a second or two off. What's important is that all transmitters and
  // receivers get the same time.
  uint64_t realGpsTime = gpsTime + FLAGS_gps_utc_offset;

  return realGpsTime;
}

uint64_t
TimeUtils::gpsTimeToBwgd(std::time_t gpsTime) {
  // Convert GPS time to BWGD.
  // BWGD (Bandwidth Grant Duration) is a 25.6ms (exact) interval. BWGDs
  // start at the GPS epoch and follow one another. For example, the first
  // BWGD goes from 0 to 25.6ms since the GPS epoch, and the second BWGD goes
  // from 25.6 to 51.2ms.
  // The BWGD index is floor(gps time / 25.6ms). Our calculation is a bit
  // different:
  // 1. We multiply GPS time by 1000 to convert it to milliseconds.
  // 2. We multiply GPS time by another 10, and divide by 256, rather than
  //    dividing by 25.6, thus avoiding floating point.
  // 3. We round up rather than down to give us some extra slack (we're not
  //    interested in an exact conversion, but rather in a set time in
  //    the future), thus +255.
  return (gpsTime * 1000 * 10 + 255) / 256;
}

uint64_t
TimeUtils::unixTimeToBwgd(std::time_t unixTime) {
  return gpsTimeToBwgd(unixTimeToGpsTime(unixTime));
}

std::chrono::system_clock::time_point
TimeUtils::bwgdToUnixTime(uint64_t bwgd) {
  // This is the reverse of unixTimeToBwgd, and returns milliseconds since epoch
  uint64_t realGpsTime = bwgd * 256 / 10;
  uint64_t gpsTime = realGpsTime - (FLAGS_gps_utc_offset * 1000);
  uint64_t unixTimeMs = gpsTime +
      std::chrono::duration_cast<std::chrono::milliseconds>(
          E2EConsts::kUnixGpsEpochDifference).count();
  return std::chrono::system_clock::time_point(
      std::chrono::milliseconds(unixTimeMs));
}

uint64_t
TimeUtils::currentBwgdIdx(const std::chrono::seconds& offset) {
  auto gpsNow = GpsClock::now();
  uint64_t bwgdIdx;
  if (gpsNow.time_since_epoch().count() == 0) {
    // Fallback to using system time to calculate BWGD index if GPS time
    // is not available.
    bwgdIdx = unixTimeToBwgd(std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now() + offset));
    VLOG(3) << "Current BWGD index requested, using index " << bwgdIdx
            << " based on system clock";
  } else {
    // Calculate BWGD index from GPS clock if possible.
    bwgdIdx = gpsTimeToBwgd(GpsClock::to_time_t(gpsNow + offset));
    VLOG(3) << "Current BWGD index requested, using index " << bwgdIdx
            << " based on GPS clock";
  }
  return bwgdIdx;
}

} // namespace terragraph
} // namespace facebook
