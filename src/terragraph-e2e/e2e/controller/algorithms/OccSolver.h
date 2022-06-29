/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "../topology/TopologyWrapper.h"
#include "e2e/common/SimpleGraph.h"

namespace facebook {
namespace terragraph {

/**
 * Fixed-parameter tractable algorithm for computing a minimum weight odd cycle
 * cover (OCC) for a given input graph.
*/
class OccSolver {
 public:
  /** Empty constructor. */
  OccSolver() {}

  /** Constructor from an input graph. */
  explicit OccSolver(
      const SimpleGraph& graph,
      const std::unordered_map<std::string, float>& vertexWeights);

  /** Constructor from an input topology. */
  explicit OccSolver(
      const TopologyWrapper& topologyW,
      const std::unordered_map<std::string, float>& vertexWeights =
          std::unordered_map<std::string, float>());

  /** Solve the OCC problem, and return the set of vertices to remove. */
  std::unordered_set<std::string> getOccSolution();

  // ---- Suggested levels of vertex weight ----
  /** High vertex weight */
  const static float kHighVertexWeight;
  /** Low vertex weight */
  const static float kLowVertexWeight;
  /** Default vertex weight */
  const static float kDefaultVertexWeight;
  /** Bias up vertex weight */
  const static float kBiasUpVertexWeight;
  /** Bias down vertex weight */
  const static float kBiasDownVertexWeight;

 private:
  /** Construct a site-based graph from the given topology. */
  void buildGraph(const TopologyWrapper& topologyW);

  /** Compute the tree decomposition of the graph. */
  void greedyTreeDecomp();

  /**
   * Solve the minimum weight odd cycle cover problem.
   *
   * Assumes tree decomposition is available.
   */
  void solveOcc();

  /**
   * Find all extra edges we need to add to ensure graph connectivity after
   * removing a specific vertex.
   *
   * This iterates through all neighbors of the vertex and adds extra edges
   * between them if edges do not already exist.
   */
  std::vector<std::pair<std::string, std::string>> fillin(
      const SimpleGraph& g,
      const std::unordered_set<std::string>& vNames) const;

  /** Vertex priority determined by its fillin() size. */
  size_t priority(const SimpleGraph& g, const std::string& vName) const;

  /** Find the vertex with minimum priority. */
  std::string minPriorityVertex(const SimpleGraph& g) const;

  /**
   * Add a vertex to the tree decomposition.
   *
   * This also fills tree vertex properties ('bag' and 'step').
   */
  void addTreeVertex(
      const std::string& vName,
      const std::unordered_set<std::string>& bag,
      const int step);

  /** Tree vertex properties. */
  struct TreeVertexProperties {
    /** The step. */
    int step;
    /** The bag. */
    std::unordered_set<std::string> bag;
  };

  /** Data structure representing a subproblem in the DP formulation. */
  class SubProblem {
   public:
    /** Empty constructor. */
    SubProblem() {}

    /** Constructor. */
    explicit SubProblem(
        std::string node,
        std::set<std::string> left,
        std::set<std::string> write,
        std::set<std::string> pick);

    /** Compute subproblem-unique string signature. */
    std::string signature() const;

    /** Compute cost of subproblem minus weights of (pick set - {node}). */
    float modifiedCost(const OccSolver* parentHelper) const;

    /** Compute the list of subproblems which this subproblem depends on. */
    std::vector<SubProblem> dependencies(const OccSolver* parentHelper);

    /** Subproblem-unique sting signature. */
    std::string sig_;

    /**
     * Node in the decomposition.
     *
     * SubProblem optimizes over solutions to the subgraph associated
     * with the subtree rooted at this node.
     */
    std::string node_;

    /**
     * Set of nodes that must be placed in the left shore of the bipartition.
     *
     * Sorted to guarantee signature consistency.
     */
    std::set<std::string> left_;

    /**
     * Set of nodes that must be placed in the right shore of the bipartition.
     *
     * Sorted to guarantee signature consistency.
     */
    std::set<std::string> right_;

    /**
     * Set of nodes that must be deleted in the bipartition.
     *
     * Sorted to guarantee signature consistency.
     */
    std::set<std::string> pick_;

    /** Dependency list. */
    std::optional<std::vector<SubProblem>> depend_;

    /** Maps dependent vertices to best-cost subproblem. */
    std::unordered_map<std::string, std::string> chosen_;

    /** Cost for the subproblem. */
    float cost_;
  };

  /** The graph. */
  SimpleGraph graph_;

  /** Tree decomposition of graph. */
  SimpleGraph tree_;

  /** Root vertex used for tree decomposition. */
  std::string rootVertex_;

  /**
   * Maps vertex name to vertex weight.
   *
   * Vertex weight is used in the cost function.
   */
  std::unordered_map<std::string, float> vertexWeights_;

  /** Maps tree's vertex name to vertex properties. */
  std::unordered_map<std::string, TreeVertexProperties> nameToTreeVertexProp_;

  /** Minimum cost problem as determined by OCC algorithm. */
  SubProblem minCostProb_;

  /** Maps subproblem signature to subproblem. */
  std::unordered_map<std::string, SubProblem> subProblems_;
};

} // namespace terragraph
} // namespace facebook
