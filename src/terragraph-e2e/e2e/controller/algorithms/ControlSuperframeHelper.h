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

namespace facebook {
namespace terragraph {

/**
 * Control superframe assignment algorithms.
 */
class ControlSuperframeHelper {
 public:
  /**
   * Redo the control superframe allocation for the entire network.
   *
   * This functions respects user-configured values and avoids changing the
   * existing allocation as much as possible.
   *
   * When 'clearUserConfig' is set, the function clears all user-configured
   * values if the operation is successful.
   */
  static void assignAllControlSuperframes(
      const TopologyWrapper& topologyW,
      ConfigHelper& configHelper,
      bool clearUserConfig);

  /** Perform control superframe validation on the given link. */
  static bool isValidLinkControlSuperframe(
      const TopologyWrapper& topologyW,
      ConfigHelper& configHelper,
      const thrift::Link& link,
      std::optional<int64_t> aSframe,
      std::optional<int64_t> zSframe) noexcept;

  /** Assign a valid control superframe to the link, if possible. */
  static bool assignLinkControlSuperframe(
      const TopologyWrapper& topologyW,
      ConfigHelper& configHelper,
      const thrift::Link& link) noexcept;

 private:
  /** Returns the preferred control superframe assignment for the given link. */
  static int64_t getPreferredControlSuperframe(
      const TopologyWrapper& topologyW,
      const thrift::Link& link,
      const std::unordered_map<std::string, int64_t>& userConfigured,
      const std::unordered_map<std::string, int64_t>& autoConfigured,
      const std::unordered_map<std::string, std::unordered_set<std::string>>&
          mac2LinkNames,
      const std::unordered_map<std::string, int64_t>& assignedMap);
};

} // namespace terragraph
} // namespace facebook
