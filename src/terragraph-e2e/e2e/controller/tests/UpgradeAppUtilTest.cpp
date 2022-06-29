/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/String.h>
#include <folly/init/Init.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "../UpgradeAppUtil.h"
#include "../ConfigHelper.h"
#include "e2e/common/ConfigUtil.h"
#include <e2e/common/TestUtils.h>

using namespace facebook::terragraph;

class UpgradeAppUtilTest : public ::testing::Test {
public:
  void SetUp() override {}
  void TearDown() override {}
};

// Validate candidate nodes for golden image upgrades.
TEST_F(UpgradeAppUtilTest, GoldenImageCandidateNodes) {
  // Constants
  const SwVersion M20_1 = SwVersion(
      "Facebook Terragraph Release RELEASE_M20_1 "
      "(user@dev12345 Tue Apr 24 09:38:31 PDT 2018)");
  const SwVersion M20_4 = SwVersion(
      "Facebook Terragraph Release RELEASE_M20_4-user "
      "(user@dev12345 Wed May 23 21:21:04 PDT 2018)");
  const SwVersion M21 = SwVersion(
      "Facebook Terragraph Release RELEASE_M21 "
      "(user@dev12345 Tue Jun 5 16:01:52 PDT 2018");
  const std::string hwBoardIdP = "MVL_ARMADA39X_P";
  const std::string hwBoardIdS = "MVL_ARMADA39X_S";

  // Create topology
  thrift::Node node1 = createNode(
      "node1",
      "00:00:00:00:00:01",
      "site",
      true,
      thrift::NodeStatusType::ONLINE);
  thrift::Node node2 = createNode(
      "node2",
      "00:00:00:00:00:02",
      "site",
      false,
      thrift::NodeStatusType::ONLINE_INITIATOR);
  thrift::Node node3 = createNode(
      "node3",
      "00:00:00:00:00:03",
      "site",
      false,
      thrift::NodeStatusType::ONLINE);
  thrift::Node node4 = createNode(
      "node3",
      "00:00:00:00:00:04",
      "site",
      false,
      thrift::NodeStatusType::OFFLINE);
  thrift::Site site = createSite("site", 1, 1, 1, 0);
  thrift::Topology topology = createTopology({node1, node2, node3}, {}, {site});
  TopologyWrapper topologyW;
  topologyW.setTopology(topology);

  // Start tests...
  std::unordered_map<std::string, thrift::StatusReport> minionToStatus;
  std::unordered_map<std::string, std::string> nodeBlacklist;
  std::vector<std::string> results;

  // No status reports - should return nothing
  results = UpgradeAppUtil::findNodesToUpgradeToVersion(
      M21.fullVersion, hwBoardIdP, nodeBlacklist, topologyW, minionToStatus);
  EXPECT_TRUE(results.empty());

  // Add some status reports...
  minionToStatus[node1.mac_addr].version = M20_1.version;
  minionToStatus[node2.mac_addr].version = M20_1.version;
  minionToStatus[node3.mac_addr].version = M20_1.version;
  minionToStatus[node4.mac_addr].version = M20_1.version;
  minionToStatus[node1.mac_addr].status = thrift::NodeStatusType::ONLINE;
  minionToStatus[node2.mac_addr].status =
      thrift::NodeStatusType::ONLINE_INITIATOR;
  minionToStatus[node3.mac_addr].status =
      thrift::NodeStatusType::ONLINE_INITIATOR;
  minionToStatus[node4.mac_addr].status = thrift::NodeStatusType::ONLINE;
  minionToStatus[node1.mac_addr].hardwareBoardId = hwBoardIdP;
  minionToStatus[node2.mac_addr].hardwareBoardId = hwBoardIdS;
  minionToStatus[node3.mac_addr].hardwareBoardId = hwBoardIdS;
  minionToStatus[node4.mac_addr].hardwareBoardId = hwBoardIdS;

  // Normal cases
  results = UpgradeAppUtil::findNodesToUpgradeToVersion(
      M20_1.fullVersion, hwBoardIdP, nodeBlacklist, topologyW, minionToStatus);
  EXPECT_TRUE(results.empty());
  results = UpgradeAppUtil::findNodesToUpgradeToVersion(
      M20_1.fullVersion, hwBoardIdS, nodeBlacklist, topologyW, minionToStatus);
  EXPECT_TRUE(results.empty());
  results = UpgradeAppUtil::findNodesToUpgradeToVersion(
      M20_4.fullVersion, hwBoardIdP, nodeBlacklist, topologyW, minionToStatus);
  EXPECT_EQ(results.size(), 1);
  EXPECT_EQ(results[0], node1.name);
  results = UpgradeAppUtil::findNodesToUpgradeToVersion(
      M20_4.fullVersion, hwBoardIdS, nodeBlacklist, topologyW, minionToStatus);
  EXPECT_EQ(results.size(), 2);
  EXPECT_TRUE(
      std::find(results.begin(), results.end(), node2.name) != results.end());
  EXPECT_TRUE(
      std::find(results.begin(), results.end(), node3.name) != results.end());
  results = UpgradeAppUtil::findNodesToUpgradeToVersion(
      M21.fullVersion, hwBoardIdP, nodeBlacklist, topologyW, minionToStatus);
  EXPECT_EQ(results.size(), 1);
  EXPECT_EQ(results[0], node1.name);
  results = UpgradeAppUtil::findNodesToUpgradeToVersion(
      M21.fullVersion, hwBoardIdS, nodeBlacklist, topologyW, minionToStatus);
  EXPECT_EQ(results.size(), 2);
  EXPECT_TRUE(
      std::find(results.begin(), results.end(), node2.name) != results.end());
  EXPECT_TRUE(
      std::find(results.begin(), results.end(), node3.name) != results.end());

  // Use node blacklist
  nodeBlacklist[node2.name] = M20_4.fullVersion;
  nodeBlacklist[node3.name] = M21.fullVersion;
  results = UpgradeAppUtil::findNodesToUpgradeToVersion(
      M21.fullVersion, hwBoardIdS, nodeBlacklist, topologyW, minionToStatus);
  EXPECT_EQ(results.size(), 1);
  EXPECT_EQ(results[0], node2.name);
}

