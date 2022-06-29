/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "../CentralizedPrefixAllocator.h"

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

class SPAFixture : public ::testing::Test {
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

    nodes_ = {createNode(
                 "1",
                 "01:01:01:01:01:01",
                 thrift::NodeType::DN,
                 true,
                 thrift::NodeStatusType::OFFLINE,
                 "pole-mpk16"),
             createNode(
                 "2",
                 "02:02:02:02:02:02",
                 thrift::NodeType::DN,
                 true,
                 thrift::NodeStatusType::OFFLINE,
                 "pole-mpk16"),
             createNode(
                 "3",
                 "03:03:03:03:03:03",
                 thrift::NodeType::DN,
                 false,
                 thrift::NodeStatusType::OFFLINE,
                 "pole-mpk17"),
             createNode(
                 "4",
                 "04:04:04:04:04:04",
                 thrift::NodeType::DN,
                 false,
                 thrift::NodeStatusType::ONLINE,
                 "pole-mpk17"),
             createNode(
                 "5",
                 "05:05:05:05:05:05",
                 thrift::NodeType::DN,
                 false,
                 thrift::NodeStatusType::OFFLINE,
                 "pole-mpk18"),
             createNode(
                 "6",
                 "06:06:06:06:06:06",
                 thrift::NodeType::DN,
                 false,
                 thrift::NodeStatusType::OFFLINE,
                 "pole-mpk18"),
             createNode(
                 "7",
                 "07:07:07:07:07:07",
                 thrift::NodeType::DN,
                 false,
                 thrift::NodeStatusType::ONLINE,
                 "pole-mpk18"),
             createNode(
                 "8",
                 "08:08:08:08:08:08",
                 thrift::NodeType::DN,
                 false,
                 thrift::NodeStatusType::OFFLINE,
                 "pole-mpk18")};
    // mkp16 <-- 20m --> mpk17 <-- 120m --> mpk18 <-- 1km --> mpk20
             thrift::Site site16 =
                 createSite("pole-mpk16", 37.485301, -122.148005, -25, 10);

             thrift::Site site17 =
                 createSite("pole-mpk17", 37.485180, -122.147885, -25, 10);

             thrift::Site site18 =
                 createSite("pole-mpk18", 37.485811, -122.146738, -25, 10);

             thrift::Site site20 =
                 createSite("pole-mpk20", 37.481044, -122.157929, -25, 10);

             thrift::Site siteUnknown =
                 createSite("pole-unknown", 0, 0, 0, 40000000);

    sites_ = {site16, site17, site18, site20, siteUnknown};

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

  thrift::Node
  createNode(
      const string& nodeName,
      const string& nodeMac,
      const thrift::NodeType nodeType,
      const bool popNode,
      const thrift::NodeStatusType status,
      const string& siteName = "",
      const string& prefix = "",
      const std::vector<string>& wlanMacs = std::vector<std::string>()) {
    thrift::Node node;
    node.name = nodeName;
    node.node_type = nodeType;
    node.mac_addr = nodeMac;
    node.pop_node = popNode;
    node.status = status;
    node.site_name = siteName;
    node.has_cpe_ref() = false;
    node.prefix_ref() = prefix;
    if (wlanMacs.empty() && !nodeMac.empty()) {
      node.wlan_mac_addrs.push_back(nodeMac);
    } else {
      node.wlan_mac_addrs = wlanMacs;
    }
    return node;
  }

  std::unique_ptr<TopologyWrapper>
  getTopoW(
      const vector<thrift::Node>& nodes,
      const vector<thrift::Link>& links,
      const vector<thrift::Site>& sites) {
    thrift::Topology topology;
    topology.name = "test";
    topology.nodes = nodes;
    topology.links = links;
    topology.sites = sites;

    return std::make_unique<TopologyWrapper>(topology, "", true, false);
  }

  ConfigHelper configHelper_;
  std::vector<thrift::Node> nodes_;
  std::vector<thrift::Site> sites_;
  thrift::PrefixAllocParams prefixAllocParams_;
};

}  // anonymous namespace

TEST_F(SPAFixture, invalidNetworkPrefixTest) {
  thrift::PrefixAllocParams invalidSeedPrefix;
  invalidSeedPrefix.seedPrefix = "blabla";
  invalidSeedPrefix.allocPrefixLen = 64;

  thrift::PrefixAllocParams shortAllocLen;
  shortAllocLen.seedPrefix = "face:b00c::/56";
  shortAllocLen.allocPrefixLen = 50;

  thrift::PrefixAllocParams longAllocLen;
  longAllocLen.seedPrefix = "face:b00c::/56";
  longAllocLen.allocPrefixLen = 129;

  thrift::Topology topology;
  topology.name = "test";
  topology.sites = sites_;
  topology.nodes = {
    createNode(
       "1",
       "01:01:01:01:01:01",
       thrift::NodeType::DN,
       true,
       thrift::NodeStatusType::OFFLINE,
       "pole-mpk16")};
  TopologyWrapper topoW(topology, "", true, false);

  EXPECT_THROW(
      CentralizedPrefixAllocator cpa(
          invalidSeedPrefix, &topoW, configHelper_),
      std::invalid_argument);
  EXPECT_THROW(
      CentralizedPrefixAllocator cpa(
          shortAllocLen, &topoW, configHelper_),
      std::invalid_argument);
  EXPECT_THROW(
      CentralizedPrefixAllocator cpa(
          longAllocLen, &topoW, configHelper_),
      std::invalid_argument);
}

