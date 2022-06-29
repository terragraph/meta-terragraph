/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "VppCounters.h"
#include "SharedObjects.h"

#include <algorithm>
#include <regex>
#include <unordered_set>

#include <folly/Conv.h>
#include <folly/String.h>
#include <gflags/gflags.h>

#include <e2e/common/SysUtils.h>
#include <e2e/common/NodeInfoWrapper.h>

DEFINE_string(vppctl_path, "/usr/bin/vppctl", "Path to 'vppctl' VPP binary");
DEFINE_string(
    vpp_get_stats_path,
    "/usr/bin/vpp_get_stats",
    "Path to 'vpp_get_stats' VPP binary");

namespace {
// Prefix for all key names from this module
const std::string kVppStatPrefix{"vpp"};

// custom `vppctl` command to dump the pipe and tc HQoS stats
const std::string kVppShowAllStatsCmd{"tghqos show all-stats"};

// `vpp_get_stats` command to dump stats
const std::string kVppDumpStatsCmd{"dump"};
// List of patterns passed to kVppDumpStatsCmd
const std::string kVppDumpStatsPatterns[]{{"^/if"}, {"^/err/ip6-input"}};
// List of interface counters (only!) filtered OUT OF results
const std::unordered_set<std::string> kVppDumpStatsSkipIfStats{{
    "/if/rx-no-buf",
    "/if/rx-miss",
    "/if/rx-unicast",
    "/if/rx-multicast",
    "/if/rx-broadcast",
    "/if/tx-unicast-miss",
    "/if/tx-multicast",
    "/if/tx-broadcast",
}};
// List of normal counters/gauges (only!) filtered INTO results
const std::unordered_set<std::string> kVppDumpStatsKeepNormalStats{{
    "/err/ip6-input/ip6 destination lookup miss",
    "/err/ip6-input/ip6 source lookup miss",
    "/err/ip6-input/ip6 drop",
    "/err/ip6-input/ip6 MTU exceeded",
}};

// VPP interface name prefix for terraX interfaces
const std::string kVppTerraIfacePrefix{"vpp-terra"};
// Starting offset in VPP terraX interface names to map to the string "terraX"
const size_t kVppTerraIfaceOffset{4}; // "vpp-"

// Regular expression matching VPP interface counters in `vpp_get_stats`
// - Simple counter:
//   [5 @ 0]: 4479 packets /if/tx-error
// - Combined counter:
//   [3 @ 1]: 3112 packets, 199168 bytes /if/rx
const std::regex kVppIfaceCounterRegex(
    R"(\[([0-9]+) @ ([0-9]+)\]: ([0-9]+) packets(, ([0-9]+) bytes)? (/.+))");

// Regular expression matching VPP "/if/names" STAT_DIR_TYPE_NAME_VECTOR entries
// in `vpp_get_stats`, which dumps the sw_if_index table.
//
// Examples:
//   [3]: TenGigabitEthernet0 /if/names
//   [4]: Wigig0/1/0/0 /if/names
//   [5]: vpp-terra0 /if/names
const std::regex kVppIfaceNameRegex(R"(\[([0-9]+)\]: (.+) /if/names)");
}

// Constant for the number of pipes on a Wigig interface
const int kNumPipes = 16;
// Constant for the number of traffic classes on a Wigig interface
const int kNumTcs = 4;
// Constant for the minimum number of tokens in the 'all-stats' command header
const int kAllStatsMinHeaderTokens = 3;

