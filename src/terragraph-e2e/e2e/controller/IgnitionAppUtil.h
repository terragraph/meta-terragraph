/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "CtrlApp.h"
#include <e2e/if/gen-cpp2/Topology_types.h>
#include <topology/TopologyWrapper.h>
#include <ConfigHelper.h>

namespace facebook {
namespace terragraph {

/**
 * Utilities for link ignition on the E2E controller.
 * @see IgnitionApp
 */
class IgnitionAppUtil {
 public:
  /**
   * Find all possible ignition candidates.
   *
   * This is called as the first step in findAllParallelIgnitionCandidates().
   * It is not used directly, except when trying to query ignition candidates
   * via API/CLI.
   */
  static std::vector<thrift::IgnitionCandidate> findAllIgnitionCandidates(
      const TopologyWrapper& topologyW,
      std::unordered_map<std::string, std::chrono::steady_clock::time_point>&
          cnToPossibleIgnitionTs,
      const std::unordered_map<
          std::string,
          std::chrono::steady_clock::time_point>& initiatorToAttemptTs,
      const std::unordered_map<
          std::string,
          std::pair<std::chrono::steady_clock::time_point, std::string>>&
              radioToLinkUpTs,
      std::chrono::seconds bfTimeout,
      std::chrono::seconds backupCnLinkInterval,
      std::chrono::seconds p2mpAssocDelay,
      std::unordered_map<std::string, size_t>& linkupIterationIndex,
      const std::unordered_set<std::string>& linkAutoIgniteOff = {});

  /**
   * Identify all ignition candidates for this cycle that can be attempted in
   * parallel.
   *
   * This takes into account various dampening intervals, ignition history,
   * possible interference, etc.
   */
  static std::vector<thrift::IgnitionCandidate>
    findAllParallelIgnitionCandidates(
        const TopologyWrapper& topologyW,
        std::unordered_map<std::string, std::chrono::steady_clock::time_point>&
            linkToAttemptTs,
        std::unordered_map<std::string, std::chrono::steady_clock::time_point>&
            linkToInitialAttemptTs,
        std::unordered_map<std::string, std::chrono::steady_clock::time_point>&
            cnToPossibleIgnitionTs,
        std::unordered_map<std::string, std::chrono::steady_clock::time_point>&
            initiatorToAttemptTs,
        const std::unordered_map<
            std::string,
            std::pair<std::chrono::steady_clock::time_point, std::string>>&
                radioToLinkUpTs,
        std::chrono::seconds bfTimeout,
        std::chrono::seconds dampenInterval,
        std::chrono::seconds extendedDampenInterval,
        std::chrono::seconds extendedDampenFailureInterval,
        std::chrono::seconds backupCnLinkInterval,
        std::chrono::seconds p2mpAssocDelay,
        std::unordered_map<std::string, size_t>& linkupIterationIndex,
        const std::unordered_set<std::string>& linkAutoIgniteOff = {});

  /**
   * Determine which links may be subject to interference from one of the
   * ignition candidate initiator nodes during initial beamforming.
   *
   * Returns the list of radios to send a slot exclusion command to.
   */
  static std::unordered_set<std::string> getExclusionSet(
      const TopologyWrapper& topologyW,
      const ConfigHelper& configHelper,
      std::vector<thrift::IgnitionCandidate>& igCandidates,
      int16_t excludeDistanceShort,
      int16_t excludeDistanceLong,
      int16_t excludeAngleRel);
};

} // namespace terragraph
} // namespace facebook
