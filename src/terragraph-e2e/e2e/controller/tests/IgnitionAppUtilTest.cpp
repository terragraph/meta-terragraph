/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <folly/init/Init.h>

#include "../ConfigHelper.h"
#include "../IgnitionAppUtil.h"

#include <e2e/common/TestUtils.h>

using namespace facebook::terragraph;
using std::string;

namespace {
  std::chrono::seconds kBfTimeout{15 + 1};
  std::chrono::seconds kDampenInterval{5};
  std::chrono::seconds kExtendedDampenInterval{300};
  std::chrono::seconds kExtendedDampenFailureInterval{1800};
  std::chrono::seconds kBackupCnLinkInterval{300};
  std::chrono::seconds kP2mpAssocDelay{0};

  // wrapper function for IgnitionAppUtil::findAllParallelIgnitionCandidates()
  std::vector<thrift::IgnitionCandidate> ignite(
      const TopologyWrapper& topologyW,
      std::unordered_map<std::string, std::chrono::steady_clock::time_point>&
          linkToAttemptTs) {
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        linkToInitialAttemptTs;  // unused here
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        cnToPossibleIgnitionTs;  // unused here
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        initiatorToAttemptTs;  // unused here
    std::unordered_map<
        std::string,
        std::pair<std::chrono::steady_clock::time_point, std::string>>
            radioToLinkUpTs;  // unused here
    std::unordered_map<std::string, size_t> linkIterationIndex;  // unused here
    return IgnitionAppUtil::findAllParallelIgnitionCandidates(
        topologyW,
        linkToAttemptTs,
        linkToInitialAttemptTs,
        cnToPossibleIgnitionTs,
        initiatorToAttemptTs,
        radioToLinkUpTs,
        kBfTimeout,
        kDampenInterval,
        kExtendedDampenInterval,
        kExtendedDampenFailureInterval,
        kBackupCnLinkInterval,
        kP2mpAssocDelay,
        linkIterationIndex);
  }
}

class IgnitionAppUtilTest : public ::testing::Test {
public:
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
  }

  void
  TearDown() override {
    // Delete any configs created by tests
    remove("/tmp/node_config_overrides.json");
    remove("/tmp/auto_node_config_overrides.json");
    remove("/tmp/network_config_overrides.json");
  }

  ConfigHelper configHelper_;
};