TEST_F(SPAFixture, invalidPrefixTest) {
  thrift::Topology topology;
  topology.name = "test";
  topology.sites = sites_;
  topology.nodes = {
      createNode(
          "0",
          "0:0:0:0:0:0",
          thrift::NodeType::DN,
          true,
          thrift::NodeStatusType::OFFLINE,
          "pole-mpk16",
          "face:b00c:0:1::/64")
  };
  TopologyWrapper topologyW(
      topology,
      "",  // topologyDir
      true,  // validateTopology
      false);

  CentralizedPrefixAllocator cpa(
      prefixAllocParams_, &topologyW, configHelper_);

  // Gibberish, invalid IP
  auto node = createNode(
       "1",
       "1:1:1:1:1:1",
       thrift::NodeType::DN,
       true,
       thrift::NodeStatusType::OFFLINE,
       "pole-mpk16",
       "blabla");

  std::unique_ptr<TopologyWrapper> topoW = getTopoW(
      {node}, {}, sites_);
  EXPECT_THROW(
      CentralizedPrefixAllocator tmp(
          prefixAllocParams_, topoW.get(), configHelper_),
      std::invalid_argument);

  topologyW.addNode(node);
  EXPECT_THROW(
      cpa.addNode(node, configHelper_), std::invalid_argument);

  // IP not in subnet, should be reallocated to be in subnet
  node = createNode(
       "2",
       "2:2:2:2:2:2",
       thrift::NodeType::DN,
       true,
       thrift::NodeStatusType::OFFLINE,
       "pole-mpk16",
       "face:b00d::/64");

  topoW = getTopoW({node}, {}, sites_);
  EXPECT_NO_THROW(
      CentralizedPrefixAllocator tmp(
          prefixAllocParams_, topoW.get(), configHelper_));
  auto prefix = folly::IPAddress::createNetwork(
    topoW->getNode("2")->prefix_ref().value());
  auto seedPrefix = folly::IPAddress::createNetwork(
      prefixAllocParams_.seedPrefix);
  EXPECT_TRUE(prefix.first.inSubnet(seedPrefix.first, seedPrefix.second));

  topologyW.addNode(node);
  EXPECT_THROW(
      cpa.addNode(node, configHelper_), std::invalid_argument);

  // IP longer than allocPrefixLen (64)
  node = createNode(
       "3",
       "3:3:3:3:3:3",
       thrift::NodeType::DN,
       true,
       thrift::NodeStatusType::OFFLINE,
       "pole-mpk16",
       "face:b00c:0:ff:f000::/80");
  topoW = getTopoW({node}, {}, sites_);
  EXPECT_THROW(
      CentralizedPrefixAllocator tmp(
          prefixAllocParams_, topoW.get(), configHelper_),
      std::invalid_argument);

  topologyW.addNode(node);
  EXPECT_THROW(
      cpa.addNode(node, configHelper_), std::invalid_argument);

  // Duplicate IPs
  node = createNode(
       "4",
       "4:4:4:4:4:4",
       thrift::NodeType::DN,
       true,
       thrift::NodeStatusType::OFFLINE,
       "pole-mpk17",
       "face:b00c:0:ff::/64");
  auto dupNode = createNode(
       "5",
       "5:5:5:5:5:5",
       thrift::NodeType::DN,
       true,
       thrift::NodeStatusType::OFFLINE,
       "pole-mpk17",
       "face:b00c:0:ff::/64");
  topoW = getTopoW({node, dupNode}, {}, sites_);
  EXPECT_THROW(
      CentralizedPrefixAllocator tmp(
          prefixAllocParams_, topoW.get(), configHelper_),
      std::invalid_argument);

  topologyW.addNode(node);
  EXPECT_NO_THROW(cpa.addNode(node, configHelper_));

  topologyW.addNode(dupNode);
  EXPECT_THROW(
      cpa.addNode(dupNode, configHelper_), std::invalid_argument);
}

TEST_F(SPAFixture, prefixAllocation) {
  thrift::Topology topology;
  topology.name = "test";
  topology.sites = sites_;
  topology.nodes = nodes_;
  topology.links = {};

  TopologyWrapper topologyW(
      topology,
      "",  // topologyDir
      true,  // validateTopology
      false);

  CentralizedPrefixAllocator cpa(
      prefixAllocParams_, &topologyW, configHelper_);

  auto node9 = createNode(
      "9",
      "9:9:9:9:9:9",
      thrift::NodeType::DN,
      true,
      thrift::NodeStatusType::OFFLINE,
      "pole-mpk17");
  topologyW.addNode(node9);
  cpa.addNode(node9, configHelper_);

  auto allocatedPrefixes = cpa.getAllocatedPrefixes();
  auto nodesW = topologyW.getAllNodes();
  // Make sure all nodes have unique prefixes
  for (auto& node : nodesW) {
    EXPECT_NO_THROW(folly::IPAddress::createNetwork(node.prefix_ref().value()));
    EXPECT_EQ(
        node.name,
        allocatedPrefixes[folly::IPAddress::createNetwork(
            node.prefix_ref().value())]);
  }
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
