/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "../PolarityHelper.h"

#include "../../topology/TopologyWrapper.h"

#include <folly/String.h>
#include <folly/init/Init.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <e2e/common/MacUtils.h>
#include <e2e/common/TestUtils.h>

using namespace std;
using namespace facebook::terragraph;

namespace { // anonymous namespace

class PolarityFixture : public ::testing::Test {
 public:
  // Create a map from node MACs to the given polarities.
  // The input `polarities` is a list of polarity values per MAC, in order, as
  // integer values of thrift::PolarityType (or 0 to skip assignment).
  std::unordered_map<std::string, thrift::PolarityType>
  getPolarityMap(std::vector<int> polarities) {
    std::unordered_map<std::string, thrift::PolarityType> macPolarityMap;
    for (size_t i = 0; i < polarities.size(); ++i) {
      if (polarities[i] <= 0) {
        continue;
      }
      auto polarity = static_cast<thrift::PolarityType>(polarities[i]);
      std::string nodeMac = MacUtils::standardizeMac(
          folly::sformat("{}:{}:{}:{}:{}:{}", i, i, i, i, i, i));
      macPolarityMap[nodeMac] = polarity;
    }
    return macPolarityMap;
  }

  void
  SetUp() override {
    // Six nodes creating a triangle topology
    /*
          [site 0]
            1 0
           /  \
          /    \
         2      5
        3 ------ 4
    [site 1]   [site 2]
    */
    triangleTopology = createTopology(
        6,
        {0},
        {{1, 2}, {3, 4}, {5, 0}},
        3,
        {{0, 0}, {1, 0}, {2, 1}, {3, 1}, {4, 2}, {5, 2}});

    // Five nodes creating a triangle topology with one p2mp node
    /*
          [site 0]
             0
           /  \
          /    \
         1      4
        2 ------ 3
    [site 1]   [site 2]
    */
    p2mpTriangleTopology = createTopology(
        5,
        {0},
        {{0, 1}, {0, 4}, {2, 3}},
        3,
        {{0, 0}, {1, 1}, {2, 1}, {3, 2}, {4, 2}});

    // Three nodes creating a p2mp odd cycle
    /*
          [site 0]
             0
           /  \
         1 --- 2
    [site 1]   [site 2]
    */
    unsolvableTopology = createTopology(
        3,
        {0},
        {{0, 1}, {1, 2}, {2, 0}},
        3,
        {{0, 0}, {1, 1}, {2, 2}});

    // p2mp solvable odd cycle
    /*
             [site 0]
              0   1(??)
            /  \
           /    \
         2 ----- 3 4 ------ 5
    [site 1]  [site 2]   [site 3]
    */
    solvableP2mpTopology = createTopology(
        6,
        {0},
        {{0, 4}, {0, 2}, {2, 3}, {4, 5}},
        4,
        {{0, 0}, {1, 0}, {2, 1}, {3, 2}, {4, 2}, {5, 3}});
  }

  thrift::Topology triangleTopology;
  thrift::Topology p2mpTriangleTopology;
  thrift::Topology unsolvableTopology;
  thrift::Topology solvableP2mpTopology;
};

} // anonymous namespace

TEST_F(PolarityFixture, triangleTopologyTest) {
  TopologyWrapper topologyW(
      triangleTopology, "", false, false);
  std::unordered_map<std::string, thrift::PolarityType> oldPolarities;
  std::unordered_map<std::string, thrift::PolarityType> userPolarities;
  std::unordered_map<std::string, thrift::PolarityType> newPolarities;
  std::unordered_map<std::string, thrift::PolarityType> expectedPolarities;
  std::unordered_map<std::string, thrift::PolarityType> expectedPolarities2;
  std::vector<std::string> errMsgs;

  // Test with user config to influence hybrid pick
  userPolarities = getPolarityMap({1, 2}); // Site_0 is configured as hybrid
  expectedPolarities = getPolarityMap({1, 2, 1, 1, 2, 2});
  bool success = PolarityHelper::optimizePolarity(
      topologyW, userPolarities, oldPolarities, newPolarities, errMsgs);

  EXPECT_TRUE(success);
  EXPECT_EQ(newPolarities, expectedPolarities);

  // Test with auto config to influence hybrid pick
  userPolarities.clear();
  oldPolarities = getPolarityMap({1, 2}); // Site_0 is currently a hybrid
  expectedPolarities = getPolarityMap({1, 2, 1, 1, 2, 2});
  success = PolarityHelper::optimizePolarity(
      topologyW, userPolarities, oldPolarities, newPolarities, errMsgs);

  EXPECT_TRUE(success);
  EXPECT_EQ(newPolarities, expectedPolarities);

  // Test with bad user config
  userPolarities.clear();
  oldPolarities.clear();
  userPolarities = getPolarityMap({0, 1, 1}); // link_1_2 bad polarity config
  oldPolarities = getPolarityMap({2, 1}); // Site_0 is currently a hybrid
  expectedPolarities = getPolarityMap({2, 1, 2, 2, 1, 1});
  expectedPolarities2 = getPolarityMap({1, 2, 1, 1, 2, 2});
  success = PolarityHelper::optimizePolarity(
      topologyW, userPolarities, oldPolarities, newPolarities, errMsgs);

  EXPECT_TRUE(success);
  EXPECT_TRUE(
      newPolarities == expectedPolarities ||
      newPolarities == expectedPolarities2);
}