// 10 node topology
//
//                                |-------- node9
//                                |
//                                |
//        |-------- node3 ----- node6 ----- node7
//        |                                  |
//        |                                  |
// node0 (pop)         |---------------------|
//        |            |
//        |            |
//        |-------> node2 ----- node5 ----- node8
//        ||                      |
//        ||                      |
// node1(pop) ----- node4 --------|
TEST_F(IgnitionAppUtilTest, 10nodeIgnition) {

  SCOPE_EXIT { LOG(INFO) << "IgnitionApp 10node ignition util test is done"; };

  std::vector<thrift::Node> nodes;
  std::vector<thrift::Site> sites;

  for (int i = 0; i < 3; i++) {
    string siteName = folly::sformat("pole-{}", i);
    sites.push_back(createSite(
        siteName,
        (i + 1) * 0.001,
        (i + 1) * 0.001,
        (i + 1) * 0.001,
        (i + 1) * 0.001));
  }

  // setup network topology file
  std::string errorMsg;
  for (int i = 0; i < 10; i++) {
    string nodeName = folly::sformat("node-{}", i);
    string nodeMac = folly::sformat("{}:{}:{}:{}:{}:{}", i, i, i, i, i, i);
    auto node = createNode(
        nodeName,
        nodeMac,
        sites[(i / 4) % sites.size()].name,
        (i <= 1 ? true : false));
    nodes.push_back(node);
    configHelper_.setNodePolarity(
        node.name,
        node.mac_addr,
        (i % 2) ? thrift::PolarityType::ODD : thrift::PolarityType::EVEN,
        false,
        errorMsg);
  }
  auto linkNode0And1 = createLink(nodes[0], nodes[1]);
  auto linkNode0And3 = createLink(nodes[0], nodes[3]);
  auto linkNode1And2 = createLink(nodes[1], nodes[2]);
  auto linkNode1And4 = createLink(nodes[1], nodes[4]);
  auto linkNode2And5 = createLink(nodes[2], nodes[5]);
  auto linkNode2And7 = createLink(nodes[2], nodes[7]);
  auto linkNode3And6 = createLink(nodes[3], nodes[6]);
  auto linkNode4And5 = createLink(nodes[4], nodes[5]);
  auto linkNode5And8 = createLink(nodes[5], nodes[8]);
  auto linkNode6And7 = createLink(nodes[6], nodes[7]);
  auto linkNode6And9 = createLink(nodes[6], nodes[9]);
  std::vector<thrift::Link> links = {linkNode0And1,
                                     linkNode0And3,
                                     linkNode1And2,
                                     linkNode1And4,
                                     linkNode2And5,
                                     linkNode2And7,
                                     linkNode3And6,
                                     linkNode4And5,
                                     linkNode5And8,
                                     linkNode6And7,
                                     linkNode6And9};

  auto topology = createTopology(nodes, links, sites);
  TopologyWrapper topologyW(topology, "", false);
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      linkToAttemptTs;

  // This should be a full successful ignition process
  // pop nodes are not online yet, no link to ignite
  std::vector<thrift::IgnitionCandidate> igCandidates;
  igCandidates = ignite(topologyW, linkToAttemptTs);
  std::vector<thrift::IgnitionCandidate> expectedIgCandidates = {};
  EXPECT_EQ(expectedIgCandidates, igCandidates);
  // mark pop nodes (node-0, node-1) to be alive
  for (auto const& popNodeName : topologyW.getPopNodeNames()) {
    topologyW.setNodeStatus(
        popNodeName, thrift::NodeStatusType::ONLINE_INITIATOR);
  }

  // check candidate links (unignited links of node-0, node-1)
  igCandidates = ignite(topologyW, linkToAttemptTs);
  std::vector<thrift::IgnitionCandidate> expectedIgCandidates1 =
      {createIgCandidate(nodes[0], linkNode0And1)};
  std::vector<thrift::IgnitionCandidate> expectedIgCandidates2 =
      {createIgCandidate(nodes[1], linkNode0And1)};
  std::vector<thrift::IgnitionCandidate> expectedIgCandidates3 =
      {createIgCandidate(nodes[0], linkNode0And3),
       createIgCandidate(nodes[1], linkNode1And2)};
  EXPECT_TRUE(expectedIgCandidates1 == igCandidates ||
              expectedIgCandidates2 == igCandidates ||
              expectedIgCandidates3 == igCandidates);
  // mark candidate links and node-1 to be alive
  for (auto const& igCandidate : expectedIgCandidates) {
    topologyW.setLinkStatus(igCandidate.linkName, true);
    auto nbrName = topologyW.getNbrNodeName(
        igCandidate.initiatorNodeName, igCandidate.linkName);
    topologyW.setNodeStatus(*nbrName, thrift::NodeStatusType::ONLINE_INITIATOR);
  }

  // check candidate links (unignited links of node-0, node-1)
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates = {createIgCandidate(nodes[0], linkNode0And3),
                          createIgCandidate(nodes[1], linkNode1And2)};
  // mark candidate links and node-3, node-2 to be alive
  for (auto const& igCandidate : expectedIgCandidates) {
    topologyW.setLinkStatus(igCandidate.linkName, true);
    auto nbrName = topologyW.getNbrNodeName(
        igCandidate.initiatorNodeName, igCandidate.linkName);
    topologyW.setNodeStatus(*nbrName, thrift::NodeStatusType::ONLINE_INITIATOR);
  }

  // check candidate links (unignited links of node-1, node-2, node-3)
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates = {createIgCandidate(nodes[1], linkNode1And4),
                          createIgCandidate(nodes[2], linkNode2And5),
                          createIgCandidate(nodes[3], linkNode3And6)};
  // mark candidate links and node-4, node-5, node-6 to be alive
  for (auto const& igCandidate : expectedIgCandidates) {
    topologyW.setLinkStatus(igCandidate.linkName, true);
    auto nbrName = topologyW.getNbrNodeName(
        igCandidate.initiatorNodeName, igCandidate.linkName);
    topologyW.setNodeStatus(*nbrName, thrift::NodeStatusType::ONLINE_INITIATOR);
  }

  // check candidate links (unignited links of node-2, node-5, node-6)
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates = {createIgCandidate(nodes[2], linkNode2And7),
                          createIgCandidate(nodes[5], linkNode5And8),
                          createIgCandidate(nodes[6], linkNode6And9)};
  // mark candidate links and node-7, node-8, node-9 to be alive
  for (auto const& igCandidate : expectedIgCandidates) {
    topologyW.setLinkStatus(igCandidate.linkName, true);
    auto nbrName = topologyW.getNbrNodeName(
        igCandidate.initiatorNodeName, igCandidate.linkName);
    topologyW.setNodeStatus(*nbrName, thrift::NodeStatusType::ONLINE_INITIATOR);
  }

  // check candidate links (unignited links of node-6)
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates = {createIgCandidate(nodes[6], linkNode6And7)};
  // mark candidate links and node-7 to be alive
  for (auto const& igCandidate : expectedIgCandidates) {
    topologyW.setLinkStatus(igCandidate.linkName, true);
    auto nbrName = topologyW.getNbrNodeName(
        igCandidate.initiatorNodeName, igCandidate.linkName);
    topologyW.setNodeStatus(*nbrName, thrift::NodeStatusType::ONLINE_INITIATOR);
  }

  // all links ignited, nothing to ignite now
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates = {};
  EXPECT_EQ(expectedIgCandidates, igCandidates);
}

