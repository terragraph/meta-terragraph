/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "../ControlSuperframeHelper.h"

#include "../../ConfigHelper.h"
#include "../../topology/TopologyWrapper.h"

#include <folly/String.h>
#include <folly/init/Init.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <e2e/common/TestUtils.h>

using namespace std;
using namespace facebook::terragraph;

namespace { // anonymous namespace

class ControlSuperframeFixture : public ::testing::Test {
 public:
  bool
  controlSuperframesMatch(
      TopologyWrapper& topologyW,
      ConfigHelper& configHelper,
      std::vector<int> csfValues) {
    int i = 0;
    for (const auto& link : topologyW.getAllLinks()) {
      auto aSframe = configHelper.getLinkControlSuperframe(
          link.a_node_name, link.z_node_mac, false);
      auto zSframe = configHelper.getLinkControlSuperframe(
          link.z_node_name, link.a_node_mac, false);

      if (!aSframe || !zSframe) {
        LOG(INFO) << folly::sformat(
            "Unknown control superframe for `{}` on `{}` using responder MAC ",
            link.name,
            !aSframe ? link.a_node_name : link.z_node_name,
            !aSframe ? link.z_node_mac : link.a_node_mac);
        return false;
      }

      if (aSframe.value() != zSframe.value()) {
        LOG(INFO) << folly::sformat(
            "Control superframe mismatch on both ends of `{}`. Values {}/{}",
            link.name,
            aSframe.value(),
            zSframe.value());
        return false;
      }

      if (aSframe.value() != csfValues[i]) {
        LOG(INFO) << folly::sformat(
            "Unexpected control superframe for `{}`. Expected {} Actual {}",
            link.name,
            aSframe.value(),
            csfValues[i]);
        return false;
      }

      ++i;
    }

    return true;
  }

  void
  initConfigHelper(ConfigHelper& configHelper) {
    configHelper.setConfigFiles(
        "/etc/e2e_config/base_versions/",       // base_config_dir
        "/etc/e2e_config/base_versions/fw_versions/",  // fw_base_config_dir
        "/etc/e2e_config/base_versions/hw_versions/",  // hw_base_config_dir
        // hw_config_types_file
        "/etc/e2e_config/base_versions/hw_versions/hw_types.json",
        "/tmp/node_config_overrides.json",      // node_config_overrides_file
        // auto_node_config_overrides_file
        "/tmp/auto_node_config_overrides.json",
        "/tmp/network_config_overrides.json",   // network_config_overrides_file
        "/etc/e2e_config/config_metadata.json", // node_config_metadata_file
        "/tmp/cfg_backup/",                     // config_backup_dir
        {});
  }

  void
  SetUp() override {
    // Y-Street topology with CNs
    yStreetTopology_ = createTopology(
        5,
        {0},
        {{0, 1}, {0, 2}, {0, 3}, {1, 4}},
        5,
        {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}},
        {3, 4});

    // Z-Street topology
    zStreetTopology_ = createTopology(
        4,
        {0},
        {{0, 1}, {1, 2}, {2, 3}},
        4,
        {{0, 0}, {1, 1}, {2, 2}, {3, 3}});

    // W-Shape topology
    wShapeTopology_ = createTopology(
        5,
        {0},
        {{0, 1}, {3, 4}, {1, 2}, {2, 3}},
        4,
        {{0, 0}, {1, 1}, {2, 2}, {3, 3}});

    // Unsolvable triangle p2mp topology
    p2mpTriangleTopology_ = createTopology(
        3,
        {0},
        {{0, 1}, {0, 2}, {1, 2}},
        3,
        {{0, 0}, {1, 1}, {2, 2}});
  }

  thrift::Topology yStreetTopology_;
  thrift::Topology zStreetTopology_;
  thrift::Topology wShapeTopology_;
  thrift::Topology p2mpTriangleTopology_;
};

} // anonymous namespace

TEST_F(ControlSuperframeFixture, yStreetTopologyTest) {
  TopologyWrapper topologyW(yStreetTopology_, "", false, false);
  ConfigHelper configHelper;
  initConfigHelper(configHelper);

  EXPECT_NO_THROW(ControlSuperframeHelper::assignAllControlSuperframes(
      topologyW, configHelper, true));
  // Order of expected values follows order of adding links to topology
  EXPECT_TRUE(
      controlSuperframesMatch(topologyW, configHelper, {0, 1, 255, 255}) ||
      controlSuperframesMatch(topologyW, configHelper, {1, 0, 255, 255}));
}

TEST_F(ControlSuperframeFixture, zStreetTopologyTest) {
  TopologyWrapper topologyW(zStreetTopology_, "", false, false);
  ConfigHelper configHelper;
  initConfigHelper(configHelper);

  EXPECT_NO_THROW(ControlSuperframeHelper::assignAllControlSuperframes(
      topologyW, configHelper, true));
  // Order of expected values follws order of adding links to topology
  EXPECT_TRUE(
      controlSuperframesMatch(topologyW, configHelper, {0, 1, 0}) ||
      controlSuperframesMatch(topologyW, configHelper, {1, 0, 1}));
}

TEST_F(ControlSuperframeFixture, wShapeTopologyTest) {
  TopologyWrapper topologyW(wShapeTopology_, "", false, false);
  ConfigHelper configHelper;
  initConfigHelper(configHelper);

  EXPECT_NO_THROW(ControlSuperframeHelper::assignAllControlSuperframes(
      topologyW, configHelper, true));
  // Order of expected values follws order of adding links to topology
  EXPECT_TRUE(
      controlSuperframesMatch(topologyW, configHelper, {0, 1, 1, 0}) ||
      controlSuperframesMatch(topologyW, configHelper, {1, 0, 0, 1}));
}

TEST_F(ControlSuperframeFixture, p2mpTriangleTopologyTest) {
  TopologyWrapper topologyW(p2mpTriangleTopology_, "", false, false);
  ConfigHelper configHelper;
  initConfigHelper(configHelper);

  EXPECT_THROW(ControlSuperframeHelper::assignAllControlSuperframes(
      topologyW, configHelper, true), std::invalid_argument);
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
