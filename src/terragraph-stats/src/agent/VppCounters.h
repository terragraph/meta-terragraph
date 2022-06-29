/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "BaseCounters.h"

#include <vector>

#include <folly/Expected.h>
#include <folly/Subprocess.h>

namespace facebook {
namespace terragraph {
namespace stats {

/**
 * Stats collector for VPP interface counters.
 *
 * This class will fork VPP CLIs (`vpp_get_stats` and `vppctl`) and parse their
 * output; it does not use the VPP API and stats libraries.
 */
class VppCounters : public BaseCounters {
 public:
  VppCounters();

  std::unordered_map<std::string, fbzmq::thrift::Counter> fetchStats() override;
  std::unordered_map<std::string, fbzmq::thrift::Counter> fetchStats(
      const std::unordered_map<
          std::string,
          std::pair<std::string, std::string>>& interfaceToRadio);

  /**
   * Fetch the HQoS stats from the node for each combination of terra interface
   * and traffic class.
   *
   * Returns a map of keys to counters containing the stats value.
   * Keys are in the following format (where N is the traffic class):
   *     "vpp.<macOrIfname>.qos.tcN.<stats_parameter>"
   */
  std::unordered_map<std::string, fbzmq::thrift::Counter> fetchHqosStats(
      const std::unordered_map<
          std::string,
          std::pair<std::string, std::string>>& terraIfaceMap);

 private:
  /**
   * Reformat a VPP counter name to match other stat counter names:
   *
   * - Remove leading slash ('/')
   * - Convert slashes ('/') to dots ('.')
   * - Convert spaces (' ') to underscores ('_')
   */
  std::string reformatKey(const std::string& key);

  /**
   * Parse a raw line of stats from `vpp_get_stats`.
   *
   * Returns a vector of stat entries (key and counter pairs) upon success, or
   * an empty list if either the input format is unrecognized or represents an
   * interface counter for a "vpp-terraX" interface not present in
   * `terraIfaceMap`.
   */
  std::vector<std::pair<std::string, fbzmq::thrift::Counter>> parseVppStat(
      const std::string& line,
      const std::unordered_map<
          std::string,
          std::pair<std::string, std::string>>& terraIfaceMap);

  /**
   * Dump the current VPP counters.
   *
   * All interface counters for "vpp-terraX" interfaces will only be returned
   * if the corresponding "terraX" interface is present in `terraIfaceMap`.
   */
  std::unordered_map<std::string, fbzmq::thrift::Counter> vppDumpStats(
      const std::unordered_map<
          std::string,
          std::pair<std::string, std::string>>& terraIfaceMap);

  /**
   * Get the mapping from VPP software interface index to interface name using
   * the given raw output lines from `vpp_get_stats`, and remove these lines
   * from the input vector.
   */
  std::unordered_map<uint32_t, std::string> vppGetInterfaceMap(
      std::vector<std::string>& lines);

  /** Given the interface, parse and return the formatted HQoS stats */
  std::unordered_map<std::string, fbzmq::thrift::Counter> getAndParseHqosStat(
      const std::unordered_map<std::string, size_t>& wigigIfToIdx,
      const std::unordered_map<
          std::string,
          std::pair<std::string, std::string>>& terraIfaceMap);

  /**
   * Remove any interfaces from the given list if not present in
   * `vppInterfaceMap_`.
   */
  void removeOfflineWigigIfs(std::vector<std::string>& wigigIfs);

  /**
   * Retrieve all Wigig interfaces on this node.
   *
   * Returns the vector of detected Wigig interfaces in the
   * following format: Wigig0/1/0/0, or an empty list if either the radio
   * address format is unrecognized or no radios are detected on this node.
   *
   * This Wigig interface format may only work on Puma and should be updated if
   * needed to support other hardware.
   */
  std::vector<std::string> getWigigInterfaces();

  /** Map from VPP software interface index to interface name. */
  std::unordered_map<uint32_t /* sw_if_index */, std::string /* ifname */>
      vppInterfaceMap_{};
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