// 5 node topology with linkup request returning failure during ignition
//
// node0(pop) ----- node3
//
//
//        |-------- node2
//        |
//        |
// node1(pop) ----- node4
TEST_F(IgnitionAppUtilTest, 5nodeIgnitionWithLinkupFailure) {

  SCOPE_EXIT {
    LOG(INFO)
        << "IgnitionApp 5node ignition with linkup failure util test is done";
  };

  std::vector<thrift::Node> nodes;
  std::vector<thrift::Site> sites;

  for (int i = 0; i < 2; i++) {
    string siteName = folly::sformat("pole-{}", i);
    sites.push_back(createSite(
        siteName,
        (i + 1) * 0.001,
        (i + 1) * 0.001,
        (i + 1) * 0.001,
        (i + 1) * 0.001));
  }

  // setup network topology file
  std::string errorMsg;
  for (int i = 0; i < 5; i++) {
    string nodeName = folly::sformat("node-{}", i);
    string nodeMac = folly::sformat("{}:{}:{}:{}:{}:{}", i, i, i, i, i, i);
    auto node = createNode(
        nodeName,
        nodeMac,
        sites[(i / 4) % sites.size()].name,
        i <= 1 ? true : false);
    nodes.push_back(node);
    configHelper_.setNodePolarity(
        node.name,
        node.mac_addr,
        (i % 2) ? thrift::PolarityType::ODD : thrift::PolarityType::EVEN,
        false,
        errorMsg);
  }

  auto linkNode0And3 = createLink(nodes[0], nodes[3]);
  auto linkNode1And2 = createLink(nodes[1], nodes[2]);
  auto linkNode1And4 = createLink(nodes[1], nodes[4]);
  configHelper_.setLinkControlSuperframe(linkNode1And2, 0, false, errorMsg);
  configHelper_.setLinkControlSuperframe(linkNode1And4, 1, false, errorMsg);

  std::vector<thrift::Link> links = {
      linkNode0And3, linkNode1And2, linkNode1And4};

  auto topology = createTopology(nodes, links, sites);
  TopologyWrapper topologyW(topology);
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      linkToAttemptTs;

  for (auto const& popNodeName : topologyW.getPopNodeNames()) {
    topologyW.setNodeStatus(
        popNodeName, thrift::NodeStatusType::ONLINE_INITIATOR);
  }

  // first try to ignite node-0-node-3
  std::vector<thrift::IgnitionCandidate> igCandidates;
  igCandidates = ignite(topologyW, linkToAttemptTs);
  std::vector<thrift::IgnitionCandidate> expectedIgCandidates = {
      createIgCandidate(nodes[0], linkNode0And3),
      createIgCandidate(nodes[1], linkNode1And2)};
  EXPECT_EQ(expectedIgCandidates, igCandidates);
  // ignition requests for node-0->node-3, node1->node2 return linkup failure
  // next ignition app should skip the failure link
  // try to ignite node-1-node-4
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates = {createIgCandidate(nodes[1], linkNode1And4)};
  EXPECT_EQ(expectedIgCandidates, igCandidates);

  // linkup attempt timeout, trying to ignite node0->node3, node1->node2 again
  linkToAttemptTs.clear();
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates = {
      createIgCandidate(nodes[0], linkNode0And3),
      createIgCandidate(nodes[1], linkNode1And2)};
  EXPECT_EQ(expectedIgCandidates, igCandidates);
}

