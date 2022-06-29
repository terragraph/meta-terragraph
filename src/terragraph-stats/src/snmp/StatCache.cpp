/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "StatCache.h"

#include <folly/Format.h>
#include <glog/logging.h>

namespace facebook {
namespace terragraph {

static folly::Singleton<folly::Synchronized<StatCacheMap>> statCache_;
static folly::Singleton<folly::Synchronized<KeyNameCacheMap>> keyNameCache_;

std::shared_ptr<folly::Synchronized<StatCacheMap>>
StatCache::getRadioStatsInstance() {
  return statCache_.try_get();
}

std::shared_ptr<folly::Synchronized<KeyNameCacheMap>>
StatCache::getKeyNameCacheInstance() {
  return keyNameCache_.try_get();
}

std::unordered_map<std::string /* raw metric name */, LinkMetric>
StatCache::generateLinkKeys(
    const std::vector<StatFormat>& statsFormat,
    const std::unordered_map<std::string, thrift::LinkStatus>& linkStatusMap) {
  std::unordered_map<std::string /* raw metric name */, LinkMetric> retKeys;
  // generate link stat name mappings
  for (const auto& linkStatusPair : linkStatusMap) {
    const thrift::LinkStatus& linkStatus = linkStatusPair.second;
    for (const auto& linkMetricFormat : statsFormat) {
      const std::string rawKeyName = folly::sformat(
          "{}.{}.{}",
          linkMetricFormat.keyPrefix,
          linkStatus.responderMac,
          linkMetricFormat.keyName);
      VLOG(1) << "Adding key name mapping: " << rawKeyName;
      retKeys[rawKeyName] = LinkMetric(
          linkMetricFormat.snmpColumn,
          linkStatus.ifname_ref().value(),
          linkStatus.radioMac_ref().value(),
          linkStatus.responderMac);
    }
  }
  return retKeys;
}

} // namespace terragraph
} // namespace facebook
