/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "CentralizedPrefixAllocator.h"

#include <openr/common/Util.h>
#include <openr/common/LsdbUtil.h>

namespace facebook {
namespace terragraph {
CentralizedPrefixAllocator::CentralizedPrefixAllocator(
    const thrift::PrefixAllocParams& prefixAllocParams,
    TopologyWrapper *topologyW,
    ConfigHelper& configHelper)
    : BasePrefixAllocator(prefixAllocParams, topologyW) {

  populatePrefixes();
  setSummaryRoute(configHelper);
}

void
CentralizedPrefixAllocator::setSummaryRoute(ConfigHelper& configHelper) {
  auto seedPrefixStr = folly::IPAddress::networkToString(
      prefixAllocParams_.first);

  std::string errorMsg;
  for (const auto& node : topologyW_->getAllNodes()) {
    if (!node.pop_node) {
      continue;
    }
    // Write network prefix to config
    if (!configHelper.setAutoNodeSpecificNetworkPrefixes(
        node.name, seedPrefixStr, errorMsg)) {
      LOG(ERROR) << folly::format(
          "Unable to write network prefix {} to node {} config: {}",
          seedPrefixStr, node.name, errorMsg);
    }
  }
}

std::map<folly::CIDRNetwork, std::string>
CentralizedPrefixAllocator::getAllocatedPrefixes() const {
  return prefix2NodeName_;
}

std::unordered_map<std::string, std::unordered_set<std::string>>
CentralizedPrefixAllocator::getZonePrefixes() const {
  std::unordered_map<std::string, std::unordered_set<std::string>> ret;
  for (const auto& node : topologyW_->getAllNodes()) {
    if (!node.pop_node) {
      continue;
    }

    if (ret.count(node.site_name)) {
      continue;
    }

    ret[node.site_name] = {
        folly::IPAddress::networkToString(prefixAllocParams_.first)};
  }

  return ret;
}

void
CentralizedPrefixAllocator::updatePrefixAllocParams(
    const thrift::PrefixAllocParams& prefixAllocParams,
    ConfigHelper& configHelper) {
  populatePrefixAllocParams(prefixAllocParams);
  allocate(configHelper);
  setSummaryRoute(configHelper);
}

void
CentralizedPrefixAllocator::populatePrefixes() {
  prefix2NodeName_.clear();

  auto allNodes = topologyW_->getAllNodes();

  // First, populate and validate all nodes that have set prefixes
  for (auto& node : allNodes) {
    if (!node.prefix_ref().has_value() || node.prefix_ref().value().empty()) {
      continue;
    }
    validatePrefix(node, false /*validateSubnet*/);

    auto prefix = folly::IPAddress::createNetwork(node.prefix_ref().value());
    auto seedPrefix = prefixAllocParams_.first;
    // Make sure prefix is in seedPrefix subnet. If it isn't, then clear the
    // prefix so it is set in the next iteration.
    if (!prefix.first.inSubnet(seedPrefix.first, seedPrefix.second)) {
      LOG(ERROR) << folly::format(
          "Invalid prefix ({}) for {} (not in subnet of network prefix {}). "
          "Allocating a different prefix for this node...",
          node.prefix_ref().value(),
          node.name,
          folly::IPAddress::networkToString(seedPrefix));
      node.prefix_ref().reset();
    }

    if (node.prefix_ref().has_value()) {
      prefix2NodeName_[prefix] = node.name;
    }
  }

  // Second, allocate prefixes for nodes that don't have any set
  for (auto& node : allNodes) {
    if (!node.prefix_ref().has_value() || node.prefix_ref().value().empty()) {
      auto newPrefix = getNextUnallocatedPrefix();
      node.prefix_ref() = folly::IPAddress::networkToString(newPrefix);
      validatePrefix(node, true);
      assignPrefixToNode(node, newPrefix);
    }
  }
}

void
CentralizedPrefixAllocator::addNode(
    thrift::Node& node, ConfigHelper& /*configHelper*/) {
  // Allocate prefix if node doesn't have one
  if (!node.prefix_ref().has_value() || node.prefix_ref().value().empty()) {
    auto newPrefix = getNextUnallocatedPrefix();
    node.prefix_ref() = folly::IPAddress::networkToString(newPrefix);
  }
  validatePrefix(node, true);
  assignPrefixToNode(node, folly::IPAddress::createNetwork(node.prefix_ref()
      .value()));
}

void
CentralizedPrefixAllocator::delNode(
    const thrift::Node& node, ConfigHelper& /*configHelper*/) {
  if (!node.prefix_ref().has_value() || node.prefix_ref().value().empty()) {
    return;
  }
  auto prefix = folly::IPAddress::createNetwork(node.prefix_ref().value());
  if (prefix2NodeName_.erase(prefix) == 0) {
    LOG(WARNING) << folly::format(
        "Node `{}` with prefix `{}` does not exist in prefix2NodeName_",
        node.name, node.prefix_ref().value());
  }

  // Don't need to update prefix in node because it has already been deleted
}

void
CentralizedPrefixAllocator::editNode(
    const thrift::Node& oldNode, const thrift::Node& newNode) {
  bool hasNewName = !newNode.name.empty() && (newNode.name != oldNode.name);
  if (hasNewName && oldNode.prefix_ref().has_value()) {
    auto prefix = folly::IPAddress::createNetwork(oldNode.prefix_ref().value());
    auto iter = prefix2NodeName_.find(prefix);
    if (iter != prefix2NodeName_.end()) {
      iter->second = newNode.name;
    }
  }
}

void
CentralizedPrefixAllocator::allocate(ConfigHelper& /*configHelper*/) {
  prefix2NodeName_.clear();

  auto allNodes = topologyW_->getAllNodes();
  for (auto& node : allNodes) {
    auto newPrefix = getNextUnallocatedPrefix();
    node.prefix_ref() = folly::IPAddress::networkToString(newPrefix);
    validatePrefix(node, true);
    assignPrefixToNode(node, newPrefix);
  }
}

folly::CIDRNetwork
CentralizedPrefixAllocator::getNextUnallocatedPrefix() {
  auto seedPrefix = prefixAllocParams_.first;
  auto allocPrefixLen = prefixAllocParams_.second;

  // power 2
  uint32_t prefixCount = 1 << (allocPrefixLen - seedPrefix.second);

  // Loop to find an unallocated prefix
  // Simply make a linear pass through all possible prefixes and allocate the
  // first unallocated prefix.
  uint32_t newVal;
  folly::CIDRNetwork newPrefix;
  for (newVal = 0; newVal < prefixCount; ++newVal) {
    newPrefix = openr::getNthPrefix(seedPrefix, allocPrefixLen, newVal);
    const auto it = prefix2NodeName_.find(newPrefix);
    if (it == prefix2NodeName_.end()) {
      // found
      break;
    }
  }
  if (newVal == prefixCount) {
    throw std::invalid_argument("No available prefixes");
  }

  return newPrefix;
}

void
CentralizedPrefixAllocator::validatePrefix(
    const thrift::Node& node, bool validateSubnet) {
  // If prefix is not allocated
  if (!node.prefix_ref().has_value() || node.prefix_ref().value().empty()) {
    return;
  }

  auto seedPrefix = prefixAllocParams_.first;
  auto allocPrefixLen = prefixAllocParams_.second;

  // Make sure prefix is valid
  folly::CIDRNetwork prefix;
  try {
    prefix = folly::IPAddress::createNetwork(node.prefix_ref().value());
  } catch (std::exception const& err) {
    std::string error = folly::sformat(
        "Invalid prefix ({}) for {}, err: {}",
        node.prefix_ref().value(), node.name, folly::exceptionStr(err));
    LOG(ERROR) << error;
    throw std::invalid_argument(error);
  }

  // Make sure prefix is in seedPrefix subnet
  if (validateSubnet) {
    if (!prefix.first.inSubnet(seedPrefix.first, seedPrefix.second)) {
      std::string error = folly::sformat(
          "Invalid prefix ({}) for {} (not in subnet of network prefix {})",
          node.prefix_ref().value(),
          node.name,
          folly::IPAddress::networkToString(seedPrefix));
      LOG(ERROR) << error;
      throw std::invalid_argument(error);
      }
  }

  // Make sure prefix CIDR equal to allocPrefixLen
  if (prefix.second != allocPrefixLen) {
    std::string error = folly::sformat(
        "Invalid prefix ({}) for {} (longer than allocPrefixLen ({}) bits)",
        node.prefix_ref().value(), node.name, allocPrefixLen);
    LOG(ERROR) << error;
    throw std::invalid_argument(error);
  }

  // Make sure prefix is unique
  const auto it = prefix2NodeName_.find(prefix);
  if (it != prefix2NodeName_.end() && it->second != node.name) {
    std::string error = folly::sformat(
        "Invalid prefix ({}) for {} (prefix already allocated to node {})",
        node.prefix_ref().value(), node.name, it->second);
    LOG(ERROR) << error;
    throw std::invalid_argument(error);
  }
}

void
CentralizedPrefixAllocator::assignPrefixToNode(
    const thrift::Node& node, folly::CIDRNetwork prefix) {
  prefix2NodeName_[prefix] = node.name;
  topologyW_->setNodePrefix(node.name, prefix);
}

} // namespace terragraph
} // namespace facebook
