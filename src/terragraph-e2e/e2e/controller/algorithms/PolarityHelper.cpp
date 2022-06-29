/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "PolarityHelper.h"

#include <folly/FileUtil.h>
#include <folly/Format.h>

#include "OccSolver.h"
#include "e2e/common/SysUtils.h"

using apache::thrift::detail::TEnumMapFactory;
using std::invalid_argument;

namespace facebook {
namespace terragraph {

bool
PolarityHelper::isOddPolarity(thrift::PolarityType polarity) {
  return polarity == thrift::PolarityType::ODD ||
         polarity == thrift::PolarityType::HYBRID_ODD;
}

bool
PolarityHelper::isEvenPolarity(thrift::PolarityType polarity) {
  return polarity == thrift::PolarityType::EVEN ||
         polarity == thrift::PolarityType::HYBRID_EVEN;
}

bool
PolarityHelper::isHybridPolarity(thrift::PolarityType polarity) {
  return polarity == thrift::PolarityType::HYBRID_ODD ||
         polarity == thrift::PolarityType::HYBRID_EVEN;
}

bool
PolarityHelper::isValidPolarityType(thrift::PolarityType polarity) {
  return TEnumMapFactory<thrift::PolarityType>::makeValuesToNamesMap()
      .count(polarity);
}

bool
PolarityHelper::isValidLinkPolarity(
    std::optional<thrift::PolarityType> aPolarity,
    std::optional<thrift::PolarityType> zPolarity) {
  return (
      aPolarity && zPolarity && isValidPolarityType(aPolarity.value()) &&
      isValidPolarityType(zPolarity.value()) &&
      ((isOddPolarity(aPolarity.value()) &&
        isEvenPolarity(zPolarity.value())) ||
       (isEvenPolarity(aPolarity.value()) &&
        isOddPolarity(zPolarity.value()))) &&
      !(isHybridPolarity(aPolarity.value()) &&
        isHybridPolarity(zPolarity.value())));
}

bool
PolarityHelper::hasOtherWirelessLinks(
    const TopologyWrapper& topologyW,
    const thrift::Link& testLink,
    const std::string& nodeName) {
  auto nodeMac = nodeName == testLink.a_node_name ? testLink.a_node_mac
                                                  : testLink.z_node_mac;
  if (nodeMac.empty()) {
    return false;
  }

  for (const auto& link : topologyW.getLinksByNodeName(nodeName)) {
    if (link != testLink && link.link_type == thrift::LinkType::WIRELESS &&
        !link.a_node_mac.empty() && !link.z_node_mac.empty() &&
        (link.a_node_mac == nodeMac || link.z_node_mac == nodeMac)) {
      return true;
    }
  }

  return false;
}

std::optional<thrift::PolarityType>
PolarityHelper::getSitePolarity(
    const TopologyWrapper& topologyW,
    const ConfigHelper& configHelper,
    const std::string& siteName) {
  auto siteNodes = topologyW.getNodesBySiteName(siteName);
  auto macToPolarity = configHelper.getPolarities(siteNodes);
  for (const auto& pair : macToPolarity) {
    // Extra checks so we don't return hybrid polarity values
    if (isOddPolarity(pair.second)) {
      return thrift::PolarityType::ODD;
    } else if (isEvenPolarity(pair.second)) {
      return thrift::PolarityType::EVEN;
    }
  }
  return std::nullopt;
}

thrift::PolarityType
PolarityHelper::getPreferedPolarity(
    const std::string& macAddr,
    MacToPolarity& userPolarities,
    MacToPolarity& oldPolarities,
    MacToPolarity& newPolarities,
    std::unordered_set<std::string>& hybridMacs,
    std::unordered_map<std::string, std::unordered_set<std::string>>&
        site2MacsMap,
    std::unordered_map<std::string, std::string>& mac2SiteNameMap) {

  // Check if MAC address has a user configured polarity
  auto userPolarityIt = userPolarities.find(macAddr);
  if (userPolarityIt != userPolarities.end()) {
    return userPolarityIt->second;
  }

  // If site is hybrid, return existing auto allocation if present
  auto oldPolarityIt = oldPolarities.find(macAddr);
  if (hybridMacs.count(macAddr) && oldPolarityIt != oldPolarities.end()) {
    return oldPolarityIt->second;
  }

  // Match majority of nodes on same site
  auto siteName = mac2SiteNameMap[macAddr];
  int oddCount = 0;
  int evenCount = 0;
  for (const auto& mac : site2MacsMap[siteName]) {
    auto alreadyAssignedIt = newPolarities.find(mac);
    if (alreadyAssignedIt != newPolarities.end()) {
      oddCount += isOddPolarity(alreadyAssignedIt->second) ? 1 : 0;
      evenCount += isEvenPolarity(alreadyAssignedIt->second) ? 1 : 0;
    }
  }

  // If no majority, pick old polarity if it exists
  if (oddCount == evenCount && oldPolarityIt != oldPolarities.end()) {
    return oldPolarityIt->second;
  }

  if (oddCount >= evenCount) {
    return thrift::PolarityType::ODD;
  }

  return thrift::PolarityType::EVEN;
}

bool
PolarityHelper::assignPolarityAndFollow(
    const TopologyWrapper& topologyW,
    const std::string& macAddr,
    bool followSameSite,
    std::unordered_set<std::string>& hybridMacs,
    MacToPolarity& userPolarities,
    MacToPolarity& oldPolarities,
    MacToPolarity& newPolarities,
    std::map<std::string, std::string>& mac2NodeName,
    std::unordered_map<std::string, std::unordered_set<std::string>>&
        site2MacsMap,
    std::unordered_map<std::string, std::string>& mac2SiteNameMap,
    std::vector<std::string>& errMsgs) {
  bool success = true;

  std::deque<std::string> visitStack;
  visitStack.push_back(macAddr);
  while (!visitStack.empty()) {
    auto currMacAddr = visitStack.front();
    visitStack.pop_front();
    auto currNodeName = mac2NodeName[currMacAddr];
    auto node = topologyW.getNode(currNodeName);

    // Assign polarity if not yet assigned
    if (!newPolarities.count(currMacAddr)) {
      newPolarities[currMacAddr] = getPreferedPolarity(
          currMacAddr,
          userPolarities,
          oldPolarities,
          newPolarities,
          hybridMacs,
          site2MacsMap,
          mac2SiteNameMap);
      VLOG(3) << folly::format(
          "Polarity assigned: node {} mac {} polarity {}",
          currNodeName,
          currMacAddr,
          static_cast<int>(newPolarities[currMacAddr]));
    }

    // Follow MAC addresses on the same site only if instructed and the site is
    // not a hybrid
    // Do not allocate polarity yet. Just add to stack.
    if (followSameSite && !hybridMacs.count(currMacAddr)) {
      for (const auto& mac : site2MacsMap[node->site_name]) {
        // Skip if already assigned
        if (newPolarities.count(mac)) {
          continue;
        }

        // Push to the back of stack to give priority to nodes reachable via
        // wireless links
        visitStack.push_back(mac);
      }
    }

    // Always follow across wireless links
    auto polarity = newPolarities[currMacAddr];
    for (const auto& link : topologyW.getLinksByNodeName(currNodeName)) {
      if (link.link_type != thrift::LinkType::WIRELESS ||
          link.a_node_mac.empty() || link.z_node_mac.empty() ||
          (link.a_node_mac != currMacAddr && link.z_node_mac != currMacAddr)) {
        continue;
      }

      auto nbrMac =
          link.a_node_mac == currMacAddr ? link.z_node_mac : link.a_node_mac;
      auto nbrNodeName = mac2NodeName[nbrMac];
      // Pick opposite polarity for neighbor
      auto nbrPolarity = isOddPolarity(polarity) ? thrift::PolarityType::EVEN
                                                 : thrift::PolarityType::ODD;
      // Check if already assigned
      auto nbrCurrAssignedPolarityIt = newPolarities.find(nbrMac);
      if (nbrCurrAssignedPolarityIt != newPolarities.end()) {
        // Check if already assigned polarity is conforming
        if (nbrPolarity != nbrCurrAssignedPolarityIt->second) {
          LOG(ERROR) << folly::format(
              "POLARITY MISMATCH: node {} mac {} existing {} desired {}",
              nbrNodeName,
              nbrMac,
              static_cast<int>(nbrCurrAssignedPolarityIt->second),
              static_cast<int>(nbrPolarity));
          success = false;
          errMsgs.push_back(
              folly::format("Unsolvable odd cycle exists at {}", nbrMac).str());
        }

        continue;
      }

      VLOG(3) << folly::format(
          "Polarity assigned (cross link): node {} mac {} polarity {}",
          nbrNodeName,
          nbrMac,
          static_cast<int>(nbrPolarity));

      newPolarities.emplace(nbrMac, nbrPolarity);
      visitStack.push_front(nbrMac);
    }
  }

  return success;
}

bool
PolarityHelper::allocatePolarities(
    const TopologyWrapper& topologyW,
    std::unordered_set<std::string>& hybridMacs,
    MacToPolarity& userPolarities,
    MacToPolarity& oldPolarities,
    MacToPolarity& newPolarities,
    std::map<std::string, std::string>& mac2NodeName,
    std::unordered_map<std::string, std::unordered_set<std::string>>&
        site2MacsMap,
    std::unordered_map<std::string, std::string>& mac2SiteNameMap,
    std::vector<std::string>& errMsgs) {

  bool success = true;
  // Process MAC addresses with user configured polarity first
  for (const auto& it : userPolarities) {
    if (!assignPolarityAndFollow(
            topologyW,
            it.first,
            false, // don't follow same-site MAC addresses
            hybridMacs,
            userPolarities,
            oldPolarities,
            newPolarities,
            mac2NodeName,
            site2MacsMap,
            mac2SiteNameMap,
            errMsgs)) {
      success = false;
    }
  }

  // Process remaining MAC addresses less the ones belonging to a hybrid site
  // Start with MAC addresses already assigned a polarity to expand within site
  auto populatedMacs = newPolarities;
  for (const auto& it : populatedMacs) {
    if (!hybridMacs.count(it.first)) {
      if (!assignPolarityAndFollow(
              topologyW,
              it.first,
              true,
              hybridMacs,
              userPolarities,
              oldPolarities,
              newPolarities,
              mac2NodeName,
              site2MacsMap,
              mac2SiteNameMap,
              errMsgs)) {
        success = false;
      }
    }
  }
  for (const auto& it : mac2NodeName) {
    if (!newPolarities.count(it.first) && !hybridMacs.count(it.first)) {
      if (!assignPolarityAndFollow(
              topologyW,
              it.first,
              true,
              hybridMacs,
              userPolarities,
              oldPolarities,
              newPolarities,
              mac2NodeName,
              site2MacsMap,
              mac2SiteNameMap,
              errMsgs)) {
        success = false;
      }
    }
  }

  // Process any remaining MAC address belonging to a hybrid site
  for (const auto& mac : hybridMacs) {
    if (!newPolarities.count(mac)) {
      if (!assignPolarityAndFollow(
              topologyW,
              mac,
              false,
              hybridMacs,
              userPolarities,
              oldPolarities,
              newPolarities,
              mac2NodeName,
              site2MacsMap,
              mac2SiteNameMap,
              errMsgs)) {
        success = false;
      }
    }
  }

  return success;
}

std::unordered_set<std::string>
PolarityHelper::getYStreetMacs(const TopologyWrapper& topologyW) {
  std::unordered_set<std::string> linksMacs;
  std::unordered_set<std::string> yStreetMacs;

  auto dnNodeNames = topologyW.getNodeNamesByType(thrift::NodeType::DN);

  for (const auto& link : topologyW.getAllLinks()) {
    // Only DN-DN wireless links
    if (!dnNodeNames.count(link.a_node_name) ||
        !dnNodeNames.count(link.z_node_name) ||
        link.a_node_mac.empty() ||
        link.z_node_mac.empty() ||
        link.link_type != thrift::LinkType::WIRELESS) {
      continue;
    }

    if (linksMacs.count(link.a_node_mac)) {
      yStreetMacs.insert(link.a_node_mac);
    } else {
      linksMacs.insert(link.a_node_mac);
    }

    if (linksMacs.count(link.z_node_mac)) {
      yStreetMacs.insert(link.z_node_mac);
    } else {
      linksMacs.insert(link.z_node_mac);
    }
  }

  return yStreetMacs;
}

bool
PolarityHelper::optimizePolarity(
    const TopologyWrapper& topologyW,
    MacToPolarity& userPolarities,
    MacToPolarity& oldPolarities,
    MacToPolarity& newPolarities,
    std::vector<std::string>& errMsgs) {
  auto yStreetMacs = getYStreetMacs(topologyW);
  auto allNodes = topologyW.getAllNodes();

  // Create wlan_mac->name and site<->mac maps
  std::map<std::string, std::string> mac2NodeName;
  std::unordered_map<std::string, std::string> mac2SiteNameMap;
  std::unordered_map<std::string, std::unordered_set<std::string>> site2MacsMap;
  for (const auto& node : allNodes) {
    for (const auto& mac : node.wlan_mac_addrs) {
      mac2NodeName[mac] = node.name;
      mac2SiteNameMap[mac] = node.site_name;
      site2MacsMap[node.site_name].insert(mac);
    }
  }

  // Find desired hybrid sites based off user/auto polarity config
  std::unordered_set<std::string> userHybridSites;
  std::unordered_set<std::string> oldHybridSites;
  for (const auto& site : topologyW.getAllSites()) {
    int userOddCount = 0;
    int userEvenCount = 0;
    int oldOddCount = 0;
    int oldEvenCount = 0;
    for (const auto& mac : site2MacsMap[site.name]) {
      auto userPolarityIt = userPolarities.find(mac);
      if (userPolarityIt != userPolarities.end()) {
        userOddCount += isOddPolarity(userPolarityIt->second) ? 1 : 0;
        userEvenCount += isEvenPolarity(userPolarityIt->second) ? 1 : 0;
      }

      auto oldPolarityIt = oldPolarities.find(mac);
      if (oldPolarityIt != oldPolarities.end()) {
        oldOddCount += isOddPolarity(oldPolarityIt->second) ? 1 : 0;
        oldEvenCount += isEvenPolarity(oldPolarityIt->second) ? 1 : 0;
      }
    }

    if (userOddCount > 0 && userEvenCount > 0) {
      userHybridSites.insert(site.name);
    }

    if (oldOddCount > 0 && oldEvenCount > 0) {
      oldHybridSites.insert(site.name);
    }
  }

  // Assign high weights to P2MP
  // Avoid having P2MP sites as hybrids as much as possible
  std::unordered_map<std::string, float> siteWeights;
  for (const auto& mac : yStreetMacs) {
    auto siteName = mac2SiteNameMap[mac];
    siteWeights[siteName] = OccSolver::kHighVertexWeight;
    VLOG(4) << folly::format(
        "Assigned high vertex weight to: {} -> {}",
        siteName,
        OccSolver::kHighVertexWeight);
  }

  // Assign low weights to user hybrid sites
  // This strongly influences the algorithm to pick these sites as hybrid sites
  // This could override the weight set earlier
  for (const auto& siteName : userHybridSites) {
    siteWeights[siteName] = OccSolver::kLowVertexWeight;
    VLOG(4) << folly::format(
        "Assigned low vertex weight to: {} -> {}",
        siteName,
        OccSolver::kLowVertexWeight);
  }

  // Assign slightly biased weights to existing auto configured hybrid sites
  // This slightly influences the algorithm to pick these sites as hybrid sites
  // to reduce polarity fluctuations
  // This should not change already assigned weights
  for (const auto& siteName : oldHybridSites) {
    siteWeights.emplace(siteName, OccSolver::kBiasDownVertexWeight);
    VLOG(4) << folly::format(
        "Assigned biased vertex weight to: {} -> {}",
        siteName,
        siteWeights[siteName]);
  }

  // Construct and solve OCC problem
  OccSolver occSolver(topologyW, siteWeights);
  auto occResult = occSolver.getOccSolution();

  // Get MAC addresses of hybrid sites
  std::unordered_set<std::string> hybridMacs;
  for (const auto& siteName : occResult) {
    for (const auto& mac : site2MacsMap[siteName]) {
      hybridMacs.insert(mac);
    }
  }

  newPolarities.clear();
  return allocatePolarities(
      topologyW,
      hybridMacs,
      userPolarities,
      oldPolarities,
      newPolarities,
      mac2NodeName,
      site2MacsMap,
      mac2SiteNameMap,
      errMsgs);
}

bool
PolarityHelper::assignLinkPolarity(
    const TopologyWrapper& topologyW,
    ConfigHelper& configHelper,
    const thrift::Link& link) noexcept {
  if (link.link_type != thrift::LinkType::WIRELESS || link.a_node_mac.empty() ||
      link.z_node_mac.empty()) {
    return true;
  }

  auto aPolarity =
      configHelper.getRadioPolarity(link.a_node_name, link.a_node_mac, false);
  auto zPolarity =
      configHelper.getRadioPolarity(link.z_node_name, link.z_node_mac, false);
  if (isValidLinkPolarity(aPolarity, zPolarity)) {
    // Current polarity assignment is valid
    return true;
  }

  std::optional<thrift::PolarityType> aPolarityNew = std::nullopt;
  std::optional<thrift::PolarityType> zPolarityNew = std::nullopt;

  // Both ends of link not assigned a polarity
  if (!aPolarity && !zPolarity) {
    // Try to assign the site polarity to either node, and the opposite polarity
    // to the other node. If neither site polarity exists, assign arbitrarily.
    auto aNode = topologyW.getNode(link.a_node_name);
    auto zNode = topologyW.getNode(link.z_node_name);
    if (aNode && (aPolarityNew =
        getSitePolarity(topologyW, configHelper, aNode->site_name))) {
      zPolarityNew = isOddPolarity(aPolarityNew.value())
          ? thrift::PolarityType::EVEN
          : thrift::PolarityType::ODD;
    } else if (zNode && (zPolarityNew =
        getSitePolarity(topologyW, configHelper, zNode->site_name))) {
      aPolarityNew = isOddPolarity(zPolarityNew.value())
          ? thrift::PolarityType::EVEN
          : thrift::PolarityType::ODD;
    } else {
      // Default: assign ODD polarity to aNode and EVEN to zNode
      aPolarityNew = thrift::PolarityType::ODD;
      zPolarityNew = thrift::PolarityType::EVEN;
    }
  } else if (aPolarity && !zPolarity) {
    // One side of the link has polarity assigned
    // Assign opposite polarity to zNode
    zPolarityNew = isOddPolarity(aPolarity.value())
        ? thrift::PolarityType::EVEN
        : thrift::PolarityType::ODD;
  } else if (!aPolarity && zPolarity) {
    // One side of the link has polarity assigned
    // Assign opposite polarity to aNode
    aPolarityNew = isOddPolarity(zPolarity.value())
        ? thrift::PolarityType::EVEN
        : thrift::PolarityType::ODD;
  } else {
    // Invalid polarity assigned
    // Change polarity of node that has no other wireless links and whose
    // polarity is not configured by the user
    auto aUserPolarity =
        configHelper.getRadioPolarity(link.a_node_name, link.a_node_mac, true);
    auto zUserPolarity =
        configHelper.getRadioPolarity(link.z_node_name, link.z_node_mac, true);
    if (!aUserPolarity &&
        !hasOtherWirelessLinks(topologyW, link, link.a_node_name)) {
      aPolarityNew = isOddPolarity(zPolarity.value())
                         ? thrift::PolarityType::EVEN
                         : thrift::PolarityType::ODD;
    } else if (
        !zUserPolarity &&
        !hasOtherWirelessLinks(topologyW, link, link.z_node_name)) {
      zPolarityNew = isOddPolarity(aPolarity.value())
                         ? thrift::PolarityType::EVEN
                         : thrift::PolarityType::ODD;
    }
  }

  std::string errorMsg;
  if (aPolarityNew) {
    LOG(INFO) << folly::sformat(
        "Assigned {} polarity to {}",
        folly::get_default(
            TEnumMapFactory<thrift::PolarityType>::makeValuesToNamesMap(),
            aPolarityNew.value(),
            "UNKNOWN"),
        link.a_node_mac);
    configHelper.setNodePolarity(
        link.a_node_name, link.a_node_mac, aPolarityNew, false, errorMsg);
  }

  if (zPolarityNew) {
    LOG(INFO) << folly::sformat(
        "Assigned {} polarity to {}",
        folly::get_default(
            TEnumMapFactory<thrift::PolarityType>::makeValuesToNamesMap(),
            zPolarityNew.value(),
            "UNKNOWN"),
        link.z_node_mac);
    configHelper.setNodePolarity(
        link.z_node_name, link.z_node_mac, zPolarityNew, false, errorMsg);
  }

  // validate
  return isValidLinkPolarity(aPolarityNew ? aPolarityNew : aPolarity,
                             zPolarityNew ? zPolarityNew : zPolarity);
}

} // namespace terragraph
} // namespace facebook
