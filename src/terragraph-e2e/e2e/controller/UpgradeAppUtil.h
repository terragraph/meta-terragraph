/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "CtrlApp.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/Topology_types.h"
#include "topology/TopologyWrapper.h"

namespace facebook {
namespace terragraph {

/**
 * Utilities for software upgrades on the E2E controller.
 * @see UpgradeApp
 */
class UpgradeAppUtil {
 public:
  /**
   * Returns a set of nodes to commit together.
   * @param topologyW the topology wrapper
   * @param nodesPendingUpgrade nodes ready to commit
   * @param limit maximum number of nodes in a batch (0 for unlimited)
   */
  static std::unordered_set<std::string> getCommitCandidates(
      const TopologyWrapper& topologyW,
      const std::unordered_set<std::string>& nodesPendingUpgrade,
      int limit);

  /** Dry-run a commit procedure and return the commit plan. */
  static thrift::UpgradeCommitPlan getCommitPlan(
      const TopologyWrapper& topologyW,
      int limit = 0,
      const std::vector<std::string>& excludeNodes = {});

  /** Get request description. */
  static std::string getReqDesc(const thrift::UpgradeGroupReq& ugReq);

  /** Check if a node has a pending commit. */
  static bool isPendingCommit(const thrift::StatusReport& statusReport);

  /** Check if a node already has the input version. */
  static bool hasCorrectVersion(
      const thrift::StatusReport& statusReport,
      const std::string& expectedVersion);

  /** Check if a node is already prepared to commit the given image. */
  static bool isPrepared(
      const thrift::StatusReport& statusReport,
      const thrift::UpgradeGroupReq& ugReq);

  /** Check if a node failed to prepare. */
  static bool prepareFailed(
      const thrift::StatusReport& statusReport,
      const thrift::UpgradeGroupReq& ugReq);

  /** Check if a node is ready to prepare. */
  static bool isPrepareReady(
      const thrift::StatusReport& report,
      const thrift::UpgradeGroupReq& ugReq,
      std::string& errorMsg);

  /** Check if a node is ready to commit. */
  static bool isCommitReady(
      const thrift::StatusReport& report,
      const thrift::UpgradeGroupReq& ugReq,
      std::string& errorMsg);

  /** Check if a node is up after reboot and is running the new version. */
  static bool commitSuccess(
      const thrift::StatusReport& report, const std::string& expectedVersion);

  /** Check if a node failed to commit. */
  static bool commitFailure(
      const thrift::StatusReport& report, const std::string& expectedVersion);

  /** Get a snapshot of all dead links in the topology. */
  static std::unordered_set<std::string> getDeadWirelessLinkNames(
      const TopologyWrapper& topologyW);

  /** Returns true if there are no dead, un-skipped wireless links in a list. */
  static bool wirelessLinkAlive(
      const std::vector<thrift::Link>& links,
      const std::unordered_set<std::string>& skipLinks);

  /**
   * Find online nodes on the given hardware with software versions older than
   * the target version, and return a list of node names.
   */
  static std::vector<std::string> findNodesToUpgradeToVersion(
      const std::string& targetVersion,
      const std::string& targetHardwareBoardId,
      const std::unordered_map<std::string, std::string>& nodeBlacklist,
      const TopologyWrapper& topologyW,
      const std::unordered_map<std::string, thrift::StatusReport>&
          minionToStatus);

  /**
   * Check if the given time is allowed under the given time restriction rule
   * (defined under thrift::GoldenImage::timeRestriction).
   *
   * Returns true if allowed, or false if disallowed.
   *
   * Throws std::invalid_argument if the rule is invalid.
   */
  static bool checkTimeRestriction(time_t t, const std::string& rule);
};

} // namespace terragraph
} // namespace facebook
