/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/IPAddress.h>

#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/Topology_types.h"

#include "../ConfigHelper.h"
#include "../topology/TopologyWrapper.h"

namespace facebook {
namespace terragraph {

/**
 * Abstract base class for allocated prefixes to nodes, given a topology file.
 *
 * This abstract class represents the interface from which all prefix allocators
 * must be derived.
 */
class BasePrefixAllocator {
 public:
  /**
   * Constructor.
   *
   * This will invoke populatePrefixAllocParams().
   */
  explicit BasePrefixAllocator(
      const thrift::PrefixAllocParams& prefixAllocParams,
      TopologyWrapper *topologyW);

  /** Destructor. */
  virtual ~BasePrefixAllocator() {};

  /**
   * Return the mapping from allocated prefix to node name for all allocated
   * prefixes in the topology.
   */
  virtual std::map<folly::CIDRNetwork, std::string> getAllocatedPrefixes() const
      = 0;

  /** Called after adding a new node to the topology. */
  virtual void addNode(thrift::Node& node, ConfigHelper& configHelper) = 0;

  /** Called after deleting a node from the topology. */
  virtual void delNode(
      const thrift::Node& node, ConfigHelper& configHelper) = 0;

  /** Called after editing a node in the topology. */
  virtual void editNode(
      const thrift::Node& oldNode, const thrift::Node& newNode) = 0;

  /** Called after adding a link to the topology. */
  virtual void addLink(
      const thrift::Link& link, ConfigHelper& configHelper) = 0;

  /** Called after editing a site in the topology. */
  virtual void editSite (
      const std::string& siteName, const thrift::Site& newSite) = 0;

  /** Called to reallocate prefixes to the entire network. */
  virtual void allocate(ConfigHelper& configHelper) = 0;

  /** Called when the prefixAllocParams are changed. */
  virtual void updatePrefixAllocParams(
      const thrift::PrefixAllocParams& prefixAllocParams,
      ConfigHelper& configHelper) = 0;

  /**
   * Return a mapping of the POP sites to the BGP routes that will be
   * advertised.
   */
  virtual std::unordered_map<std::string, std::unordered_set<std::string>>
      getZonePrefixes() const { return {}; };

 protected:
  /**
   * Populate prefix allocation params.
   *
   * Can throw exceptions with folly::IPAddress::createNetwork().
   */
  void populatePrefixAllocParams(
      const thrift::PrefixAllocParams& prefixAllocParams);

  /**
   * Validate that the network prefix info has a valid seed_prefix string and
   * alloc_prefix_len is in a valid range, i.e. (seed_prefix CIDR, 128].
   *
   * Throws std::invalid_argument if validation fails.
   */
  void validatePrefixAllocParams(
      const thrift::PrefixAllocParams& prefixAllocParams) const;

  /** Pointer to TopologyWrapper to manipulate the topology. */
  TopologyWrapper* topologyW_{nullptr};

  /**
   * Pair of {seed_prefix, alloc_prefix_len} after parsing the prefix allocation
   * params (seedPrefix, allocPrefixLen) passed into the constructor.
   */
  std::pair<folly::CIDRNetwork, int32_t> prefixAllocParams_;
};

} // namespace terragraph
} // namespace facebook
