/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>

namespace facebook {
namespace terragraph {

/**
 * MAC address-related utilities.
 */
class MacUtils {
 public:
  /**
   * Returns a standardized MAC address string using folly::MacAddress.
   *
   * Throws std::invalid_argument if the MAC address could not be parsed.
   */
  static std::string standardizeMac(const std::string& nodeMac);
};

} // namespace terragraph
} // namespace facebook
