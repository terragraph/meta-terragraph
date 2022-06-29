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

#include "../IpUtil.h"

using facebook::terragraph::IpUtil;

TEST(IpUtilTest, ZmqUriChecks) {
  EXPECT_FALSE(IpUtil::isValidZmqUri(""));

  // tcp
  EXPECT_TRUE(IpUtil::isValidZmqUri("tcp://*:5555"));
  EXPECT_TRUE(IpUtil::isValidZmqUri("tcp://eth0:5555"));
  EXPECT_TRUE(IpUtil::isValidZmqUri("tcp://eth1:0;server1:5555"));
  EXPECT_TRUE(IpUtil::isValidZmqUri("tcp://127.0.0.1:5555"));
  EXPECT_TRUE(IpUtil::isValidZmqUri(
      "tcp://192.168.1.17:5555;192.168.1.1:5555"));
  EXPECT_TRUE(IpUtil::isValidZmqUri("tcp://eth0:0;127.0.0.1:5555"));
  EXPECT_TRUE(IpUtil::isValidZmqUri("tcp://[2001::1]:5555"));
  EXPECT_TRUE(IpUtil::isValidZmqUri("tcp://lo:0;[2001::1]:5555"));
  EXPECT_TRUE(IpUtil::isValidZmqUri(
      "tcp://[2001:123:abcd:9876:543:aa11:bb22:cc33]:12345"));
  EXPECT_TRUE(IpUtil::isValidZmqUri(
      "tcp://lo:0;[2001:123:abcd:9876:543:aa11:bb22:cc33]:12345"));
  EXPECT_FALSE(IpUtil::isValidZmqUri(
      "tcp://lo;[2001:123:abcd:9876:543:aa11:bb22:cc33]:12345"));
  EXPECT_FALSE(IpUtil::isValidZmqUri("tcp://potato"));

  // ipc
  EXPECT_TRUE(IpUtil::isValidZmqUri("ipc:///tmp/feeds/0"));

  // inproc
  EXPECT_TRUE(IpUtil::isValidZmqUri("inproc://#1"));
  EXPECT_TRUE(IpUtil::isValidZmqUri("inproc://my-endpoint"));
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
