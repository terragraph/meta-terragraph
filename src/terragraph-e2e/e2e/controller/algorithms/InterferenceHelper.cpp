/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "InterferenceHelper.h"

#include <algorithm>
#include <cmath>
#include <folly/Random.h>

#include <e2e/if/gen-cpp2/Topology_types.h>

#include "LinkGroupHelper.h"
#include "OccSolver.h"
#include "PolarityHelper.h"

namespace {
// Parameter used to tune the color assignment algorithm for a new link,
// default = 10
const int kColorWeight{10};
// Distance at which interference power drops to below -80 dBm with perfect
// alignment
const int kMaxInterferenceDistance{500};
// Minimum estimated interference. Any interference below this level is ignored
// by algorithm to assign color for a new link
const int kMinInterferenceLevel{-80};
// Threshold large angle value between two links (in degrees)
const int kObserveLargeAngDiff{50};
// Threshold narrow angle value between two links (in degrees)
const int kObserveNarrowAngDiff{20};
} // namespace

namespace facebook {
namespace terragraph {

std::unordered_map<std::string, std::vector<std::pair<std::string, double>>>
InterferenceHelper::createGraphWithLinkAngles(
    const TopologyWrapper& topologyW) {
  std::unordered_map<std::string, std::vector<std::pair<std::string, double>>>
      graph;

  std::vector<thrift::Link> links = topologyW.getAllLinks();
  for (const auto& aLink : links) {
    // Skip links that are not wireless
    if (aLink.link_type != thrift::LinkType::WIRELESS) {
      continue;
    }

    auto aLinkANode = topologyW.getNode(aLink.a_node_name);
    auto aLinkZNode = topologyW.getNode(aLink.z_node_name);
    std::string aLinkASiteName = aLinkANode->site_name;
    std::string aLinkZSiteName = aLinkZNode->site_name;

    auto l1 = topologyW.getSite(aLinkASiteName)->location;
    auto l2 = topologyW.getSite(aLinkZSiteName)->location;
    double aLinkAng = computeAngle(l1, l2);

    for (const auto& bLink : links) {
      // Skip links that are not wireless
      if (bLink.link_type != thrift::LinkType::WIRELESS) {
        continue;
      }

      // Skip if same link
      if (aLink.name == bLink.name) {
        continue;
      }

      auto bLinkANode = topologyW.getNode(bLink.a_node_name);
      auto bLinkZNode = topologyW.getNode(bLink.z_node_name);
      std::string bLinkASiteName = bLinkANode->site_name;
      std::string bLinkZSiteName = bLinkZNode->site_name;

      if (aLinkASiteName != bLinkASiteName &&
          aLinkASiteName != bLinkZSiteName &&
          aLinkZSiteName != bLinkASiteName &&
          aLinkZSiteName != bLinkZSiteName) {
        continue;
      }

      l1 = topologyW.getSite(bLinkASiteName)->location;
      l2 = topologyW.getSite(bLinkZSiteName)->location;
      double bLinkAng = computeAngle(l1, l2);

      bool flip = false;
      if (aLinkASiteName == bLinkASiteName ||
          aLinkZSiteName == bLinkZSiteName) {
        flip = false;
      } else if (
          aLinkZSiteName == bLinkASiteName ||
          aLinkASiteName == bLinkZSiteName) {
        flip = true;
      }

      double angleDiff =
          computeUndirectedLinkAngleDiff(aLinkAng, bLinkAng, flip);

      graph[aLink.name].push_back(std::make_pair(bLink.name, angleDiff));
    }
  }

  return graph;
}

InterferenceHelper::InterferenceMatrix
InterferenceHelper::getInterferenceMatrix(
    const TopologyWrapper& topologyW,
    ConfigHelper& configHelper,
    const LinkGroupHelper::GroupNameToLinkNames& group2Links,
    const bool shouldAccountForChannel) {
  InterferenceHelper::InterferenceMatrix interferenceMatrix;
  std::unordered_set<std::pair<std::string, std::string>> alreadyComputed;
  for (const auto& aGroupIt : group2Links) {
    for (const auto& bGroupIt : group2Links) {
      if (aGroupIt.first == bGroupIt.first ||
          alreadyComputed.count(
              std::make_pair(bGroupIt.first, aGroupIt.first))) {
        continue;
      }

      auto interference = estimateGroup2GroupInterference(
          topologyW, configHelper, aGroupIt.second, bGroupIt.second,
          shouldAccountForChannel);
      if (interference > 0) {
        // interferenceMatrix is symmetric
        VLOG(3) << folly::format(
            "Interference between groups {} and {} is {}",
            aGroupIt.first,
            bGroupIt.first,
            interference);
        interferenceMatrix[aGroupIt.first][bGroupIt.first] = interference;
        interferenceMatrix[bGroupIt.first][aGroupIt.first] = interference;
      }

      alreadyComputed.insert(std::make_pair(aGroupIt.first, bGroupIt.first));
    }
  }

  return interferenceMatrix;
}

bool
InterferenceHelper::almostEqualPower(float value1, float value2) {
  // Add bias to divisor to avoid division by 0
  float ratio = std::max(value1, value2) / (std::min(value1, value2) + .0001);
  return ratio < 1.2589;
}

SimpleGraph
InterferenceHelper::buildGraph(
    const LinkGroupHelper::GroupNameToLinkNames& group2Links,
    const InterferenceHelper::InterferenceMatrix& interferenceMatrix,
    std::unordered_map<std::string, float>& vertexWeights) {
  SimpleGraph groupsGraph = SimpleGraph(false); // undirected graph
  // Add groups as vertices
  for (const auto& groupIter : group2Links) {
    groupsGraph.addVertex(groupIter.first);
  }

  // Add group-to-group interference as edges
  for (const auto& it1 : interferenceMatrix) {
    float vertexWeight = 0.0;
    for (const auto& it2 : it1.second) {
      if (it2.second > 0) {
        groupsGraph.addEdge(it1.first, it2.first);
        vertexWeight += it2.second;
      }
    }
    vertexWeights[it1.first] = vertexWeight;
  }

  return groupsGraph;
}

int
InterferenceHelper::getPreferredLinkColor(
    const std::string& groupId,
    const GroupName2Color& group2AssignedColor,
    const InterferenceMatrix& interferenceMatrix,
    const GroupName2Color& group2AutoColor,
    const std::set<int> colors) {

  if (colors.size() == 1) {
    return *colors.begin();
  }

  std::map<int, float> color2Cost;
  // Initial cost for all choices is 0
  for (const int& color : colors) {
    color2Cost.emplace(color, 0.0);
  }

  // Check for previous allocation
  std::optional<int> previousColor = std::nullopt;
  auto iter = group2AutoColor.find(groupId);
  if (iter != group2AutoColor.end()) {
    previousColor = iter->second;
  }

  auto currentGroupIter = interferenceMatrix.find(groupId);
  if (currentGroupIter == interferenceMatrix.end()) {
    // Return existing color if present and was passed in the set of choices.
    if (previousColor && colors.count(previousColor.value())) {
      return previousColor.value();
    }
    return *colors.begin();
  }

  // Find best color allocation by checking all allocated neighbors
  for (const auto& neighborGroupIter : currentGroupIter->second) {
    auto group2AssignedColorIter =
        group2AssignedColor.find(neighborGroupIter.first);
    if (group2AssignedColorIter == group2AssignedColor.end()) {
      continue;
    }
    int neighborAssignedColor = group2AssignedColorIter->second;
    color2Cost[neighborAssignedColor] += neighborGroupIter.second;
  }

  std::pair<int, float> minColorCost = *color2Cost.begin();
  for (const auto& colorCostIter : color2Cost) {
    if (colorCostIter.second < minColorCost.second) {
      minColorCost = colorCostIter;
    }
  }

  // Always prefer existing color if it has same cost as minimum
  if (previousColor && color2Cost.count(previousColor.value())) {
    float previousColorCost = color2Cost[previousColor.value()];
    if (almostEqualPower(previousColorCost, minColorCost.second)) {
      return previousColor.value();
    }
  }

  return minColorCost.first;
}

int
InterferenceHelper::getNewLinkColor(
    const thrift::Link& link,
    const TopologyWrapper& topologyW,
    ConfigHelper& configHelper,
    std::function<std::optional<int>(const thrift::Link&, ConfigHelper&)>
        getLinkColor,
    const std::set<int> colors) {
  // choices contains all viable color options for the new link.
  // It is modified by the algorithm and, at the end, a random element is chosen
  // to be the link's color.
  std::vector<int> choices(colors.begin(), colors.end());

  auto aNodeName = link.a_node_name;
  auto zNodeName = link.z_node_name;

  // Angle-based connectivity graph of wireless links
  auto graph = InterferenceHelper::createGraphWithLinkAngles(topologyW);

  for (const auto& e : graph[link.name]) {
    std::string adjLinkName = e.first;
    double angDiff = e.second;
    auto adjLink = topologyW.getLink(adjLinkName);
    if (!adjLink) {
      continue;
    }

    auto adjColorConfig = getLinkColor(adjLink.value(), configHelper);
    if (!adjColorConfig) {
      continue;
    }
    int adjColor = adjColorConfig.value();

    if (!colors.count(adjColor)) {
      // Color not present in set of options
      continue;
    }

    // Observation 2:
    // Adjacent links with > kObserveLargeAngDiff deg angles do not show much
    // interference
    if (angDiff > kObserveLargeAngDiff) {
      choices.insert(choices.end(), kColorWeight, adjColor);
    }

    std::string adjANodeName = adjLink->a_node_name;
    std::string adjZNodeName = adjLink->z_node_name;

    // Check y-street based on a-z node names rather than angle
    // force already set y-street links to be the same channel
    if (adjANodeName == aNodeName || adjANodeName == zNodeName ||
        adjZNodeName == aNodeName || adjZNodeName == zNodeName) {
      choices = {adjColor};
      break;
    }

    // Observation 1
    // Adjacent links with < kObserveNarrowAngDiff deg angle may have
    // interference, regardless of polarity (e.g., alpha turn setup)
    if (angDiff < kObserveNarrowAngDiff) {
      choices.erase(
          std::remove(choices.begin(), choices.end(), adjColor), choices.end());
    }

    // Observation 4:
    // Two links with 1 link in between will have interference (between the
    // initial sector of the first link and last sector of the last link)
    // - We should not have three consecutive links having the same color
    // - And it can't be in the form of 1-2-1 (considering polarities - Note
    //   below). We should either have 1-1-2 or 1-2-2 format.
    //
    // Note: We assume same polarity on the same pole (at least for initial
    //         ignition).
    for (const auto& eAdj : graph[adjLinkName]) {
      const std::string& nextLinkName = eAdj.first;
      // Skip if current link
      if (nextLinkName == link.name) {
        continue;
      }

      auto nextLink = topologyW.getLink(nextLinkName);
      if (!nextLink) {
        continue;
      }

      auto nextColorConfig = getLinkColor(nextLink.value(), configHelper);
      if (!nextColorConfig) {
        continue;
      }
      int nextColor = nextColorConfig.value();

      if (!colors.count(nextColor)) {
        // Color not present in set of options
        continue;
      }
      // Skip if is one of current neighbors
      bool isNextLinkNeighbor = false;
      for (const auto& neighbors : graph[link.name]) {
        if (nextLinkName == neighbors.first) {
          isNextLinkNeighbor = true;
        }
      }
      if (isNextLinkNeighbor) {
        continue;
      }

      if (nextColor == adjColor) {
        // Never have three consecutive links with same color
        choices.erase(
            std::remove(choices.begin(), choices.end(), nextColor),
            choices.end());
      } else {
        // Otherwise prefer same color as adjacent link
        choices.insert(choices.end(), kColorWeight, nextColor);
      }
    }
  }

  // If there are no more choices after the algorithm, repopulate it with all
  // the coloring options.
  if (choices.empty()) {
    for (const auto& color : colors) {
      choices.push_back(color);
    }
  }

  // Assign this link a color at random from choices
  auto pickIdx = folly::Random::rand32(choices.size());
  auto color = choices[pickIdx];

  return color;
}

InterferenceHelper::GroupName2Color
InterferenceHelper::allocateNewLinkColors(
    const LinkGroupHelper::GroupNameToLinkNames& group2Links,
    const TopologyWrapper& topologyW,
    ConfigHelper& configHelper,
    const GroupName2Color& group2UserColor,
    const GroupName2Color& group2AutoColor,
    const bool shouldAccountForChannel,
    const std::set<int> colors) {
  GroupName2Color group2AssignedColor;
  std::deque<std::string> visitStack;

  // Find group interference matrix
  auto interferenceMatrix = InterferenceHelper::getInterferenceMatrix(
      topologyW,
      configHelper,
      group2Links,
      shouldAccountForChannel);

  // Build graph for OCC algorithm
  std::unordered_map<std::string, float> groupInterference;
  SimpleGraph groupsGraph = InterferenceHelper::buildGraph(
      group2Links,
      interferenceMatrix,
      groupInterference);

  // Construct and solve OCC problem
  OccSolver occSolver(groupsGraph, groupInterference);
  auto occResult = occSolver.getOccSolution();

  // Assign user-configured colors first
  // User configuration should not be changed unless "clearUserConfig" was set.
  // In such case, group2UserColor should be empty
  for (const auto& it : group2UserColor) {
    group2AssignedColor[it.first] = it.second;
    visitStack.push_front(it.first);
  }

  // Assign colors to all groups not picked for exclusion by the OCC
  // algorithm
  // Color assignment to these groups is guaranteed to generate no
  // interference (Assuming no user config conflict)
  // Start with user configured groups (already added to visitStack)
  std::transform(
      group2Links.begin(),
      group2Links.end(),
      back_inserter(visitStack),
      [](std::pair<std::string, std::unordered_set<std::string>> const& pair) {
        return pair.first;
      });

  std::unordered_set<std::string> visitedGroups;
  while (!visitStack.empty()) {
    auto groupName = visitStack.front();
    visitStack.pop_front();
    if (visitedGroups.count(groupName) ||
        (occResult.count(groupName) &&
        !group2AssignedColor.count(groupName))) {
      // Skip if group already visited or is part of the OCC solution but not
      // configured by the user.
      continue;
    }

    visitedGroups.insert(groupName);
    if (!group2AssignedColor.count(groupName)) {
      auto color = getPreferredLinkColor(
          groupName,
          group2AssignedColor,
          interferenceMatrix,
          group2AutoColor,
          colors);
      group2AssignedColor[groupName] = color;
    }

    // Configures neighbors of group first. This guarantees a zero-interference
    // solution
    auto it = interferenceMatrix.find(groupName);
    if (it != interferenceMatrix.end()) {
      for (const auto& kv : it->second) {
        if (!group2AssignedColor.count(kv.first)) {
          visitStack.push_front(kv.first);
        }
      }
    }
  }

  // Assign colors to the remaining groups.
  // Color assignment to these groups may add interference to the network.
  // Visit groups in the order of their sum interference estimate
  std::vector<std::pair<std::string, float>> groupInterferencePairs;
  for (const auto& groupId : occResult) {
    auto it = groupInterference.find(groupId);
    if (it != groupInterference.end()) {
      groupInterferencePairs.push_back(std::make_pair(groupId, it->second));
    } else {
      groupInterferencePairs.push_back(std::make_pair(groupId, 0.0));
    }
  }

  // Sort by sum interference in descending order
  std::sort(
      groupInterferencePairs.begin(),
      groupInterferencePairs.end(),
      [=](std::pair<std::string, float>& a, std::pair<std::string, float>& b) {
        return a.second > b.second;
      });

  for (const auto& it : groupInterferencePairs) {
    if (group2AssignedColor.count(it.first)) {
      continue;
    }

    visitedGroups.insert(it.first);
    auto color = getPreferredLinkColor(
        it.first, group2AssignedColor, interferenceMatrix, group2AutoColor,
        colors);
    group2AssignedColor[it.first] = color;
  }

  return group2AssignedColor;
}

double
InterferenceHelper::computeUndirectedLinkAngleDiff(
    double angle, double angleBase, bool flip) {
  double angleDiff = angle - angleBase;
  if (flip) {
    angleDiff = 180 - angleBase;
  }
  angleDiff = abs(angleDiff);
  if (angleDiff > 180) {
    angleDiff = 360 - angleDiff;
  }
  if (angleDiff > 90) {
    angleDiff = 180 - angleDiff;
  }
  return angleDiff;
}

double
InterferenceHelper::computeDirectedLinkAngleDiff(double angle1, double angle2) {
  auto diff = abs(angle1 - angle2);
  if (diff > 360) {
    diff -= 360;
  }

  return (180 - abs(diff - 180));
}

double
InterferenceHelper::getArrayLoss(double angle) {
  // refer to https://fb.quip.com/YHM1A2HYqcKT for array response as a function
  // of scan angle
  if (angle > 90) {
    return -45;
  } else if (angle > 16) {
    return -20;
  } else if (angle > 10) {
    return -17;
  } else if (angle > 5) {
    return -13;
  } else {
    return 0;
  }
}

double
InterferenceHelper::getInterferenceEstimate(
    double distance, double angleTx, double angleRx) {
  // Check distance.
  if (distance <= 0 || distance > kMaxInterferenceDistance) {
    return 0.0;
  }

  double arrayLossTx = getArrayLoss(angleTx);
  double arrayLossRx = getArrayLoss(angleRx);

  // Interference received signal power
  double irsp =
      -8 - 20 * log10(distance) - 36e-3 * distance + arrayLossTx + arrayLossRx;

  // Check minimum interference threshold
  if (irsp < kMinInterferenceLevel) {
    return 0.0;
  }

  // To linear (with 60 dBm bias)
  return (pow(10, (irsp + 60) / 10));
}

double
InterferenceHelper::estimateGroup2GroupInterference(
    const TopologyWrapper& topologyW,
    ConfigHelper& configHelper,
    const std::unordered_set<std::string>& group1,
    const std::unordered_set<std::string>& group2,
    const bool shouldAccountForChannel) {

  double totalInterference = 0.0;
  for (const auto& linkName1 : group1) {
    for (const auto& linkName2 : group2) {
      auto link1 = topologyW.getLink(linkName1);
      auto link2 = topologyW.getLink(linkName2);
      if (!link1 || !link2) {
        continue;
      }
      auto link1Channel = configHelper.getLinkChannel(
          link1.value(),
          false, /* userConfiguredOnly */
          false /* autoConfiguredOnly */);
      auto link2Channel = configHelper.getLinkChannel(
          link2.value(),
          false, /* userConfiguredOnly */
          false /* autoConfiguredOnly */);

      // Links on different channels have no interference
      if (shouldAccountForChannel &&
          link1Channel &&
          link2Channel &&
          link1Channel.value() != link2Channel.value()) {
        continue;
      }
      auto polarity11 = configHelper.getRadioPolarity(
          link1->a_node_name, link1->a_node_mac, false);
      auto polarity12 = configHelper.getRadioPolarity(
          link1->z_node_name, link1->z_node_mac, false);
      auto polarity21 = configHelper.getRadioPolarity(
          link2->a_node_name, link2->a_node_mac, false);
      auto polarity22 = configHelper.getRadioPolarity(
          link2->z_node_name, link2->z_node_mac, false);

      auto node11 = topologyW.getNode(link1->a_node_name);
      auto node12 = topologyW.getNode(link1->z_node_name);
      auto node21 = topologyW.getNode(link2->a_node_name);
      auto node22 = topologyW.getNode(link2->z_node_name);

      auto site11 = topologyW.getSite(node11->site_name);
      auto site12 = topologyW.getSite(node12->site_name);
      auto site21 = topologyW.getSite(node21->site_name);
      auto site22 = topologyW.getSite(node22->site_name);

      double baseAng1 = computeAngle(site12->location, site11->location);
      double baseAng2 = computeAngle(site22->location, site21->location);

      double interference = 0.0;

      // For every combination of tx-rx interference
      // When isValidLinkPolarity for two nodes is false, there will be no
      // tx-rx interference as the nodes are transmitting and
      // receiving during the same intervals.
      // node11 -> node21
      if (site11->name != site21->name &&
          PolarityHelper::isValidLinkPolarity(polarity11, polarity21)) {
        double crossAng = computeAngle(site21->location, site11->location);
        double crossDistance =
            approxDistance(site11->location, site21->location);
        auto angleTx = computeDirectedLinkAngleDiff(baseAng1, crossAng);
        auto angleRx = computeDirectedLinkAngleDiff(baseAng2, crossAng + 180);
        interference +=
            getInterferenceEstimate(crossDistance, angleTx, angleRx);
      }

      // node11 -> node22
      if (site11->name != site22->name &&
          PolarityHelper::isValidLinkPolarity(polarity11, polarity22)) {
        double crossAng = computeAngle(site22->location, site11->location);
        double crossDistance =
            approxDistance(site11->location, site22->location);
        auto angleTx = computeDirectedLinkAngleDiff(baseAng1, crossAng);
        auto angleRx =
            computeDirectedLinkAngleDiff(baseAng2 + 180, crossAng + 180);
        interference +=
            getInterferenceEstimate(crossDistance, angleTx, angleRx);
      }

      // node12 -> node21
      if (site12->name != site21->name &&
          PolarityHelper::isValidLinkPolarity(polarity12, polarity21)) {
        double crossAng = computeAngle(site21->location, site12->location);
        double crossDistance =
            approxDistance(site12->location, site21->location);
        auto angleTx = computeDirectedLinkAngleDiff(baseAng1 + 180, crossAng);
        auto angleRx = computeDirectedLinkAngleDiff(baseAng2, crossAng + 180);
        interference +=
            getInterferenceEstimate(crossDistance, angleTx, angleRx);
      }

      // node12 -> node22
      if (site12->name != site22->name &&
          PolarityHelper::isValidLinkPolarity(polarity12, polarity22)) {
        double crossAng = computeAngle(site22->location, site12->location);
        double crossDistance =
            approxDistance(site12->location, site22->location);
        auto angleTx = computeDirectedLinkAngleDiff(baseAng1 + 180, crossAng);
        auto angleRx =
            computeDirectedLinkAngleDiff(baseAng2 + 180, crossAng + 180);
        interference +=
            getInterferenceEstimate(crossDistance, angleTx, angleRx);
      }

      totalInterference += interference;
    }
  }

  return totalInterference;
}

} // namespace terragraph
} // namespace facebook
