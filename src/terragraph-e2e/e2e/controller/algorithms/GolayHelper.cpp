/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "GolayHelper.h"

#include <algorithm>
#include <folly/Random.h>

#include "InterferenceHelper.h"
#include "LinkGroupHelper.h"

using std::invalid_argument;

namespace {
// Number of Golay codes (NOTE: starting at index 1!)
// Distinct valid range is [0,3]
const int kNumGolays{2};
} // namespace

namespace facebook {
namespace terragraph {

bool
GolayHelper::isValidLinkGolay(
    const std::optional<thrift::GolayIdx>& aGolayIdx,
    const std::optional<thrift::GolayIdx>& zGolayIdx) {
  return aGolayIdx && zGolayIdx && (aGolayIdx.value() == zGolayIdx.value()) &&
         aGolayIdx->txGolayIdx == aGolayIdx->rxGolayIdx;
}

bool
GolayHelper::assignLinkGolay(
    const TopologyWrapper& topologyW,
    ConfigHelper& configHelper,
    const thrift::Link& link) {
  if (link.link_type != thrift::LinkType::WIRELESS || link.a_node_mac.empty() ||
      link.z_node_mac.empty()) {
    return true;
  }

  auto aNodeName = link.a_node_name;
  auto zNodeName = link.z_node_name;
  auto aGolayIdx =
      configHelper.getLinkGolayIdx(aNodeName, link.z_node_mac, false);
  auto zGolayIdx =
      configHelper.getLinkGolayIdx(zNodeName, link.a_node_mac, false);

  // Check if current assignment is valid
  if (isValidLinkGolay(aGolayIdx, zGolayIdx)) {
    return true;
  }

  // Check if any side of the link has user-configured golay index
  if (aGolayIdx || zGolayIdx) {
    auto aUserGolayIdx =
        configHelper.getLinkGolayIdx(aNodeName, link.z_node_mac, true);
    auto zUserGolayIdx =
        configHelper.getLinkGolayIdx(zNodeName, link.a_node_mac, true);
    if (aUserGolayIdx && zUserGolayIdx) {
      // User configuration is bad
      return false;
    } else if (aUserGolayIdx && !zUserGolayIdx) {
      std::string errorMsg;
      return configHelper.setLinkGolay(link, aUserGolayIdx, false, errorMsg);
    } else if (!aUserGolayIdx && zUserGolayIdx) {
      std::string errorMsg;
      return configHelper.setLinkGolay(link, zUserGolayIdx, false, errorMsg);
    }
  }
  std::set<int> golayColors;
  for (int i = 1; i <= kNumGolays; ++i) {
    golayColors.insert(i);
  }

  // Lambda to fetch Golay config and translate to color
  std::function<std::optional<int>(thrift::Link, ConfigHelper&)>
      getGolayColor =
          [](const thrift::Link link, ConfigHelper& configHelper) ->
              std::optional<int> {
            auto golayConfig = configHelper.getLinkGolayIdx(link, false);
            // tx index should be the same as rx
            return golayConfig.has_value() ?
                std::make_optional((int)(golayConfig.value().txGolayIdx)) :
                std::nullopt;
          };
  auto golay = InterferenceHelper::getNewLinkColor(
      link,
      topologyW,
      configHelper,
      getGolayColor,
      golayColors);
  thrift::GolayIdx golayIdx;
  golayIdx.txGolayIdx = golay;
  golayIdx.rxGolayIdx = golay;

  LOG(INFO) << folly::sformat(
      "Assigned golayIdx {}/{} to {}", golay, golay, link.name);

  std::string errorMsg;
  return configHelper.setLinkGolay(link, golayIdx, false, errorMsg);
}

void
GolayHelper::getCurrentGolayConfig(
    const TopologyWrapper& topologyW,
    ConfigHelper& configHelper,
    const LinkGroupHelper::GroupNameToLinkNames& group2Links,
    bool clearUserConfig,
    InterferenceHelper::GroupName2Color& group2UserGolay,
    InterferenceHelper::GroupName2Color& group2AutoGolay) {
  for (const auto& groupIter : group2Links) {
    std::optional<int> groupUserGolay = std::nullopt;
    std::optional<int> groupAutoGolay = std::nullopt;
    for (const auto& linkName : groupIter.second) {
      auto link = topologyW.getLink(linkName);
      if (!link) {
        continue;
      }

      // Get user configured golays
      if (!clearUserConfig) {
        auto aGolayIdx = configHelper.getLinkGolayIdx(
            link->a_node_name, link->z_node_mac, true);
        auto zGolayIdx = configHelper.getLinkGolayIdx(
            link->z_node_name, link->a_node_mac, true);

        if ((aGolayIdx && zGolayIdx &&
             isValidLinkGolay(aGolayIdx, zGolayIdx)) ||
            (aGolayIdx && !zGolayIdx &&
             isValidLinkGolay(aGolayIdx, aGolayIdx)) ||
            (!aGolayIdx && zGolayIdx &&
             isValidLinkGolay(zGolayIdx, zGolayIdx))) {
          int userGolay =
              aGolayIdx ? aGolayIdx->txGolayIdx : zGolayIdx->txGolayIdx;
          if (groupUserGolay && userGolay != groupUserGolay.value()) {
            throw invalid_argument(
                "Incompatible user assigned golay index for link: " + linkName);
          }
          groupUserGolay = userGolay;
        } else if (aGolayIdx || zGolayIdx) {
          throw invalid_argument(
              "Invalid user assigned golay index for:  " + linkName);
        }
      }

      // Get auto configured golays
      auto aGolayIdx = configHelper.getLinkGolayIdx(
          link->a_node_name, link->z_node_mac, false);
      auto zGolayIdx = configHelper.getLinkGolayIdx(
          link->z_node_name, link->a_node_mac, false);

      if ((aGolayIdx && zGolayIdx && isValidLinkGolay(aGolayIdx, zGolayIdx)) ||
          (aGolayIdx && !zGolayIdx && isValidLinkGolay(aGolayIdx, aGolayIdx)) ||
          (!aGolayIdx && zGolayIdx && isValidLinkGolay(zGolayIdx, zGolayIdx))) {
        int autoGolay =
            aGolayIdx ? aGolayIdx->txGolayIdx : zGolayIdx->txGolayIdx;
        if (groupAutoGolay && autoGolay != groupAutoGolay.value()) {
          // Incompatible auto assigned golay index for link. Skip group
          groupAutoGolay = std::nullopt;
          break;
        }
        groupAutoGolay = autoGolay;
      } else if (aGolayIdx || zGolayIdx) {
        // Invalid auto assigned golay. Skip group
        groupAutoGolay = std::nullopt;
        break;
      }
    }

    if (groupUserGolay) {
      group2UserGolay[groupIter.first] = groupUserGolay.value();
    }
    if (groupAutoGolay) {
      group2AutoGolay[groupIter.first] = groupAutoGolay.value();
    }
  }
}

void
GolayHelper::assignNetworkGolay(
    const TopologyWrapper& topologyW,
    ConfigHelper& configHelper,
    bool clearUserConfig) {
  // Get link groups
  LinkGroupHelper::GroupNameToLinkNames group2Links =
      LinkGroupHelper::getLinkGroups(topologyW);

  // Get current golay configuration
  InterferenceHelper::GroupName2Color group2UserGolay;
  InterferenceHelper::GroupName2Color group2AutoGolay;
  getCurrentGolayConfig(
      topologyW,
      configHelper,
      group2Links,
      clearUserConfig,
      group2UserGolay,
      group2AutoGolay);

  std::set<int> golayChoices;
  for (int i = 1; i <= kNumGolays; ++i) {
    golayChoices.insert(i);
  }
  // Find optimized golay allocation
  auto assignedGolay = InterferenceHelper::allocateNewLinkColors(
      group2Links,
      topologyW,
      configHelper,
      group2UserGolay,
      group2AutoGolay,
      true, /* shouldAccountForChannel */
      golayChoices);

  // Group to link back fill
  std::string errorMsg;
  for (const auto& groupIt : group2Links) {
    auto golay = assignedGolay[groupIt.first];
    LOG(INFO) << folly::format(
        "Assign golay {} to group {}", golay, groupIt.first);
    for (const auto& linkName : groupIt.second) {
      auto link = topologyW.getLink(linkName);
      if (!link) {
        continue;
      }

      if (clearUserConfig) {
        // Clear user-configured golay
        configHelper.setLinkGolay(link.value(), std::nullopt, true, errorMsg);
      }
      thrift::GolayIdx golayIdx;
      golayIdx.txGolayIdx = golay;
      golayIdx.rxGolayIdx = golay;
      configHelper.setLinkGolay(
          link.value(),
          std::make_optional(golayIdx),
          false,
          errorMsg);
    }
  }
}

} // namespace terragraph
} // namespace facebook
