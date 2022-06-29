/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "../ConfigHelper.h"
#include "../topology/TopologyWrapper.h"
#include "InterferenceHelper.h"

namespace facebook {
namespace terragraph {

/** Default channel of transmission. */
const int8_t kDefaultChannel{2};

/**
 * Channel assignment algorithms.
 */
class ChannelHelper {
 public:
  /** Check if a provided channel falls within valid channel range. */
  static bool isValidChannel(int8_t channel);

  /**
   * Check if provided channels are valid across a wireless link.
   *
   * A link's channel configuration is valid if both ends have
   * a channel set and that channel is the same for both.
   */
  static bool isValidLinkChannel(
      const std::optional<int8_t>& aChannel,
      const std::optional<int8_t>& zChannel);

  /**
   * Assign a channel to a link if possible.
   *
   * If the radios have a valid channel assignment, this returns true.
   *
   * If one radio has a channel assigned and the other does not, the latter's
   * channel is set to the former's and the success of the set operation is
   * returned.
   *
   * If both radios have a configured channel, this returns false.
   *
   * If both radios are unconfigured, the link is set to kDefaultChannel.
   */
  static bool assignLinkChannel(
    const TopologyWrapper& topologyW,
    ConfigHelper& configHelper,
    const thrift::Link& link);

  /**
   * Reassign channel configurations across the entire network.
   *
   * The function assigns channel with the goal of assigning different channels
   * to link groups that have strong interference between them.
   *
   * This functions respects user-configured values and avoids changing the
   * existing allocation as much as possible.
   *
   * When 'clearUserConfig' is set, the function clears all user-configured
   * values if the operation is successful.
   */
  static void assignNetworkChannels(
      const TopologyWrapper& topologyW,
      ConfigHelper& configHelper,
      bool clearUserConfig);

  /**
   * Check if the current channel assignment is valid, and reassigns channels
   * across the topology if an invalid assignment is discovered.
   */
  static void validateTopologyChannels(
      const TopologyWrapper& topologyW, ConfigHelper& configHelper);

 private:
  /**
   * Check if any channels that have not been enabled are in use.
   *
   * Checks each link group's channel and returns true if any are not
   * enabled via the "topologyParams.enabledChannels" E2E configuration.
   */
  static bool disabledChannelInUse(
      const TopologyWrapper& topologyW, ConfigHelper& configHelper);

  /** Get existing user/auto-configured channels. */
  static void getCurrentChannelConfig(
      const TopologyWrapper& topologyW,
      ConfigHelper& configHelper,
      const LinkGroupHelper::GroupNameToLinkNames& group2Links,
      bool getUserConfig,
      InterferenceHelper::GroupName2Color& group2UserChannel,
      InterferenceHelper::GroupName2Color& group2AutoChannel);

  /**
   * Fetch and validate the channel for a provided link.
   *
   * Return the channel if valid, or throws error 'invalid_argument' otherwise.
   *
   * If 'groupChannel' is set, the channel of the input link must form a valid
   * link with that 'groupChannel' or it will be considered invalid.
   */
  static std::optional<int> getCurrentChannel(
      ConfigHelper& configHelper,
      thrift::Link link,
      std::optional<int> groupChannel,
      bool userConfigOnly);
};

} // namespace terragraph
} // namespace facebook
