/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include <folly/IPAddress.h>

#include "e2e/if/gen-cpp2/Topology_types.h"

namespace facebook {
namespace terragraph {

/**
 * A prefix zone, representing a group of nodes to their closest POP site (using
 * wireless hop count as the distance metric).
 *
 * @see DeterministicPrefixAllocator
 */
class PrefixZone {
 public:
  /** Empty constructor. */
  explicit PrefixZone();

  /** Construct zone from existing zone in topology file. */
  explicit PrefixZone(const thrift::Zone& zone, int allocPrefixLen);

  //
  // Getters
  //

  /** Returns the number of node prefixes this zone still needs. */
  int getRemaining() const;

  /** The number of node prefixes this zone can allocate. */
  int maxSize() const;

  /** The number of nodes in this zone. */
  int size() const;

  /**
   * Returns the minimum number of node prefixes this zone should be able to
   * support after zone prefix allocation has completed.
   */
  int getAllocationSize() const;

  /** Returns the prefix spaces allocated to this zone. */
  std::unordered_set<int> getAllocatedPrefixSpaces() const;

  /** Returns the zone prefixes allocated to this zone. */
  std::unordered_set<folly::CIDRNetwork> getZonePrefixes() const;

  /** Returns zone prefixes allocated to this zone as strings. */
  std::unordered_set<std::string> getZonePrefixStrings() const;

  /** Returns the node names in this zone. */
  std::unordered_set<std::string> getNodes() const;

  /** Returns a map of node prefixes to the node name they are allocated to. */
  std::unordered_map<folly::CIDRNetwork, std::string>
      getAllocatedNodePrefixes() const;

  //
  // Setters
  //

  /**
   * Set the allocation size override.
   *
   * This is used as the minimum number of prefix space to allocate for this
   * zone.
   */
  void setAllocationSizeOverride(int n);

  /**
   * Set the allocated prefix spaces.
   *
   * This is used when reading precomputed, saved information.
   */
  void setAllocatedPrefixSpaces(std::unordered_set<int> prefixSpaces);

  /**
   * Add a node to this zone.
   *
   * Returns true if the node is successfully added, or false if the node
   * already existed.
   */
  bool addNode(const std::string& nodeName);

  /**
   * Adds a number to prefix space.
   *
   * This function will consolidate prefix spaces if it can. This means if 64
   * already existed in this zone's prefix space and we add another 64, then the
   * old 64 will be removed and a 128 will be added.
   */
  void addToPrefixSpace(int n);

  /**
   * Add a zone prefix.
   *
   * Returns true if prefix is successfully added, or false if it already
   * existed.
   */
  bool addZonePrefix(folly::CIDRNetwork zonePrefix);

  /**
   * Assign a prefix to a node.
   *
   * This overwrites any existing prefix that the node was assigned.
   */
  void assignNodePrefix(const std::string& nodeName, folly::CIDRNetwork prefix);

  /**
   * Delete a node prefix from this zone.
   *
   * Returns true if successfully deleted node prefix from zone, otherwise
   * false.
   */
  bool delNode(const std::string& nodeName, folly::CIDRNetwork prefix);

  /** Change internal mappings to use a new node name. */
  void editNodeName(
      const std::string& oldNodeName,
      folly::CIDRNetwork prefix,
      const std::string& newNodeName);

  /** Clear allocated prefix spaces for all zones. */
  void clearPrefixSpaces();

 private:
  /**
   * Overrides the number of nodes in this zone.
   *
   * This is sseful to ensure the zone is allocated enough prefix space to allow
   * additional nodes.
   */
  int allocationSizeOverride_{-1};

  /** Nodes in this PrefixZone. */
  std::unordered_set<std::string> nodes_{};

  /** Prefix spaces given to this PrefixZone. */
  std::unordered_set<int> allocatedPrefixSpaces_{};

  /** Prefixes allocated to this PrefixZone. */
  std::unordered_set<folly::CIDRNetwork> allocatedZonePrefixes_{};

  /** Map of node prefixes to the node that was assigned that prefix. */
  std::unordered_map<folly::CIDRNetwork, std::string>
      allocatedNodePrefixes_{};
};

} // namespace terragraph
} // namespace facebook
