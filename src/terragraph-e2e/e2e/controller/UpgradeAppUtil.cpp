/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "UpgradeAppUtil.h"

#include <algorithm>
#include <ctime>
#include <folly/MapUtil.h>

#include "ConfigHelper.h"
#include "GraphHelper.h"
#include "e2e/common/ConfigUtil.h"

using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {

thrift::UpgradeCommitPlan
UpgradeAppUtil::getCommitPlan(
    const TopologyWrapper& topologyW,
    int limit,
    const std::vector<std::string>& excludeNodes) {
  thrift::UpgradeCommitPlan commitPlan;
  auto nodeNames = folly::gen::from(topologyW.getAllNodes()) |
         folly::gen::field(&thrift::Node::name) |
         folly::gen::as<std::unordered_set<std::string>>();

  // Remove excluded nodes
  for (const auto& nodeName : excludeNodes) {
    nodeNames.erase(nodeName);
  }

  while (!nodeNames.empty()) {
    auto candidates = getCommitCandidates(topologyW, nodeNames, limit);
    if (candidates.empty()) {
      break;
    }
    for (const auto& nodeName : candidates) {
      nodeNames.erase(nodeName);
    }
    commitPlan.commitBatches.push_back(candidates);
  }
  return commitPlan;
}

std::unordered_set<std::string>
UpgradeAppUtil::getCommitCandidates(
    const TopologyWrapper& topologyW,
    const std::unordered_set<std::string>& nodesPendingUpgrade,
    int limit) {
  LOG(INFO) << folly::sformat(
      "Getting commit candidates from {} nodes with limit = {}",
      nodesPendingUpgrade.size(), limit);
  GraphHelper graphHelper;
  return graphHelper.getBatch(topologyW, nodesPendingUpgrade, limit);
}

std::string
UpgradeAppUtil::getReqDesc(const thrift::UpgradeGroupReq& ugReq) {
  auto urType = folly::get_default(
      TEnumMapFactory<thrift::UpgradeReqType>::makeValuesToNamesMap(),
      ugReq.urReq.urType, "UNKNOWN");

  auto ugType = folly::get_default(
      TEnumMapFactory<thrift::UpgradeGroupType>::makeValuesToNamesMap(),
      ugReq.ugType, "UNKNOWN");

  auto reqId = ugReq.urReq.upgradeReqId;

  return folly::sformat("{} {} request (Req id: {})", urType, ugType, reqId);
}

bool
UpgradeAppUtil::isPendingCommit(const thrift::StatusReport& statusReport) {
  const auto& uStatus = statusReport.upgradeStatus;
  return uStatus.usType == thrift::UpgradeStatusType::FLASHED &&
         uStatus.whenToCommit != 0;
}

bool
UpgradeAppUtil::hasCorrectVersion(
    const thrift::StatusReport& statusReport,
    const std::string& expectedVersion) {
  if (expectedVersion.empty() || statusReport.version.empty()) {
    return false;
  }

  auto curVersion = folly::trimWhitespace(statusReport.version);
  auto reqVersion = folly::trimWhitespace(expectedVersion);
  return curVersion == reqVersion;
}

bool
UpgradeAppUtil::isPrepared(
    const thrift::StatusReport& statusReport,
    const thrift::UpgradeGroupReq& ugReq) {
  const auto& uStatus = statusReport.upgradeStatus;
  return uStatus.usType == thrift::UpgradeStatusType::FLASHED &&
         uStatus.nextImage.md5 == ugReq.urReq.md5;
}

bool
UpgradeAppUtil::isPrepareReady(
    const thrift::StatusReport& report,
    const thrift::UpgradeGroupReq& ugReq,
    std::string& errorMsg) {
  if (isPendingCommit(report)) {
    errorMsg = "Node is pending commit";
    return false;
  }

  if (hasCorrectVersion(report, ugReq.version)) {
    errorMsg = "Node has new image";
    return false;
  }

  if (UpgradeAppUtil::isPrepared(report, ugReq)) {
    errorMsg = "Node already prepared";
    return false;
  }

  // Check if hardware board ID matches any in the given list
  // NOTE: For compatibility, skip this check if the field is missing or the
  //       node didn't report its hardware board ID
  if (ugReq.urReq.hardwareBoardIds.size() && !report.hardwareBoardId.empty()) {
    auto iter = std::find(
        ugReq.urReq.hardwareBoardIds.begin(),
        ugReq.urReq.hardwareBoardIds.end(),
        report.hardwareBoardId);
    if (iter == ugReq.urReq.hardwareBoardIds.end()) {
      errorMsg = "Hardware board ID mismatch";
      return false;
    }
  }

  return true;
}

bool
UpgradeAppUtil::prepareFailed(
    const thrift::StatusReport& statusReport,
    const thrift::UpgradeGroupReq& ugReq) {
  const auto& uStatus = statusReport.upgradeStatus;
  return (uStatus.upgradeReqId == ugReq.urReq.upgradeReqId) &&
         (uStatus.usType == thrift::UpgradeStatusType::DOWNLOAD_FAILED ||
          uStatus.usType == thrift::UpgradeStatusType::FLASH_FAILED);
}

bool
UpgradeAppUtil::isCommitReady(
    const thrift::StatusReport& report,
    const thrift::UpgradeGroupReq& ugReq,
    std::string& errorMsg) {
  const auto& uStatus = report.upgradeStatus;

  if (hasCorrectVersion(report, ugReq.version)) {
    errorMsg = "Node has new image";
    return false;
  }
  if (uStatus.usType != thrift::UpgradeStatusType::FLASHED) {
    errorMsg = folly::sformat(
        "Node upgrade status error: {}",
        folly::get_default(TEnumMapFactory<thrift::UpgradeStatusType>::
            makeValuesToNamesMap(), report.upgradeStatus.usType, "UNKNOWN"));
    return false;
  }
  if (!ugReq.version.empty() && ugReq.version != uStatus.nextImage.version) {
    errorMsg = folly::sformat(
        "Node is flashed with incorrect version (Current: {} Next: {})",
        ugReq.version,
        report.upgradeStatus.nextImage.version);
    return false;
  }
  return true;
}

bool
UpgradeAppUtil::commitSuccess(
    const thrift::StatusReport& report, const std::string& expectedVersion) {
  const auto& uStatus = report.upgradeStatus;
  return uStatus.usType == thrift::UpgradeStatusType::NONE &&
         (expectedVersion.empty() ||
          hasCorrectVersion(report, expectedVersion));
}

bool
UpgradeAppUtil::commitFailure(
    const thrift::StatusReport& report, const std::string& expectedVersion) {
  const auto& uStatus = report.upgradeStatus;
  return uStatus.usType == thrift::UpgradeStatusType::COMMIT_FAILED ||
         (uStatus.usType == thrift::UpgradeStatusType::NONE &&
          !expectedVersion.empty() &&
          !hasCorrectVersion(report, expectedVersion));
}

std::unordered_set<std::string>
UpgradeAppUtil::getDeadWirelessLinkNames(const TopologyWrapper& topologyW) {
  return folly::gen::from(topologyW.getAllLinks()) |
         folly::gen::filter([](const auto link) {
           return link.link_type == thrift::LinkType::WIRELESS &&
                  !link.is_alive;
         }) |
         folly::gen::field(&thrift::Link::name) |
         folly::gen::as<std::unordered_set<std::string>>();
}

bool
UpgradeAppUtil::wirelessLinkAlive(
    const std::vector<thrift::Link>& links,
    const std::unordered_set<std::string>& skipLinks) {
  int wirelessLinks = 0;
  for (auto const& link : links) {
    if (link.link_type == thrift::LinkType::WIRELESS) {
      wirelessLinks++;
      if (link.is_alive || skipLinks.count(link.name)) {
        return true;
      }
    }
  }

  return wirelessLinks == 0;
}

std::vector<std::string>
UpgradeAppUtil::findNodesToUpgradeToVersion(
    const std::string& targetVersion,
    const std::string& targetHardwareBoardId,
    const std::unordered_map<std::string, std::string>& nodeBlacklist,
    const TopologyWrapper& topologyW,
    const std::unordered_map<std::string, thrift::StatusReport>&
        minionToStatus) {
  std::vector<std::string> nodesToUpgrade;

  // Find nodes to upgrade (version < golden image version)
  auto goldenVersion = SwVersion(targetVersion);
  for (const auto& kv : minionToStatus) {
    const thrift::StatusReport& report = kv.second;

    // Check hardware board ID
    if (report.hardwareBoardId != targetHardwareBoardId) {
      continue;  // unsupported hardware board ID
    }

    // Check version
    auto nodeVersion = SwVersion(report.version);
    if (nodeVersion >= goldenVersion) {
      continue;  // not an old version
    }

    // Look up node in topology
    auto maybeNode = topologyW.getNodeByMac(kv.first);
    if (!maybeNode) {
      continue;  // not in topology
    }
    if (maybeNode->status == thrift::NodeStatusType::OFFLINE) {
      continue;  // node is offline
    }

    // Check if this node-version pair is blacklisted
    auto iter = nodeBlacklist.find(maybeNode->name);
    if (iter != nodeBlacklist.end() && iter->second == targetVersion) {
      continue;  // blacklisted
    }

    nodesToUpgrade.push_back(maybeNode->name);
  }

  return nodesToUpgrade;
}

bool
UpgradeAppUtil::checkTimeRestriction(time_t t, const std::string& rule) {
  if (rule.empty()) {
    return true;  // empty = no restrictions
  }

  // Validate config. Format:
  //   <days>:<timeframe>:<timezone>
  //   <any|mon,tue,wed,thu,fri,sat,sun>:<any|hhmm-hhmm>:<utc|local>
  std::vector<std::string> tokens;
  folly::split(":", rule, tokens);
  if (tokens.size() != 3) {
    throw std::invalid_argument("Time restriction format is incorrect");
  }
  const std::string& days = tokens[0];
  const std::string& timeframe = tokens[1];
  const std::string& timezone = tokens[2];

  // Get current time in the given timezone
  tm tTm;
  if (timezone == "utc") {
    tTm = *gmtime(&t);
  } else if (timezone == "local") {
    tTm = *localtime(&t);
  } else {
    throw std::invalid_argument(
        "Time restriction timezone format is incorrect");
  }

  // Check day
  if (days != "any") {
    // Convert to C day-of-week format (tm_wday: days since Sunday)
    const std::vector<std::string> kDayOfWeek =
        {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
    std::unordered_set<int> allowedDays;
    std::vector<std::string> dayList;
    folly::split(",", days, dayList);
    for (const std::string& dayStr : dayList) {
      auto iter = std::find(kDayOfWeek.begin(), kDayOfWeek.end(), dayStr);
      if (iter == kDayOfWeek.end()) {
        throw std::invalid_argument(
            "Time restriction days format is incorrect");
      }
      allowedDays.insert(std::distance(kDayOfWeek.begin(), iter));
    }

    // Check against rule
    if (!allowedDays.count(tTm.tm_wday)) {
      return false;
    }
  }

  // Check timeframe
  if (timeframe != "any") {
    // Convert 24-hour time to a decimal number (hh * 100 + mm)
    std::vector<std::string> fromTo;
    folly::split("-", timeframe, fromTo);
    if (fromTo.size() != 2 ||
        fromTo[0].length() != 4 ||
        fromTo[1].length() != 4) {
      throw std::invalid_argument(
          "Time restriction timeframe format is incorrect");
    }
    auto fromTime = folly::tryTo<int>(fromTo[0]);
    auto toTime = folly::tryTo<int>(fromTo[1]);
    if (!fromTime.hasValue() || !toTime.hasValue()) {
      throw std::invalid_argument(
          "Time restriction timeframe format is incorrect");
    }

    // Check against rule
    int timeNow = tTm.tm_hour * 100 + tTm.tm_min;
    if (timeNow < fromTime.value() || timeNow > toTime.value()) {
      return false;
    }
  }

  return true;
}

} // namespace terragraph
} // namespace facebook
