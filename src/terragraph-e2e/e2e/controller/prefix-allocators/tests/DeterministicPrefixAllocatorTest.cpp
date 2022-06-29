/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "../CentralizedPrefixAllocator.h"
#include "../DeterministicPrefixAllocator.h"

#include "../../ConfigHelper.h"
#include "../../topology/TopologyWrapper.h"

#include <folly/init/Init.h>
#include <folly/IPAddress.h>
#include <folly/String.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <e2e/common/TestUtils.h>

using namespace std;
using namespace facebook::terragraph;

namespace { // anonymous namespace

class DPAFixture : public ::testing::Test {

 protected:
  void
  SetUp() override {
    configHelper_.setConfigFiles(
        "/etc/e2e_config/base_versions/",       // base_config_dir
        "/etc/e2e_config/base_versions/fw_versions/",  // fw_base_config_dir
        "/etc/e2e_config/base_versions/hw_versions/",  // hw_base_config_dir
        // hw_config_types_file
        "/etc/e2e_config/base_versions/hw_versions/hw_types.json",
        "/tmp/node_config_overrides.json",      // node_config_overrides_file
        // auto_node_config_overrides_file
        "/tmp/auto_node_config_overrides.json",
        // network_config_overrides_file
        "/tmp/network_config_overrides.json",
        "/etc/e2e_config/config_metadata.json", // node_config_metadata_file
        "/tmp/cfg_backup/",                     // config_backup_dir
        {});
    prefixAllocParams_.seedPrefix = "face:b00c::/56";
    prefixAllocParams_.allocPrefixLen = 64;
  }

  void
  TearDown() override {
    // Delete any configs created by tests
    remove("/tmp/node_config_overrides.json");
    remove("/tmp/auto_node_config_overrides.json");
    remove("/tmp/network_config_overrides.json");
  }

  // Checks if node prefix is in the subnet of any of the given zone prefixes
  void
  checkNodePrefixInZonePrefixSubnets(
      const folly::CIDRNetwork& nodePrefix,
      const std::unordered_set<std::string>& zonePrefixes) {
    bool inZonePrefixSubnet = false;
    for (const auto& zonePrefix : zonePrefixes) {
      if (nodePrefix.first.inSubnet(zonePrefix)) {
        inZonePrefixSubnet = true;
        break;
      }
    }
    EXPECT_TRUE(inZonePrefixSubnet) << folly::format(
        "{} not in subnet of any of the zone prefixes: [{}]",
        folly::IPAddress::networkToString(nodePrefix),
        folly::join(", ", zonePrefixes));
  }

  // Checks to make sure node prefixes are all unique
  void
  checkNodePrefixesUnique(const std::vector<thrift::Node>& nodes) {
    std::unordered_map<folly::CIDRNetwork, std::string> seenPrefixes;

    for (const auto& node : nodes) {
      // Check all nodes have prefix
      EXPECT_TRUE(node.prefix_ref().has_value() && !node.prefix_ref()
          .value().empty())
          << folly::format("{} doesn't have a prefix.", node.name);

      // Check for uniqueness
      auto prefix = folly::IPAddress::createNetwork(node.prefix_ref().value());
      auto iter = seenPrefixes.find(prefix);
      EXPECT_EQ(iter, seenPrefixes.end()) << folly::format(
          "Prefix {} for node {} already allocated to {}",
          node.prefix_ref().value(),
          node.name,
          iter->second);

      // Save prefix
      seenPrefixes[prefix] = node.name;
    }
  }

  // Ensure all zone prefixes are in the seed prefix's subnet
  void
  checkZonePrefixesInSeedPrefixSubnet(
      const std::unordered_set<std::string>& zonePrefixes,
      const thrift::PrefixAllocParams& prefixAllocParams) {

    const auto seedPrefix = folly::IPAddress::createNetwork(
        prefixAllocParams.seedPrefix);

    // Loop through all zone prefixes and make sure they are in the seed
    // prefix's subnet
    for (const auto& zonePrefixStr : zonePrefixes) {
      auto zonePrefix = folly::IPAddress::createNetwork(zonePrefixStr);
      EXPECT_TRUE(zonePrefix.first.inSubnet(
          seedPrefix.first, seedPrefix.second)) << folly::format(
              "{} not in seed prefix {}",
              zonePrefixStr,
              prefixAllocParams.seedPrefix);
    }
  }

