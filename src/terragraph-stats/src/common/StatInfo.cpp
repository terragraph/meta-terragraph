/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "StatInfo.h"

namespace {
  // kGpsUnixOffset = Unix Time - Gps Time
  // GPS time starts at midnight Jan 6, 1980
  // UNIX time starts at midnight Jan 1, 1970.
  // The number of seconds between the two dates is 315964800.
  // factor in 18 leap seconds
  // https://en.wikipedia.org/wiki/Leap_second
  // https://en.wikipedia.org/wiki/Coordinated_Universal_Time
  const int64_t kGpsUnixOffset = 315964782;
  const int64_t kSecondsInHour = 3600;
}

namespace facebook {
namespace terragraph {
namespace stats {

int64_t
getUnixUsTimestamp(int64_t counterTs, int64_t localTs) {
  // counterTs is the time stamp from generator of key-value
  // localTs is the time when key-value was received by stats agent
  // firmware generates timestamp based on gps time or uptime
  int64_t unixUsFw = counterTs + (kGpsUnixOffset * 1e6);
  // apps generates timestamp based on unix time in milisecond
  int64_t unixUsApp = counterTs * 1e3;
  // use within 1hr to validate source timestamps
  int64_t unixUsMin = (localTs - kSecondsInHour) * 1e6;
  int64_t unixUsMax = (localTs + kSecondsInHour) * 1e6;
  // if counterTs makes sense, use it, else use localTs
  int64_t unixUs = localTs * 1e6;
  if (unixUsFw > unixUsMin && unixUsFw < unixUsMax) {
    // use FW timestamp if within range (1hr) of unix/system time
    unixUs = unixUsFw;
  } else if (unixUsApp > unixUsMin && unixUsApp < unixUsMax) {
    unixUs = unixUsApp;
  }
  return unixUs;
}


} // namespace stats
} // namespace terragraph
} // namespace facebook
