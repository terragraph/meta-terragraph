/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <e2e/if/gen-cpp2/Topology_types.h>

#include "../ConfigHelper.h"
#include "../topology/TopologyWrapper.h"
#include "e2e/common/EventClient.h"
#include "e2e/common/SimpleGraph.h"
#include "InterferenceHelper.h"

namespace facebook {
namespace terragraph {

/**
 * Golay code assignment algorithms.
 */
class GolayHelper {
 public:
  /** Check if the provided set of Golays are valid across a wireless link. */
  static bool isValidLinkGolay(
      const std::optional<thrift::GolayIdx>& aGolayIdx,
      const std::optional<thrift::GolayIdx>& zGolayIdx);

  /**
   * Assigns a Golay code to link, if possible.
   *
   * Refer to InterferenceHelper::getNewLinkColor() for implementation details.
   */
  static bool assignLinkGolay(
      const TopologyWrapper& topologyW,
      ConfigHelper& configHelper,
      const thrift::Link& link);

  /**
   * Reassign Golay codes across the entire network.
   *
   * The function implements a topology-based Golay code assignment algorithm
   * that reduces network-wide interference.
   *
   * This respects user-configured values and avoids changing the existing
   * allocation as much as possible.
   *
   * When 'clearUserConfig' is set, the function clears all user-configured
   * values if the operation is successful.
  */
  static void assignNetworkGolay(
      const TopologyWrapper& topologyW,
      ConfigHelper& configHelper,
      bool clearUserConfig);

 private:
  /** Get existing user/auto-configured Golay codes. */
  static void getCurrentGolayConfig(
      const TopologyWrapper& topologyW,
      ConfigHelper& configHelper,
      const LinkGroupHelper::GroupNameToLinkNames& group2Links,
      bool clearUserConfig,
      InterferenceHelper::GroupName2Color& group2UserGolay,
      InterferenceHelper::GroupName2Color& group2AutoGolay);
};

} // namespace terragraph
} // namespace facebook
