/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "NetworkCounters.h"

#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/String.h>

namespace facebook {
namespace terragraph {
namespace stats {

NetworkCounters::NetworkCounters() {}

std::unordered_map<std::string, sigar_net_interface_stat_t>
NetworkCounters::getNetInterfaceStats() {
  std::unordered_map<std::string, sigar_net_interface_stat_t> ret;

  // Read /proc file
  std::string contents;
  if (!folly::readFile("/proc/net/dev", contents)) {
    return ret;
  }

  // Parse lines
  std::vector<std::string> lines;
  folly::split('\n', contents, lines);
  const int HEADER_LINE_COUNT = 2;  // skip header
  for (size_t i = HEADER_LINE_COUNT; i < lines.size(); i++) {
    std::vector<std::string> tokens;
    folly::split(' ', lines[i], tokens, true /* ignoreEmpty */);
    if (tokens.size() != 17) {
      continue;  // ex. last line in file
    }

    // Chop trailing colon from interface name
    if (tokens[0].back() == ':') {
      tokens[0].pop_back();
    }
    std::string name = tokens[0];

    // Fill fields
    sigar_net_interface_stat_t ifStat;
    ifStat.rx_bytes = strtoull(tokens[1].c_str(), nullptr, 10);
    ifStat.rx_packets = strtoull(tokens[2].c_str(), nullptr, 10);
    ifStat.rx_errors = strtoull(tokens[3].c_str(), nullptr, 10);
    ifStat.rx_dropped = strtoull(tokens[4].c_str(), nullptr, 10);
    ifStat.rx_overruns = strtoull(tokens[5].c_str(), nullptr, 10);
    ifStat.rx_frame = strtoull(tokens[6].c_str(), nullptr, 10);
    // skip: "compressed", "multicast"
    ifStat.tx_bytes = strtoull(tokens[9].c_str(), nullptr, 10);
    ifStat.tx_packets = strtoull(tokens[10].c_str(), nullptr, 10);
    ifStat.tx_errors = strtoull(tokens[11].c_str(), nullptr, 10);
    ifStat.tx_dropped = strtoull(tokens[12].c_str(), nullptr, 10);
    ifStat.tx_overruns = strtoull(tokens[13].c_str(), nullptr, 10);
    ifStat.tx_collisions = strtoull(tokens[14].c_str(), nullptr, 10);
    ifStat.tx_carrier = strtoull(tokens[15].c_str(), nullptr, 10);
    ifStat.speed = -1 /* SIGAR_FIELD_NOTIMPL */;

    ret[name] = std::move(ifStat);
  }

  return ret;
}

#define SET_COUNTER(NAME)                                                  \
  retMap.insert(std::make_pair(                                            \
      radioMac.empty() ? radioMacOrIfName + "." + #NAME                    \
                       : radioMacOrIfName + "." + #NAME + '\0' + radioMac, \
      createCounter(ifStat.NAME)));

std::unordered_map<std::string, fbzmq::thrift::Counter>
NetworkCounters::fetchStats() {
  return std::unordered_map<std::string, fbzmq::thrift::Counter>{};
}

std::unordered_map<std::string, fbzmq::thrift::Counter>
NetworkCounters::fetchStats(
    const std::unordered_map<std::string, std::pair<std::string, std::string>>&
        interfaceToRadio) {
  std::unordered_map<std::string, fbzmq::thrift::Counter> retMap;

  // fetch interface stats
  auto netInterfaceStats = getNetInterfaceStats();
  for (const auto& kv : netInterfaceStats) {
    const std::string& ifNameStr = kv.first;
    const sigar_net_interface_stat_t& ifStat = kv.second;

    // skip loopback
    if (ifNameStr == "lo") {
      continue;
    }

    std::string radioMacOrIfName = ifNameStr;
    std::string radioMac = "";
    auto interfaceIt = interfaceToRadio.find(ifNameStr);
    if (interfaceIt != interfaceToRadio.end()) {
      radioMacOrIfName =
          "link." + interfaceIt->second.second /* responder mac */;
      radioMac = interfaceIt->second.first /* radio mac */;
    }

    SET_COUNTER(rx_packets);
    SET_COUNTER(rx_bytes);
    SET_COUNTER(rx_errors);
    SET_COUNTER(rx_dropped);
    SET_COUNTER(rx_overruns);
    SET_COUNTER(rx_frame);
    SET_COUNTER(tx_packets);
    SET_COUNTER(tx_bytes);
    SET_COUNTER(tx_errors);
    SET_COUNTER(tx_dropped);
    SET_COUNTER(tx_overruns);
    SET_COUNTER(tx_collisions);
    SET_COUNTER(tx_carrier);

    retMap
        [radioMac.empty() ? radioMacOrIfName + ".speed"
                          : radioMacOrIfName + ".speed" + '\0' + radioMac] =
            createGauge(ifStat.speed);
  }

  return retMap;
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
