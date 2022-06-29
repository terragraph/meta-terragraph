/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "IgnitionAppUtil.h"

#include <algorithm>
#include <folly/Random.h>
#include <random>

namespace facebook {
namespace terragraph {

bool isWirelessLink(const thrift::Link& link) {
  return link.link_type == thrift::LinkType::WIRELESS;
}

// Check for valid link polarity: E-O, E-HO, O-E, O-HE, HE-O, HO-E
bool
isValidLinkPolarity(
    std::optional<thrift::PolarityType> aPolarity,
    std::optional<thrift::PolarityType> zPolarity) {
  if (!aPolarity || !zPolarity) {
    return false;
  }
  if (aPolarity.value() == thrift::PolarityType::EVEN) {
    return zPolarity.value() == thrift::PolarityType::ODD ||
           zPolarity.value() == thrift::PolarityType::HYBRID_ODD;
  }
  if (aPolarity.value() == thrift::PolarityType::ODD) {
    return zPolarity.value() == thrift::PolarityType::EVEN ||
           zPolarity.value() == thrift::PolarityType::HYBRID_EVEN;
  }
  if (aPolarity.value() == thrift::PolarityType::HYBRID_EVEN) {
    return zPolarity.value() == thrift::PolarityType::ODD;
  }
  if (aPolarity.value() == thrift::PolarityType::HYBRID_ODD) {
    return zPolarity.value() == thrift::PolarityType::EVEN;
  }
  return false;
}

// Wrap integer angle in range [-360, 360] to range [-180, 180]
int16_t wrapAngle(int16_t angle) {
  return ((angle + 540) % 360) - 180;
}

std::unordered_set<std::string>
IgnitionAppUtil::getExclusionSet(
    const TopologyWrapper& topologyW,
    const ConfigHelper& configHelper,
    std::vector<thrift::IgnitionCandidate>& igCandidates,
    int16_t excludeDistanceShort,
    int16_t excludeDistanceLong,
    int16_t excludeAngleRel) {
  std::unordered_set<std::string> exclusionSet;

  if (igCandidates.empty()) {
    return exclusionSet;
  }

  // Get polarity/channel for all radios from config
  std::unordered_map<std::string, std::optional<thrift::PolarityType>>
      radioPolarities;
  std::unordered_map<std::string, std::optional<int8_t>> radioChannels;
  for (const auto& node : topologyW.getAllNodes()) {
    for (const auto& macAddr : node.wlan_mac_addrs) {
      radioPolarities[macAddr] =
          configHelper.getRadioPolarity(node.name, macAddr, false);
      radioChannels[macAddr] =
          configHelper.getRadioChannel(node.name, macAddr, false, false);
    }
  }

  // Compute angle and distance between all sites
  const std::vector<thrift::Site>& sites = topologyW.getAllSites();
  std::unordered_map<std::string, size_t> site2idx;
  std::vector<std::vector<int16_t>> siteDistances(
      sites.size(), std::vector<int16_t>(sites.size()));
  std::vector<std::vector<int16_t>> siteAngles(
      sites.size(), std::vector<int16_t>(sites.size()));
  for (size_t i = 0; i < sites.size(); i++) {
    site2idx[sites[i].name] = i;
    thrift::Location loc1 = sites[i].location;
    for (size_t j = i + 1; j < sites.size(); j++) {
      thrift::Location loc2 = sites[j].location;
      // Ignore altitude
      loc2.altitude = loc1.altitude;
      siteDistances[i][j] = approxDistance(loc1, loc2);
      siteDistances[j][i] = siteDistances[i][j];
      siteAngles[i][j] = computeAngle(loc1, loc2);
      // Reverse integer angle in [-180, 180] and constrain to [-180, 180]
      siteAngles[j][i] = wrapAngle(siteAngles[i][j] + 180);
    }
  }

  // Loop through each ignition candidate and check for victim links
  for (const auto& igCandidate : igCandidates) {
    const std::optional<thrift::Node> atxNode =
        topologyW.getNode(igCandidate.initiatorNodeName);
    const std::optional<thrift::Link> atxLink =
        topologyW.getLink(igCandidate.linkName);
    if (!atxNode || !atxLink) {
      VLOG(3) << "Bad ignition candidate: " << igCandidate.linkName;
      continue;
    }
    const size_t atxIdx = site2idx[atxNode->site_name];
    std::string atxMac, arxMac;
    if (igCandidate.initiatorNodeName == atxLink->a_node_name) {
      atxMac = atxLink->a_node_mac;
      arxMac = atxLink->z_node_mac;
    } else {
      atxMac = atxLink->z_node_mac;
      arxMac = atxLink->a_node_mac;
    }
    const std::optional<thrift::PolarityType> atxPolarity =
        radioPolarities[atxMac];
    const std::optional<int8_t> atxChannel = radioChannels[atxMac];

    // Check proximity of uni-directional links with same polarity as initiator
    for (const thrift::Link& vrxLink : topologyW.getAllLinks()) {
      if (!isWirelessLink(vrxLink) || !vrxLink.is_alive ||
          vrxLink.a_node_mac.empty() || vrxLink.z_node_mac.empty()) {
        continue;
      }

      // Check if link uses same channel as initiator
      if (atxChannel && (
              atxChannel != radioChannels[vrxLink.a_node_mac] ||
              atxChannel != radioChannels[vrxLink.z_node_mac])) {
        continue;
      }

      // Determine which direction of vrxLink may be subject to interference
      // from ATX, if any
      std::pair<std::string, std::string> vtxNameAndMac, vrxNameAndMac;
      if (isValidLinkPolarity(
              atxPolarity, radioPolarities[vrxLink.z_node_mac])) {
        vtxNameAndMac = std::make_pair(vrxLink.a_node_name, vrxLink.a_node_mac);
        vrxNameAndMac = std::make_pair(vrxLink.z_node_name, vrxLink.z_node_mac);
      } else if (isValidLinkPolarity(
                     atxPolarity, radioPolarities[vrxLink.a_node_mac])) {
        vtxNameAndMac = std::make_pair(vrxLink.z_node_name, vrxLink.z_node_mac);
        vrxNameAndMac = std::make_pair(vrxLink.a_node_name, vrxLink.a_node_mac);
      } else {
        VLOG(3) << "Unknown polarity (or hybrid-hybrid) for " << vrxLink.name;
        continue;
      }
      if (atxMac == vtxNameAndMac.second || arxMac == vrxNameAndMac.second) {
        continue;
      }
      const auto vtxNode = topologyW.getNode(vtxNameAndMac.first);
      const auto vrxNode = topologyW.getNode(vrxNameAndMac.first);
      if (!vtxNode || !vrxNode) {
        VLOG(3) << "Failed to find node for " << vrxLink.name;
        continue;
      }
      const size_t vtxIdx = site2idx[vtxNode->site_name];
      const size_t vrxIdx = site2idx[vrxNode->site_name];

      // Check angle/distance thresholds
      const int16_t dist = siteDistances[atxIdx][vrxIdx];
      const int16_t relAngle = std::abs(
          wrapAngle(siteAngles[vrxIdx][atxIdx] - siteAngles[vrxIdx][vtxIdx]));
      if ((dist > excludeDistanceShort && relAngle > excludeAngleRel) ||
          dist > excludeDistanceLong) {
        continue;
      }
      if (vrxNode->node_type == thrift::NodeType::CN) {
        if (vtxNode->status == thrift::NodeStatusType::OFFLINE) {
          continue;
        }
        exclusionSet.insert(vtxNameAndMac.second);
      } else {
        if (vrxNode->status == thrift::NodeStatusType::OFFLINE) {
          continue;
        }
        exclusionSet.insert(vrxNameAndMac.second);
      }
    }
  }

  // Remove ignition candidates from exclusion set
  for (const auto& igCandidate : igCandidates) {
    auto link = topologyW.getLink(igCandidate.linkName);
    if (link) {
      exclusionSet.erase(link->a_node_mac);
      exclusionSet.erase(link->z_node_mac);
    }
  }

  std::vector<std::string> vec;
  for (const auto& macAddr : exclusionSet) {
    const auto node = topologyW.getNodeByMac(macAddr);
    if (node) {
      vec.push_back(node->name);
    }
  }
  if (vec.empty()) {
    VLOG(4) << "IBF exclusion set empty";
  } else {
    VLOG(4) << "IBF exclusion set: " << folly::join(", ", vec);
  }

  return exclusionSet;
}

std::vector<thrift::IgnitionCandidate>
IgnitionAppUtil::findAllIgnitionCandidates(
    const TopologyWrapper& topologyW,
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>&
        cnToPossibleIgnitionTs,
    const std::unordered_map<
        std::string,
        std::chrono::steady_clock::time_point>& initiatorToAttemptTs,
    std::chrono::seconds bfTimeout,
    std::chrono::seconds backupCnLinkInterval,
    std::unordered_map<std::string, size_t>& linkupIterationIndex,
    const std::unordered_set<std::string>& linkAutoIgniteOff) {
  auto now = std::chrono::steady_clock::now();

  // Find all CNs with any active links - we won't need to ignite them
  std::vector<thrift::Node> allNodes = topologyW.getAllNodes();
  std::unordered_set<std::string> activeCNs;
  for (const auto& node : allNodes) {
    if (node.node_type == thrift::NodeType::CN) {
      for (const auto& link : topologyW.getLinksByNodeName(node.name)) {
        if (link.is_alive && link.link_type != thrift::LinkType::ETHERNET) {
          activeCNs.insert(node.name);
          break;
        }
      }
    }
  }

  // Shuffle all nodes
  // This addresses the edge case when igniting CNs via backup links
  std::default_random_engine rng(folly::Random::rand32());
  std::shuffle(allNodes.begin(), allNodes.end(), rng);

  // Find ignition candidates
  std::vector<thrift::IgnitionCandidate> igCandidates;
  for (const auto& node : allNodes) {
    if (node.node_type != thrift::NodeType::DN) {
      continue;  // ignore CN nodes
    }
    if (node.status != thrift::NodeStatusType::ONLINE_INITIATOR) {
      continue;  // ignore offline or non-time-synced nodes
    }

    // Find candidate links
    auto links = topologyW.getLinksByNodeName(node.name);

    // Special handling for P2MP node that has tried to ignite other links
    auto numWirelessLinks =
        std::count_if(links.begin(), links.end(), isWirelessLink);
    auto initiatorAttemptTime = initiatorToAttemptTs.find(node.name);
    if (numWirelessLinks > 1 &&
        initiatorAttemptTime != initiatorToAttemptTs.end()) {
      auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(
          now - initiatorAttemptTime->second);
      if (elapsedSec < bfTimeout) {
        continue; // This node has been used as an initator for another link too
                  // recently
      }
    }

    size_t& startIndex = linkupIterationIndex[node.name];
    for (size_t i = 0; i < links.size(); i++) {
      const auto& link = links.at((i + startIndex) % links.size());
      if (link.is_alive || linkAutoIgniteOff.count(link.name)) {
        continue;  // nothing to do
      }
      if (link.a_node_mac.empty() || link.z_node_mac.empty()) {
        continue;  // skip links with empty MAC address
      }
      auto nbrNode = topologyW.getNbrNode(node.name, link);
      if (!nbrNode) {
        continue;  // shouldn't happen
      }
      if (nbrNode->node_type == thrift::NodeType::CN) {
        // Special handling for DN-to-CN links
        if (activeCNs.count(nbrNode->name)) {
          continue;  // this CN already has an active link
        }

        // At this point, ignition is possible
        cnToPossibleIgnitionTs.insert(std::make_pair(nbrNode->name, now));

        // Determine whether we can use backup links based on elapsed time
        if (link.is_backup_cn_link_ref().value_or(false)) {
          auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(
              now - cnToPossibleIgnitionTs.at(nbrNode->name));
          if (elapsedSec < backupCnLinkInterval) {
            continue;  // wait until the primary link has been down longer
          }
        }
      }

      // Ignite this link (initiator is valid and link is currently dead)
      thrift::IgnitionCandidate ignitionCandidate;
      ignitionCandidate.initiatorNodeName = node.name;
      ignitionCandidate.linkName = link.name;
      igCandidates.push_back(ignitionCandidate);
    }
    startIndex++;
  }

  return igCandidates;
}

std::vector<thrift::IgnitionCandidate>
IgnitionAppUtil::findAllParallelIgnitionCandidates(
    const TopologyWrapper& topologyW,
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>&
        linkToAttemptTs,
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>&
        linkToInitialAttemptTs,
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>&
        cnToPossibleIgnitionTs,
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>&
        initiatorToAttemptTs,
    std::chrono::seconds bfTimeout,
    std::chrono::seconds dampenInterval,
    std::chrono::seconds extendedDampenInterval,
    std::chrono::seconds extendedDampenFailureInterval,
    std::chrono::seconds backupCnLinkInterval,
    std::unordered_map<std::string, size_t>& linkupIterationIndex,
    const std::unordered_set<std::string>& linkAutoIgniteOff) {
  // Find all possible ignition candidates
  auto igCandidates = IgnitionAppUtil::findAllIgnitionCandidates(
      topologyW,
      cnToPossibleIgnitionTs,
      initiatorToAttemptTs,
      bfTimeout,
      backupCnLinkInterval,
      linkupIterationIndex,
      linkAutoIgniteOff);
  if (igCandidates.empty()) {
    return {};
  }

  VLOG(3) << "Ignition Candidates (" << igCandidates.size() << "):";

  // First, group candidates by link name and pick a random initiator per link
  // This way, we won't repeatedly pick the same initiator for a link that can
  // be ignited in both directions
  std::unordered_map<std::string, std::vector<size_t>> linkToCandidates;
  std::unordered_set<size_t> candidatesToSkip;
  for (size_t i = 0; i < igCandidates.size(); i++) {
    const auto& igCandidate = igCandidates[i];
    linkToCandidates[igCandidate.linkName].push_back(i);

    VLOG(3) << "Initiator: " << igCandidate.initiatorNodeName
            << " / Link: " << igCandidate.linkName;
  }
  for (const auto& kv : linkToCandidates) {
    if (kv.second.size() == 2) {
      // Pick a candidate to skip initially
      // Re-add the skipped candidate to the end of the candidate list, in case
      // the other is not a valid candidate
      size_t skipIdx = kv.second[folly::Random::rand32(2)];
      candidatesToSkip.insert(skipIdx);
      igCandidates.push_back(igCandidates[skipIdx]);
    }
  }

  // Determine valid candidates to ignite
  // Avoid getting the same node involved in two ignition events in parallel
  std::vector<thrift::IgnitionCandidate> realIgCandidates;
  std::unordered_set<std::string> involvedNodeNames;
  std::unordered_set<std::string> dampenedLinks;  // only used for logging
  auto now = std::chrono::steady_clock::now();
  for (size_t i = 0; i < igCandidates.size(); i++) {
    if (candidatesToSkip.count(i)) {
      continue;
    }

    const auto& igCandidate = igCandidates[i];
    auto aNodeName = igCandidate.initiatorNodeName;
    auto zNodeName = topologyW.getNbrNodeName(aNodeName, igCandidate.linkName);

    // Skip nodes already involved in another ignition event this cycle
    if (involvedNodeNames.count(aNodeName) ||
        involvedNodeNames.count(*zNodeName)) {
      continue;
    }

    std::chrono::seconds delay = dampenInterval;

    // After a long period of unsuccessful ignition attempts, wait longer
    // before trying again
    // TODO: We might want to reset when node config changes...
    auto initialAttemptTs = linkToInitialAttemptTs.find(igCandidate.linkName);
    if (initialAttemptTs != linkToInitialAttemptTs.end()) {
      auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(
          now - initialAttemptTs->second);
      if (elapsedSec >= extendedDampenFailureInterval) {
        delay = extendedDampenInterval;
      }
    }

    // Don't try to re-ignite the same link again too soon
    // Important: If igniting A -> B, we need to avoid trying to ignite B -> A
    // at the same time, or both ignition attempts will fail.
    auto lastTs = linkToAttemptTs.find(igCandidate.linkName);
    if (lastTs != linkToAttemptTs.end()) {
      auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(
          now - lastTs->second);
      if (elapsedSec < delay) {
        if (!dampenedLinks.count(igCandidate.linkName)) {
          auto remainingSec = delay - elapsedSec;
          VLOG(2) << igCandidate.linkName << " was last ignited "
                  << elapsedSec.count()
                  << " seconds ago - delaying re-ignition by "
                  << remainingSec.count() << "s...";
          dampenedLinks.insert(igCandidate.linkName);
        }
        continue;
      }
    }

    // Use this candidate
    linkToAttemptTs[igCandidate.linkName] = now;
    initiatorToAttemptTs[igCandidate.initiatorNodeName] = now;
    linkToInitialAttemptTs.insert(std::make_pair(igCandidate.linkName, now));
    realIgCandidates.push_back(igCandidate);
    involvedNodeNames.emplace(aNodeName);
    involvedNodeNames.emplace(*zNodeName);
  }

  std::sort(realIgCandidates.begin(), realIgCandidates.end());
  return realIgCandidates;
}

} // namespace terragraph
} // namespace facebook