  // Checks that the following constraints are satisfied:
  // 1. All nodes have prefixes and they are unique
  // 2. Each node's prefix is in a subnet of its zone's prefixes
  // 3. Zone prefixes are all in the seed prefix's subnet
  void
  checkDPABasics(
      const DeterministicPrefixAllocator& dpa,
      const std::unique_ptr<TopologyWrapper>& topologyW,
      const thrift::PrefixAllocParams& prefixAllocParams,
      const std::unordered_map<std::string, std::unordered_set<std::string>>&
          popSite2Node) {
    auto newTopology = topologyW->getTopology();
    EXPECT_TRUE(
        newTopology.config.deterministic_prefix_alloc_params_ref().value()
            .zones_ref().has_value()) << "Zones not saved to topology";

    // Check that all nodes have prefixes and they are unique
    checkNodePrefixesUnique(topologyW->getAllNodes());

    // Check that node prefixes are in appropriate zone prefix
    auto zonePrefixes = dpa.getZonePrefixes();
    for (const auto& node : newTopology.nodes) {
      auto prefix = folly::IPAddress::createNetwork(node.prefix_ref().value());
      checkNodePrefixInZonePrefixSubnets(
          prefix, zonePrefixes[node2PopSite(node.name, popSite2Node)]);
    }

    // Check that zone prefixes are all subnets of seed prefix
    for (const auto& zonePrefixEntry : zonePrefixes) {
      checkZonePrefixesInSeedPrefixSubnet(
          zonePrefixEntry.second, prefixAllocParams);
    }

    // Check that specificNetworkPrefixes are written to config
    for (const auto& node : newTopology.nodes) {
      auto specificPrefixes = configHelper_.getAutoNodeSpecificNetworkPrefixes(
          node.name);

      if (!node.pop_node) {
        EXPECT_FALSE(specificPrefixes.has_value());
        return;
      }

      // Node is POP
      auto iter = zonePrefixes.find(node.site_name);
      EXPECT_TRUE(iter != zonePrefixes.end()) << folly::format(
          "POP node {}'s site {} not in zonePrefixes",
          node.name,
          node.site_name);

      // Ensure seedPrefix is added to specificPrefixes
      auto seedPrefixStr = folly::IPAddress::networkToString(
          folly::IPAddress::createNetwork(prefixAllocParams.seedPrefix));
      iter->second.insert(seedPrefixStr);
      auto expectedSpecificPrefixes = folly::join(",", iter->second);
      EXPECT_EQ(expectedSpecificPrefixes, specificPrefixes);
    }
  }

  std::string
  node2PopSite(
      const std::string& nodeName,
      const std::unordered_map<std::string, std::unordered_set<std::string>>
          popSite2Node) {
    for (const auto& entry : popSite2Node) {
      if (entry.second.count(nodeName)) {
        return entry.first;
      }
    }
    return "";
  }

  thrift::Node
  createNodeWithSite(int nodeIndex, const std::string& siteName, bool isPop) {
    std::string nodeName = folly::sformat("node-{}", nodeIndex);
    std::string nodeMac = folly::sformat("00:00:00:00:00:{:0>2}", nodeIndex);
    thrift::PolarityType polarity = nodeIndex % 2 == 0
        ? thrift::PolarityType::EVEN
        : thrift::PolarityType::ODD;
    thrift::Node node = createNode(
        nodeName,
        nodeMac,
        siteName,
        isPop);

    std::string errorMsg;
    configHelper_.setNodePolarity(nodeName, nodeMac, polarity, false, errorMsg);

    return node;
  }

