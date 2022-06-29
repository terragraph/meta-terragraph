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

#include "e2e/if/gen-cpp2/Controller_types.h"

DECLARE_string(vtysh_path);
DECLARE_string(exabgp_fifo_in);
DECLARE_string(exabgp_fifo_out);

namespace facebook {
namespace terragraph {
namespace minion {

/**
 * BGP-related utilities.
 *
 * This static class contains methods to fetch BGP status from:
 * - ExaBGP (via named pipes)
 * - Quagga (via vtysh CLI)
 */
class BgpUtils {
 public:
  /** Fetch current BGP status via exabgp. */
  static std::unordered_map<std::string, thrift::BgpInfo>
      fetchExabgpBgpStatus();

  /** Fetch current BGP status via vtysh. */
  static std::unordered_map<std::string, thrift::BgpInfo> fetchVtyshBgpStatus();

  /** Fetch current BGP stats via exabgp. */
  static std::unordered_map<std::string, int> fetchExabgpBgpStats();

 private:
  /** Run an exabgp command by reading/writing named pipes. */
  static std::string runExabgpCmd(const std::string& command);

  /**
   * Run a vtysh daemon command.
   *
   * Returns the output if successful (return code = 0), else the return code
   * itself.
   */
  static folly::Expected<std::string, folly::ProcessReturnCode> runVtyshCmd(
      const std::string& command);

  /**
   * Reads a vtysh BGP info table by the first header and returns the number of
   * rows. 'firstHeader' is the first table header that identifies the table.
   */
  static int parseVtyshBgpTable(
      const std::string& bgpTableOutput,
      const std::string& firstHeader,
      std::vector<std::string>& headers,
      std::vector<std::string>& entries);

  /**
   * Creates a list of thrift::BgpRouteInfo by taking the 'Network' and
   * 'Next Hop' values of the vtysh table entries it receives.
   */
  static std::vector<thrift::BgpRouteInfo> createVtyshBgpRouteInfoList(
      int numOfRows, const std::vector<std::string>& entries);

  /**
   * Creates a list of thrift::BgpRouteInfo for each neighbor by parsing
   * exabgp adj-rib output.
   */
  static std::unordered_map<std::string, std::vector<thrift::BgpRouteInfo>>
      createExabgpBgpRouteInfoList(const std::string& rawOutput);
};

} // namespace minion
} // namespace terragraph
} // namespace facebook
