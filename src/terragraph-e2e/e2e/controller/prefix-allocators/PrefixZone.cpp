/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "PrefixZone.h"

#include <numeric>

#include <folly/Format.h>

namespace facebook {
namespace terragraph {

PrefixZone::PrefixZone() {}

PrefixZone::PrefixZone(
    const thrift::Zone& zone, int allocPrefixLen)
    : nodes_{zone.node_names} {
  for (const auto& prefix : zone.zone_prefixes) {
    auto network = folly::IPAddress::createNetwork(prefix);
    auto prefixSpace = 1 << (allocPrefixLen - network.second);
    allocatedZonePrefixes_.insert(network);
    allocatedPrefixSpaces_.insert(prefixSpace);
  }
}

int
PrefixZone::getRemaining() const {
  return getAllocationSize() - maxSize();
}

int
PrefixZone::maxSize() const {
  return std::accumulate(
      allocatedPrefixSpaces_.begin(), allocatedPrefixSpaces_.end(), 0);
}

int
PrefixZone::size() const {
  return nodes_.size();
}

int
PrefixZone::getAllocationSize() const {
  return allocationSizeOverride_ > -1 ? allocationSizeOverride_
                                      : size();
}

std::unordered_set<int>
PrefixZone::getAllocatedPrefixSpaces() const {
  return allocatedPrefixSpaces_;
}

std::unordered_set<folly::CIDRNetwork>
PrefixZone::getZonePrefixes() const {
  return allocatedZonePrefixes_;
}

std::unordered_set<std::string>
PrefixZone::getZonePrefixStrings() const {
  std::unordered_set<std::string> ret;
  for (const auto& prefix : allocatedZonePrefixes_) {
    ret.insert(folly::IPAddress::networkToString(prefix));
  }
  return ret;
}

std::unordered_set<std::string>
PrefixZone::getNodes() const {
  return nodes_;
}

std::unordered_map<folly::CIDRNetwork, std::string>
PrefixZone::getAllocatedNodePrefixes() const {
  return allocatedNodePrefixes_;
}


// Setters

void
PrefixZone::setAllocationSizeOverride(int n) {
  allocationSizeOverride_ = n;
}

void
PrefixZone::setAllocatedPrefixSpaces(std::unordered_set<int> prefixSpaces) {
  allocatedPrefixSpaces_ = prefixSpaces;
}

bool
PrefixZone::addNode(const std::string& nodeName) {
  return nodes_.insert(nodeName).second;
}

void
PrefixZone::addToPrefixSpace(int n) {
  while (true) {
    if (allocatedPrefixSpaces_.find(n) != allocatedPrefixSpaces_.end()) {
      allocatedPrefixSpaces_.erase(n);
      n *= 2;
    } else {
      allocatedPrefixSpaces_.insert(n);
      break;
    }
  }
}

bool
PrefixZone::addZonePrefix(folly::CIDRNetwork zonePrefix) {
  return allocatedZonePrefixes_.insert(zonePrefix).second;
}

void
PrefixZone::assignNodePrefix(
    const std::string& nodeName, folly::CIDRNetwork prefix) {
  allocatedNodePrefixes_[prefix] = nodeName;
}

bool
PrefixZone::delNode(const std::string& nodeName, folly::CIDRNetwork prefix) {
  auto numNodesErased = nodes_.erase(nodeName);
  if (numNodesErased != 1) {
    LOG(ERROR) << folly::format("Node {} not in nodes_", nodeName);
  }
  auto numPrefixesErased = allocatedNodePrefixes_.erase(prefix);
  if (numPrefixesErased != 1) {
    LOG(ERROR) << folly::format(
        "Prefix {} not in allocatedNodePrefixes_",
        folly::IPAddress::networkToString(prefix));
  }
  return (numNodesErased + numPrefixesErased) == 2;
}

void
PrefixZone::editNodeName(
    const std::string& oldNodeName,
    folly::CIDRNetwork prefix,
    const std::string& newNodeName) {
  if (nodes_.erase(oldNodeName) != 1) {
    LOG(ERROR) << folly::format("Node {} not in nodes_", oldNodeName);
    return;
  }
  nodes_.insert(newNodeName);

  auto iter = allocatedNodePrefixes_.find(prefix);
  if (iter != allocatedNodePrefixes_.end()) {
    iter->second = newNodeName;
  } else {
    LOG(WARNING) << folly::format(
        "Prefix {} not found",
        folly::IPAddress::networkToString(prefix));
  }
}

void
PrefixZone::clearPrefixSpaces() {
  allocatedPrefixSpaces_.clear();
}

} // namespace terragraph
} // namespace facebook