  ConfigHelper configHelper_;
  thrift::PrefixAllocParams prefixAllocParams_;
  std::unique_ptr<TopologyWrapper> topologyW_;
  std::unordered_map<std::string, thrift::Site> sites_{
    {"A", createSite(
        "A",
        37.485301,
        -122.148005,
        -25,
        10)},
    {"B", createSite(
        "B",
        37.485180,
        -122.147885,
        -25,
        10)},
    {"C", createSite(
        "C",
        37.485811,
        -122.146738,
        -25,
        10)},
    {"D", createSite(
        "D",
        37.484862,
        -122.147674,
        -25,
        10)}
  };
};

}  // anonymous namespace

// TODO check that specificNetworkPrefixes are set in config ?

TEST_F(DPAFixture, EmptyPrefixAllocationParams) {
  thrift::PrefixAllocParams prefixAllocParams;
  topologyW_ = std::make_unique<TopologyWrapper>(
      thrift::Topology(),
      "",  // topologyDir
      true,  // validateTopology
      false);  // createIntrasiteLinks

  EXPECT_THROW(
      DeterministicPrefixAllocator dpa(
          prefixAllocParams, topologyW_.get(), configHelper_),
      std::invalid_argument);
}

TEST_F(DPAFixture, OnePOPSimpleTopology) {
  thrift::Topology topology;
  topology.name = "test";

  std::string siteName = "A";
  std::unordered_map<std::string, std::unordered_set<std::string>>
      popSite2Node = {
          {siteName, {"node-1", "node-2", "node-3", "node-4"}}};

  topology.nodes = {
      createNodeWithSite(1, siteName, true),
      createNodeWithSite(2, siteName, false),
      createNodeWithSite(3, siteName, false),
      createNodeWithSite(4, siteName, false)};
  topology.sites = {sites_[siteName]};

  topologyW_ = std::make_unique<TopologyWrapper>(
      topology,
      "",  // topologyDir
      true,  // validateTopology
      false);  // createIntrasiteLinks

  DeterministicPrefixAllocator dpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);

  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);
}

TEST_F(DPAFixture, OnePOPComplexTopology) {
  thrift::Topology topology;
  topology.name = "test";

  std::string siteNameA = "A";
  std::string siteNameB = "B";
  std::unordered_map<std::string, std::unordered_set<std::string>>
      popSite2Node = {
          {siteNameA, {"node-1", "node-2", "node-3", "node-4"}}};

  topology.nodes = {
      createNodeWithSite(1, siteNameA, true),
      createNodeWithSite(2, siteNameA, false),
      createNodeWithSite(3, siteNameB, false),
      createNodeWithSite(4, siteNameB, false)};
  topology.sites = {sites_[siteNameA], sites_[siteNameB]};
  topology.links = {createLink(topology.nodes[1], topology.nodes[2])};

  topologyW_ = std::make_unique<TopologyWrapper>(
      topology,
      "",  // topologyDir
      true,  // validateTopology
      false);  // createIntrasiteLinks

  DeterministicPrefixAllocator dpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);

  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);
}

TEST_F(DPAFixture, TwoPOPSimpleTopology) {
  thrift::Topology topology;
  topology.name = "test";

  std::string siteNameA = "A";
  std::string siteNameB = "B";
  std::unordered_map<std::string, std::unordered_set<std::string>>
      popSite2Node = {
          {siteNameA, {"node-1", "node-2"}},
          {siteNameB, {"node-3", "node-4"}}};

  topology.nodes = {
      createNodeWithSite(1, siteNameA, true),
      createNodeWithSite(2, siteNameA, false),
      createNodeWithSite(3, siteNameB, true),
      createNodeWithSite(4, siteNameB, false)};
  topology.sites = {sites_[siteNameA], sites_[siteNameB]};
  topology.links = {createLink(topology.nodes[1], topology.nodes[2])};

  topologyW_ = std::make_unique<TopologyWrapper>(
      topology,
      "",  // topologyDir
      true,  // validateTopology
      false);  // createIntrasiteLinks

  DeterministicPrefixAllocator dpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);

  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);

}

