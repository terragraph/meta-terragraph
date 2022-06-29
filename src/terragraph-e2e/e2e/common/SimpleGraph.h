/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace facebook {
namespace terragraph {

/**
 * A minimal graph data structure.
 */
class SimpleGraph {
 public:
  /** Constructs a SimpleGraph, defaulting to an undirected graph. */
  explicit SimpleGraph(const bool directed = false);

  /** Add a vertex to the graph. */
  void addVertex(const std::string& v);

  /** Remove a vertex and all its edges from the graph. */
  void removeVertex(const std::string& v);

  /** Add an edge to the graph. */
  void addEdge(const std::string& u, const std::string& v);

  /** Bulk-add edges to the graph. */
  void addEdges(const std::vector<std::pair<std::string, std::string>>& edges);

  /** Remove an edge from the graph. */
  void removeEdge(const std::string& u, const std::string& v);

  /** Clear all vertices and edges from the graph. */
  void clear();

  /** Get all graph vertices. */
  std::unordered_set<std::string> getVertices() const;

  /** Get the number of vertices in the graph. */
  size_t numVertices() const;

  /** Get neighbors of vertex "v". */
  std::unordered_set<std::string> getNeighbors(const std::string& v) const;

  /** Check if vertex "v" is a neighbor of vertex "u". */
  bool isNeighbor(const std::string& u, const std::string& v) const;

 private:
  /** Is graph directed? */
  bool directed_;

  /** Graph vertices. */
  std::unordered_set<std::string> vertices_;

  /** Neighbors of each vertex. */
  std::unordered_map<std::string, std::unordered_set<std::string>> neighbors_;
};

} // namespace terragraph
} // namespace facebook
