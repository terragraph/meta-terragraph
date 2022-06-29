/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>
#include <openr/if/gen-cpp2/Network_types.h>

namespace facebook {
namespace terragraph {

/**
 * Open/R-related utilities.
 */
class OpenrUtils {
 public:
  /**
   * Convert a Terragraph node ID to an Open/R-format node name.
   *
   * Example: "00:00:00:10:0d:40" -> "node-00.00.00.10.0d.40"
   */
  static std::string toOpenrNodeName(const std::string& nodeId);

  /**
   * Convert an Open/R-format node name to a Terragraph node ID.
   *
   * Example: "node-00.00.00.10.0d.40" -> "00:00:00:10:0d:40"
   */
  static std::string fromOpenrNodeName(const std::string& name);

  /** Returns a normal string representation of a binary IPv6 address. */
  static std::string binaryAddressToString(
      const openr::thrift::BinaryAddress& addr);
};

} // namespace terragraph
} // namespace facebook