// 6 node topology with node down after ignition
//
// node1(pop) ----- node2 ----- node5
//                    |           |
//                    |           |
// node0(pop) ----- node3 ----- node4
TEST_F(IgnitionAppUtilTest, 6nodeIgnitionWithNodeFailure) {

  SCOPE_EXIT {
    LOG(INFO)
        << "IgnitionApp 6node ignition with node failure util test is done";
  };

  std::vector<thrift::Node> nodes;
  std::vector<thrift::Site> sites;

  for (int i = 0; i < 2; i++) {
    string siteName = folly::sformat("pole-{}", i);
    sites.push_back(createSite(
        siteName,
        (i + 1) * 0.001,
        (i + 1) * 0.001,
        (i + 1) * 0.001,
        (i + 1) * 0.001));
  }

  // setup network topology file
  std::string errorMsg;
  for (int i = 0; i < 6; i++) {
    string nodeName = folly::sformat("node-{}", i);
    string nodeMac = folly::sformat("{}:{}:{}:{}:{}:{}", i, i, i, i, i, i);
    auto node = createNode(
        nodeName,
        nodeMac,
        sites[(i / 4) % sites.size()].name,
        (i <= 1) ? true : false);
    nodes.push_back(node);
    configHelper_.setNodePolarity(
        node.name,
        node.mac_addr,
        (i % 2) ? thrift::PolarityType::ODD : thrift::PolarityType::EVEN,
        false,
        errorMsg);
  }
  auto linkNode0And3 = createLink(nodes[0], nodes[3]);
  auto linkNode1And2 = createLink(nodes[1], nodes[2]);
  auto linkNode2And3 = createLink(nodes[2], nodes[3]);
  auto linkNode2And5 = createLink(nodes[2], nodes[5]);
  auto linkNode3And4 = createLink(nodes[3], nodes[4]);
  auto linkNode4And5 = createLink(nodes[4], nodes[5]);
  std::vector<thrift::Link> links = {linkNode0And3,
                                     linkNode1And2,
                                     linkNode2And3,
                                     linkNode2And5,
                                     linkNode3And4,
                                     linkNode4And5};

  auto topology = createTopology(nodes, links, sites);
  TopologyWrapper topologyW(topology, "", false);
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      linkToAttemptTs;

  // mark pop nodes (node-0 node-1) alive
  for (auto const& popNodeName : topologyW.getPopNodeNames()) {
    topologyW.setNodeStatus(
        popNodeName, thrift::NodeStatusType::ONLINE_INITIATOR);
  }

  // check candidate links (unignited links of node-0, node-1)
  std::vector<thrift::IgnitionCandidate> igCandidates;
  igCandidates = ignite(topologyW, linkToAttemptTs);
  std::vector<thrift::IgnitionCandidate> expectedIgCandidates = {
      createIgCandidate(nodes[0], linkNode0And3),
      createIgCandidate(nodes[1], linkNode1And2)};
  EXPECT_EQ(expectedIgCandidates, igCandidates);
  // mark candidate links and node-2, node-3 to be alive
  for (auto const& igCandidate : expectedIgCandidates) {
    topologyW.setLinkStatus(igCandidate.linkName, true);
    auto nbrName = topologyW.getNbrNodeName(
        igCandidate.initiatorNodeName, igCandidate.linkName);
    topologyW.setNodeStatus(*nbrName, thrift::NodeStatusType::ONLINE_INITIATOR);
  }

  // check candidate links (unignited links of node-2, node-3)
  igCandidates = ignite(topologyW, linkToAttemptTs);
  std::vector<thrift::IgnitionCandidate> expectedIgCandidates1 =
      {createIgCandidate(nodes[2], linkNode2And3)};
  std::vector<thrift::IgnitionCandidate> expectedIgCandidates2 =
      {createIgCandidate(nodes[3], linkNode2And3)};
  std::vector<thrift::IgnitionCandidate> expectedIgCandidates3 =
      {createIgCandidate(nodes[2], linkNode2And5),
       createIgCandidate(nodes[3], linkNode3And4)};
  EXPECT_TRUE(expectedIgCandidates1 == igCandidates ||
              expectedIgCandidates2 == igCandidates ||
              expectedIgCandidates3 == igCandidates);
  // we are going to bring node-2 down here,

  // bring node-2 down
  topologyW.setNodeStatus(nodes[2].name, thrift::NodeStatusType::OFFLINE);
  // bring ignited links to it down (node-1->node-2 and node-2->node-5)
  topologyW.setLinkStatus(linkNode1And2.name, false);
  // since node-3->node-2 is not up in the first place,
  // no need to set it to be false

  // check link node-1->node-2 is in the candidate links
  // node-1 -> node-2, node-2 -> node-3 is still within link attempt timeout
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates1 = {createIgCandidate(nodes[3], linkNode3And4)};
  expectedIgCandidates2 = {createIgCandidate(nodes[3], linkNode2And3)};
  EXPECT_TRUE(expectedIgCandidates1 == igCandidates ||
              expectedIgCandidates2 == igCandidates);

  // ignite link node-4->node-3
  topologyW.setLinkStatus(linkNode3And4.name, true);
  // ignite link node-1->node-2
  topologyW.setLinkStatus(linkNode1And2.name, true);
  topologyW.setNodeStatus(
      nodes[2].name, thrift::NodeStatusType::ONLINE_INITIATOR);

  // now check candidate links (unignited links of node-2, node-3)
  // node-3 -> node-2 is still within link attemp timeout
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates1 = {createIgCandidate(nodes[2], linkNode2And5)};
  expectedIgCandidates2 = {};
  EXPECT_TRUE(expectedIgCandidates1 == igCandidates ||
              expectedIgCandidates2 == igCandidates);
}