TEST_F(DPAFixture, TwoPOPComplexTopology) {
  thrift::Topology topology;
  topology.name = "test";

  std::string siteNameA = "A";
  std::string siteNameB = "B";
  std::string siteNameC = "C";
  std::string siteNameD = "D";
  std::unordered_map<std::string, std::unordered_set<std::string>>
      popSite2Node = {
          {siteNameA, {"node-1", "node-2", "node-5", "node-6"}},
          {siteNameB, {"node-3", "node-4", "node-7", "node-8"}}};

  topology.nodes = {
      createNodeWithSite(1, siteNameA, true),
      createNodeWithSite(2, siteNameA, false),
      createNodeWithSite(3, siteNameB, true),
      createNodeWithSite(4, siteNameB, false),
      createNodeWithSite(5, siteNameC, false),
      createNodeWithSite(6, siteNameC, false),
      createNodeWithSite(7, siteNameD, false),
      createNodeWithSite(8, siteNameD, false)};
  topology.sites = {
      sites_[siteNameA],
      sites_[siteNameB],
      sites_[siteNameC],
      sites_[siteNameD]};
  topology.links = {
      createLink(topology.nodes[1], topology.nodes[2]),
      createLink(topology.nodes[0], topology.nodes[5]),
      createLink(topology.nodes[3], topology.nodes[6])};

  topologyW_ = std::make_unique<TopologyWrapper>(
      topology,
      "",  // topologyDir
      true,  // validateTopology
      false);  // createIntrasiteLinks

  DeterministicPrefixAllocator dpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);

  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);

  // TODO check that specificNetworkPrefixes are set in config
}

TEST_F(DPAFixture, MultiPopSimpleTopology) {
  thrift::Topology topology;
  topology.name = "test";

  std::string siteNameA = "A";
  std::string siteNameB = "B";
  std::string siteNameC = "C";
  std::unordered_map<std::string, std::unordered_set<std::string>>
      popSite2Node = {
          {siteNameA, {"node-1", "node-2"}},
          {siteNameB, {"node-3", "node-4"}},
          {siteNameC, {"node-5", "node-6"}}};

  topology.nodes = {
      createNodeWithSite(1, siteNameA, true),
      createNodeWithSite(2, siteNameA, false),
      createNodeWithSite(3, siteNameB, true),
      createNodeWithSite(4, siteNameB, false),
      createNodeWithSite(5, siteNameC, true),
      createNodeWithSite(6, siteNameC, false)};
  topology.sites = {sites_[siteNameA], sites_[siteNameB], sites_[siteNameC]};
  topology.links = {
      createLink(topology.nodes[1], topology.nodes[2]),
      createLink(topology.nodes[0], topology.nodes[5]),
      createLink(topology.nodes[3], topology.nodes[4])};

  topologyW_ = std::make_unique<TopologyWrapper>(
      topology,
      "",  // topologyDir
      true,  // validateTopology
      false);  // createIntrasiteLinks

  DeterministicPrefixAllocator dpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);

  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);
}

TEST_F(DPAFixture, NotEnoughPrefixSpace) {
  thrift::Topology topology;
  topology.name = "test";

  std::string siteName = "A";
  topology.nodes = {
      createNodeWithSite(1, siteName, true),
      createNodeWithSite(2, siteName, false),
      createNodeWithSite(3, siteName, false),
      createNodeWithSite(4, siteName, false)};
  topology.sites = {sites_[siteName]};

  // Prefix allocation params with only 2 prefixes
  thrift::PrefixAllocParams prefixAllocParams;
  prefixAllocParams.seedPrefix = "face:b00c::/63";
  prefixAllocParams.allocPrefixLen = 64;
  topologyW_ = std::make_unique<TopologyWrapper>(
      topology, "", true, false);

  EXPECT_THROW(
      DeterministicPrefixAllocator dpa(
          prefixAllocParams, topologyW_.get(), configHelper_),
      std::invalid_argument);
}

