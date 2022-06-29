/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>
#include <vector>

#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {

/**
 * Defines the statistic format received by the driver-if socket.
 *
 * We match incoming stats to '<keyPrefix>.<MAC>.<keyName>' and tag as
 * shortName.
 */
struct StatFormat {
  std::string keyName;
  std::string keyPrefix;
  std::string shortName;
  explicit StatFormat(
      const std::string& keyName,
      const std::string& keyPrefix,
      const std::string& shortName)
      : keyName(keyName), keyPrefix(keyPrefix), shortName(shortName){};
};

/**
 * Holder for mapping statistics from raw key name (EX: tgf.<MAC>.stat) to
 * a radio interface.
 */
struct RadioMetric {
  std::string radioMac;
  std::string responderMac;
  std::string shortName;
  explicit RadioMetric(){};
  explicit RadioMetric(
      const std::string& radioMac,
      const std::string& responderMac,
      const std::string& shortName)
      : radioMac(radioMac), responderMac(responderMac), shortName(shortName){};
};

/**
 * Holder for radio metric metadata and value.
 */
struct RadioStat : RadioMetric {
  int64_t timestamp;
  double value;
  explicit RadioStat(
      const std::string& radioMac,
      const std::string& responderMac,
      const std::string& shortName,
      const int64_t timestamp,
      const double value)
      : RadioMetric(radioMac, responderMac, shortName),
        timestamp(timestamp),
        value(value){};
};

/**
 * Generator for statistics mappings from raw key name to short name.
 */
class StatCache {
 public:
  /**
   * Generate mapping of raw key names coming from driver-if to LinkMetric.
   *
   * Uses list of StatFormat to determine raw key names.
   */
  static std::unordered_map<std::string /* raw metric name */, RadioMetric>
  generateLinkKeys(
      const std::vector<StatFormat>& statsFormat,
      const thrift::LinkStatus& linkStatus);
};

} // namespace terragraph
} // namespace facebook
