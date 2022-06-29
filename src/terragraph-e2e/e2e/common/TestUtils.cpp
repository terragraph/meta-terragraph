/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TestUtils.h"

#include "MacUtils.h"

using namespace std;
using namespace facebook::terragraph;

thrift::Node
createNode(
    const string& nodeName,
    const string& nodeMac,
    const string& siteName,
    const bool popNode,
    const thrift::NodeStatusType status,
    const thrift::NodeType nodeType,
    const std::vector<string>& wlanMacs) {
  thrift::Node node;
  node.name = nodeName;
  node.node_type = nodeType;
  node.mac_addr = MacUtils::standardizeMac(nodeMac);
  node.pop_node = popNode;
  node.status = status;
  node.site_name = siteName;
  node.has_cpe_ref() = false;
  if (wlanMacs.empty() && !nodeMac.empty()) {
    node.wlan_mac_addrs.push_back(nodeMac);
  } else {
    node.wlan_mac_addrs = wlanMacs;
  }
  return node;
}

thrift::Site
createSite(
    const string& siteName,
    const float latitude,
    const float longitude,
    const float altitude,
    const float accuracy) {
  thrift::Site site;
  site.name = siteName;
  thrift::Location location;
  location.latitude = latitude;
  location.longitude = longitude;
  location.altitude = altitude;
  location.accuracy = accuracy;
  site.location = location;
  return site;
}

string
getLinkName(const thrift::Node& aNode, const thrift::Node& zNode) {
  if (aNode.name < zNode.name) {
    return folly::sformat("link-{}-{}", aNode.name, zNode.name);
  } else {
    return folly::sformat("link-{}-{}", zNode.name, aNode.name);
  }
}

void
bumpLinkupAttempts(thrift::Topology& topology, const std::string linkName) {
  for (auto& link : topology.links) {
    if (link.name == linkName) {
      link.linkup_attempts++;
    }
  }
}

thrift::Link
createLink(const thrift::Node& aNode, const thrift::Node& zNode) {
  thrift::Link link;
  link.name = getLinkName(aNode, zNode);
  link.a_node_name = aNode.name;
  link.a_node_mac = aNode.mac_addr;
  link.z_node_name = zNode.name;
  link.z_node_mac = zNode.mac_addr;
  link.link_type = thrift::LinkType::WIRELESS;
  link.is_alive = false;
  return link;
}

thrift::Topology
createTopology(
    const std::vector<thrift::Node>& nodes,
    const std::vector<thrift::Link>& links,
    const std::vector<thrift::Site>& sites) {
  thrift::Topology topology;
  topology.name = "test";
  topology.nodes = nodes;
  topology.links = links;
  topology.sites = sites;
  return topology;
}

thrift::IgnitionCandidate
createIgCandidate(const thrift::Node& initiatorNode, const thrift::Link& link) {
  thrift::IgnitionCandidate ignitionCandidate;
  ignitionCandidate.initiatorNodeName = initiatorNode.name;
  ignitionCandidate.linkName = link.name;
  return ignitionCandidate;
}

thrift::Topology
createTopology(
    const int32_t numNodes,
    const std::vector<int32_t> popNodeNums,
    const std::vector<std::pair<int32_t /* node id */, int32_t /* node id */>>&
        linkIds,
    const int32_t numSites,
    const std::vector<std::pair<int32_t /* node id */, int32_t /* site id */>>&
        nodeSiteMap,
    const std::vector<int32_t>& cnNodeNums) {
  std::vector<thrift::Node> nodes;
  std::vector<thrift::Link> links;
  std::vector<thrift::Site> sites;

  for (int i = 0; i < numNodes; i++) {
    string nodeName = folly::sformat("node-{}", i);
    string nodeMac = MacUtils::standardizeMac(
        folly::sformat("{}:{}:{}:{}:{}:{}", i, i, i, i, i, i));
    nodes.push_back(createNode(nodeName, nodeMac));
  }
  for (const auto& popNodeNum : popNodeNums) {
    CHECK(popNodeNum < numNodes);
    nodes[popNodeNum].pop_node = true;
    nodes[popNodeNum].status = thrift::NodeStatusType::ONLINE_INITIATOR;
  }
  for (const auto& cnNodeNum : cnNodeNums) {
    CHECK(cnNodeNum < numNodes);
    nodes[cnNodeNum].node_type = thrift::NodeType::CN;
  }
  for (const auto& linkPair : linkIds) {
    CHECK(linkPair.first < numNodes);
    CHECK(linkPair.second < numNodes);
    auto link = createLink(nodes[linkPair.first], nodes[linkPair.second]);
    links.push_back(link);
  }

  if (numSites == 0 || nodeSiteMap.empty()) {
    // if not specified, assume all nodes are associated to the same pole
    sites.push_back(createSite("pole-0", 0.001, 0.001, 0.001, 0.001));
    for (int i = 0; i < numNodes; i++) {
      nodes[i].site_name = sites.front().name;
    }
  } else {
    for (int i = 0; i < numSites; i++) {
      string siteName = folly::sformat("pole-{}", i);
      sites.push_back(createSite(
          siteName,
          (i + 1) * 0.001,
          (i + 1) * 0.001,
          (i + 1) * 0.001,
          (i + 1) * 0.001));
    }

    for (const auto& nodeSitePair : nodeSiteMap) {
      CHECK(nodeSitePair.first < numNodes);
      CHECK(nodeSitePair.second < numSites);
      nodes[nodeSitePair.first].site_name = sites[nodeSitePair.second].name;
    }
  }

  return createTopology(nodes, links, sites);
}
