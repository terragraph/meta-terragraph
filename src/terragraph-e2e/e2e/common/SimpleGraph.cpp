/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "SimpleGraph.h"

namespace facebook {
namespace terragraph {

SimpleGraph::SimpleGraph(const bool directed) : directed_(directed) {}

void
SimpleGraph::addVertex(const std::string& v) {
  vertices_.insert(v);
  neighbors_[v] = {};
}

void
SimpleGraph::removeVertex(const std::string& v) {
  vertices_.erase(v);
  neighbors_.erase(v);

  for (auto& vIt : neighbors_) {
    vIt.second.erase(v);
  }
}

void
SimpleGraph::addEdge(const std::string& u, const std::string& v) {
  auto uIt = neighbors_.find(u);
  auto vIt = neighbors_.find(v);
  if (uIt == neighbors_.end() || vIt == neighbors_.end()) {
    return;
  }

  uIt->second.insert(v);
  if (!directed_) {
    vIt->second.insert(u);
  }
}

void
SimpleGraph::addEdges(
    const std::vector<std::pair<std::string, std::string>>& edges) {
  for (const auto& edgePair : edges) {
    addEdge(edgePair.first, edgePair.second);
  }
}

void
SimpleGraph::removeEdge(const std::string& u, const std::string& v) {
  auto it = neighbors_.find(u);
  if (it != neighbors_.end()) {
    it->second.erase(v);
  }
  if (!directed_) {
    it = neighbors_.find(v);
    if (it != neighbors_.end()) {
      it->second.erase(u);
    }
  }
}

std::unordered_set<std::string>
SimpleGraph::getVertices() const {
  return vertices_;
}

size_t
SimpleGraph::numVertices() const {
  return vertices_.size();
}

std::unordered_set<std::string>
SimpleGraph::getNeighbors(const std::string& v) const {
  auto it = neighbors_.find(v);
  if (it != neighbors_.end()) {
    return it->second;
  }
  return {};
}

bool
SimpleGraph::isNeighbor(const std::string& u, const std::string& v) const {
  auto it = neighbors_.find(u);
  if (it != neighbors_.end()) {
    return it->second.count(v);
  }
  return false;
}

void
SimpleGraph::clear() {
  vertices_.clear();
  neighbors_.clear();
}

} // namespace terragraph
} // namespace facebook
