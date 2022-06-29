/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "BaseCounters.h"

#include <map>

#include <sigar.h>  // for sigar_net_interface_stat_t

namespace facebook {
namespace terragraph {
namespace stats {

class NetworkCounters : public BaseCounters {
 public:
  NetworkCounters();

  std::unordered_map<std::string, fbzmq::thrift::Counter> fetchStats() override;
  std::unordered_map<std::string, fbzmq::thrift::Counter> fetchStats(
      const std::unordered_map<
          std::string,
          std::pair<std::string, std::string>>& interfaceToRadio);

 private:
  /**
   * Retrieve all network interface stats.
   *
   * This replaces `sigar_net_interface_stat_get()` loop with one file read,
   * and avoids the need to call `sigar_net_interface_list_get()`.
   */
  std::unordered_map<std::string, sigar_net_interface_stat_t>
      getNetInterfaceStats();
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