// 3 node topology with one DN GPS Failure -> stays in ONLINE state
//
// node0(pop) ------------ node1 ----------- node2
//  ONLINE           ONLINE_INITIATOR       OFFLINE
//
TEST_F(IgnitionAppUtilTest, 3nodeIgnitionWithGPSFailure) {

  SCOPE_EXIT { LOG(INFO) << "IgnitionApp 3node ignition with GPS Failure"; };

  // setup network topology file
  std::vector<thrift::Site> sites;
  sites.push_back(createSite("pole-0", 0.001, 0.001, 0.001, 0.001));

  std::vector<thrift::Node> nodes;
  std::string errorMsg;
  auto node = createNode(
      "node-0",
      "0:0:0:0:0:0",
      sites[0].name,
      true, // isPop
      thrift::NodeStatusType::ONLINE, // status
      thrift::NodeType::DN);
  nodes.push_back(node);
  configHelper_.setNodePolarity(
      node.name, node.mac_addr, thrift::PolarityType::EVEN, false, errorMsg);
  node = createNode(
      "node-1",
      "1:1:1:1:1:1",
      sites[0].name,
      false, // isPop
      thrift::NodeStatusType::ONLINE, // status
      thrift::NodeType::DN);
  nodes.push_back(node);
  configHelper_.setNodePolarity(
      node.name, node.mac_addr, thrift::PolarityType::ODD, false, errorMsg);
  node = createNode(
      "node-2",
      "2::2:2:2:2:2",
      sites[0].name,
      false, // isPop
      thrift::NodeStatusType::OFFLINE, // status
      thrift::NodeType::DN);
  nodes.push_back(node);
  configHelper_.setNodePolarity(
      node.name, node.mac_addr, thrift::PolarityType::EVEN, false, errorMsg);

  auto linkNode0And1 = createLink(nodes[0], nodes[1]);
  auto linkNode1And2 = createLink(nodes[1], nodes[2]);
  configHelper_.setLinkControlSuperframe(linkNode0And1, 0, false, errorMsg);
  configHelper_.setLinkControlSuperframe(linkNode1And2, 1, false, errorMsg);
  std::vector<thrift::Link> links = {linkNode0And1, linkNode1And2};

  auto topology = createTopology(nodes, links, sites);
  TopologyWrapper topologyW(topology);
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      linkToAttemptTs;
  topologyW.setLinkStatus(linkNode0And1.name, true);

  // check candidate links (should be empty)
  std::vector<thrift::IgnitionCandidate> igCandidates;
  std::vector<thrift::IgnitionCandidate> expectedIgCandidates;
  igCandidates = ignite(topologyW, linkToAttemptTs);
  EXPECT_EQ(expectedIgCandidates, igCandidates);

  // set node1 to ONLINE_INITIATOR state
  topologyW.setNodeStatus(
      nodes[1].name, thrift::NodeStatusType::ONLINE_INITIATOR);

  // now candidate links should be node1->node2
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates = {createIgCandidate(nodes[1], linkNode1And2)};
  EXPECT_EQ(expectedIgCandidates, igCandidates);
}


