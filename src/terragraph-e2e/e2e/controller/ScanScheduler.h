/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Random.h>

#include "topology/TopologyWrapper.h"

namespace facebook {
namespace terragraph {

/**
 * Scan scheduling algorithms and utilities.
 */
class ScanScheduler {
 public:
  /**
   * Constructor.
   *
   * @param topo the topology wrapper
   */
  explicit ScanScheduler(const TopologyWrapper& topo);

  /**
   * Partition nodes into scheduling groups. Nodes in one group are part of
   * a single scan, while nodes in different groups should be scheduled in
   * different scans.
   *
   * To do that, the functions calculate the adjacency matrix (nodes
   * connected by a link) and a hearability matrix (nodes close to each other
   * distance-wise, even if not connected by a link), calculate an exclusion
   * matrix based on that, run a vertex coloring algorithm on the resultant
   * graph, and return a coloring.
   *
   * Returns a vector of vector of nodes. All nodes in a subvector are colored
   * with the same color (i.e. part of the same scan). Different subvectors
   * imply different scans.
   *
   * e.g. a result of { {1,2,3}, {4,5} } means that nodes [1,2,3] are colored
   * with one color (i.e. assigned to scan 1), and nodes [4,5] with a different
   * color (i.e. assigned to scan 2).
   */
  std::vector<std::vector<size_t>> getSchedGroups();

  /**
   * Returns the neighbors of node in the hearability matrix, i.e. nodes that
   * would hear node's transmissions (and whose transmission node would hear).
   */
  std::vector<size_t> getHearabilityNeighbors(size_t node);

  /**
   * Returns the neighbors of node in the adjacency matrix, i.e. nodes that
   * have a link to the given node.
   */
  std::vector<size_t> getAdjacencyNeighbors(size_t node);

  /**
   * Returns a mapping from nodeId (the size_t used elsewhere) to MAC address.
   */
  std::vector<std::string> getAllMacs();

 private:
  /** Undirected graph on nodes 0 to n-1. */
  class Graph {
   public:
    /** Constructor. */
    explicit Graph(size_t n) : matrix_(n, std::vector<bool>(n, false)) {}

    /** Add an edge between 'i' and 'j'. */
    void
    addEdge(size_t i, size_t j) {
      matrix_[i][j] = matrix_[j][i] = true;
    }

    /** The graph size. */
    size_t
    size() const {
      return matrix_.size();
    }

    /** Returns the neighbors of the given node. */
    std::vector<size_t>
    neighbors(size_t node) const {
      std::vector<size_t> res;
      const std::vector<bool>& row = matrix_[node];
      for (size_t i = 0; i < row.size(); i++) {
        if (row[i]) {
          res.push_back(i);
        }
      }
      return res;
    }

    /** Returns the neighbors of the given node, including the node itself. */
    std::vector<size_t>
    neighborsWithSelf(size_t node) const {
      std::vector<size_t> res = neighbors(node);
      res.push_back(node);
      return res;
    }

   private:
    /** The graph. */
    std::vector<std::vector<bool>> matrix_;
  };

  /** Construct the adjacency matrix. */
  static Graph getAdjacencyMatrix(
      const TopologyWrapper& topo, const std::vector<std::string>& macs);
  /** Construct the hearability matrix. */
  static Graph getHearabilityMatrix(
      const TopologyWrapper& topo, const std::vector<std::string>& macs);
  /** Returns the MAC addresses of all nodes in the topology. */
  static std::vector<std::string> getAllMacsInternal(
      const TopologyWrapper& topo);
  /** Construct the exclusion matrix. */
  static Graph getExclusionMatrix(
      const Graph& hearability, const Graph& adjacency);
  /** Returns true if any common nodes exist between the two vectors. */
  static bool hasCommonNodes(
      const std::vector<size_t>& a, const std::vector<size_t>& b);
  /** Returns the complement of the given color set. */
  static std::vector<int> colorsComplement(
      const std::set<int>& colors, int maxColor);

  /**
   * Run a vertex coloring algorithm on a graph and return a coloring.
   *
   * @see getSchedGroups()
   */
  static std::vector<std::vector<size_t>> graphColoring(const Graph& g);

  /** The MAC addresses of all nodes in the topology. */
  std::vector<std::string> macs_;

  /** The adjacency matrix. */
  Graph adjacencyMatrix_;
  /** The hearability matrix. */
  Graph hearabilityMatrix_;
};

} // namespace terragraph
} // namespace facebook
