/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ChannelHelper.h"

#include <folly/Random.h>

#include "InterferenceHelper.h"
#include "LinkGroupHelper.h"
#include "SharedObjects.h"

using std::invalid_argument;

namespace facebook {
namespace terragraph {

// valid channel config range
const int kMinChannel{1};
const int kMaxChannel{4};

bool
ChannelHelper::isValidChannel(int8_t channel) {
  return kMinChannel <= channel && channel <= kMaxChannel;
}

bool
ChannelHelper::isValidLinkChannel(
    const std::optional<int8_t>& aChannel,
    const std::optional<int8_t>& zChannel) {
  return aChannel && zChannel && aChannel.value() == zChannel.value();
}

bool
ChannelHelper::assignLinkChannel(
  const TopologyWrapper& topologyW,
  ConfigHelper& configHelper,
  const thrift::Link& link) {

  if (link.link_type != thrift::LinkType::WIRELESS ||
      link.a_node_mac.empty() ||
      link.z_node_mac.empty()) {
    return true;
  }

  auto aChannel = configHelper.getRadioChannel(
      link.a_node_name, link.a_node_mac, false, false);
  auto zChannel = configHelper.getRadioChannel(
      link.z_node_name, link.z_node_mac, false, false);

  // Check if current channel assignment is valid
  if (isValidLinkChannel(aChannel, zChannel)) {
    return true;
  }

  // Check if only one side has channel set (e.g. a P2MP radio)
  if (aChannel && !zChannel) {
    std::string errorMsg;
    return configHelper.setLinkChannel(link, aChannel, false, errorMsg);
  } else if (!aChannel && zChannel) {
    std::string errorMsg;
    return configHelper.setLinkChannel(link, zChannel, false, errorMsg);
  }

  // Check if any side of the link has user-configured channel specifically
  // (we won't re-assign bad user configs here, but may override auto configs)
  if (aChannel || zChannel) {
    auto aUserChannel = configHelper.getRadioChannel(
        link.a_node_name, link.a_node_mac, true, false);
    auto zUserChannel = configHelper.getRadioChannel(
        link.z_node_name, link.z_node_mac, true, false);

    if (aUserChannel && zUserChannel) {
      // User configuration is bad (if it wasn't, it would have been valid)
      return false;
    } else if (aUserChannel && !zUserChannel) {
      std::string errorMsg;
      return configHelper.setLinkChannel(link, aUserChannel, false, errorMsg);
    } else if (!aUserChannel && zUserChannel) {
      std::string errorMsg;
      return configHelper.setLinkChannel(link, zUserChannel, false, errorMsg);
    }
  }

  std::set<int8_t> enabledChannels = configHelper.getEnabledChannels();
  std::set<int> channelColors;
  for (const auto& channel : enabledChannels) {
    channelColors.insert(channel);
  }

  // Lambda to fetch channel config and translate to color
  std::function<std::optional<int>(const thrift::Link&, ConfigHelper&)>
      getChannelColor =
          [](const thrift::Link& link, ConfigHelper& configHelper) ->
              std::optional<int> {
            auto channelConfig = configHelper.getLinkChannel(
                link, false, false);
            return channelConfig.has_value() ?
                std::make_optional((int) channelConfig.value()) : std::nullopt;
          };
  auto channel = InterferenceHelper::getNewLinkColor(
      link, topologyW, configHelper, getChannelColor, channelColors);

  LOG(INFO) << folly::sformat("Assigned channel {} to {}", channel, link.name);

  std::string errorMsg;
  return configHelper.setLinkChannel(link, channel, false, errorMsg);
}

void
ChannelHelper::assignNetworkChannels(
    const TopologyWrapper& topologyW,
    ConfigHelper& configHelper,
    bool clearUserConfig) {
  LinkGroupHelper::GroupNameToLinkNames group2Links =
      LinkGroupHelper::getLinkGroups(topologyW);
  std::set<int8_t> enabledChannels = configHelper.getEnabledChannels();
  // Get current channel configuration
  InterferenceHelper::GroupName2Color group2UserChannel;
  InterferenceHelper::GroupName2Color group2AutoChannel;
  getCurrentChannelConfig(
      topologyW,
      configHelper,
      group2Links,
      !clearUserConfig /* useUserConfig */,
      group2UserChannel,
      group2AutoChannel);
  std::set<int> channelChoices;
  for (const auto& channel : enabledChannels) {
    channelChoices.insert((int)channel);
  }

  // Find channel allocation that will minimize estimated interference
  auto group2AssignedColor = InterferenceHelper::allocateNewLinkColors(
      group2Links,
      topologyW,
      configHelper,
      group2UserChannel,
      group2AutoChannel,
      false, /* shouldAccountForChannel */
      channelChoices);

  for (const auto& groupIt : group2Links) {
    int8_t channel = group2AssignedColor[groupIt.first];
    // Assign all nodes in link group to channel
    LOG(INFO) << folly::format(
        "Assign channel {} to group {}", channel, groupIt.first);
    for (const auto& linkName : groupIt.second) {
      auto link = topologyW.getLink(linkName);
      if (!link) {
        continue;
      }

      std::string errorMsg;
      if (clearUserConfig) {
        // Clear user config
        configHelper.setLinkChannel(link.value(), std::nullopt, true, errorMsg);
      }
      configHelper.setLinkChannel(
          link.value(), std::make_optional(channel), false, errorMsg);
    }
  }
}

void
ChannelHelper::validateTopologyChannels(
    const TopologyWrapper& topologyW, ConfigHelper& configHelper) {
  if (disabledChannelInUse(topologyW, configHelper)) {
    // Reassign channels across topology
    assignNetworkChannels(topologyW, configHelper, false);
  }
}

bool
ChannelHelper::disabledChannelInUse(
    const TopologyWrapper& topologyW, ConfigHelper& configHelper) {
  std::set<int8_t> usedChannels = configHelper.getUsedChannels(topologyW);
  std::set<int8_t> enabledChannels = configHelper.getEnabledChannels();

  std::set<int8_t> usedAndDisabled;
  std::set_difference(
      usedChannels.begin(), usedChannels.end(),
      enabledChannels.begin(), enabledChannels.end(),
      std::inserter(usedAndDisabled, usedAndDisabled.end()));

  return !usedAndDisabled.empty();
}

void
ChannelHelper::getCurrentChannelConfig(
    const TopologyWrapper& topologyW,
    ConfigHelper& configHelper,
    const LinkGroupHelper::GroupNameToLinkNames& group2Links,
    bool getUserConfig,
    InterferenceHelper::GroupName2Color& group2UserChannel,
    InterferenceHelper::GroupName2Color& group2AutoChannel) {
  for (const auto& groupIter : group2Links) {
    std::optional<int> groupUserChannel = std::nullopt;
    std::optional<int> groupAutoChannel = std::nullopt;
    for (const auto& linkName : groupIter.second) {
      auto link = topologyW.getLink(linkName);
      if (!link) {
        continue;
      }

      // Get user configured channels
      if (getUserConfig) {
        groupUserChannel = getCurrentChannel(
            configHelper,
            link.value(),
            groupUserChannel,
            true /* userConfigOnly */);
      }
      groupAutoChannel = getCurrentChannel(
          configHelper,
          link.value(),
          groupAutoChannel,
          false /* userConfigOnly */);
      // groupAutoChannel will only be set to null after an invalid
      // configuration is encountered, so don't add group to map.
      if (!groupAutoChannel) {
        break;
      }
    }
    if (groupUserChannel) {
      group2UserChannel[groupIter.first] = groupUserChannel.value();
    }
    if (groupAutoChannel) {
      group2AutoChannel[groupIter.first] = groupAutoChannel.value();
    }
  }
}

std::optional<int>
ChannelHelper::getCurrentChannel(
    ConfigHelper& configHelper,
    thrift::Link link,
    std::optional<int> groupChannel,
    bool userConfigOnly) {
  auto aChannel = configHelper.getRadioChannel(
      link.a_node_name,
      link.a_node_mac,
      userConfigOnly,
      false /* autoConfigOnly */);
  auto zChannel = configHelper.getRadioChannel(
      link.z_node_name,
      link.z_node_mac,
      userConfigOnly,
      false /* autoConfigOnly */);

  if (isValidLinkChannel(aChannel, zChannel) || (!aChannel != !zChannel)) {
    auto channel = configHelper.getLinkChannel(link, userConfigOnly, false);
    if (groupChannel && channel && channel.value() != groupChannel.value()) {
      throw invalid_argument(
          "Incompatible assigned channel for link: " + link.name);
    }
    if (channel) {
      groupChannel = channel.value();
    }
  } else if (aChannel || zChannel) {
    throw invalid_argument(
        "Incompatible assigned channel for link: " + link.name);
  }

  return groupChannel;
}

} // namespace terragraph
} // namespace facebook