TEST_F(DPAFixture, AddNode) {
  thrift::Topology topology;
  topology.name = "test";

  std::string siteNameA = "A";
  std::string siteNameB = "B";
  std::string siteNameC = "C";
  std::unordered_map<std::string, std::unordered_set<std::string>>
      popSite2Node = {
          {siteNameA, {"node-1", "node-2", "node-3", "node-4", "node-5",
                       "node-6", "node-7", "node-8", "node-9"}}};

  topology.nodes = {createNodeWithSite(1, siteNameA, true)};
  topology.sites = {sites_[siteNameA], sites_[siteNameB], sites_[siteNameC]};

  // Prefix allocation params with only 2 prefixes
  thrift::PrefixAllocParams prefixAllocParams;
  prefixAllocParams.seedPrefix = "face:b00c::/61";
  prefixAllocParams.allocPrefixLen = 64;
  topologyW_ = std::make_unique<TopologyWrapper>(
      topology, "", true, false);

  DeterministicPrefixAllocator dpa(
      prefixAllocParams, topologyW_.get(), configHelper_);

  auto node2 = createNodeWithSite(2, siteNameA, false);
  auto node3 = createNodeWithSite(3, siteNameA, false);
  auto node4 = createNodeWithSite(4, siteNameA, false);

  auto node5 = createNodeWithSite(5, siteNameB, false);
  auto node6 = createNodeWithSite(6, siteNameB, false);
  auto node7 = createNodeWithSite(7, siteNameB, false);
  auto node8 = createNodeWithSite(8, siteNameB, false);

  auto node9 = createNodeWithSite(9, siteNameC, false);

  // Add all the nodes on site A (intrasite links created automatically)
  std::vector<thrift::Node> siteANodes = {node2, node3, node4};
  for (auto& node : siteANodes) {
    topologyW_->addNode(node);
    EXPECT_NO_THROW(dpa.addNode(node, configHelper_));
    checkDPABasics(dpa, topologyW_, prefixAllocParams, popSite2Node);
  }

  // Create link between siteA and siteB
  topologyW_->addNode(node5);
  EXPECT_NO_THROW(dpa.addNode(node5, configHelper_));

  auto link45 = createLink(node4, node5);
  topologyW_->addLink(link45);
  EXPECT_NO_THROW(dpa.addLink(link45, configHelper_));
  checkDPABasics(dpa, topologyW_, prefixAllocParams, popSite2Node);

  // Add all the nodes on site B (intrasite links created automatically)
  std::vector<thrift::Node> siteBNodes = {node6, node7, node8};
  for (auto& node : siteBNodes) {
    topologyW_->addNode(node);
    EXPECT_NO_THROW(dpa.addNode(node, configHelper_));
    checkDPABasics(dpa, topologyW_, prefixAllocParams, popSite2Node);
  }

  // Add another node that will exceed number of available prefixes
  topologyW_->addNode(node9);
  EXPECT_NO_THROW(dpa.addNode(node9, configHelper_));

  auto link89 = createLink(node8, node9);
  topologyW_->addLink(link89);
  EXPECT_THROW(dpa.addLink(link89, configHelper_), std::invalid_argument);
}

TEST_F(DPAFixture, AddPopNode) {
  thrift::Topology topology;
  topology.name = "test";

  std::string siteNameA = "A";
  std::string siteNameB = "B";
  std::unordered_map<std::string, std::unordered_set<std::string>>
      popSite2Node = {
          {siteNameA, {"node-1"}},
          {siteNameB, {"node-2"}}};

  auto node1 = createNodeWithSite(1, siteNameA, true);
  auto node2 = createNodeWithSite(2, siteNameB, true);
  topology.nodes = {node1};
  topology.sites = {sites_[siteNameA], sites_[siteNameB]};

  topologyW_ = std::make_unique<TopologyWrapper>(
      topology,
      "",
      true,
      false);

  DeterministicPrefixAllocator dpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);

  // Create link between siteA and siteB
  topologyW_->addNode(node2);
  EXPECT_NO_THROW(dpa.addNode(node2, configHelper_));

  auto link12 = createLink(node1, node2);
  topologyW_->addLink(link12);
  EXPECT_NO_THROW(dpa.addLink(link12, configHelper_));
  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);
}

