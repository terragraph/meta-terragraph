/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>
#include <unordered_set>

#include <folly/IPAddress.h>

#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/Topology_types.h"

#include "BasePrefixAllocator.h"
#include "../topology/TopologyWrapper.h"
#include "PrefixZone.h"

namespace facebook {
namespace terragraph {

/**
 * Handles deterministically allocating prefixes to nodes given a topology file.
 *
 * Nodes are split into PrefixZones, which are groupings of nodes to their
 * closest POP site. Each PrefixZone is then allocated a subnet of the network
 * prefix, that contains enough prefixes to allocate one to each node within the
 * PrefixZone. Length of node prefixes is set in controller config in
 * "prefixAllocParams.allocPrefixLen". If such an allocation is not possible,
 * the allocator will fail to allocate any prefixes.
 */
class DeterministicPrefixAllocator : public BasePrefixAllocator {
 public:
  /**
   * Initialize the helper for the given topology.
   *
   * This will also perform the necessary steps to either read existing
   * configuration from the topology or allocate prefixes from scratch.
   *
   * Currently, if zones exist in the topology, the allocator will attempt to
   * load all of those (with some basic validation). If this fails, an exception
   * will be thrown.
   */
  explicit DeterministicPrefixAllocator(
      const thrift::PrefixAllocParams& prefixAllocParams,
      TopologyWrapper *topologyW,
      ConfigHelper& configHelper);

  /**
   * Returns the mapping from allocated prefix to node name for all allocated
   * prefixes in the topology.
   */
  std::map<folly::CIDRNetwork, std::string> getAllocatedPrefixes() const;

  /** Returns mapping from zone name to a set of its allocated zone prefixes. */
  std::unordered_map<std::string, std::unordered_set<std::string>>
      getZonePrefixes() const;

  /**
   * Called when adding a new node to the topology.
   *
   * If the node is disconnected from the rest of the network, then it will not
   * not be assigned a zone or a prefix, and an error will be logged.
   *
   * If there are no more prefixes available in the node's zone, a
   * std::invalid_argument will be thrown.
   */
  void addNode(thrift::Node& node, ConfigHelper& configHelper);

  /** Unallocate the prefix for the deleted node. */
  void delNode(const thrift::Node& node, ConfigHelper& configHelper);

  /** Modify internal mappings to use the new node name, if it changed. */
  void editNode(const thrift::Node& oldNode, const thrift::Node& newNode);

  /** Check both sides of the new link and allocate any necessary prefixes. */
  void addLink(const thrift::Link& link, ConfigHelper& configHelper);

  /** Modify internal maps to use the new site name, if it changed. */
  void editSite(const std::string& siteName, const thrift::Site& newSite);

  /** Deterministically (re)allocate prefixes to all nodes. */
  void allocate(ConfigHelper& configHelper);

  /**
   * Change the prefixAllocParams and reallocate all prefixes.
   *
   * This will also update the BGP summary route in POP node config.
   *
   * The prefixAllocParams will first be validated and an std::invalid_argument
   * will be thrown if it fails validation.
   */
  void updatePrefixAllocParams(
      const thrift::PrefixAllocParams& prefixAllocParams,
      ConfigHelper& configHelper);

 private:
  /** Mapping from site name to PrefixZone. */
  using PrefixZones = std::unordered_map<std::string, PrefixZone>;

  /**
   * Populate PrefixZones (zones_) using the data stored in the topology file.
   *
   * This does not reallocate any prefixes.
   */
  void populateZonesAndPrefixes(
      ConfigHelper& configHelper,
      std::unordered_map<std::string, thrift::Zone>& zones);

  /** Validate that all POPs are in a zone */
  void validatePopNodeZone();

  /** Validate that a zone's site name is a valid site in the topology. */
  void validateZoneSiteName(const std::string& zoneSiteName);

  /**
   * Validate that a zone meets the following criteria:
   * - Zone prefixes are in seed prefix subnet
   * - Zone prefix space is big enough for all its nodes
   */
  void validateZone(
      const std::string& zoneName, const thrift::Zone& thriftZone);

  /**
   * Validate that a node prefix meets the following criteria:
   * - In the subnet of any of its zone's prefixes
   * - CIDR length is equal to allocPrefixLen
   * - Unique within the zone
   */
  void validateNodePrefix(thrift::Node& node, PrefixZone& zone);

  /** Run a BFS to group all nodes to their closest POP site. */
  std::unordered_map<std::string, std::pair<int, std::string>>
      batchNodesToPopSites();

  /** Run a BFS from a POP site to calculate hop counts to all nodes. */
  std::unordered_map<std::string, int> getNodeDistancesFromPopSite(
      const std::string& popSiteName);

  /**
   * Allocate prefix spaces to all zones. A prefix space is the number of
   * available prefixes to allocate for each node.
   */
  void allocatePrefixSpaces(int networkPrefixSpace);

  /** Recursively allocate prefix spaces to zones. */
  std::optional<PrefixZones> allocatePrefixSpacesHelper(
      PrefixZones& zones, int prefixSpace);

  /**
   * Allocate subnet prefixes to each PrefixZone.
   *
   * Each PrefixZone must contain enough prefixes to allocate a prefix of length
   * allocPrefixLen_ to each node within that PrefixZone.
   *
   * This works by first allocating prefix spaces to each zone. That is, it
   * calculates the number of nodes in each zone, then allocates prefix spaces
   * (powers of 2) to each zone until the sum of the prefix spaces is greater
   * than or equal to the number of nodes in the zone. Then, the prefix spaces
   * are converted to zone prefixes (subnets of the overall network prefix)
   * using `allocateZonePrefixesHelper()`.
   */
  void allocateZonePrefixes();

  /**
   * Recursively find an available offset with prefix length
   * `prefixLength - networkPrefixLength` not in `allocatedZonePrefixes`.
   */
  std::optional<uint32_t> allocateZonePrefixesHelper(
      uint32_t networkPrefixLength,
      uint32_t networkPrefixOffset,
      uint32_t prefixLength,
      std::unordered_map<uint32_t, uint32_t>& allocatedZonePrefixes);

  /** Allocate prefixes to every node in every zone. */
  void allocateNodePrefixes();

  /** Get the next unallocated node prefix in `zone`. */
  std::optional<folly::CIDRNetwork> getNextUnallocatedPrefix(
      PrefixZone& zone);

  /**
   * Calculate the prefix space (number of available prefixes) in the network
   * given that devices each need a prefix of length `allocPrefixLen`.
   */
  uint32_t networkToPrefixSpace(
      folly::CIDRNetwork network, uint32_t allocPrefixLen);

  /**
   * Find the zone closest to a node.
   *
   * Requires that the node already have at least 1 link, otherwise it is
   * unreachable.
   */
  std::optional<std::string> findClosestZone(const thrift::Node& node) const;

  /** Get the names of the sites with POP nodes. */
  std::unordered_set<std::string> getPopSiteNames() const;

  /** Write zone prefixes to appropriate POP node configs. */
  void writeToConfig(ConfigHelper& configHelper);

  /** Reformat and set zone data in the topology. */
  void setTopologyZones();

  /** Prefix zones for the entire network. */
  PrefixZones zones_{};

  /** Map from node name to zone name. */
  std::unordered_map<std::string, std::string> node2Zone_{};
};

} // namespace terragraph
} // namespace facebook
