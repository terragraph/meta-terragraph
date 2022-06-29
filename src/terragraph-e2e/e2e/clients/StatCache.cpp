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

std::unordered_map<std::string /* raw metric name */, RadioMetric>
StatCache::generateLinkKeys(
    const std::vector<StatFormat>& statsFormat,
    const thrift::LinkStatus& linkStatus) {
  std::unordered_map<std::string /* raw metric name */, RadioMetric> retKeys;
  // generate link stat name mappings
  for (const auto& linkMetricFormat : statsFormat) {
    const std::string rawKeyName = folly::sformat(
        "{}.{}.{}",
        linkMetricFormat.keyPrefix,
        linkStatus.responderMac,
        linkMetricFormat.keyName);
    VLOG(1) << "Adding key name mapping: " << rawKeyName << " ("
            << linkMetricFormat.shortName << ")";
    retKeys[rawKeyName] = RadioMetric(
        linkStatus.radioMac_ref().value(),
        linkStatus.responderMac,
        linkMetricFormat.shortName);
  }
  return retKeys;
}

} // namespace terragraph
} // namespace facebook
