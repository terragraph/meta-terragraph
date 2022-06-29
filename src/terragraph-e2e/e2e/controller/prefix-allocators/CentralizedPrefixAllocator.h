/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "BasePrefixAllocator.h"

namespace facebook {
namespace terragraph {

/**
 * Handles allocating prefixes to nodes given a topology file.
 *
 * Nodes are visited arbitrarily and assigned the first unallocated prefix from
 * the given prefix allocation parameters. That is, for every node, simple
 * prefix allocation will make a linear scan through the available prefix space
 * and assign the first unassigned prefix to the node.
 */
class CentralizedPrefixAllocator final : public BasePrefixAllocator {
 public:
  /**
   * Constructor.
   *
   * This will invoke populatePrefixAllocParams() followed by setSummaryRoute().
   */
  explicit CentralizedPrefixAllocator(
      const thrift::PrefixAllocParams& prefixAllocParams,
      TopologyWrapper *topologyW,
      ConfigHelper& configHelper);

  /**
   * Returns the mapping from allocated prefix to node name for all allocated
   * prefixes in the topology.
   */
  std::map<folly::CIDRNetwork, std::string> getAllocatedPrefixes() const;

  /**
   * Return a mapping of the POP sites to the BGP routes that will be
   * advertised. For SPA, each POP site will be advertising the entire seed
   * prefix.
   */
  std::unordered_map<std::string, std::unordered_set<std::string>>
      getZonePrefixes() const;

  // Allocates a prefix for the added node, if needed. Otherwise, this will just
  // validate a given prefix. If there are no more prefixes available
  // std::invalid_argument will be thrown.
  void addNode(thrift::Node& node, ConfigHelper& configHelper);

  /** Unallocates the prefix for the deleted node. */
  void delNode(const thrift::Node& node, ConfigHelper& configHelper);

  /** Modifies internal mappings to use the new node name, if it changed. */
  void editNode(const thrift::Node& oldNode, const thrift::Node& newNode);

  /** (Re)allocates prefixes to all nodes. */
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

  /** unused */
  void addLink(const thrift::Link& /*link*/, ConfigHelper& /*configHelper*/) {}

  /** unused */
  void editSite(
      const std::string& /*siteName*/, const thrift::Site& /*newSite*/) {}

 private:
  /** Populate/allocate/validate prefixes for all nodes. */
  void populatePrefixes();

  /**
   * Get the next unallocated prefix.
   *
   * Throws invalid_argument if no more prefixes are available.
   */
  folly::CIDRNetwork getNextUnallocatedPrefix();

  /**
   * Perform validation on the prefix of a given node.
   *
   * Throws std::invalid_argument if the node's assigned prefix is:
   * - Not in the network subnet
   * - Longer than the allocated prefix length
   *   (set in controller config in "prefixAllocParams.allocPrefixLen")
   * - Not unique
   */
  void validatePrefix(const thrift::Node& node, bool validateSubnet);

  /**
   * Write the seedPrefix to the config for POP nodes, ensuring that the
   * seedPrefix will be advertised over BGP.
   *
   * This will prevent creating a networking black hole.
   */
  void setSummaryRoute(ConfigHelper& configHelper);

  /** Assigns the given prefix to the given node. */
  void assignPrefixToNode(const thrift::Node& node, folly::CIDRNetwork prefix);

  /** Map of allocated prefix to node name. */
  std::map<folly::CIDRNetwork, std::string> prefix2NodeName_;
};

} // namespace terragraph
} // namespace facebook
