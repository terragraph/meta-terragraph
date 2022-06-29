/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <functional>

#include "../ConfigHelper.h"
#include "../topology/TopologyWrapper.h"
#include "e2e/common/SimpleGraph.h"
#include "LinkGroupHelper.h"

namespace facebook {
namespace terragraph {

/**
 * Interference estimation algorithms.
 *
 * @see GolayHelper
 * @see ChannelHelper
 */
class InterferenceHelper {
 public:
  /** Map from link group name to the color assigned to that link group. */
  using GroupName2Color = std::unordered_map<std::string, int>;

  /**
   * Derive the uni-directional graph from a topology.
   *
   * The graph vertices are links, and edges are whether they are connected
   * and the angular difference between two links.
   *
   * Format: {link_name: [(connected_link1, angle diff), ...], ...}
   */
  static std::
     unordered_map<std::string, std::vector<std::pair<std::string, double>>>
     createGraphWithLinkAngles(const TopologyWrapper& topologyW);

  /**
   * Allocate colors with the goal of coloring link groups with high estimated
   * interference.
   *
   * A "color" can be any integer value.
   *
   * Returns a mapping from group names to colors, representing the edge
   * coloring solution.
   *
   * @param group2Links the link groups
   * @param topologyW the network topology
   * @param configHelper the network configs
   * @param group2UserColor the current user-specified coloring of link groups
   * @param group2AutoColor the current auto coloring of link groups
   * @param shouldAccountForChannel if true, links on different channels
   *                                will be considered to have no interference
   * @param colors the set of color choices
   */
  static GroupName2Color allocateNewLinkColors(
      const LinkGroupHelper::GroupNameToLinkNames& group2Links,
      const TopologyWrapper& topologyW,
      ConfigHelper& configHelper,
      const GroupName2Color& group2UserColor,
      const GroupName2Color& group2AutoColor,
      const bool shouldAccountForChannel,
      const std::set<int> colors);

  /**
   * Returns a new link color.
   *
   * This implementation is based on a heuristic utilizing only the angular
   * separation of links and their connectivity graph.
   *
   * @param link the link
   * @param topologyW the network topology
   * @param configHelper the network configs
   * @param getLinkColor a lambda that returns the color (as an int) of a given
   *                     link in the topology
   * @param choices the set of color choices
   */
  static int getNewLinkColor(
      const thrift::Link& link,
      const TopologyWrapper& topologyW,
      ConfigHelper& configHelper,
      std::function<std::optional<int>(const thrift::Link&, ConfigHelper&)>
          getLinkColor,
      const std::set<int> choices);

 private:
  /**
   * Matrix that stores interference between link groups, with link group IDs
   * used as keys.
   */
  using InterferenceMatrix =
      std::unordered_map<std::string, std::unordered_map<std::string, float>>;

  /**
   * Compute the estimated interference matrix between all link groups.
   *
   * If `shouldAccountForChannel` is set to true, links on different channels
   * will be considered to have no interference.
   */
  static InterferenceMatrix getInterferenceMatrix(
      const TopologyWrapper& topologyW,
      ConfigHelper& configHelper,
      const LinkGroupHelper::GroupNameToLinkNames& group2Links,
      const bool shouldAccountForChannel);

  /** Check if the two power values are within 1dB of each other. */
  static bool almostEqualPower(float value1, float value2);

  /**
   * Build the group graph.
   *
   * Vertices are the link groups. An edge is added for every non-zero
   * group-to-group interference estimate.
   */
  static SimpleGraph buildGraph(
      const LinkGroupHelper::GroupNameToLinkNames& group2Links,
      const InterferenceMatrix& interferenceMatrix,
      std::unordered_map<std::string, float>& vertexWeights);

  /**
   * Returns the color for the group that will minimize interference with other
   * link groups.
   *
   * In the case of ties, the group's previous color is always preferred.
   *
   * @param groupId the link group ID
   * @param group2Assigned map of the in-progress coloring of the graph
   * @param interferenceMatrix matrix of the interference between groups
   * @param group2AutoColor the previous edge coloring
   * @param colors the set of possible colors
   */
  static int getPreferredLinkColor(
      const std::string& groupId,
      const GroupName2Color& group2Assigned,
      const InterferenceMatrix& interferenceMatrix,
      const GroupName2Color& group2AutoColor,
      const std::set<int> colors);

  /**
   * Compute the angle difference between two undirected links.
   *
   * Returns the angle in the range 0-90 degrees.
   *
   * @param angle the angle, in degrees
   * @param angleBase the angle base, in degrees
   * @param flip used to reflect the angle over the y-axis, if needed
   */
  static double computeUndirectedLinkAngleDiff(
      double angle, double angleBase, bool flip = false);

  /**
   * Compute the angle difference between two directed links.
   *
   * Returns the angle in the range 0-180 degrees.
   *
   * @param angle1 the first angle, in degrees
   * @param angle2 the second angle, in degrees
   */
  static double computeDirectedLinkAngleDiff(double angle1, double angle2);

  /** Estimate signal loss in dB due to array response. */
  static double getArrayLoss(double angle);

  /**
   * Estimate the Interference Received Signal Power (IRSP) and return a value
   * that is proportinal to it.
   *
   * 'angleTx' and 'angleRx' are the AoD and AoA of the interference beam
   * compared to the links under consideration.
   */
  static double getInterferenceEstimate(
      double distance, double angleTx, double angleRx);

  /**
   * Estimate the sum interference between two groups of links.
   *
   * If `shouldAccountForChannel` is set to true, links on different channels
   * will be considered to have no interference.
   */
  static double estimateGroup2GroupInterference(
      const TopologyWrapper& topologyW,
      ConfigHelper& configHelper,
      const std::unordered_set<std::string>& aLinks,
      const std::unordered_set<std::string>& bLinks,
      const bool shouldAccountForChannel);
};

} // namespace terragraph
} // namespace facebook