TEST_F(PolarityFixture, p2mpTriangleTopologyTest) {
  TopologyWrapper topologyW(
      p2mpTriangleTopology, "", false, false);
  std::unordered_map<std::string, thrift::PolarityType> oldPolarities;
  std::unordered_map<std::string, thrift::PolarityType> userPolarities;
  std::unordered_map<std::string, thrift::PolarityType> newPolarities;
  std::unordered_map<std::string, thrift::PolarityType> expectedPolarities;
  std::unordered_map<std::string, thrift::PolarityType> expectedPolarities2;
  std::unordered_map<std::string, thrift::PolarityType> expectedPolarities3;
  std::vector<std::string> errMsgs;

  // Test with user config to influence polarity pattern
  userPolarities = getPolarityMap({0, 1, 2});
  expectedPolarities = getPolarityMap({2, 1, 2, 1, 1});
  bool success = PolarityHelper::optimizePolarity(
      topologyW, userPolarities, oldPolarities, newPolarities, errMsgs);

  EXPECT_TRUE(success);
  EXPECT_EQ(newPolarities, expectedPolarities);

  // Test with bad user config
  userPolarities = getPolarityMap({2, 1, 2, 2, 2});
  expectedPolarities = getPolarityMap({2, 1, 2, 1, 1});
  expectedPolarities2 = getPolarityMap({1, 2, 1, 2, 2});
  expectedPolarities3 = getPolarityMap({1, 2, 2, 1, 2});
  success = PolarityHelper::optimizePolarity(
      topologyW, userPolarities, oldPolarities, newPolarities, errMsgs);

  EXPECT_TRUE(success);
  EXPECT_TRUE(
      newPolarities == expectedPolarities ||
      newPolarities == expectedPolarities2 ||
      newPolarities == expectedPolarities3);
}

TEST_F(PolarityFixture, unsolvableTopologyTest) {
  TopologyWrapper topologyW(
      unsolvableTopology, "", false, false);
  std::unordered_map<std::string, thrift::PolarityType> oldPolarities;
  std::unordered_map<std::string, thrift::PolarityType> userPolarities;
  std::unordered_map<std::string, thrift::PolarityType> newPolarities;
  std::unordered_map<std::string, thrift::PolarityType> expectedPolarities;
  std::vector<std::string> errMsgs;

  bool success = PolarityHelper::optimizePolarity(
      topologyW, userPolarities, oldPolarities, newPolarities, errMsgs);

  EXPECT_FALSE(success);
}

TEST_F(PolarityFixture, solvableP2mpTopologyTest) {
  TopologyWrapper topologyW(
      solvableP2mpTopology, "", false, false);
  std::unordered_map<std::string, thrift::PolarityType> oldPolarities;
  std::unordered_map<std::string, thrift::PolarityType> userPolarities;
  std::unordered_map<std::string, thrift::PolarityType> newPolarities;
  std::unordered_map<std::string, thrift::PolarityType> expectedPolarities;
  std::vector<std::string> errMsgs;

  userPolarities = getPolarityMap({1, 2}); // Site_0 is currently a hybrid
  expectedPolarities = getPolarityMap({1, 2, 2, 1, 2, 1});
  bool success = PolarityHelper::optimizePolarity(
      topologyW, userPolarities, oldPolarities, newPolarities, errMsgs);

  EXPECT_TRUE(success);
  EXPECT_EQ(newPolarities, expectedPolarities);
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
