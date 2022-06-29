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

#include "../ConfigUtil.h"

using facebook::terragraph::SwVersion;

TEST(ConfigUtilTest, SwVersion) {
  // Major release (M21)
  SwVersion M21 = SwVersion(
      "Facebook Terragraph Release RELEASE_M21 "
      "(user@dev12345 Tue Jun 5 16:01:52 PDT 2018");
  EXPECT_EQ("RELEASE_M21", M21.version);
  EXPECT_EQ(21, M21.majorVersion);
  EXPECT_EQ(0, M21.minorVersion);

  // Major/minor release (M20.1)
  SwVersion M20_1 = SwVersion(
      "Facebook Terragraph Release RELEASE_M20_1 "
      "(user@dev12345 Tue Apr 24 09:38:31 PDT 2018)");
  EXPECT_EQ("RELEASE_M20_1", M20_1.version);
  EXPECT_EQ(20, M20_1.majorVersion);
  EXPECT_EQ(1, M20_1.minorVersion);

  // Major/minor release (M20.4)
  SwVersion M20_4 = SwVersion(
      "Facebook Terragraph Release RELEASE_M20_4-user "
      "(user@dev12345 Wed May 23 21:21:04 PDT 2018)");
  EXPECT_EQ("RELEASE_M20_4_user", M20_4.version);
  EXPECT_EQ(20, M20_4.majorVersion);
  EXPECT_EQ(4, M20_4.minorVersion);

  // Pre-release (M22-PRE)
  SwVersion M22_PRE = SwVersion(
      "Facebook Terragraph Release RELEASE_M22_PRE1-83-g5be6d6b-user "
      "(user@dev12345 Thu Jun  7 23:10:59 UTC 2018)");
  EXPECT_EQ("RELEASE_M22_PRE1_83_g5be6d6b_user", M22_PRE.version);
  EXPECT_EQ(22, M22_PRE.majorVersion);
  EXPECT_EQ(0, M22_PRE.minorVersion);

  // Unparsed
  SwVersion unparsedSwVer = SwVersion(" asdf  ");
  EXPECT_EQ("asdf", unparsedSwVer.version);
  EXPECT_EQ(0, unparsedSwVer.majorVersion);
  EXPECT_EQ(0, unparsedSwVer.minorVersion);

  // Old Release (before config management)
  SwVersion M5 = SwVersion(
      "Facebook Terragraph Release RELEASE_M5 "
      "(user@dev12345 Thu Jun  7 23:10:59 UTC 2018)");
  EXPECT_EQ("RELEASE_M5", M5.version);
  EXPECT_EQ(5, M5.majorVersion);
  EXPECT_EQ(0, M5.minorVersion);

  // Comparators (major/minor versions)
  EXPECT_TRUE(unparsedSwVer < M5);
  EXPECT_TRUE(M5 < M20_1);
  EXPECT_TRUE(M20_1 < M20_4);
  EXPECT_TRUE(M20_4 < M22_PRE);
  EXPECT_TRUE(M22_PRE == M22_PRE);

  // Comparators (strings)
  EXPECT_TRUE(SwVersion() == SwVersion(""));
  EXPECT_TRUE(SwVersion() < SwVersion("a"));
  EXPECT_TRUE(SwVersion("cat") < SwVersion("fish"));
  EXPECT_TRUE(SwVersion("blah") == SwVersion("blah    "));
  EXPECT_TRUE(SwVersion("asdf12345") > SwVersion("asdf123"));
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