// Validate time restriction checks for golden image upgrades.
TEST_F(UpgradeAppUtilTest, GoldenImageTimeRestriction) {
  // Thursday, September 5, 2019 10:50:43 PM (UTC)
  time_t t = 1567723843L;

  // Rule: invalid
  EXPECT_THROW(
      UpgradeAppUtil::checkTimeRestriction(t, "potato"),
      std::invalid_argument);
  EXPECT_THROW(
      UpgradeAppUtil::checkTimeRestriction(t, "tomato:0100-2000:utc"),
      std::invalid_argument);
  EXPECT_THROW(
      UpgradeAppUtil::checkTimeRestriction(t, "any:12345-6789:utc"),
      std::invalid_argument);

  // Rule: allow all
  EXPECT_EQ(UpgradeAppUtil::checkTimeRestriction(t, ""), true);
  EXPECT_EQ(UpgradeAppUtil::checkTimeRestriction(t, "any:any:utc"), true);
  EXPECT_EQ(UpgradeAppUtil::checkTimeRestriction(t, "any:any:local"), true);

  // Rule: allowed
  EXPECT_EQ(UpgradeAppUtil::checkTimeRestriction(t, "any:0001-2359:utc"), true);
  EXPECT_EQ(UpgradeAppUtil::checkTimeRestriction(t, "thu,fri:any:utc"), true);
  EXPECT_EQ(UpgradeAppUtil::checkTimeRestriction(t, "thu:2230-2300:utc"), true);

  // RULE: disallowed
  EXPECT_EQ(
      UpgradeAppUtil::checkTimeRestriction(t, "any:0000-0400:utc"), false);
  EXPECT_EQ(UpgradeAppUtil::checkTimeRestriction(t, "mon,fri:any:utc"), false);
  EXPECT_EQ(UpgradeAppUtil::checkTimeRestriction(t, "sun:any:local"), false);
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