// p2mp topology
// node0(DN) -----------node1(CN)
//   |
//   |------------------node2(CN)
//   |
//   |------------------node3(CN)
TEST_F(IgnitionAppUtilTest, P2MP) {
  SCOPE_EXIT { LOG(INFO) << "IgnitionApp p2mp"; };

  std::vector<thrift::Node> nodes;
  std::vector<thrift::Site> sites;

  string siteName = "pole-0";
  sites.push_back(createSite(siteName, 1.001, 1.001, 1.001, 1.001));

  // setup network topology file
  std::string errorMsg;
  for (int i = 0; i < 4; i++) {
    string nodeName = folly::sformat("node-{}", i);
    string nodeMac = folly::sformat("{}:{}:{}:{}:{}:{}", i, i, i, i, i, i);
    auto node = createNode(
        nodeName,
        nodeMac,
        sites[0].name,
        (i == 0) ? true : false, // isPop
        thrift::NodeStatusType::OFFLINE, // status
        (i > 0) ? thrift::NodeType::CN : thrift::NodeType::DN);
    nodes.push_back(node);
    configHelper_.setNodePolarity(
        node.name,
        node.mac_addr,
        (i > 0) ? thrift::PolarityType::ODD : thrift::PolarityType::EVEN,
        false,
        errorMsg);
  }

  auto linkNode0And1 = createLink(nodes[0], nodes[1]);
  auto linkNode0And2 = createLink(nodes[0], nodes[2]);
  auto linkNode0And3 = createLink(nodes[0], nodes[3]);
  configHelper_.setLinkControlSuperframe(linkNode0And1, 255, false, errorMsg);
  configHelper_.setLinkControlSuperframe(linkNode0And2, 255, false, errorMsg);
  configHelper_.setLinkControlSuperframe(linkNode0And3, 255, false, errorMsg);

  std::vector<thrift::Link> links = {linkNode0And1,
                                     linkNode0And2,
                                     linkNode0And3};

  auto topology = createTopology(nodes, links, sites);
  TopologyWrapper topologyW(topology);
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      linkToAttemptTs;

  // mark pop nodes (node-0) to be alive
  for (auto const& popNodeName : topologyW.getPopNodeNames()) {
    topologyW.setNodeStatus(
        popNodeName, thrift::NodeStatusType::ONLINE_INITIATOR);
  }

  // first try to ignite node-0-node-1
  std::vector<thrift::IgnitionCandidate> igCandidates;
  igCandidates = ignite(topologyW, linkToAttemptTs);
  std::vector<thrift::IgnitionCandidate> expectedIgCandidates = {
      createIgCandidate(nodes[0], linkNode0And1)};
  EXPECT_EQ(expectedIgCandidates, igCandidates);
  // ignition request for node-0->node-1 returns linkup failure
  // next ignition app should skip the failure link
  // try to ignite node-0-node-2
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates = {createIgCandidate(nodes[0], linkNode0And2)};
  EXPECT_EQ(expectedIgCandidates, igCandidates);
  // ignition request for node-0->node-2 returns linkup failure
  // next ignition app should skip the failure link
  // try to ignite node-0-node-3
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates = {createIgCandidate(nodes[0], linkNode0And3)};
  EXPECT_EQ(expectedIgCandidates, igCandidates);

  // linkup attempt timeout, trying to ignite node0->node1 again
  linkToAttemptTs.clear();
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates = {createIgCandidate(nodes[0], linkNode0And1)};
  EXPECT_EQ(expectedIgCandidates, igCandidates);
  // mark candidate link to be alive
  for (auto const& igCandidate : expectedIgCandidates) {
    topologyW.setLinkStatus(igCandidate.linkName, true);
  }
  // linkup attempt timeout, trying to ignite node0->node2 again
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates = {createIgCandidate(nodes[0], linkNode0And2)};
  EXPECT_EQ(expectedIgCandidates, igCandidates);
  // mark candidate link to be alive
  for (auto const& igCandidate : expectedIgCandidates) {
    topologyW.setLinkStatus(igCandidate.linkName, true);
  }
  // linkup attempt timeout, trying to ignite node0->node3 again
  igCandidates = ignite(topologyW, linkToAttemptTs);
  expectedIgCandidates = {createIgCandidate(nodes[0], linkNode0And3)};
  EXPECT_EQ(expectedIgCandidates, igCandidates);
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
