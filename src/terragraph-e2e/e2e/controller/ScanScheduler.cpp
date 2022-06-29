/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ScanScheduler.h"

#include <fbzmq/zmq/Zmq.h>
#include <folly/MapUtil.h>
#include <folly/gen/Base.h>
#include <gflags/gflags.h>

#include "e2e/common/Consts.h"

DEFINE_double(
    scan_max_distance,
    350.0,
    "Maximum distance (in meters) for two nodes to be considered 'close' to "
    "each other for scan scheduling purposes");

namespace facebook {
namespace terragraph {

ScanScheduler::ScanScheduler(const TopologyWrapper& topo)
    : macs_(getAllMacsInternal(topo)),
      adjacencyMatrix_(getAdjacencyMatrix(topo, macs_)),
      hearabilityMatrix_(getHearabilityMatrix(topo, macs_)) {}

std::vector<std::vector<size_t>>
ScanScheduler::getSchedGroups() {
  Graph exclusion = getExclusionMatrix(hearabilityMatrix_, adjacencyMatrix_);
  return graphColoring(exclusion);
}

std::vector<size_t>
ScanScheduler::getHearabilityNeighbors(size_t node) {
  return hearabilityMatrix_.neighbors(node);
}

std::vector<size_t>
ScanScheduler::getAdjacencyNeighbors(size_t node) {
  return adjacencyMatrix_.neighbors(node);
}

std::vector<std::string>
ScanScheduler::getAllMacs() {
  return macs_;
}

//----------- Implementation --------------

std::vector<std::string>
ScanScheduler::getAllMacsInternal(const TopologyWrapper& topo) {
  std::vector<std::string> macs;
  for (const thrift::Node& node : topo.getAllNodes()) {
    for (const std::string& mac : node.wlan_mac_addrs) {
      macs.push_back(mac);
    }
  }
  return macs;
}

ScanScheduler::Graph
ScanScheduler::getAdjacencyMatrix(
    const TopologyWrapper& topo, const std::vector<std::string>& macs) {
  std::unordered_map<std::string, size_t> mac2index;
  for (size_t i = 0; i < macs.size(); i++) {
    mac2index[macs[i]] = i;
  }

  Graph g(macs.size());
  for (const thrift::Link& link : topo.getAllLinks()) {
    size_t i = mac2index[link.a_node_mac];
    size_t j = mac2index[link.z_node_mac];
    g.addEdge(i, j);
  }
  return g;
}

ScanScheduler::Graph
ScanScheduler::getHearabilityMatrix(
    const TopologyWrapper& topo, const std::vector<std::string>& macs) {
  std::vector<thrift::Location> locations =
      folly::gen::from(macs) | folly::gen::map([&topo](const std::string& mac) {
        if (auto maybeNode = topo.getNodeByMac(mac)) {
          if (auto maybeSite = topo.getSite(maybeNode->site_name)) {
            return maybeSite->location;
          }
        }
        return thrift::Location();
      }) |
      folly::gen::as<std::vector>();

  Graph g(macs.size());
  for (size_t i = 1; i < macs.size(); i++) {
    for (size_t j = 0; j < i; j++) {
      if (approxDistance(locations[i], locations[j]) <
          FLAGS_scan_max_distance) {
        g.addEdge(i, j);
      }
    }
  }
  return g;
}

bool
ScanScheduler::hasCommonNodes(
    const std::vector<size_t>& a, const std::vector<size_t>& b) {
  for (size_t x : a) {
    for (size_t y : b) {
      if (x == y) {
        return true;
      }
    }
  }
  return false;
}

ScanScheduler::Graph
ScanScheduler::getExclusionMatrix(
    const Graph& hearability, const Graph& adjacency) {
  const size_t n = hearability.size();
  Graph exclusion(n);
  for (size_t i = 1; i < n; i++) {
    for (size_t j = 0; j < i; j++) {
      if (hasCommonNodes(
              hearability.neighborsWithSelf(i),
              hearability.neighborsWithSelf(j)) ||
          hasCommonNodes(hearability.neighbors(i), adjacency.neighbors(j)) ||
          hasCommonNodes(adjacency.neighbors(i), hearability.neighbors(j))) {
        exclusion.addEdge(i, j);
      }
    }
  }
  return exclusion;
}

namespace {

template <class K, class V>
class MaxFinder {
 public:
  void
  add(K k, V v) {
    if (!k_ || *k_ < k) {
      k_ = std::move(k);
      v_ = std::move(v);
    }
  }
  V
  get() const {
    return v_;
  }

 private:
  std::optional<K> k_;
  V v_;
};
}

std::vector<int>
ScanScheduler::colorsComplement(const std::set<int>& colors, int maxColor) {
  std::vector<int> result;
  for (int i = 1; i <= maxColor; i++) {
    if (colors.find(i) == colors.end()) {
      result.push_back(i);
    }
  }
  return result;
}

std::vector<std::vector<size_t>>
ScanScheduler::graphColoring(const Graph& g) {
  const size_t n = g.size();
  std::vector<int> colorId(n);

  std::set<size_t> unassignedVertices;
  // fill unassignedVertices with 0..n-1
  for (size_t i = 0; i < n; i++) {
    unassignedVertices.insert(i);
  }

  int curMaxColorId = 1;
  while (!unassignedVertices.empty()) {
    // key is (numColorsInNeighbourhood, numNeighbors)
    // value is (node, potentialColor)
    MaxFinder<std::pair<size_t, size_t>, std::pair<size_t, int>> maxFinder;
    for (size_t vertex : unassignedVertices) {
      // Find all neighbors
      std::vector<size_t> neighbors = g.neighbors(vertex);

      // Find all colors used by neighbors
      std::set<int> colorsInNeighborhood =
          folly::gen::from(neighbors) | folly::gen::filter([&](size_t neigh) {
            return colorId[neigh] != 0;
          }) |
          folly::gen::map([&](size_t neigh) { return colorId[neigh]; }) |
          folly::gen::as<std::set<int>>();

      // Compute color we would assign if we pick this vertex
      std::vector<int> availableColors =
          colorsComplement(colorsInNeighborhood, curMaxColorId);
      int potentialColor;
      if (availableColors.empty()) {
        // No colors available, need extra color
        potentialColor = curMaxColorId + 1;
      } else {
        // Pick random color
        potentialColor =
            availableColors[folly::Random::rand32(availableColors.size())];
      }

      // Record value for max key
      maxFinder.add(
          {colorsInNeighborhood.size(), neighbors.size()},
          {vertex, potentialColor});
    }

    // Extract value of max key
    std::pair<size_t, int> max = maxFinder.get();
    size_t thisVertex = max.first;
    int thisColor = max.second;

    if (thisColor == curMaxColorId + 1) {
      curMaxColorId++;
    }

    colorId[thisVertex] = thisColor;
    unassignedVertices.erase(thisVertex);
  }

  std::vector<std::vector<size_t>> coloring(curMaxColorId);
  for (size_t i = 0; i < colorId.size(); i++) {
    coloring[colorId[i] - 1].push_back(i);
  }
  return coloring;
}

} // namespace terragraph
} // namespace facebook
