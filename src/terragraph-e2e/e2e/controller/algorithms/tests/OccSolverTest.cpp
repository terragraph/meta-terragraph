/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "../OccSolver.h"

#include "../../topology/TopologyWrapper.h"

#include <folly/IPAddress.h>
#include <folly/String.h>
#include <folly/init/Init.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <e2e/common/TestUtils.h>

using namespace std;
using namespace facebook::terragraph;

namespace { // anonymous namespace

class OCCFixture : public ::testing::Test {
 public:
  std::unordered_map<std::string, float>
  getSiteWeightsMap(const size_t numSites, std::vector<float> weights) {
    std::unordered_map<std::string, float> siteWeights;
    for (size_t i = 0; i < numSites; ++i) {
      std::string siteName = folly::sformat("pole-{}", i);
      siteWeights[siteName] = weights[i];
    }
    return siteWeights;
  }

  void
  SetUp() override {
    wheelTopology = createTopology(
        5,
        {0},
        {{0, 1}, {0, 2}, {0, 3}, {0, 4}, {1, 2}, {2, 3}, {3, 4}, {4, 1}},
        5,
        {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}});

    gridTopology = createTopology(
        9,
        {0},
        {{0, 1},
         {0, 3},
         {1, 2},
         {1, 4},
         {2, 5},
         {3, 4},
         {3, 6},
         {4, 5},
         {4, 7},
         {5, 8},
         {6, 7},
         {7, 8}},
        9,
        {{0, 0},
         {1, 1},
         {2, 2},
         {3, 3},
         {4, 4},
         {5, 5},
         {6, 6},
         {7, 7},
         {8, 8}});

    triangleTopology = createTopology(
        3, {0}, {{0, 1}, {1, 2}, {2, 0}}, 3, {{0, 0}, {1, 1}, {2, 2}});

    simpleTopology =
        createTopology(2, {0}, {{0, 1}}, 2, {{0, 0}, {1, 1}});

    trivialTopology = createTopology(1, {0}, {}, 1, {{0, 0}});
  }

  thrift::Topology wheelTopology;
  thrift::Topology gridTopology;
  thrift::Topology triangleTopology;
  thrift::Topology simpleTopology;
  thrift::Topology trivialTopology;
};

} // anonymous namespace

TEST_F(OCCFixture, trivialTopologyTest) {
  auto siteWeights = getSiteWeightsMap(1, {1});
  TopologyWrapper topologyW(
      trivialTopology, "", false, false);

  OccSolver occSolver(topologyW, siteWeights);
  auto solution = occSolver.getOccSolution();

  EXPECT_EQ(0, solution.size());
}

TEST_F(OCCFixture, simpleTopologyTest) {
  auto siteWeights = getSiteWeightsMap(2, {1, 1});
  TopologyWrapper topologyW(
      simpleTopology, "", false, false);

  OccSolver occSolver(topologyW, siteWeights);
  auto solution = occSolver.getOccSolution();

  EXPECT_EQ(0, solution.size());
}

TEST_F(OCCFixture, gridTopologyTest) {
  auto siteWeights = getSiteWeightsMap(9, {1, 1, 1, 1, 1, 1, 1, 1, 1});
  TopologyWrapper topologyW(gridTopology, "", false, false);

  OccSolver occSolver(topologyW, siteWeights);
  auto solution = occSolver.getOccSolution();

  EXPECT_EQ(0, solution.size());
}

TEST_F(OCCFixture, uniformTriangleTopologyTest) {
  auto siteWeights = getSiteWeightsMap(3, {1, 1, 1});
  TopologyWrapper topologyW(
      triangleTopology, "", false, false);

  OccSolver occSolver(topologyW, siteWeights);
  auto solution = occSolver.getOccSolution();

  EXPECT_EQ(1, solution.size());
}

TEST_F(OCCFixture, nonUniformTriangleTopologyTest) {
  auto siteWeights = getSiteWeightsMap(3, {0.5, 1, 1});
  TopologyWrapper topologyW(
      triangleTopology, "", false, false);

  OccSolver occSolver(topologyW, siteWeights);
  auto solution = occSolver.getOccSolution();

  EXPECT_EQ(std::unordered_set<std::string>({"pole-0"}), solution);
}

TEST_F(OCCFixture, uniformWheelTopologyTest) {
  auto siteWeights = getSiteWeightsMap(5, {1, 1, 1, 1, 1});
  TopologyWrapper topologyW(wheelTopology, "", false, false);

  OccSolver occSolver(topologyW, siteWeights);
  auto solution = occSolver.getOccSolution();

  std::unordered_set<std::string> expectedSolution = {"pole-0"};
  EXPECT_EQ(expectedSolution, solution);
}

TEST_F(OCCFixture, nonUniformWheelTopologyTest) {
  OccSolver occSolver;
  TopologyWrapper topologyW(wheelTopology, "", false, false);

  auto siteWeights = getSiteWeightsMap(5, {1.9, 1, 1, 1, 1});
  occSolver = OccSolver(topologyW, siteWeights);
  auto solution = occSolver.getOccSolution();

  EXPECT_EQ(std::unordered_set<std::string>({"pole-0"}), solution);

  siteWeights = getSiteWeightsMap(5, {2.1, 1, 1, 1, 1});
  occSolver = OccSolver(topologyW, siteWeights);
  solution = occSolver.getOccSolution();

  EXPECT_TRUE(
      std::unordered_set<std::string>({"pole-1", "pole-3"}) == solution ||
      std::unordered_set<std::string>({"pole-2", "pole-4"}) == solution);
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
