/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Expected.h>
#include <folly/Subprocess.h>
#include <gflags/gflags.h>

DECLARE_string(vppctl_path);

namespace facebook {
namespace terragraph {
namespace minion {

/**
 * IP neighbor-related utilities.
 *
 * This static class contains methods to fetch IP neighbor status from:
 * - `ip` command
 * - VPP (via `vppctl` CLI)
 */
class NeighborUtils {
 public:
  /** Information about network adjacency connections. */
  struct NeighborConnectionData {
    /** The remote IP address. */
    std::string remoteAddress;
    /** The remote MAC address. */
    std::string remoteMac;
    /** The state string. */
    std::string state;
  };

  /** Fetch current IP neighbor status via the `ip` command. */
  static std::unordered_map<std::string, bool> fetchIpNeighborStatus(
      const std::vector<std::string>& interfaces);

  /**
   * Fetch current VPP IP neighbor status, filtering by the given network
   * interface substring (if non-empty).
   */
  static std::unordered_map<std::string, bool> fetchVppIpNeighborStatus(
      const std::string& interfaceSubstr);

  /**
   * Parse connection info from output of `ip` commands.
   *
   * Upon error, returns the command exit code.
   */
  static folly::Expected<
      std::vector<NeighborConnectionData>, folly::ProcessReturnCode>
          getIpNeighborInfo(const std::string& device);
};

} // namespace minion
} // namespace terragraph
} // namespace facebook
