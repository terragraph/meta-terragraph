/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/init/Init.h>
#include <folly/portability/GFlags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include "../OpenrUtils.h"

using facebook::terragraph::OpenrUtils;

TEST(OpenrUtilsTest, OpenrNameConversion) {
  // Basic functionality check
  std::string tgMac = "00:00:00:10:0d:40";
  std::string openrName = "node-00.00.00.10.0d.40";
  EXPECT_EQ(OpenrUtils::toOpenrNodeName(tgMac), openrName);
  EXPECT_EQ(OpenrUtils::fromOpenrNodeName(openrName), tgMac);
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
