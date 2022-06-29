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

#include <folly/dynamic.h>
#include <folly/json.h>

#include "../JsonUtils.h"

using facebook::terragraph::JsonUtils;
using folly::dynamic;

TEST(JsonUtilsTest, DynamicObjectDifference) {
  // Empty object diff is empty
  dynamic empty = dynamic::object;
  EXPECT_EQ(JsonUtils::dynamicObjectDifference(empty, empty), empty);

  // Diff iterates on the second object
  dynamic obj1 = dynamic::object("a", 1)("b", 2);
  EXPECT_EQ(JsonUtils::dynamicObjectDifference(obj1, empty), empty);
  EXPECT_EQ(JsonUtils::dynamicObjectDifference(empty, obj1), obj1);

  // Only output differences
  dynamic obj2 = dynamic::object("a", 1)("b", 22)("c", 333);
  dynamic expectedDiff12 = dynamic::object("b", 22)("c", 333);
  EXPECT_EQ(JsonUtils::dynamicObjectDifference(obj1, obj2), expectedDiff12);

  // Arrays
  dynamic obj3 =
      dynamic::object("x", dynamic::array(1, 2, 3))("y", dynamic::array(4, 5));
  dynamic obj4 =
      dynamic::object("x", dynamic::array(1, 22, 3))("y", dynamic::array(4, 5));
  dynamic expectedDiff34 = dynamic::object("x", dynamic::array(1, 22, 3));
  EXPECT_EQ(JsonUtils::dynamicObjectDifference(obj3, obj4), expectedDiff34);

  // Nested objects
  dynamic obj5 = folly::parseJson(R"({"i": 1, "j": {"a": 2, "b": {"x": 0}}})");
  dynamic obj6 = folly::parseJson(R"({"i": 3, "j": {"a": 4, "b": {"x": 0}}})");
  dynamic expectedDiff56 = folly::parseJson(R"({"i": 3, "j": {"a": 4}})");
  EXPECT_EQ(JsonUtils::dynamicObjectDifference(obj5, obj6), expectedDiff56);
}

TEST(JsonUtilsTest, DynamicObjectFullDifference) {
  // Empty object diff is empty
  dynamic empty = dynamic::object;
  EXPECT_EQ(JsonUtils::dynamicObjectFullDifference(empty, empty), empty);

  // Full diff produces same output in both directions
  dynamic obj1 = dynamic::object("a", 1)("b", 2);
  EXPECT_EQ(JsonUtils::dynamicObjectFullDifference(obj1, empty), obj1);
  EXPECT_EQ(JsonUtils::dynamicObjectFullDifference(empty, obj1), obj1);

  // Full diff takes first object's values on conflicting keys
  dynamic obj2 = dynamic::object("a", 1)("b", 22)("c", 333);
  dynamic expectedDiff12 = dynamic::object("b", 2)("c", 333);
  EXPECT_EQ(JsonUtils::dynamicObjectFullDifference(obj1, obj2), expectedDiff12);

  // Nested objects
  dynamic obj3 = folly::parseJson(
      R"({"envParams": {"FW_IF2IF": "1"}, )"
      R"("fwParams": {"linkOptParams": {"mcs": 35, "txPower": 28}}, )"
      R"("statsAgentParams": {"sources": {)"
      R"("controller": {"enabled": true, "zmq_url": "tcp://localhost:28989"})"
      R"(}}})");
  dynamic obj4 = folly::parseJson(
      R"({"envParams": {"FW_IF2IF": "1"}, )"
      R"("fwParams": {"linkOptParams": {"mcs": 9, "txPower": 28}}, )"
      R"("statsAgentParams": {"sources": {)"
      R"("controller": {"enabled": false, "zmq_url": "tcp://localhost:28989"}, )"
      R"("asdfjkl": {"enabled": true, "zmq_url": "ipc://asdfjkl"})"
      R"(}}})");
  dynamic expectedDiff34 = folly::parseJson(
      R"({"fwParams": {"linkOptParams": {"mcs": 35}}, )"
      R"("statsAgentParams": {"sources": {)"
      R"("controller": {"enabled": true}, )"
      R"("asdfjkl": {"enabled": true, "zmq_url": "ipc://asdfjkl"})"
      R"(}}})");
  EXPECT_EQ(JsonUtils::dynamicObjectFullDifference(obj3, obj4), expectedDiff34);
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