TEST_F(DPAFixture, DelPopNode) {
  thrift::Topology topology;
  topology.name = "test";

  std::string siteNameA = "A";
  std::string siteNameB = "B";
  std::unordered_map<std::string, std::unordered_set<std::string>>
      popSite2Node = {
          {siteNameA, {"node-1", "node-3", "node-4"}}};

  auto node1 = createNodeWithSite(1, siteNameA, true);
  auto node2 = createNodeWithSite(2, siteNameB, true);
  auto node3 = createNodeWithSite(3, siteNameA, false);
  auto node4 = createNodeWithSite(4, siteNameB, false);
  auto link12 = createLink(node1, node2);
  auto link34 = createLink(node3, node4);

  topology.nodes = {node1, node2, node3, node4};
  topology.sites = {sites_[siteNameA], sites_[siteNameB]};
  topology.links = {link12, link34};

  topologyW_ = std::make_unique<TopologyWrapper>(
      topology,
      "",
      true,
      true);

  DeterministicPrefixAllocator dpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);

  topologyW_->delNode(node2.name, false);
  EXPECT_NO_THROW(dpa.delNode(node2, configHelper_));
  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);
}

TEST_F(DPAFixture, EditNode) {
  thrift::Topology topology;
  topology.name = "test";

  std::string siteNameA = "A";
  std::string siteNameB = "B";
  std::unordered_map<std::string, std::unordered_set<std::string>>
      popSite2Node;

  auto node1 = createNodeWithSite(1, siteNameA, true);
  auto node2 = createNodeWithSite(2, siteNameB, true);
  auto node3 = createNodeWithSite(3, siteNameA, false);
  auto node4 = createNodeWithSite(4, siteNameB, false);
  auto link12 = createLink(node1, node2);
  auto link34 = createLink(node3, node4);

  topology.nodes = {node1, node2, node3, node4};
  topology.sites = {sites_[siteNameA], sites_[siteNameB]};
  topology.links = {link12, link34};

  topologyW_ = std::make_unique<TopologyWrapper>(
      topology,
      "",
      true,
      true);

  DeterministicPrefixAllocator dpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);

  // Edit node4's name
  auto oldNode = topologyW_->getNode(node4.name);
  EXPECT_TRUE(oldNode);
  thrift::Node newNode = *oldNode;
  newNode.name = "new-" + oldNode->name;
  topologyW_->editNode(oldNode->name, newNode);
  EXPECT_NO_THROW(dpa.editNode(*oldNode, newNode));
  popSite2Node = {
      {siteNameA, {"node-1", "node-3"}},
      {siteNameB, {"node-2", "new-node-4"}}};
  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);

  // Edit node4's name
  oldNode = topologyW_->getNode(newNode.name);
  EXPECT_TRUE(oldNode);
  newNode = *oldNode;
  newNode.name = node4.name;
  topologyW_->editNode(oldNode->name, newNode);
  EXPECT_NO_THROW(dpa.editNode(*oldNode, newNode));
  popSite2Node = {
      {siteNameA, {"node-1", "node-3"}},
      {siteNameB, {"node-2", "node-4"}}};
  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);
}

