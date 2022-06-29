/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "CtrlApp.h"
#include "topology/TopologyWrapper.h"

namespace facebook {
namespace terragraph {

/**
 * Implementation of the Terragraph network upgrade batching algorithm.
 *
 * This algorithm is design to avoid network isolation all together. If
 * upgrading a site has to cause a network isolation, then we upgrade the
 * isolated portion of the network as well.
 *
 * This is a site-based batching algorithm. This simplifies the logic and is in
 * line with the future Terragraph DN architecture.
 *
 * The algorithm has two stages:
 *
 * 1. Identify articulation points (APs).
 *    Articulation points are sites that will isolate portions of the network if
 *    taken down or removed from the graph. We also filter APs to remove any
 *    sub-AP (APs isolated by other APs). Each AP forms an "AP group"
 *    (apGroup: the AP and all sites it isolates). All sites in an AP group will
 *    be upgraded simultaneously (same batch).
 *
 * 2. Once we have identified AP groups, each site/apGroup is now accessible via
 *    multiple paths. We use a spanning tree algorithm to identify batches of
 *    sites/apGroup that can be upgraded without causing any network isolation.
 *    To achieve that, we implement a modified BFS traversal algorithm that
 *    gives preference to already upgraded sites/apGroups by pushing them to the
 *    top of the queue.
 */
class GraphHelper {
 public:
  /**
   * Get the next batch of nodes to upgrade.
   *
   * This function rebuilds the graph on every call.
   */
  std::unordered_set<std::string> getBatch(
      const TopologyWrapper& topologyW,
      const std::unordered_set<std::string>& nodesPendingUpdate,
      int limit);

 private:
  /** Set of sites in the graph. */
  std::unordered_set<std::string> vertices_;
  /** Map of adjacent sites. */
  std::unordered_map<std::string, std::unordered_set<std::string>> adjacencies_;
  /** Set of all sites with POP nodes in them. */
  std::unordered_set<std::string> popSiteNames_;
  /** Map of site name to all nodes within. */
  std::unordered_map<std::string, std::unordered_set<std::string>>
      siteName2nodeNames_;
  /** Nodes waiting to start the upgrade stage. */
  std::unordered_set<std::string> nodesPendingUpdate_;
  /** Sites with nodes waiting to start the upgrade stage. */
  std::unordered_set<std::string> sitesPendingUpdate_;
  /**
   * Maximum number of nodes to upgrade in each batch.
   *
   * This not honored if any AP group size is larger.
   */
  size_t batchSizeLimit_;
  /** The step at which a vertex is discovered in the AP finding algorithm. */
  int discoverTime_;

  /** Build a site graph from the given topology. */
  void buildGraph(const TopologyWrapper& topologyW);

  /**
   * Find articulation points using DFS (recursive).
   * @param u current vertex
   * @param visited already-visited vertices
   * @param discoveryTime step at which vertices were first visited
   * @param lowestDiscoveryTime minimum discovery time (minimum number of steps
   *                            needed to reach a vertex)
   * @param parent parent vertex in DFS tree
   * @param aps articulation points and their immediate children
   */
  void findArticulationPoints(
      const std::string& u,
      std::unordered_set<std::string>& visited,
      std::unordered_map<std::string, int>& discoveryTime,
      std::unordered_map<std::string, int>& lowestDiscoveryTime,
      std::unordered_map<std::string, std::string>& parent,
      std::unordered_map<std::string, std::unordered_set<std::string>>& aps);

  /**
   * Graph breadth-first search.
   *
   * This is needed to traverse dependent children of AP sites.
   *
   * Returns all visited sites.
   *
   * @param rootSite starting point in the graph
   * @param excludeSite site to exclude when traversing the graph (used to
   *                    prevent the algorithm from traversing upwards in the
   *                    AP tree)
   */
  std::unordered_set<std::string> bfs(
      const std::string& rootSite, const std::string& excludeSite);

  /**
   * Get candidate nodes to upgrade.
   *
   * We create a spanning tree and pick the leaves of the tree that are
   * pending upgrade. This prevents isolation of other sites in the network and
   * keeps the upgrade candidates one hop away from an ignited node (i.e faster
   * ignition after upgrade is done).
   *
   * Ideally, we should use a Weighted Max Leaf Spanning Tree algorithm to
   * find the maximum number of sites we can upgrade at a time. But, since
   * finding a MLST is NP-complete. We use a DFS based approach with bias toward
   * already-upgraded sites.
   *
   * @param apGroups map of articulation points to their dependent sites
   */
  std::unordered_set<std::string> getCandidateNodes(
      const std::unordered_map<std::string, std::unordered_set<std::string>>&
          apGroups);

  /**
   * Get groups of articulation points in the network and their corresponding
   * dependent sites (i.e. sites isolated by the AP).
   */
  std::unordered_map<std::string, std::unordered_set<std::string>>
      getApGroups();

  /**
   * Add an edge to the graph, as well as the vertices (if missing).
   * @param v vertex on side A of the edge
   * @param w vertex on side Z of the edge
   */
  void addEdge(const std::string& v, const std::string& w);

  /**
   * Add a vertex to the graph.
   * @param v vertex name
   */
  void addVertex(const std::string& v);

  /**
   * Remove a vertex and all its edges from the graph.
   * @param v vertex name
   */
  void removeVertex(const std::string& v);
};

} // namespace terragraph
} // namespace facebook
