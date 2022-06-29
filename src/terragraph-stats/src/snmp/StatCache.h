/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

// because net-snmp is not the best
#ifdef FREE
#undef FREE
#endif

#ifdef LOCKED
#undef LOCKED
#endif

#include <unordered_map>
#include <vector>

#include <folly/Singleton.h>
#include <folly/Synchronized.h>

#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {

/**
 * Column number definitions must match tgRadioInterfacesTable table MIB
 * indexes.
 */
enum SnmpColumn {
  IF_INDEX = 1,
  IF_NAME = 2,
  MAC_ADDR = 3,
  REMOTE_MAC_ADDR = 4,
  MCS = 5,
  SNR = 6,
  RSSI = 7,
};

/**
 * Defines the statistic format received by the driver-if socket.
 *
 * We match incoming stats to '<keyPrefix>.<MAC>.<keyName>' and tag as
 * snmpColumn.
 */
struct StatFormat {
  std::string keyName;
  std::string keyPrefix;
  SnmpColumn snmpColumn;
  explicit StatFormat(
      const std::string& keyName,
      const std::string& keyPrefix,
      const SnmpColumn& snmpColumn)
      : keyName(keyName), keyPrefix(keyPrefix), snmpColumn(snmpColumn){};
};

/**
 * Maps a statistic name to the local radio mac, local interface name, and
 * remote radio mac.
 */
struct LinkMetric {
  SnmpColumn snmpColumn;
  std::string ifName;
  std::string localMac;
  std::string remoteMac;
  explicit LinkMetric(){};
  explicit LinkMetric(
      const SnmpColumn& snmpColumn,
      const std::string& ifName,
      const std::string& localMac,
      const std::string& remoteMac)
      : snmpColumn(snmpColumn),
        ifName(ifName),
        localMac(localMac),
        remoteMac(remoteMac){};
};

/**
 * Stats for an individual interface.
 */
struct RadioStat {
  // extra meta-data not needed in the lookup
  std::string ifName;
  // last updated time of any metric received
  std::time_t lastUpdated;
  // all metrics from SnmpColumn
  long snr;
  u_long mcs;
  long rssi;
};

using StatCacheMap = std::unordered_map<
    std::string /* local mac */,
    std::unordered_map<std::string /* remote mac */, RadioStat>>;

using KeyNameCacheMap =
    std::unordered_map<std::string /* raw metric name */, LinkMetric>;

/**
 * Holder for statistic mappings for fast lookups.
 */
class StatCache {
 public:
  explicit StatCache(){};

  /**
   * Returns a synchronized StatCacheMap.
   *
   * Maps local radio mac to a map of remote radio mac to a RadioStat.
   */
  static std::shared_ptr<folly::Synchronized<StatCacheMap>>
  getRadioStatsInstance();

  /**
   * Returns a synchronized KeyNameCacheMap.
   *
   * Maps raw stats names to a LinkMetric.
   */
  static std::shared_ptr<folly::Synchronized<KeyNameCacheMap>>
  getKeyNameCacheInstance();

  /**
   * Generate mapping of raw key names coming from driver-if to LinkMetric.
   *
   * Uses list of StatFormat to determine raw key names.
   */
  static std::unordered_map<std::string /* raw metric name */, LinkMetric>
  generateLinkKeys(
      const std::vector<StatFormat>& statsFormat,
      const std::unordered_map<std::string, thrift::LinkStatus>& linkStatus);
};

} // namespace terragraph
} // namespace facebook
