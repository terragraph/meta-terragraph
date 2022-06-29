/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>

namespace facebook {
namespace terragraph {
namespace stats {

/**
 * Utility for parsing structured data in the "key" and "timestamp" fields
 * of fbzmq's Counter.
 *
 * driver-if pushes stats with an "entity" (baseband MAC) appended to the key;
 * do all the string splitting here (shared by publisher classes).
 */

struct StatInfo {
  std::string key;
  std::string entity;

  explicit StatInfo(
      const std::string& rawKey, const std::string& defaultEntity = "") {
    auto pos = rawKey.find('\0');
    if (pos == std::string::npos) {
      key = rawKey;
      entity = defaultEntity;
    } else {
      key = rawKey.substr(0, pos);
      entity = rawKey.substr(pos + 1);
    }
  }
};

// counterTs can be coming from fw in microseconds, or apps in milliseconds, etc
// localTs is unixtimestamp in seconds
// return best guess of unix time in microseconds
int64_t getUnixUsTimestamp(int64_t counterTs, int64_t localTs);

} // namespace stats
} // namespace terragraph
} // namespace facebook
