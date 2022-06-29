/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <openr/decision/Decision.h>

#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/Topology_types.h"

namespace facebook {
namespace terragraph {

// Open/R default area "0". E2E does not use any area other than "0".
const std::string kDefaultArea {openr::Constants::kDefaultArea};

/**
 * Route computation helper for adjacencies and prefixes received from Open/R.
 */
class RoutesHelper {
 public:
  /**
   * Initialize the helper for the given topology.
   *
   * TODO: This class currently doesn't handle topology changes.
   *
   * @param topology the network topology
   */
  explicit RoutesHelper(const thrift::Topology& topology);

  /**
   * Notify this class that the next call to computeRoutes() will contain new
   * routing adjacencies, otherwise results may be out of sync.
   */
  void routingAdjacenciesUpdated();

  /**
   * Returns a list of routes between the given source and destination nodes.
   */
  std::vector<std::vector<std::string>> computeRoutes(
      const std::string& srcNode,
      const std::optional<std::string>& dstNode,
      const thrift::RoutingAdjacencies& routingAdjacencies);

 private:
  /** Compute the route database for the given MAC address. */
  openr::thrift::RouteDatabase getRouteDatabase(const std::string& mac);

  /** Returns whether a given node is advertising a POP prefix. */
  bool hasPopPrefix(
      const std::string& mac,
      const thrift::RoutingAdjacencies& routingAdjacencies);

  /** Returns the IP prefix for the given MAC address. */
  std::optional<std::string> findPrefix(
      const std::string& mac,
      const thrift::RoutingAdjacencies& routingAdjacencies);

  /** Resolve a link-local address to a MAC using the given adjacency info. */
  std::optional<std::string> resolveLinkLocalAddr(
      const openr::thrift::BinaryAddress& linkLocalAddr,
      const openr::thrift::AdjacencyDatabase& adjDatabase);

  /**
   * Cached LinkStates. Under the current implementation, this map is expected
   * to only contain one key, kDefaultArea.
   */
  std::unordered_map<std::string, openr::LinkState> areaLinkStates_;

  /** Cached PrefixState for nodes in the topology. */
  openr::PrefixState prefixState_;

  /** Cached route databases (cleared when receiving new RoutingAdjacencies). */
  std::unordered_map<std::string /* mac */, openr::thrift::RouteDatabase>
      routeDatabaseCache_;

  // Topology mappings
  /** Map from node MAC addresses to node names. */
  std::unordered_map<std::string, std::string> nodeMacToName_;
  /** Map from node names to node MAC addresses. */
  std::unordered_map<std::string, std::string> nodeNameToMac_;
  /** Map from node MAC addresses to prefixes. */
  std::unordered_map<std::string, std::string> nodeMacToPrefix_;

  /** Whether we need to update spfSolver_ with new routing adjacencies. */
  bool routingAdjNeedsUpdate_;
};

} // namespace terragraph
} // namespace facebook
