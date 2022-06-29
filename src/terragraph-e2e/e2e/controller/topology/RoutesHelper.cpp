/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "RoutesHelper.h"

#include <folly/IPAddress.h>
#include <folly/MacAddress.h>
#include <openr/if/gen-cpp2/KvStore_constants.h>

#include "e2e/common/OpenrUtils.h"

using std::string;

namespace facebook {
namespace terragraph {

RoutesHelper::RoutesHelper(const thrift::Topology& topology) {
  // Create mappings from node name <-> MAC address
  // TODO: Figure out how this will work for multi-MAC case.
  for (const auto& node : topology.nodes) {
    try {
      string macAddr = folly::MacAddress(node.mac_addr).toString();
      nodeMacToName_[macAddr] = node.name;
      nodeNameToMac_[node.name] = macAddr;
    } catch (std::invalid_argument& ex) {
      LOG(ERROR) << "Invalid MAC address: " << node.mac_addr << " for node "
                 << node.name;
    }
  }
}

void
RoutesHelper::routingAdjacenciesUpdated() {
  // Lazily update spfSolver_ when computeRoutes() is called.
  // This will avoid flooding our logs with openr::Decision LOG(INFO) messages.
  routingAdjNeedsUpdate_ = true;
}

std::vector<std::vector<string>>
RoutesHelper::computeRoutes(
    const string& srcNode,
    const std::optional<string>& dstNode,
    const thrift::RoutingAdjacencies& routingAdjacencies) {
  VLOG(4) << "Computing"
          << (dstNode ? " routes from " + srcNode + " to " + *dstNode
                      : " default routes for " + srcNode);

  // Do we need to update routing adjacencies?
  if (routingAdjNeedsUpdate_) {
    routingAdjNeedsUpdate_ = false;

    areaLinkStates_ = {{kDefaultArea, openr::LinkState(kDefaultArea)}};
    auto& linkState = areaLinkStates_.at(kDefaultArea);
    for (const auto& kv : routingAdjacencies.adjacencyMap) {
      linkState.updateAdjacencyDatabase(kv.second, kDefaultArea);
    }
    prefixState_ = openr::PrefixState();
    for (const auto& kv : routingAdjacencies.prefixMap) {
      for (const openr::thrift::PrefixEntry entry:
          kv.second.prefixEntries_ref().value()) {
        auto network = openr::toIPNetwork(entry.prefix_ref().value());
        openr::PrefixKey key = openr::PrefixKey(kv.first /* Node Name */,
            network /* prefix */, kDefaultArea);
        prefixState_.updatePrefix(key, entry);
      }
    }
    // Clear route database cache
    routeDatabaseCache_.clear();
  }

  std::vector<std::vector<string>> routes;

  // Verify that the nodes exist in the topology
  auto srcNodeMac = nodeNameToMac_.find(srcNode);
  if (srcNodeMac == nodeNameToMac_.end()) {
    VLOG(2) << "ERROR: src node " << srcNode << " not found in topology";
    return routes;  // not in topology
  }

  std::optional<string> targetPrefix;
  if (dstNode) {
    auto dstNodeMac = nodeNameToMac_.find(*dstNode);
    if (dstNodeMac == nodeNameToMac_.end()) {
      VLOG(2) << "ERROR: dst node " << *dstNode << " not found in topology";
      return routes;  // not in topology
    }

    targetPrefix = findPrefix(dstNodeMac->second, routingAdjacencies);
    if (!targetPrefix) {
      VLOG(2) << "No prefix found for dst node " << *dstNode;
      return routes;
    }
  } else {
    // If dstNode is empty, we are searching for default routes
    targetPrefix = "::";  // ::/0 advertised by POP nodes
  }

  // Find routes using DFS
  using NodeAndPath = std::pair<string, std::vector<string>>;
  std::stack<NodeAndPath> stack;
  stack.push(NodeAndPath(srcNode, {srcNode}));

  while (!stack.empty()) {
    auto node = std::move(stack.top().first);
    auto path = std::move(stack.top().second);
    stack.pop();

    // Verify that the node MAC address exists
    auto mac = nodeNameToMac_.find(node);
    if (mac == nodeNameToMac_.end()) {
      continue;
    }

    // Is this the destination node or have we arrived at a POP (default route)?
    if ((dstNode && node == *dstNode) ||
        (!dstNode && hasPopPrefix(mac->second, routingAdjacencies))) {
      // Done, record the full route
      routes.push_back(std::move(path));
      continue;
    }

    // Verify that the AdjacencyDatabase exists
    auto adjDatabase = routingAdjacencies.adjacencyMap.find(
        OpenrUtils::toOpenrNodeName(mac->second));
    if (adjDatabase == routingAdjacencies.adjacencyMap.end()) {
      continue;
    }

    // Compute the route database
    openr::thrift::RouteDatabase routeDatabase = getRouteDatabase(mac->second);

    // Check each route for its destination
    for (const auto& route : routeDatabase.unicastRoutes_ref().value()) {
      string dstPrefix =
          OpenrUtils::binaryAddressToString(
              route.dest_ref().value().prefixAddress_ref().value());
      if (dstPrefix != targetPrefix) {
        continue;  // not a possible route
      }

      // Find minimum metric
      int32_t minMetric = route.nextHops_ref().value()[0].metric_ref().value();
      for (const auto& nextHop : route.nextHops_ref().value()) {
        if (nextHop.metric_ref().value() < minMetric) {
          minMetric = nextHop.metric_ref().value();
        }
      }

      // Add next hop(s) to the destination node
      for (const auto& nextHop : route.nextHops_ref().value()) {
        // Skip this path if metric is higher than minMetric
        if (nextHop.metric_ref().value() > minMetric) {
          continue;
        }

        // Resolve next hop address to node id using adjacency information
        openr::thrift::BinaryAddress addr = nextHop.address_ref().value();
        auto nextHopMac = resolveLinkLocalAddr(addr, adjDatabase->second);
        if (!nextHopMac) {
          continue;  // not found in adjacencies (shouldn't happen)
        }
        auto nextHopNodeIter = nodeMacToName_.find(nextHopMac.value());
        if (nextHopNodeIter == nodeMacToName_.end()) {
          continue;  // not in topology
        }
        string nextHopNode = nextHopNodeIter->second;

        // Check for loops
        if (std::find(path.begin(), path.end(), nextHopNode) != path.end()) {
          continue;
        }

        // Push to stack
        std::vector<string> newPath(path);
        newPath.push_back(nextHopNode);
        stack.push(NodeAndPath(nextHopNode, std::move(newPath)));
      }
    }
  }

  return routes;
}

openr::thrift::RouteDatabase
RoutesHelper::getRouteDatabase(const string& mac) {
  auto cachedRouteDb = routeDatabaseCache_.find(mac);
  if (cachedRouteDb == routeDatabaseCache_.end()) {
    // No cached value, so run SPF to find paths
    openr::thrift::RouteDatabase rtdb;
    openr::SpfSolver spfSolver ("", false /* enableV4 */,
        false /* enableNodeSegmentLabels */, true /* enableAdjacencyLabels */);
    auto decisionRouteDb =
        spfSolver.buildRouteDb(OpenrUtils::toOpenrNodeName(mac),
            areaLinkStates_, prefixState_);
    if (decisionRouteDb.has_value()) {
      auto routeDb = decisionRouteDb->toThrift();
      routeDatabaseCache_[mac] = routeDb;
      return routeDb;
    }
    VLOG(2) << "No routes found for " << mac;
    return rtdb;
  } else {
    // Return cached value
    return cachedRouteDb->second;
  }
}

bool
RoutesHelper::hasPopPrefix(
    const string& mac, const thrift::RoutingAdjacencies& routingAdjacencies) {
  // Find PrefixDatabase entry
  auto prefixDatabase =
      routingAdjacencies.prefixMap.find(OpenrUtils::toOpenrNodeName(mac));
  if (prefixDatabase == routingAdjacencies.prefixMap.end()) {
    return false;  // no prefix key (shouldn't happen)
  }

  for (const openr::thrift::PrefixEntry& entry :
       prefixDatabase->second.prefixEntries_ref().value()) {
    if (entry.prefix_ref().value().prefixLength_ref().value() == 0) {
      return true;  // ::/0 advertised by POP nodes
    }
  }

  return false;
}

std::optional<string>
RoutesHelper::findPrefix(
    const string& mac, const thrift::RoutingAdjacencies& routingAdjacencies) {
  // Find PrefixDatabase entry
  auto prefixDatabase =
      routingAdjacencies.prefixMap.find(OpenrUtils::toOpenrNodeName(mac));
  if (prefixDatabase == routingAdjacencies.prefixMap.end()) {
    return std::nullopt;  // no prefix key (shouldn't happen)
  }

  // Make sure the network prefix is valid
  folly::CIDRNetwork networkPrefix;
  try {
    networkPrefix = folly::IPAddress::createNetwork(routingAdjacencies.network);
  } catch (const std::exception& ex) {
    VLOG(2) << "Invalid e2e-network-prefix (" << routingAdjacencies.network
            << "): " << folly::exceptionStr(ex);
    return std::nullopt;
  }

  auto prefixEntries = prefixDatabase->second.prefixEntries_ref().value();
  if (!prefixEntries.size()) {
    return std::nullopt;  // no prefix (shouldn't happen)
  }

  int16_t minPrefixLength = INT16_MAX;
  std::optional<string> minPrefixStr;

  // Find the prefix with the minimum prefix length belonging to the topology's
  // subnet. Nodes may also be advertising PD prefixes (potentially on the same
  // network) when CPEs get DHCP leases.
  for (const openr::thrift::PrefixEntry& entry : prefixEntries) {
    auto prefixLength = entry.prefix_ref().value().prefixLength_ref().value();
    if (prefixLength == 0) {
      continue;  // skip /0 prefixes (::/0 advertised by POP nodes)
    }

    string prefixStr =
        OpenrUtils::binaryAddressToString(
            entry.prefix_ref().value().prefixAddress_ref().value());

    auto prefix = folly::IPAddress(prefixStr);
    if (prefix.inSubnet(networkPrefix.first, networkPrefix.second) &&
        prefixLength < minPrefixLength) {
      minPrefixLength = prefixLength;
      minPrefixStr = prefixStr;
    }
  }

  return minPrefixStr;
}

std::optional<string>
RoutesHelper::resolveLinkLocalAddr(
    const openr::thrift::BinaryAddress& linkLocalAddr,
    const openr::thrift::AdjacencyDatabase& adjDatabase) {
  for (const auto& adj : adjDatabase.adjacencies_ref().value()) {
    if (adj.nextHopV6_ref().value().addr_ref().value() ==
        linkLocalAddr.addr_ref().value()) {
      return OpenrUtils::fromOpenrNodeName(adj.otherNodeName_ref().value());
    }
  }
  return std::nullopt;
}

} // namespace terragraph
} // namespace facebook