TEST_F(DPAFixture, EditSite) {
  thrift::Topology topology;
  topology.name = "test";

  std::string siteNameA = "A";
  std::string siteNameB = "B";
  std::unordered_map<std::string, std::unordered_set<std::string>>
      popSite2Node = {
          {siteNameA, {"node-1", "node-2", "node-3", "node-4"}}};

  auto node1 = createNodeWithSite(1, siteNameA, true);
  auto node2 = createNodeWithSite(2, siteNameB, false);
  auto node3 = createNodeWithSite(3, siteNameA, false);
  auto node4 = createNodeWithSite(4, siteNameB, false);
  auto link12 = createLink(node1, node2);
  auto link34 = createLink(node3, node4);

  topology.nodes = {node1, node2, node3, node4};
  topology.sites = {sites_[siteNameA], sites_[siteNameB]};
  topology.links = {link12, link34};

  topologyW_ = std::make_unique<TopologyWrapper>(
      topology,
      "",
      true,
      true);

  DeterministicPrefixAllocator dpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);

  // Edit siteB's name
  auto oldSite = topologyW_->getSite(siteNameB);
  EXPECT_TRUE(oldSite);
  thrift::Site newSite = *oldSite;
  newSite.name = "new-" + oldSite->name;
  topologyW_->editSite(oldSite->name, newSite);
  EXPECT_NO_THROW(dpa.editSite(oldSite->name, newSite));
  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);

  // Edit siteB's name again
  oldSite = topologyW_->getSite(newSite.name);
  EXPECT_TRUE(oldSite);
  newSite = *oldSite;
  newSite.name = siteNameB;
  topologyW_->editSite(oldSite->name, newSite);
  EXPECT_NO_THROW(dpa.editSite(oldSite->name, newSite));
  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);
}

TEST_F(DPAFixture, EditPopSite) {
  thrift::Topology topology;
  topology.name = "test";

  std::string siteNameA = "A";
  std::string siteNameB = "B";
  std::unordered_map<std::string, std::unordered_set<std::string>>
      popSite2Node;

  auto node1 = createNodeWithSite(1, siteNameA, true);
  auto node2 = createNodeWithSite(2, siteNameB, true);
  auto node3 = createNodeWithSite(3, siteNameA, false);
  auto node4 = createNodeWithSite(4, siteNameB, false);
  auto link12 = createLink(node1, node2);
  auto link34 = createLink(node3, node4);

  topology.nodes = {node1, node2, node3, node4};
  topology.sites = {sites_[siteNameA], sites_[siteNameB]};
  topology.links = {link12, link34};

  topologyW_ = std::make_unique<TopologyWrapper>(
      topology,
      "",
      true,
      true);

  DeterministicPrefixAllocator dpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);

  // Edit siteB's name
  auto oldSite = topologyW_->getSite(siteNameB);
  EXPECT_TRUE(oldSite);
  thrift::Site newSite = *oldSite;
  newSite.name = "new-" + oldSite->name;
  topologyW_->editSite(oldSite->name, newSite);
  EXPECT_NO_THROW(dpa.editSite(oldSite->name, newSite));
  popSite2Node = {
      {siteNameA, {"node-1", "node-3"}},
      {"new-" + siteNameB, {"node-2", "node-4"}}};
  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);

  // Edit siteB's name
  oldSite = topologyW_->getSite(newSite.name);
  EXPECT_TRUE(oldSite);
  newSite = *oldSite;
  newSite.name = siteNameB;
  topologyW_->editSite(oldSite->name, newSite);
  EXPECT_NO_THROW(dpa.editSite(oldSite->name, newSite));
  popSite2Node = {
      {siteNameA, {"node-1", "node-3"}},
      {siteNameB, {"node-2", "node-4"}}};
  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);
}

