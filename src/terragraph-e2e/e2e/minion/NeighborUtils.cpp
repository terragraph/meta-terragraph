/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "NeighborUtils.h"

#include <sstream>

#include <folly/Format.h>
#include <folly/String.h>

#include <e2e/common/SysUtils.h>

DEFINE_string(vppctl_path, "/usr/bin/vppctl", "Path to 'vppctl' VPP binary");

namespace {
// Command to show ipv6 neighbors
// (requires one appended argument for the device to query)
const std::string kGetNeighborsCommand{"ip -6 neighbor show dev"};

// `vppctl` command to show VPP ipv6 neighbors
const std::string kVppGetNeighborsCommand{"show ip6 neighbors"};
}

namespace facebook {
namespace terragraph {
namespace minion {

std::unordered_map<std::string, bool>
NeighborUtils::fetchIpNeighborStatus(
    const std::vector<std::string>& interfaces) {
  std::unordered_map<std::string, bool> connectionStatusMap;
  for (const std::string& interface : interfaces) {
    auto connectionStatus = getIpNeighborInfo(interface);
    if (connectionStatus.hasError()) {
      LOG(ERROR) << "Error getting neighboring connections on interface "
                 << interface;
      continue;
    }

    for (const auto& status : connectionStatus.value()) {
      bool isConnectionAlive = status.state != "FAILED" &&
                               status.state != "INCOMPLETE" &&
                               status.state != "NONE";

      // Some neighbor devices may have connections on more than 1 IP address.
      // If this is the case, we assume the connection is alive if at least 1
      // of these connections is up.
      auto iter = connectionStatusMap.find(status.remoteMac);
      if (iter != connectionStatusMap.end()) {
        iter->second = iter->second || isConnectionAlive;
      } else {
        connectionStatusMap[status.remoteMac] = isConnectionAlive;
      }
    }
  }
  return connectionStatusMap;
}

folly::Expected<
    std::vector<NeighborUtils::NeighborConnectionData>,
    folly::ProcessReturnCode>
NeighborUtils::getIpNeighborInfo(const std::string& device) {
  std::string command = folly::sformat("{} {}", kGetNeighborsCommand, device);
  VLOG(3) << "Fetching network adjacencies via ip command: `" << command << "`";

  // Create command vector
  std::vector<std::string> commandVec;
  folly::split(" ", kGetNeighborsCommand, commandVec);
  commandVec.push_back(device);  // push separately for proper escaping
  auto output = SysUtils::runCommand(commandVec);
  if (output.hasError()) {
    return folly::makeUnexpected(output.error());
  }
  std::string ipOutput = folly::trimWhitespace(output.value()).toString();

  std::vector<NeighborConnectionData> ret;

  // If stdout is empty, no neighbors on this device
  if (ipOutput.empty()) {
    VLOG(3)
        << folly::format("ip command `{}` returned with no output", command);
    return ret;
  }

  // Split stdout by newline
  std::vector<folly::StringPiece> lines;
  folly::split("\n", ipOutput, lines);
  for (const auto& line : lines) {
    if (line.empty()) {
      continue;
    }

    // Split each line by space
    std::vector<folly::StringPiece> words;
    folly::split(" ", line, words);

    // A line of the output of kGetNeighborsCommand looks like either:
    //   `fe80::2c8:8bff:feae:e9c1 lladdr 00:c8:8b:ae:e9:c1 router STALE`
    //   `fe80::250:c2ff:fec9:9d5c lladdr 00:50:c2:c9:9d:5c REACHABLE`
    // Ensure minimum # of expected words is met (and minion doesn't crash)
    if (words.size() < 4) {
      continue;
    }

    NeighborConnectionData ncData;
    ncData.remoteAddress = words[0].toString();
    ncData.remoteMac = words[2].toString();
    ncData.state = words.back().toString();
    ret.push_back(ncData);
  }

  return ret;
}

std::unordered_map<std::string, bool>
NeighborUtils::fetchVppIpNeighborStatus(const std::string& interfaceSubstr) {
  std::unordered_map<std::string, bool> connectionStatusMap;

  std::string command =
      folly::sformat("{} {}", FLAGS_vppctl_path, kVppGetNeighborsCommand);
  VLOG(3) << "Fetching neighbors via VPP command: `" << command << "`";

  // Create command vector
  std::vector<std::string> commandVec{FLAGS_vppctl_path};
  folly::split(" ", kVppGetNeighborsCommand, commandVec);
  auto output = SysUtils::runCommand(commandVec);

  if (output.hasValue()) {
    std::vector<std::string> lines;
    folly::split('\n', output.value(), lines);
    for (size_t i = 1 /* skip the table header */; i < lines.size(); i++) {
      // Split line by whitespace
      std::istringstream lineStrStream(lines[i]);
      std::vector<std::string> lineEntries{
          std::istream_iterator<std::string>{lineStrStream},
          std::istream_iterator<std::string>{}};

      // Table headers:
      // [Time] [Address] [Flags] [Link layer] [Interface]
      // ("Flags" can be empty)
      if (lineEntries.size() != 4 && lineEntries.size() != 5) {
        continue;
      }

      std::string remoteMac = lineEntries[lineEntries.size() - 2];
      std::string iface = lineEntries[lineEntries.size() - 1];
      if (interfaceSubstr.empty() ||
          iface.find(interfaceSubstr) != std::string::npos) {
        connectionStatusMap[remoteMac] = true;
      }
    }
  } else {
    LOG(ERROR) << "Command failed: `" << command << "`";
  }

  return connectionStatusMap;
}

} // namespace minion
} // namespace terragraph
} // namespace facebook