namespace facebook {
namespace terragraph {
namespace stats {

VppCounters::VppCounters() {}

std::string
VppCounters::reformatKey(const std::string& key) {
  std::string s = key;

  if (s.at(0) == '/') {
    s = s.substr(1);
  }
  std::replace(s.begin(), s.end(), '/', '.');
  std::replace(s.begin(), s.end(), ' ', '_');

  return s;
}

std::vector<std::pair<std::string, fbzmq::thrift::Counter>>
VppCounters::parseVppStat(
    const std::string& line,
    const std::unordered_map<
        std::string,
        std::pair<std::string, std::string>>& terraIfaceMap) {
  std::vector<std::pair<std::string, fbzmq::thrift::Counter>> ret;
  if (line.empty()) {
    return ret;
  }

  std::string input = line;
  if (input.length() >= 4 && input.at(0) == '[' && input.at(1) == '@') {
    // Line has no sw_if_index, only core (worker) index.
    // Currently, this is the format only for STAT_DIR_TYPE_ERROR_INDEX, and
    // we don't care about the core index so just drop it here.
    //
    // Examples:
    //   [@0] 0 /err/ip6-input/ip6 drop
    //   [@1] 0 /err/ip6-input/ip6 drop
    //   [@2] 0 /err/ip6-input/ip6 drop
    size_t idx = input.find(']');
    if (
        idx != std::string::npos &&
        idx + 2 < input.length() &&
        input.at(idx + 1) == ' ') {
      input = input.substr(idx + 2);
    }
  }

  if (input.at(0) == '[') {
    std::smatch m;
    if (std::regex_match(input, m, kVppIfaceCounterRegex) && m.size() == 7) {
      // Interface counters
      bool isCombinedCounter = !m.str(5).empty();
      uint32_t swIfIndex = folly::to<uint32_t>(m.str(1));
      // int coreIndex = folly::to<int>(m.str(2));
      double pktCount = folly::to<double>(m.str(3));
      double byteCount = isCombinedCounter ? folly::to<double>(m.str(5)) : 0;
      std::string key = m.str(6);

      if (kVppDumpStatsSkipIfStats.count(key)) {
        // Dropped
      } else {
        // Look up sw_if_index
        auto iter = vppInterfaceMap_.find(swIfIndex);
        if (iter == vppInterfaceMap_.end()) {
          VLOG(4) << "Unknown VPP sw_if_index " << swIfIndex;
        } else {
          // Merged key name:
          //   <mac_or_ifname>/<name>/<pkts|bytes>
          //
          // We're dropping the core index and summing counters on each core.
          std::string macOrIfname = iter->second;
          std::string radioMacKeySuffix;
          bool skip = false;
          if (iter->second.rfind(kVppTerraIfacePrefix, 0) == 0) {
            auto macIter = terraIfaceMap.find(
                iter->second.substr(kVppTerraIfaceOffset));
            if (macIter != terraIfaceMap.end()) {
              macOrIfname = macIter->second.second;
              radioMacKeySuffix = '\0' + macIter->second.first;
            } else {
              VLOG(5) << "Skipping inactive terraX interface '" << iter->second
                      << "' for key " << key;
              skip = true;
            }
          }
          if (!skip) {
            ret.push_back(std::make_pair(
                reformatKey(
                    kVppStatPrefix + "." + macOrIfname + key + "/pkts") +
                    radioMacKeySuffix,
                createCounter(pktCount)));
            if (isCombinedCounter) {
              ret.push_back(std::make_pair(
                  reformatKey(
                      kVppStatPrefix + "." + macOrIfname + key + "/bytes") +
                      radioMacKeySuffix,
                  createCounter(byteCount)));
            }
          }
        }
      }
    } else {
      VLOG(4) << "Unknown counter format: " << input;
    }
  } else {
    // Normal counters/gauges (have to assume gauge...):
    //   4019 /err/vpp-terra0-output/interface is down
    size_t spaceIdx = input.find(' ');
    if (spaceIdx != std::string::npos) {
      auto value = folly::tryTo<double>(input.substr(0, spaceIdx));
      std::string key = input.substr(spaceIdx + 1);
      if (!kVppDumpStatsKeepNormalStats.count(key)) {
        // Dropped
      } else if (value.hasValue()) {
        ret.push_back(std::make_pair(
            reformatKey(kVppStatPrefix + key),
            createGauge(*value)));
      }
    }
  }

  return ret;
}

std::unordered_map<uint32_t, std::string>
VppCounters::vppGetInterfaceMap(std::vector<std::string>& lines) {
  std::unordered_map<uint32_t, std::string> ret;

  auto iter = lines.begin();
  while (iter != lines.end()) {
    const std::string& input = *iter;
    if (!input.empty()) {
      std::smatch m;
      if (std::regex_match(input, m, kVppIfaceNameRegex) && m.size() == 3) {
        // Name vector counters (specifically /if/names)
        uint32_t swIfIndex = folly::to<uint32_t>(m.str(1));
        std::string ifaceName = m.str(2);

        ret[swIfIndex] = ifaceName;
        iter = lines.erase(iter);
        continue;
      }
    }
    ++iter;
  }

  VLOG(4) << "VPP interface map has " << ret.size() << " entries";
  return ret;
}

std::unordered_map<std::string, fbzmq::thrift::Counter>
VppCounters::vppDumpStats(
    const std::unordered_map<
        std::string,
        std::pair<std::string, std::string>>& terraIfaceMap) {
  std::unordered_map<std::string, fbzmq::thrift::Counter> ret;

  // Run vpp_get_stats command and parse output
  std::vector<std::string> command{FLAGS_vpp_get_stats_path, kVppDumpStatsCmd};
  for (const std::string& pattern : kVppDumpStatsPatterns) {
    command.push_back(pattern);
  }
  auto maybeOutput = SysUtils::runCommand(command);
  if (maybeOutput.hasValue()) {
    std::vector<std::string> lines;
    folly::split('\n', maybeOutput.value(), lines);

    // First pass: parse out the current VPP interface map
    vppInterfaceMap_ = std::move(vppGetInterfaceMap(lines));

    // Parse stats
    for (const std::string& line : lines) {
      if (line.empty()) {
        continue;
      }

      auto entries = parseVppStat(line, terraIfaceMap);
      if (entries.empty()) {
        VLOG(5) << "Not publishing any stats for VPP line: " << line;
      } else {
        for (const auto& pair : entries) {
          // Sum existing counters (i.e. interface counters across cores)
          auto iter = ret.find(pair.first);
          if (iter == ret.end()) {
            ret.insert(pair);
          } else {
            iter->second.value_ref().value() += pair.second.value_ref().value();
          }
        }
      }
    }
  } else {
    LOG(ERROR) << "'" << command[0]
               << "' command failed: " << maybeOutput.error().str();
  }

  VLOG(4) << "Recorded " << ret.size() << " stat(s) from VPP";
  return ret;
}

std::unordered_map<std::string, fbzmq::thrift::Counter>
VppCounters::fetchStats() {
  std::unordered_map<std::string, std::pair<std::string, std::string>>
      interfaceToRadio;
  return fetchStats(interfaceToRadio);
}

std::unordered_map<std::string, fbzmq::thrift::Counter>
VppCounters::fetchStats(
    const std::unordered_map<
        std::string,
        std::pair<std::string, std::string>>& interfaceToRadio) {
  return vppDumpStats(interfaceToRadio);
}

std::unordered_map<std::string, fbzmq::thrift::Counter>
VppCounters::fetchHqosStats(
    const std::unordered_map<
        std::string,
        std::pair<std::string, std::string>>& terraIfaceMap) {
  std::unordered_map<std::string, fbzmq::thrift::Counter> ret;
  // Get ordered list of all active interfaces
  std::vector<std::string> wigigInterfaces = getWigigInterfaces();
  if (wigigInterfaces.empty()) {
    return ret;
  }
  removeOfflineWigigIfs(wigigInterfaces);

  // Create a mapping from wigigIf name to index
  std::unordered_map<std::string, size_t> wigigIfToIdx;
  for (size_t i = 0; i < wigigInterfaces.size(); i++) {
    wigigIfToIdx[wigigInterfaces[i]] = i;
  }

  return getAndParseHqosStat(wigigIfToIdx, terraIfaceMap);
}

std::unordered_map<std::string, fbzmq::thrift::Counter>
VppCounters::getAndParseHqosStat(
    const std::unordered_map<std::string, size_t>& wigigIfToIdx,
    const std::unordered_map<
        std::string,
        std::pair<std::string, std::string>>& terraIfaceMap) {
  std::unordered_map<std::string, fbzmq::thrift::Counter> ret;
  // Construct and run command to get HQoS stats
  std::vector<std::string> command{FLAGS_vppctl_path, kVppShowAllStatsCmd};
  auto maybeOutput = SysUtils::runCommand(command);
  if (maybeOutput.hasValue()) {
    std::vector<std::string> lines;
    folly::split('\n', maybeOutput.value(), lines);
    // Ensure that we have a header and start parsing there
    // Only match the first items in the header to
    // allow for more stats to be added later on
    std::string headerStr = "device_name,pipe,tc";
    size_t headerIdx = 0;
    while (headerIdx < lines.size()
          && lines[headerIdx].find(headerStr) == std::string::npos) {
      headerIdx++;
    }

    if (headerIdx == lines.size()) {
      LOG(ERROR) << "No header found";
      return ret;
    }

    // "tghqos show all-stats" output header format
    // device_name,pipe,tc,<key>,<key>,...
    std::vector<std::string> header;
    folly::split(',', folly::trimWhitespace(lines[headerIdx]), header);

    for (size_t i = headerIdx+1 /* skip the header */; i < lines.size(); i++) {
      if (lines[i].empty()) {
        continue;
      }

      std::vector<std::string> tokens;
      folly::split(',', lines[i], tokens);
      // Ensure that we have the expected # of tokens
      if (tokens.size() != header.size() ||
          tokens.size() < kAllStatsMinHeaderTokens) {
        LOG(ERROR) << "Unexpected number of tokens in line - " << lines[i];
        break;
      }

      // Parse the wigigIf, pipe, and tc
      const std::string& wigigIf = tokens[0];
      auto maybePipe = folly::tryTo<int>(tokens[1]);
      if (!maybePipe.hasValue()) {
        LOG(ERROR) << "Unable to convert to int - " << tokens[1];
        break;
      }
      const std::string& tc = tokens[2];

      // Get the wigigIdx
      auto maybeIdx = wigigIfToIdx.find(wigigIf);
      if (maybeIdx == wigigIfToIdx.end()) {
        continue;
      }
      size_t wigigIdx = maybeIdx->second;

      // Convert wigig interface and pipe to a terra interface
      // Terra interfaces in the same order as wigig interfaces
      // Terra X % 16 = pipe Y => Terra X = pipe Y + 16*Z
      // where Z is the position of the wigig interface in the
      // ordered list
      int terraIfIdx = wigigIdx*kNumPipes + maybePipe.value();
      std::string terraIfName = "terra" + std::to_string(terraIfIdx);
      auto macIter = terraIfaceMap.find(terraIfName);
      if (macIter == terraIfaceMap.end()) {
        VLOG(5) << "Skipping inactive terraX interface '" << terraIfName << "'";
        continue;
      }
      std::string radioMac = macIter->second.first;
      std::string responderMac = macIter->second.second;
      for (size_t j = 3 /* skip wigigIf,pipe,tc */; j < tokens.size(); j++) {
        // Convert the value
        auto maybeValue = folly::tryTo<double>(tokens[j]);
        if (!maybeValue.hasValue()) {
          LOG(ERROR) << "Unable to convert to double - " << tokens[j];
          continue;
        }

        // Format the key
        std::string key = folly::sformat(
            "{}.{}.qos.tc{}.{}", kVppStatPrefix, responderMac, tc, header[j]);
        if (!radioMac.empty()) {
          key += '\0' + radioMac;
        }

        ret[key] = createGauge(maybeValue.value());
      }
    }
  } else {
    LOG(ERROR) << "'" << command[0]
               << "' command failed: " << maybeOutput.error().str();
  }

  return ret;
}

void
VppCounters::removeOfflineWigigIfs(std::vector<std::string>& wigigIfs) {
  // Assume vppInterfaceMap_ is up to date
  std::unordered_set<std::string> onlineIfaces;
  for (const auto& kv : vppInterfaceMap_) {
    onlineIfaces.insert(kv.second);
  }
  auto iter = wigigIfs.begin();
  while (iter != wigigIfs.end()) {
    if (!onlineIfaces.count(*iter)) {
      iter = wigigIfs.erase(iter);
    } else {
      ++iter;
    }
  }
}

std::vector<std::string>
VppCounters::getWigigInterfaces() {
  // Run the command to get list of all radio interfaces
  // and parse the values into Wigig format to populate the vector
  std::vector<std::string> wigigInterfaces;
  std::string pciOrderVal = SharedObjects::getNodeInfoWrapper()
                                        ->rlock()->getPciOrder().value_or("");
  // Check if we have an empty value for PCI_ORDER
  if (pciOrderVal.empty()) {
    VLOG(3) << "No PCI_ORDER key for this node";
    return wigigInterfaces;
  }

  // Parse the radio addr from the PCI_ORDER value
  std::vector<std::string> radioAddr;
  folly::split(',', pciOrderVal, radioAddr);

  // Parse each radio addr  ie. 0000:01:00.0
  // into Wigig format      ie. Wigig0/1/0/0
  // Add to wigigInterfaces
  for (const std::string& addr : radioAddr) {
    std::string wigigIf = "Wigig";
    std::vector<std::string> tokens;
    folly::split(':', addr, tokens);
    // Check that we split into the expected 3 tokens for ":"
    if (tokens.size() != 3) {
      LOG(ERROR) << "parse error - unsupported address format: "
                  << addr;
      wigigInterfaces.clear();
      return wigigInterfaces;
    }

    // Split the last token on "."
    std::string lastToken = tokens[2];
    tokens.pop_back();
    folly::split(".", lastToken, tokens);

    // Check that we split into the expected 4 tokens
    int numTokensTotal = 4;
    if ((int)tokens.size() != numTokensTotal) {
      LOG(ERROR) << "parse error - unsupported address format: "
                  << addr;
      wigigInterfaces.clear();
      return wigigInterfaces;
    }

    // Format each token into the Wigig addr
    for (int i = 0; i < numTokensTotal; i++) {
      // Token is in hex
      int tokenVal = std::stoi(tokens[i], nullptr, 16);
      // Wigig format is in dec
      wigigIf += std::to_string(tokenVal);
      wigigIf += (i != numTokensTotal - 1) ? "/" : "";
    }
    wigigInterfaces.push_back(wigigIf);
  }
  return wigigInterfaces;
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