TEST_F(DPAFixture, DpaToCpaToDpaWithPopDel) {
  thrift::Topology topology;
  topology.name = "test";

  std::string siteNameA = "A";
  std::string siteNameB = "B";
  std::string siteNameC = "C";
  std::string siteNameD = "D";
  std::unordered_map<std::string, std::unordered_set<std::string>> popSite2Node;
  // figure-0
  // A[1]* --- B[2]*
  //  |         |
  // C[3] ---- D[4]
  auto node1 = createNodeWithSite(1, siteNameA, true /* pop */);
  auto node2 = createNodeWithSite(2, siteNameB, true /* pop */);
  auto node3 = createNodeWithSite(3, siteNameC, false);
  auto node4 = createNodeWithSite(4, siteNameD, false);
  auto link12 = createLink(node1, node2);
  auto link13 = createLink(node1, node3);
  auto link24 = createLink(node2, node4);
  auto link34 = createLink(node3, node4);

  topology.nodes = {node1, node2, node3, node4};
  topology.sites = {
      sites_[siteNameA],
      sites_[siteNameB],
      sites_[siteNameC],
      sites_[siteNameD]};
  topology.links = {link12, link13, link24, link34};

  topologyW_ = std::make_unique<TopologyWrapper>(topology, "", true, true);

  // enable DPA
  DeterministicPrefixAllocator dpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);
  popSite2Node = {
      {siteNameA, {node1.name, node3.name}},
      {siteNameB, {node2.name, node4.name}}};
  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);

  // switch to CPA
  CentralizedPrefixAllocator cpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);

  // delete one pop site/node before re-enabling DPA
  EXPECT_NO_THROW(topologyW_->delLink(node1.name, node2.name, false));
  EXPECT_NO_THROW(topologyW_->delLink(node2.name, node4.name, false));
  EXPECT_NO_THROW(topologyW_->delNode(node2.name, false));
  EXPECT_NO_THROW(topologyW_->delSite(siteNameB));

  // re-run DPA logic to make sure it's valid
  DeterministicPrefixAllocator backToDpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);
  popSite2Node = {{siteNameA, {node1.name, node3.name, node4.name}}};
  checkDPABasics(backToDpa, topologyW_, prefixAllocParams_, popSite2Node);
}

TEST_F(DPAFixture, DpaToCpaToDpaWithPopAdd) {
  thrift::Topology topology;
  topology.name = "test";

  std::string siteNameA = "A";
  std::string siteNameB = "B";
  std::string siteNameC = "C";
  std::string siteNameD = "D";
  std::unordered_map<std::string, std::unordered_set<std::string>> popSite2Node;
  // figure-0
  // A[1]*
  //  |
  // C[3] ---- D[4]
  auto node1 = createNodeWithSite(1, siteNameA, true /* pop */);
  auto node3 = createNodeWithSite(3, siteNameC, false);
  auto node4 = createNodeWithSite(4, siteNameD, false);
  auto link13 = createLink(node1, node3);
  auto link34 = createLink(node3, node4);

  topology.nodes = {node1, node3, node4};
  topology.sites = {sites_[siteNameA], sites_[siteNameC], sites_[siteNameD]};
  topology.links = {link13, link34};

  topologyW_ = std::make_unique<TopologyWrapper>(topology, "", true, true);

  // enable DPA
  DeterministicPrefixAllocator dpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);
  popSite2Node = {{siteNameA, {node1.name, node3.name, node4.name}}};
  checkDPABasics(dpa, topologyW_, prefixAllocParams_, popSite2Node);

  // switch to CPA
  CentralizedPrefixAllocator cpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);

  // add one pop site/node before re-enabling DPA
  EXPECT_NO_THROW(topologyW_->addSite(sites_[siteNameB]));
  auto node2 = createNodeWithSite(2, siteNameB, true /* pop */);
  EXPECT_NO_THROW(topologyW_->addNode(node2));
  auto link12 = createLink(node1, node2);
  EXPECT_NO_THROW(topologyW_->addLink(link12));
  auto link24 = createLink(node2, node4);
  EXPECT_NO_THROW(topologyW_->addLink(link24));

  // re-run DPA logic to make sure it's valid
  DeterministicPrefixAllocator backToDpa(
      prefixAllocParams_, topologyW_.get(), configHelper_);
  popSite2Node = {
      {siteNameA, {node1.name, node3.name}},
      {siteNameB, {node2.name, node4.name}}};
  checkDPABasics(backToDpa, topologyW_, prefixAllocParams_, popSite2Node);
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
