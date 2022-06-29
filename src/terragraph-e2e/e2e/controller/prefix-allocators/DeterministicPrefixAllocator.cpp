/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "DeterministicPrefixAllocator.h"

#include <algorithm>
#include <cmath>
#include <deque>

#include <folly/Format.h>
#include <folly/MacAddress.h>
#include <openr/common/Util.h>
#include <openr/common/LsdbUtil.h>

DEFINE_int32(
    dpa_zone_prefix_space_buffer,
    7,
    "Amount of headroom to add to the prefix space allocated to each "
    "PrefixZone to allow adding more nodes to each zone.");

namespace {
  int nextPowerOf2(int x) {
    int power = 1;
    while (power < x) {
      power *= 2;
    }
    return power;
  }

  bool isPowerOf2(int x) {
    return (x != 0) && ((x & (x - 1)) == 0);
  }
}

namespace facebook {
namespace terragraph {
DeterministicPrefixAllocator::DeterministicPrefixAllocator(
    const thrift::PrefixAllocParams& prefixAllocParams,
    TopologyWrapper *topologyW,
    ConfigHelper& configHelper)
    : BasePrefixAllocator(prefixAllocParams, topologyW) {

  // If zones exist in topology file, then use those. Otherwise, reallocate from
  // scratch.
  // TODO Make this more flexible. Allow partially specified zone or
  // node prefixes.
  auto zones = topologyW_->getPrefixAllocZones();
  if (zones) {
    try {
      populateZonesAndPrefixes(configHelper, *zones);
      LOG(INFO) << "Found zones in topology file, skipping allocation...";
    } catch (const std::invalid_argument& ex) {
      LOG(INFO) << "Deterministic prefix allocation is invalid, re-allocating "
                   "from scratch...";
      allocate(configHelper);
    }
  } else {
    LOG(INFO) << "No zones found in topology file, "
              << "allocating prefixes for network...";
    allocate(configHelper);
  }
}

void
DeterministicPrefixAllocator::allocate(ConfigHelper& configHelper) {
  // Group nodes with their closest pop site
  auto node2ClosestPopSite = batchNodesToPopSites();

  // Add nodes to appropriate zone structure
  zones_.clear();
  node2Zone_.clear();
  for (const auto& entry : node2ClosestPopSite) {
    auto nodeName = entry.first;
    auto popSiteName = entry.second.second;
    if (!zones_.count(popSiteName)) {
      zones_[popSiteName] = PrefixZone();
    }
    zones_[popSiteName].addNode(nodeName);
    node2Zone_[nodeName] = popSiteName;
  }

  // Add some room for other nodes to be added to the topology
  for (auto& entry : zones_) {
    int zoneSize = entry.second.size();
    entry.second.setAllocationSizeOverride(
        zoneSize + FLAGS_dpa_zone_prefix_space_buffer);

    VLOG(4) << folly::format(
        "POP SITE `{}` has `{}` nodes with allocation size: `{}`",
        entry.first,
        zoneSize,
        zoneSize + FLAGS_dpa_zone_prefix_space_buffer);
  }

  // Allocate prefixes to each zone
  allocateZonePrefixes();

  // Allocate prefixes to each node
  allocateNodePrefixes();

  // Write zone prefixes to config
  writeToConfig(configHelper);
}

void
DeterministicPrefixAllocator::writeToConfig(ConfigHelper& configHelper) {
  // Iterate through pop nodes
  std::string errorMsg;
  for (const auto& node : topologyW_->getAllNodes()) {
    if (!node.pop_node) {
      configHelper.setAutoNodeSpecificNetworkPrefixes(
          node.name, std::nullopt, errorMsg);
      continue;
    }

    // Find zone containing pop node
    const auto iter = node2Zone_.find(node.name);
    if (iter == node2Zone_.end()) {
      LOG(ERROR) << folly::format("POP node {} is not in a zone", node.name);
      continue;
    }
    auto& zone = zones_[iter->second];

    // Write the zone's zone prefixes to the config for that pop node
    auto prefixStrings = zone.getZonePrefixStrings();
    // Add seedPrefix to prefixStrings to ensure it is written to config
    auto seedPrefixStr = folly::IPAddress::networkToString(
        prefixAllocParams_.first);
    prefixStrings.insert(seedPrefixStr);
    if (!configHelper.setAutoNodeSpecificNetworkPrefixes(
          node.name, folly::join(",", prefixStrings), errorMsg)) {
      LOG(ERROR) << folly::format(
          "Couldn't write auto node specific network prefixes for popNode {}: "
          "{}",
          node.name,
          errorMsg);
    }
  }
}

void
DeterministicPrefixAllocator::populateZonesAndPrefixes(
    ConfigHelper& configHelper,
    std::unordered_map<std::string, thrift::Zone>& zones) {

  zones_.clear();
  node2Zone_.clear();
  for (const auto& zoneEntry : zones) {
    const auto& zoneSiteName = zoneEntry.first;
    validateZoneSiteName(zoneSiteName);
    validateZone(zoneSiteName, zoneEntry.second);

    // Create zone
    PrefixZone zone(zoneEntry.second, prefixAllocParams_.second);

    // Add all node prefixes to zone structure
    for (const auto& nodeName : zoneEntry.second.node_names) {
      auto node = topologyW_->getNode(nodeName);
      if (!node) {
        throw std::invalid_argument(
            folly::sformat("Invalid nodeName: {}", nodeName));
      }

      validateNodePrefix(*node, zone);
      zone.assignNodePrefix(
          nodeName, folly::IPAddress::createNetwork(node->prefix_ref()
              .value()));

      // Add to node2Zone_ map
      node2Zone_[nodeName] = zoneEntry.first;
    }

    // Save zone
    zones_[zoneEntry.first] = zone;
  }

  // ensure existing pop nodes are in a zone
  validatePopNodeZone();

  // Write zone prefixes to config, in case anything changed in topology (e.g.
  // pop_nodes were tagged incorrectly)
  writeToConfig(configHelper);
}

void
DeterministicPrefixAllocator::validatePopNodeZone() {
  for (const auto& node : topologyW_->getAllNodes()) {
    if (!node.pop_node) {
      continue;
    }
    // Find zone containing pop node
    if (!node2Zone_.count(node.name)) {
      throw std::invalid_argument(
          folly::sformat("POP node {} is not in a zone", node.name));
    }
  }
}

void
DeterministicPrefixAllocator::validateZoneSiteName(
    const std::string& zoneSiteName) {
  if (!topologyW_->getSite(zoneSiteName)) {
    throw std::invalid_argument(
        folly::sformat("Site {} does not exist", zoneSiteName));
  }

  auto popSiteNames = getPopSiteNames();
  if (popSiteNames.find(zoneSiteName) == popSiteNames.end()) {
    LOG(WARNING) << folly::format(
        "Zone site name {} is not a pop site.", zoneSiteName);
  }
}

void
DeterministicPrefixAllocator::validateZone(
    const std::string& zoneName, const thrift::Zone& thriftZone) {
  auto seedPrefix = prefixAllocParams_.first;

  // TODO consolidate zone prefixes (?)

  unsigned int totalPrefixSpace = 0;
  std::unordered_set<int> allocatedPrefixSpaces;
  for (const auto& zonePrefix : thriftZone.zone_prefixes) {
    auto network = folly::IPAddress::createNetwork(zonePrefix);

    // Make sure zone prefix is in seed prefix subnet
    if (!network.first.inSubnet(seedPrefix.first, seedPrefix.second)) {
      throw std::invalid_argument(folly::sformat(
          "Invalid prefix ({}) in zone {} (not in subnet of network prefix {})",
          zonePrefix,
          zoneName,
          folly::IPAddress::networkToString(seedPrefix)));
    }

    auto prefixSpace = networkToPrefixSpace(network, prefixAllocParams_.second);
    allocatedPrefixSpaces.insert(prefixSpace);
    totalPrefixSpace += prefixSpace;
  }

  // Make sure zone has enough prefix space to account for all its nodes
  if (totalPrefixSpace < thriftZone.node_names.size()) {
    throw std::invalid_argument(folly::sformat(
        "Zone {} has more nodes ({}) than available prefix space ({})",
        zoneName,
        thriftZone.node_names.size(),
        totalPrefixSpace));
  }
}

void
DeterministicPrefixAllocator::validateNodePrefix(
    thrift::Node& node, PrefixZone& zone) {
  // Make sure prefix is valid
  folly::CIDRNetwork prefix;
  try {
    prefix = folly::IPAddress::createNetwork(node.prefix_ref().value());
  } catch (std::exception const& err) {
    throw std::invalid_argument(folly::sformat(
        "Invalid prefix ({}) for {}, err: {}",
        node.prefix_ref().value(), node.name, folly::exceptionStr(err)));
  }

  // Make sure prefix is in subnet of any of the zone prefixes
  bool prefixInZonePrefixes = false;
  for (const auto& zonePrefix : zone.getZonePrefixes()) {
    if (prefix.first.inSubnet(zonePrefix.first, zonePrefix.second)) {
      prefixInZonePrefixes = true;
      break;
    }
  }

  if (!prefixInZonePrefixes) {
    throw std::invalid_argument(folly::sformat(
        "Invalid prefix ({}) for {} (not in subnet of any zone prefixes: {})",
        node.prefix_ref().value(),
        node.name,
        folly::join(",", zone.getZonePrefixStrings())));
  }

  // Make sure prefix CIDR equal to allocPrefixLen
  if (prefix.second != prefixAllocParams_.second) {
    throw std::invalid_argument(folly::sformat(
        "Invalid prefix ({}) for {} (longer than allocPrefixLen ({}) bits)",
        node.prefix_ref().value(), node.name, prefixAllocParams_.second));
  }

  // Make sure prefix is unique
  auto allocatedPrefixes = zone.getAllocatedNodePrefixes();
  const auto it = allocatedPrefixes.find(prefix);
  if (it != allocatedPrefixes.end() && it->second != node.name) {
    throw std::invalid_argument(folly::sformat(
        "Invalid prefix ({}) for {} (prefix already allocated to node {})",
        node.prefix_ref().value(), node.name, it->second));
  }
}

std::unordered_map<std::string, std::pair<int, std::string>>
DeterministicPrefixAllocator::batchNodesToPopSites() {
  // nodeName -> (hopCount, siteName)
  std::unordered_map<std::string, std::pair<int, std::string>>
      node2ClosestPopSite;

  auto popSiteNames = getPopSiteNames();
  if (popSiteNames.empty()) {
    LOG(ERROR) << "There are no POP sites in the topology.";
    return node2ClosestPopSite;
  }

  // Find closest pop for each node
  for (const auto& popSiteName : popSiteNames) {
    std::unordered_map<std::string, int> node2HopCount =
        getNodeDistancesFromPopSite(popSiteName);

    // Save the lower distance to node2ClosestPopSite
    for (const auto& entry : node2HopCount) {
      auto nodeName = entry.first;
      int distance = entry.second;
      auto iter = node2ClosestPopSite.find(nodeName);
      if (iter == node2ClosestPopSite.end()) {
        node2ClosestPopSite[nodeName] = std::make_pair(distance, popSiteName);
      } else {
        // TODO random choice if equidistant from multiple pop sites (?)
        if (distance < iter->second.first) {
          node2ClosestPopSite[nodeName] = std::make_pair(distance, popSiteName);
        }
      }
    }
  }

  return node2ClosestPopSite;
}

std::unordered_map<std::string, int>
DeterministicPrefixAllocator::getNodeDistancesFromPopSite(
    const std::string& popSiteName) {
  std::unordered_set<std::string> visitedSiteNames;
  std::unordered_map<std::string, int> node2HopCount;

  const auto popNodes = topologyW_->getNodeNamesBySiteName(popSiteName);
  if (!popNodes) {
    LOG(ERROR) << folly::format(
        "No nodes found from pop site: {}", popSiteName);
    return node2HopCount;
  }

  // Initialize popNodes to distance 0
  for (const auto& node : *popNodes) {
    node2HopCount[node] = 0;
  }

  std::deque<std::string> queue;

  visitedSiteNames.insert(popSiteName);
  queue.push_back(popSiteName);

  while(!queue.empty()) {
    std::string currSiteName = queue.front();
    queue.pop_front();

    // The following visits unvisited adjacent sites. There are 4 steps.
    // 1. Get all the nodes at the current site.
    // 2. For each node, get all the links for that node.
    // 3. Follow each of those links to get the neighbor nodes.
    // 4. If the neighbor node is on an unvisited site, that site is an
    //    unvisited adjacent site.

    // 1. Get all the nodes at the current site.
    auto currSiteNodeNames = topologyW_->getNodeNamesBySiteName(currSiteName);
    if (!currSiteNodeNames) {
      continue;
    }

    for (const auto& currSiteNodeName : *currSiteNodeNames) {
      int distance = node2HopCount[currSiteNodeName] + 1;

      // 2. For each node, get all the links for that node.
      auto links = topologyW_->getLinksByNodeName(currSiteNodeName);

      // 3. Follow each of those links to get the neighbor nodes.
      for (const auto& link : links) {
        auto nbrNode = topologyW_->getNbrNode(
            currSiteNodeName, link.name);
        if (!nbrNode) {
          continue;
        }

        // 4. If the neighbor node is on an unvisited site, that site is an
        //    unvisited adjacent site.
        if (!visitedSiteNames.count(nbrNode->site_name)) {
          visitedSiteNames.insert(nbrNode->site_name);
          queue.push_back(nbrNode->site_name);

          // Save distances for all the neighbor nodes
          auto nbrNodeNames = topologyW_->getNodeNamesBySiteName(
              nbrNode->site_name);
          if (!nbrNodeNames) {
            continue;
          }
          for (const auto& nbrNodeName : *nbrNodeNames) {
            node2HopCount[nbrNodeName] = distance;
          }
        }
      }
    }
  }

  return node2HopCount;
}

void
DeterministicPrefixAllocator::allocateZonePrefixes() {
  auto seedPrefix = prefixAllocParams_.first;
  uint32_t networkPrefixSpace = networkToPrefixSpace(
      seedPrefix, prefixAllocParams_.second);

  // Allocate prefix spaces to each zone
  allocatePrefixSpaces(networkPrefixSpace);
  if (zones_.empty()) {
    LOG(ERROR) << "Unable to allocate enough prefix space to all zones!";
    return;
  }

  // Aggregate prefix spaces from all zones and convert to prefix lengths.
  // Given an allocPrefixLen of /64, then a prefix space of 32 becomes /59
  // (32 = 2^(64 - 59)).
  std::unordered_map<int, std::unordered_set<std::string>>
      prefixLengthsToZoneName;
  for (const auto& entry : zones_) {
    auto& zone = entry.second;
    for (const auto& space : zone.getAllocatedPrefixSpaces()) {
      // prefix space -> prefix length
      uint32_t prefixLength = seedPrefix.second + static_cast<uint32_t>(log2(
          networkPrefixSpace / space));

      prefixLengthsToZoneName[prefixLength].insert(entry.first);
    }
  }

  // Sort prefixLengths from shortest to longest otherwise calls to
  // allocateZonePrefixesHelper() will fail.
  std::vector<uint32_t> prefixLengths;
  for (const auto& entry : prefixLengthsToZoneName) {
    prefixLengths.push_back(entry.first);
  }
  std::sort(prefixLengths.begin(), prefixLengths.end());

  // Allocate subnet prefixes for each zone.
  std::unordered_map<uint32_t, uint32_t> allocatedZonePrefixes;
  // For each prefixLength (sorted shortest to longest),
  for (const auto& prefixLength : prefixLengths) {
    // For all zones that require said prefixLength
    for (const auto& zoneName : prefixLengthsToZoneName[prefixLength]) {
      // Get the next zone prefix with length prefixLength
      auto networkPrefixOffset = allocateZonePrefixesHelper(
          seedPrefix.second, 0, prefixLength, allocatedZonePrefixes);
      if (!networkPrefixOffset) {
        throw std::invalid_argument(folly::sformat(
            "Unable to find an available networkPrefixOffset "
            "for prefixLength {}",
            prefixLength));
      }

      // Allocate zone prefix to zone
      auto zonePrefix = openr::getNthPrefix(
          seedPrefix, prefixLength, *networkPrefixOffset);
      zones_[zoneName].addZonePrefix(zonePrefix);

      VLOG(4) << folly::format(
          "Allocated {} to zone {}",
          folly::IPAddress::networkToString(zonePrefix),
          zoneName);
    }
  }

  setTopologyZones();
}

std::optional<uint32_t>
DeterministicPrefixAllocator::allocateZonePrefixesHelper(
    uint32_t networkPrefixLength,
    uint32_t networkPrefixOffset,
    uint32_t prefixLength,
    std::unordered_map<uint32_t, uint32_t>& allocatedZonePrefixes) {
  // Recursed too far, nothing was found in this direction
  if (networkPrefixLength > prefixLength) {
    return std::nullopt;
  }

  // Already allocated this offset to a shorter or equal length prefix
  auto iter = allocatedZonePrefixes.find(networkPrefixOffset);
  if (iter != allocatedZonePrefixes.end() &&
      iter->second <= networkPrefixLength) {
    return std::nullopt;
  }

  // Found an available offset
  if (networkPrefixLength == prefixLength) {
    allocatedZonePrefixes[networkPrefixOffset] = networkPrefixLength;
    return networkPrefixOffset;
  }

  // Set current bit to 0 and recurse
  auto leftValue = allocateZonePrefixesHelper(
      networkPrefixLength + 1,
      networkPrefixOffset << 1,
      prefixLength,
      allocatedZonePrefixes);
  // If an available offset is found, return it
  if (leftValue) {
    return leftValue;
  }

  // Otherwise, set current bit to 1 and recurse
  return allocateZonePrefixesHelper(
      networkPrefixLength + 1,
      (networkPrefixOffset << 1) + 1,
      prefixLength,
      allocatedZonePrefixes);
}

void
DeterministicPrefixAllocator::allocatePrefixSpaces(int networkPrefixSpace) {
  // Clear allocated prefix spaces for all zones before reallocating
  for (auto& zonesEntry : zones_) {
    zonesEntry.second.clearPrefixSpaces();
  }

  auto zones = allocatePrefixSpacesHelper(zones_, networkPrefixSpace);
  if (!zones) {
    LOG(ERROR) << "Prefix space allocation failed!";
    return;
  }
  zones_ = *zones;
}

std::optional<DeterministicPrefixAllocator::PrefixZones>
DeterministicPrefixAllocator::allocatePrefixSpacesHelper(
    PrefixZones& zones, int prefixSpace) {
  if (zones.empty()) {
    return std::nullopt;
  }

  int totRem = 0;
  int numZones = 0;
  for (const auto &entry : zones) {
    int rem = entry.second.getRemaining();
    if (rem > 0) {
      numZones++;
      totRem += rem;
    }
  }

  // Return nothing if not enough prefixSpace or overallocated
  if (prefixSpace < totRem || prefixSpace < 0) {
    return std::nullopt;
  }

  // If allocated all of the prefixSpace and there is no more space to allocate,
  // return solution, otherwise nothing
  if (prefixSpace == 0) {
    return totRem == 0 ? std::make_optional(zones) : std::nullopt;
  }

  int numSegments = nextPowerOf2(numZones);
  while (true) {
    int prefixSpaceRem = prefixSpace;
    int perZone = prefixSpace / numSegments;
    if (!isPowerOf2(perZone)) {
      perZone = nextPowerOf2(perZone) >> 1;
    }

    PrefixZones zonesCopy = zones;
    for (auto &entry : zonesCopy) {
      PrefixZone &zone = entry.second;
      if (zone.getRemaining() > 0 || totRem == 0) {
        if (perZone > prefixSpaceRem) {
          break;
        }
        zone.addToPrefixSpace(perZone);
        prefixSpaceRem -= perZone;
        if (prefixSpaceRem <= 0) {
          break;
        }
      }
    }

    // Recurse with the remaining prefix space
    auto newZones = allocatePrefixSpacesHelper(zonesCopy, prefixSpaceRem);
    if (newZones) {
      // Return a solution if found
      for (const auto &entry : *newZones) {
        zonesCopy[entry.first].setAllocatedPrefixSpaces(
            entry.second.getAllocatedPrefixSpaces());
      }
      return zonesCopy;
    } else {
      // Double the prefixSpace divisor if not solution is found with the
      // current division
      numSegments *= 2;
      if (numSegments > prefixSpace) {
        return std::nullopt;
      }
    }
  }
}

void
DeterministicPrefixAllocator::allocateNodePrefixes() {
  // Loop through all zones and allocate a prefix for each node
  for (auto& zoneEntry : zones_) {
    for (const auto& nodeName : zoneEntry.second.getNodes()) {
      auto newPrefix = getNextUnallocatedPrefix(zoneEntry.second);
      if (!newPrefix) {
        throw std::invalid_argument(folly::sformat(
            "No available prefixes to assign to {}", nodeName));
      }

      // If a prefix is found, assign prefix to node
      zoneEntry.second.assignNodePrefix(nodeName, *newPrefix);
      topologyW_->setNodePrefix(nodeName, newPrefix);
      VLOG(4) << folly::format(
          "Assigned node {} prefix {}",
          nodeName,
          folly::IPAddress::networkToString(*newPrefix));
    }
  }
}

void
DeterministicPrefixAllocator::updatePrefixAllocParams(
    const thrift::PrefixAllocParams& prefixAllocParams,
    ConfigHelper& configHelper) {
  populatePrefixAllocParams(prefixAllocParams);
  allocate(configHelper);
}

std::optional<folly::CIDRNetwork>
DeterministicPrefixAllocator::getNextUnallocatedPrefix(PrefixZone& zone) {
  auto allocatedNodePrefixes = zone.getAllocatedNodePrefixes();
  auto zonePrefixes = zone.getZonePrefixes();

  // Sort zonePrefixes so we allocate from the larger prefix first
  std::vector<folly::CIDRNetwork> sortedZonePrefixes(
    zonePrefixes.begin(), zonePrefixes.end());
  std::sort(sortedZonePrefixes.begin(), sortedZonePrefixes.end());

  folly::CIDRNetwork newPrefix;
  for (const auto& zonePrefix : sortedZonePrefixes) {
    uint32_t prefixCount = 1 << (prefixAllocParams_.second - zonePrefix.second);

    // Loop to find an unallocated prefix
    // Simply make a linear pass through all possible prefixes and allocate the
    // first unallocated prefix.
    for (uint32_t newVal = 0; newVal < prefixCount; ++newVal) {
      newPrefix = openr::getNthPrefix(
          zonePrefix, prefixAllocParams_.second, newVal);
      const auto it = allocatedNodePrefixes.find(newPrefix);
      if (it == allocatedNodePrefixes.end()) {
        // Found an unallocated prefix
        return newPrefix;
      }
    }
  }

  return std::nullopt;
}

std::map<folly::CIDRNetwork, std::string>
DeterministicPrefixAllocator::getAllocatedPrefixes() const {
  std::map<folly::CIDRNetwork, std::string> ret;

  // Aggregate all prefixes into a single map
  for (const auto& zonesEntry : zones_) {
    auto& zone = zonesEntry.second;
    const auto& allocatedNodePrefixes = zone.getAllocatedNodePrefixes();
    ret.insert(allocatedNodePrefixes.begin(), allocatedNodePrefixes.end());
  }

  return ret;
}

uint32_t
DeterministicPrefixAllocator::networkToPrefixSpace(
    folly::CIDRNetwork network, uint32_t allocPrefixLen) {
  return 1 << (allocPrefixLen - network.second);
}

std::unordered_map<std::string, std::unordered_set<std::string>>
DeterministicPrefixAllocator::getZonePrefixes() const {
  std::unordered_map<std::string, std::unordered_set<std::string>> ret;
  for (const auto& zoneEntry : zones_) {
    ret[zoneEntry.first] =
        zoneEntry.second.getZonePrefixStrings();
  }
  return ret;
}

std::unordered_set<std::string>
DeterministicPrefixAllocator::getPopSiteNames() const {
  std::unordered_set<std::string> popSiteNames;

  // Populate popSiteNames
  for (const auto& node : topologyW_->getAllNodes()) {
    if (node.pop_node) {
      popSiteNames.insert(node.site_name);
    }
  }

  return popSiteNames;
}

void
DeterministicPrefixAllocator::setTopologyZones() {
  // Reformat zones and save to topology file
  std::unordered_map<std::string, thrift::Zone> zones;
  for (const auto& zoneEntry : zones_) {
    zones[zoneEntry.first].node_names = zoneEntry.second.getNodes();
    zones[zoneEntry.first].zone_prefixes =
        zoneEntry.second.getZonePrefixStrings();
  }
  topologyW_->setPrefixZones(zones);
}

void
DeterministicPrefixAllocator::addNode(
    thrift::Node& node, ConfigHelper& configHelper) {
  if (node.pop_node && zones_.find(node.site_name) == zones_.end()) {
    // Reallocate all prefixes if node is a pop_node in a new zone
    // TODO skb, make this smarter by not completely reallocating
    LOG(INFO) << "Added new pop node/site, reallocating all prefixes...";
    allocate(configHelper);
    return;
  }

  auto nodeName = node.name;
  auto zoneName = findClosestZone(node);
  if (!zoneName) {
    LOG(ERROR) << folly::format(
        "Could not find an existing zone that {} belongs to. "
        "This node will be allocated a prefix when a link is added to it.",
        nodeName);
    return;
  }

  auto& zone = zones_[*zoneName];
  if (zone.size() == zone.maxSize()) {
    throw std::invalid_argument(folly::sformat(
        "No available prefixes available in zone {} to assign to node {}",
        *zoneName,
        nodeName));
  }

  auto newPrefix = getNextUnallocatedPrefix(zone);
  if (!newPrefix) {
    throw std::invalid_argument(folly::sformat(
        "No available prefixes available in zone {} to assign to node {}",
        *zoneName,
        nodeName));
  }

  zone.addNode(nodeName);
  zone.assignNodePrefix(nodeName, *newPrefix);
  node2Zone_[nodeName] = *zoneName;
  topologyW_->setNodePrefix(nodeName, newPrefix);
  setTopologyZones();
  VLOG(4) << folly::format(
      "Assigned node {} in zone {} prefix {}",
      nodeName,
      *zoneName,
      folly::IPAddress::networkToString(*newPrefix));
}

std::optional<std::string>
DeterministicPrefixAllocator::findClosestZone(
    const thrift::Node& node) const {
  std::unordered_set<std::string> visitedSiteNames;
  std::deque<std::string> queue;

  visitedSiteNames.insert(node.site_name);
  queue.push_back(node.site_name);

  while(!queue.empty()) {
    std::string currSiteName = queue.front();
    queue.pop_front();

    // If currSiteName is a zone, return
    if (zones_.find(currSiteName) != zones_.end()) {
      return currSiteName;
    }

    auto currSiteNodeNames = topologyW_->getNodeNamesBySiteName(currSiteName);
    if (!currSiteNodeNames) {
      continue;
    }

    for (const auto& currSiteNodeName : *currSiteNodeNames) {
      // For each node, get all the links for that node.
      auto links = topologyW_->getLinksByNodeName(currSiteNodeName);

      // Follow each of those links to get the neighbor nodes.
      for (const auto& link : links) {
        auto nbrNode = topologyW_->getNbrNode(
            currSiteNodeName, link.name);
        if (!nbrNode) {
          continue;
        }

        // If the neighbor node is on an unvisited site, that site is an
        // unvisited adjacent site.
        if (!visitedSiteNames.count(nbrNode->site_name)) {
          visitedSiteNames.insert(nbrNode->site_name);
          queue.push_back(nbrNode->site_name);
        }
      }
    }
  }

  return std::nullopt;
}

void
DeterministicPrefixAllocator::delNode(
    const thrift::Node& node, ConfigHelper& configHelper) {
  // Check if node is a pop, because we might need to reallocate prefixes
  if (node.pop_node && zones_.find(node.site_name) != zones_.end()) {
    // Remove config for node
    std::string errorMsg;
    configHelper.setAutoNodeSpecificNetworkPrefixes(
        node.name, std::nullopt, errorMsg);

    // Check for any other pop nodes at this site
    auto nodesAtSite = topologyW_->getNodesBySiteName(node.site_name);
    bool siteHasOtherPopNodes = false;
    for (const auto& siteNode : nodesAtSite) {
      if (siteNode.pop_node) {
        siteHasOtherPopNodes = true;
      }
    }

    // Reallocate since there are no other pop nodes left at this site
    if (!siteHasOtherPopNodes) {
      // TODO skb, make this smarter by not completely reallocating
      LOG(INFO) << "Deleting pop node/site, reallocating all prefixes...";
      allocate(configHelper);
      return;
    }
  }

  const auto& nodeName = node.name;
  auto iter = node2Zone_.find(nodeName);
  if (iter == node2Zone_.end()) {
    LOG(ERROR) << folly::format("Node {} is not in a zone", nodeName);
    return;
  }

  auto& zone = zones_[iter->second];
  if (node.prefix_ref().has_value()) {
    auto prefix = folly::IPAddress::createNetwork(node.prefix_ref().value());
    if (!zone.delNode(nodeName, prefix)) {
      LOG(ERROR) << folly::format(
          "Error deleting prefix for node {}", nodeName);
    } else {
      LOG(INFO) << "Deleted prefix for node: " << nodeName
                << " prefix: " << node.prefix_ref().value();
    }
    setTopologyZones();
  }
  node2Zone_.erase(iter);
}

void
DeterministicPrefixAllocator::editNode(
    const thrift::Node& oldNode, const thrift::Node& newNode) {
  auto iter = node2Zone_.find(oldNode.name);
  if (iter == node2Zone_.end()) {
    LOG(ERROR) << folly::format("Node {} is not in a zone", oldNode.name);
    return;
  }

  auto zoneName = iter->second;
  auto& zone = zones_[zoneName];
  bool hasNewName = !newNode.name.empty() && (newNode.name != oldNode.name);
  if (hasNewName && oldNode.prefix_ref().has_value()) {
    auto prefix = folly::IPAddress::createNetwork(oldNode.prefix_ref().value());
    zone.editNodeName(oldNode.name, prefix, newNode.name);

    // Delete old node2Zone mapping
    node2Zone_.erase(iter);
    node2Zone_[newNode.name] = zoneName;

    // Write to topology file
    setTopologyZones();
  }

  bool hasPopNodeChanged = oldNode.site_name == newNode.site_name &&
      oldNode.pop_node != newNode.pop_node;

  if (hasPopNodeChanged) {
    LOG(ERROR) << "Changing pop_node status is currently unsupported with "
        << "Deterministic Prefix Allocation enabled. Please delete and re-add "
        << "the node or trigger a prefix reallocation.";
  }
}

void
DeterministicPrefixAllocator::addLink(
    const thrift::Link& link, ConfigHelper& configHelper) {

  if (!node2Zone_.count(link.a_node_name)) {
    auto node = topologyW_->getNode(link.a_node_name);
    addNode(*node, configHelper);
  }

  if (!node2Zone_.count(link.z_node_name)) {
    auto node = topologyW_->getNode(link.z_node_name);
    addNode(*node, configHelper);
  }

  topologyW_->writeToTsFile();
}

void
DeterministicPrefixAllocator::editSite(
    const std::string& siteName, const thrift::Site& newSite) {
  if (newSite.name == siteName || newSite.name.empty()) {
    return;
  }
  VLOG(3) << folly::format(
      "Changing site with name {} to {}", siteName, newSite.name);

  // change name in zones_
  auto iter = zones_.find(siteName);
  if (iter != zones_.end()) {
    auto zone = iter->second;
    zones_.erase(iter);
    zones_[newSite.name] = zone;
  }

  // change name in node2Zone_
  for (auto& entry : node2Zone_) {
    if (entry.second == siteName) {
      entry.second = newSite.name;
    }
  }

  setTopologyZones();
};

} // namespace terragraph
} // namespace facebook
