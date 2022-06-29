/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

namespace cpp2 facebook.terragraph.thrift

enum ApiCategory {
  ALL = 1,
  IGNITION = 2,
  MANAGEMENT = 3,
  CONFIG = 4,
  TOPOLOGY = 5,
  SCAN = 6,
  PERFORMANCE = 7,
  UPGRADE = 8,
}

enum ApiLevel {
  READ = 10,
  WRITE = 20,
}
