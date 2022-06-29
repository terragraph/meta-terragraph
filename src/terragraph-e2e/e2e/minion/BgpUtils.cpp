/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "BgpUtils.h"

#include <folly/Conv.h>
#include <folly/Format.h>
#include <folly/IPAddress.h>
#include <folly/String.h>

#include <e2e/common/SysUtils.h>

DEFINE_string(
    vtysh_path, "/usr/bin/vtysh", "Path to vtysh binary, if applicable");
DEFINE_string(
    exabgp_fifo_in,
    "/run/exabgp/exabgp.in",
    "ExaBGP named pipe (FIFO) file for input, if applicable");
DEFINE_string(
    exabgp_fifo_out,
    "/run/exabgp/exabgp.out",
    "ExaBGP named pipe (FIFO) file for output, if applicable");

namespace {
// -- vtysh ----------------------------------------------------------------- //
// string format to run a command in vtysh shell, without the interactive shell
// (requires one appended argument for the command to run in vtysh)
const std::string kRunVtyshDaemonCommand{"-d bgpd -c"};

// vtysh command to show the program version
// (other commands below have different syntax in Quagga vs. FRRouting)
const std::string kVtyshShowVersionCommand{"show version"};

// vtysh commands to get the BGP summary
const std::string kVtyshQuaggaGetBgpSummaryCommand{"show ipv6 bgp summary"};
const std::string kVtyshFrrGetBgpSummaryCommand{
    "show bgp ipv6 unicast summary"};

// vtysh commands to get received routes from a BGP neighbor
// (requires neighbor ipv6 passed into {})
const std::string kVtyshQuaggaGetBgpReceivedRoutesFormat{
    "show ipv6 bgp neighbor {} received-routes"};
const std::string kVtyshFrrGetBgpReceivedRoutesFormat{
    "show bgp ipv6 unicast neighbor {} routes"};

// vtysh commands to get advertised routes from a BGP neighbor
// (requires neighbor ipv6 passed into {})
const std::string kVtyshQuaggaGetBgpAdvertisedRoutesFormat{
    "show ipv6 bgp neighbor {} advertised-routes"};
const std::string kVtyshFrrGetBgpAdvertisedRoutesFormat{
    "show bgp ipv6 unicast neighbor {} advertised-routes"};
// -------------------------------------------------------------------------- //

// -- exabgp ---------------------------------------------------------------- //
// ExaBGP command to get the BGP summary
const std::string kExabgpGetBgpSummaryCommand{"show neighbor summary"};

// ExaBGP command to get the advertised/received routes summaries
const std::string kExabgpGetBgpAdvertisedRoutes{"show adj-rib out"};
const std::string kExabgpGetBgpReceivedRoutes{"show adj-rib in"};

// ExaBGP command to get the BGP stats
// NOTE: custom command
const std::string kExabgpGetBgpStatsCommand{"show stats"};

// Timeout running an ExaBGP command in seconds
const std::chrono::seconds kExabgpTimeoutS{5};
// -------------------------------------------------------------------------- //
}

namespace facebook {
namespace terragraph {
namespace minion {

std::unordered_map<std::string, thrift::BgpInfo>
BgpUtils::fetchExabgpBgpStatus() {
  std::unordered_map<std::string, thrift::BgpInfo> bgpStatus;

  // Get summary from exabgp
  std::string summaryOutput = runExabgpCmd(kExabgpGetBgpSummaryCommand);
  if (summaryOutput.empty()) {
    LOG(ERROR) << "Received no output from ExaBGP for command: "
               << kExabgpGetBgpSummaryCommand;
    return bgpStatus;
  }
  VLOG(3) << "exabgp summary:\n" << summaryOutput;

  // Iterate through neighbors and create BgpInfo per neighbor
  std::vector<std::string> lines;
  folly::split('\n', summaryOutput, lines);
  for (size_t i = 1 /* skip the table header */; i < lines.size(); i++) {
    // Split line by whitespace
    std::istringstream lineStrStream(lines[i]);
    std::vector<std::string> lineEntries{
        std::istream_iterator<std::string>{lineStrStream},
        std::istream_iterator<std::string>{}};

    // BGP Headers:
    // Peer AS up/down state #sent #recvd
    if (lineEntries.size() != 6) {
      continue;
    }

    thrift::BgpInfo neighbor;
    neighbor.ipv6Address = lineEntries[0];
    neighbor.online = (lineEntries[2] != "down" && lineEntries[3] != "down");
    neighbor.asn = folly::to<int>(lineEntries[1]);
    neighbor.upDownTime = lineEntries[2];
    neighbor.stateOrPfxRcd = neighbor.online ? lineEntries[5] : lineEntries[3];

    bgpStatus.insert(std::make_pair(neighbor.ipv6Address, neighbor));
  }

  // Query for advertised/received routes
  if (!bgpStatus.empty()) {
    std::string advertisedOutput = runExabgpCmd(kExabgpGetBgpAdvertisedRoutes);
    VLOG(3) << "exabgp advertised routes:\n" << advertisedOutput;
    auto advertisedRoutes = createExabgpBgpRouteInfoList(advertisedOutput);
    for (const auto& kv : advertisedRoutes) {
      auto iter = bgpStatus.find(kv.first);
      if (iter != bgpStatus.end()) {
        iter->second.advertisedRoutes = std::move(kv.second);
      }
    }

    std::string receivedOutput = runExabgpCmd(kExabgpGetBgpReceivedRoutes);
    VLOG(3) << "exabgp received routes:\n" << receivedOutput;
    auto receivedRoutes = createExabgpBgpRouteInfoList(receivedOutput);
    for (const auto& kv : receivedRoutes) {
      auto iter = bgpStatus.find(kv.first);
      if (iter != bgpStatus.end()) {
        iter->second.receivedRoutes = std::move(kv.second);
      }
    }
  }

  return bgpStatus;
}

std::unordered_map<std::string, thrift::BgpInfo>
BgpUtils::fetchVtyshBgpStatus() {
  std::unordered_map<std::string, thrift::BgpInfo> bgpStatus;

  // Get version (first line may start with "Quagga" or "FRRouting")
  auto versionOutput = runVtyshCmd(kVtyshShowVersionCommand);
  if (versionOutput.hasError()) { // command failed
    LOG(ERROR) << "vtysh version command failed: "
               << versionOutput.error().str();
    return bgpStatus;
  }
  std::string summaryCommand, advertisedRoutesFormat, receivedRoutesFormat;
  if (versionOutput.value().rfind("Quagga", 0) == 0) {
    summaryCommand = kVtyshQuaggaGetBgpSummaryCommand;
    advertisedRoutesFormat = kVtyshQuaggaGetBgpAdvertisedRoutesFormat;
    receivedRoutesFormat = kVtyshQuaggaGetBgpReceivedRoutesFormat;
  } else /* if (versionOutput.value().rfind("FRRouting", 0) == 0) */ {
    summaryCommand = kVtyshFrrGetBgpSummaryCommand;
    advertisedRoutesFormat = kVtyshFrrGetBgpAdvertisedRoutesFormat;
    receivedRoutesFormat = kVtyshFrrGetBgpReceivedRoutesFormat;
  }

  // Get summary from vtysh
  auto bgpSummaryOutput = runVtyshCmd(summaryCommand);
  if (bgpSummaryOutput.hasError()) { // command failed
    LOG(ERROR) << "vtysh BGP summary command failed: "
               << bgpSummaryOutput.error().str();
    return bgpStatus;
  }

  std::string bgpSummary = bgpSummaryOutput.value();
  std::vector<std::string> summaryHeaders, summaryEntries;
  int neighborCount = parseVtyshBgpTable(
      bgpSummary, "Neighbor", summaryHeaders, summaryEntries);

  // Iterate through neighbors and create BgpInfo per neighbor
  for (int i = 0; i < neighborCount; ++i) {
    // BGP Headers:
    // Neighbor V AS MsgRcvd MsgSent TblVer InQ OutQ Up/Down State/PfxRcd

    int startRowIdx = i * summaryHeaders.size();
    thrift::BgpInfo neighbor;
    // State/PfxRcd Header
    auto stateOrPfxToInt = folly::tryTo<int>(summaryEntries[startRowIdx + 9]);

    neighbor.ipv6Address = summaryEntries[startRowIdx]; // Neighbor Header
    // Need to check if the stateOrPfx is an int since it can either
    // be a string referring to its state or the actual neighbor prefix
    neighbor.online = stateOrPfxToInt.hasValue();
    // AS Header
    neighbor.asn = folly::to<int>(summaryEntries[startRowIdx + 2]);
    neighbor.upDownTime = summaryEntries[startRowIdx + 8]; // Up/Down Header
    // State/PfxRcd Header
    neighbor.stateOrPfxRcd = summaryEntries[startRowIdx + 9];

    // Run 'bgp neighbor {} advertised-routes' command
    auto advertisedRoutesOutput = runVtyshCmd(
        folly::sformat(advertisedRoutesFormat, neighbor.ipv6Address));

    if (advertisedRoutesOutput.hasValue()) { // ran successfully
      std::vector<std::string> advertisedHeaders, advertisedEntries;
      int advertisedRoutesCount = parseVtyshBgpTable(
          advertisedRoutesOutput.value(),
          "Network",
          advertisedHeaders,
          advertisedEntries);
      neighbor.advertisedRoutes =
          createVtyshBgpRouteInfoList(advertisedRoutesCount, advertisedEntries);
    } else { // command failed
      LOG(ERROR) << "vtysh BGP advertised routes command failed for neighbor "
                 << neighbor.ipv6Address << ": "
                 << advertisedRoutesOutput.error().str();
    }

    // Run 'bgp neighbor {} received-routes' command
    auto receivedRoutesOutput = runVtyshCmd(
        folly::sformat(receivedRoutesFormat, neighbor.ipv6Address));
    if (receivedRoutesOutput.hasValue()) { // ran successfully
      std::vector<std::string> receivedHeaders, receivedEntries;
      int receivedRoutesCount = parseVtyshBgpTable(
          receivedRoutesOutput.value(),
          "Network",
          receivedHeaders,
          receivedEntries);
      neighbor.receivedRoutes =
          createVtyshBgpRouteInfoList(receivedRoutesCount, receivedEntries);
    } else { // command failed
      LOG(ERROR) << "vtysh BGP received routes command failed for neighbor "
                 << neighbor.ipv6Address << ": "
                 << receivedRoutesOutput.error().str();
    }

    bgpStatus.insert(std::make_pair(neighbor.ipv6Address, neighbor));
  }

  return bgpStatus;
}

std::unordered_map<std::string, int>
BgpUtils::fetchExabgpBgpStats() {
  std::unordered_map<std::string, int> bgpStats;

  // Get summary from exabgp
  std::string summaryOutput = runExabgpCmd(kExabgpGetBgpStatsCommand);
  if (summaryOutput.empty()) {
    LOG(ERROR) << "Received no output from ExaBGP for command: "
               << kExabgpGetBgpStatsCommand;
    return bgpStats;
  }

  // Iterate through stats and add to map
  std::vector<std::string> lines;
  folly::split('\n', summaryOutput, lines);
  for (size_t i = 0; i < lines.size(); i++) {
    // Split line by ','
    std::vector<std::string> tokens;
    folly::split(",", lines[i], tokens);

    // Check that there are 2 tokens (key and value)
    if (tokens.size() != 2) {
      continue;
    }

    // Try to convert the value to a double
    auto value = folly::tryTo<double>(tokens[1]);
    if (!value.hasValue()) {
      continue;
    }

    // Add pair to map
    bgpStats.insert(std::make_pair(tokens[0], (int)value.value()));
  }

  return bgpStats;
}

std::string
BgpUtils::runExabgpCmd(const std::string& command) {
  // Write command to ExaBGP "in" pipe
  int fd = open(FLAGS_exabgp_fifo_in.c_str(), O_WRONLY | O_NONBLOCK);
  if (fd < 0) {
    LOG(ERROR) << "Failed to open " << FLAGS_exabgp_fifo_in << ": "
               << folly::errnoStr(errno);
    return "";
  }
  VLOG(3) << "Running exabgp command: " << command;
  std::string commandLF = command + "\n";  // LF needed to terminate command
  int wlen = write(fd, commandLF.c_str(), commandLF.length());
  if (wlen < (int)commandLF.length()) {
    LOG(ERROR) << "Wrote only " << wlen << " bytes (expected "
               << commandLF.length() <<") to " << FLAGS_exabgp_fifo_in;
  }
  if (close(fd) < 0) {
    LOG(ERROR) << "Failed to close " << FLAGS_exabgp_fifo_in << ": "
               << folly::errnoStr(errno);
  }

  // Read output from ExaBGP "out" pipe
  fd = open(FLAGS_exabgp_fifo_out.c_str(), O_RDONLY | O_NONBLOCK);
  if (fd < 0) {
    LOG(ERROR) << "Failed to open " << FLAGS_exabgp_fifo_out << ": "
               << folly::errnoStr(errno);
    return "";
  }
  std::string output;
  char buf[512];
  // We're done only when we've read the string "done\n", which may happen AFTER
  // read() has returned "EOF" (0 bytes read)
  const std::string kExabgpDoneStr{"done\n"};
  const auto start_time = std::chrono::steady_clock::now();
  while (true) {
    if (std::chrono::steady_clock::now() - start_time > kExabgpTimeoutS) {
      LOG(ERROR) << "ExaBGP command timed-out: " << command;
      return "";
    }
    // Need to select() because exabgp may not send all data to pipe at once...
    fd_set fdSet;
    FD_ZERO(&fdSet);
    FD_SET(fd, &fdSet);
    // Using arbitrary 200ms timeout
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    int rv = select(fd + 1, &fdSet, NULL, NULL, &tv);
    if (rv == -1) {
      if (errno == EINTR || errno == EAGAIN) {
        break;  // interrupted or resource allocation failed, give up
      } else {
        LOG(ERROR) << "select() failed: " << folly::errnoStr(errno);
        return "";
      }
    } else if (rv == 0) {
      LOG(ERROR) << "Timeout reading from " << FLAGS_exabgp_fifo_out;
      break;
    } else {
      ssize_t count = read(fd, buf, sizeof(buf));
      if (count == -1) {
        if (errno == EINTR) {
          continue;  // interrupted, try again
        } else {
          LOG(ERROR) << "Failed to read from " << FLAGS_exabgp_fifo_out << ": "
                     << folly::errnoStr(errno);
          return "";
        }
      }
      output += std::string(buf, count);
      if (output.length() >= kExabgpDoneStr.length() &&
          output.compare(
              output.length() - kExabgpDoneStr.length(),
              kExabgpDoneStr.length(),
              kExabgpDoneStr) == 0) {
        // Strip the "done\n" and return
        output = output.substr(0, output.length() - kExabgpDoneStr.length());
        break;
      }
    }
  }
  if (close(fd) < 0) {
    LOG(ERROR) << "Failed to close " << FLAGS_exabgp_fifo_out << ": "
               << folly::errnoStr(errno);
  }
  VLOG(3) << "ExaBGP output: " << output;
  return output;
}

folly::Expected<std::string, folly::ProcessReturnCode>
BgpUtils::runVtyshCmd(const std::string& command) {
  // Create command vector
  std::vector<std::string> commandVec;
  commandVec.push_back(FLAGS_vtysh_path);
  folly::split(" ", kRunVtyshDaemonCommand, commandVec);
  commandVec.push_back(command);

  return SysUtils::runCommand(commandVec);
}

int
BgpUtils::parseVtyshBgpTable(
    const std::string& bgpTableOutput,
    const std::string& firstHeader,
    std::vector<std::string>& headers,
    std::vector<std::string>& entries) {
  // Split output line by line
  std::vector<folly::StringPiece> bgpTableLines;
  folly::split('\n', bgpTableOutput, bgpTableLines);

  int rowCount = 0;
  bool inTable = false;

  for (const auto& line : bgpTableLines) {
    // Split line by whitespace
    std::string lineStr = line.toString();
    std::istringstream lineStrStream(lineStr);
    std::vector<std::string> lineEntries{
        std::istream_iterator<std::string>{lineStrStream},
        std::istream_iterator<std::string>{}};

    if (inTable) {
      // Table is finished when the next line is empty
      if (lineStr.empty()) {
        inTable = false;
        continue;
      }

      entries.insert(entries.end(), lineEntries.begin(), lineEntries.end());
    } else {
      // Not in the table
      if (!lineEntries.empty() && lineEntries[0] == firstHeader) {
        // We are at the start of the table
        headers.insert(headers.end(), lineEntries.begin(), lineEntries.end());
        inTable = true;
      } else if (line.startsWith("Total number")) {
        // We are at the section that states the number of neighbors/prefixes
        rowCount = folly::to<int>(lineEntries.back());
        break;
      } else if (line.startsWith("Displayed") && lineEntries.size() == 7) {
        rowCount = folly::to<int>(lineEntries[1]);
        break;
      }
    }
  }

  return rowCount;
}

std::vector<thrift::BgpRouteInfo>
BgpUtils::createVtyshBgpRouteInfoList(
    int rowCount, const std::vector<std::string>& entries) {
  // Since each row can have a variable number of entries
  // (due to the variable path length), if the next entry is a 'i', 'e' or '?'
  // then it marks the end of the row.
  std::vector<thrift::BgpRouteInfo> routeInfo;
  unsigned int rowStartIdx = 0;
  for (int i = 0; i < rowCount; ++i) {
    // Skip the first entry on each row since it is the status code
    thrift::BgpRouteInfo bgpRouteInfo;
    bgpRouteInfo.network = entries[rowStartIdx + 1];
    bgpRouteInfo.nextHop = entries[rowStartIdx + 2];
    routeInfo.push_back(bgpRouteInfo);

    // Look for a row termination token
    while (rowStartIdx < entries.size() &&
           entries[rowStartIdx] != "i" &&
           entries[rowStartIdx] != "e" &&
           entries[rowStartIdx] != "?") {
      ++rowStartIdx;
    }

    // Pick the token right after the row termination token
    ++rowStartIdx;
  }

  return routeInfo;
}

std::unordered_map<std::string, std::vector<thrift::BgpRouteInfo>>
BgpUtils::createExabgpBgpRouteInfoList(const std::string& rawOutput) {
  std::unordered_map<std::string, std::vector<thrift::BgpRouteInfo>> routes;

  // Parse one [neighbor, network, next-hop] entry per line
  std::vector<std::string> lines;
  folly::split('\n', rawOutput, lines);
  for (const std::string& line : lines) {
    // Expected format:
    //   neighbor [ip] ipv{4,6} {addressing} [network] next-hop [ip]
    std::vector<std::string> fields;
    folly::split(' ', line, fields);
    if (fields.size() < 7 || fields[0] != "neighbor") {
      continue;
    }
    if (!folly::IPAddress::validate(fields[1])) {
      LOG(ERROR) << "Invalid neighbor IP from exabgp: " << fields[1];
      continue;
    }
    try {
      folly::IPAddress::createNetwork(fields[4]);
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Invalid network prefix from exabgp: " << fields[4];
      continue;
    }
    if (!folly::IPAddress::validate(fields[6])) {
      LOG(ERROR) << "Invalid next-hop IP from exabgp: " << fields[6];
      continue;
    }
    thrift::BgpRouteInfo bgpRouteInfo;
    bgpRouteInfo.network = fields[4];
    bgpRouteInfo.nextHop = fields[6];
    routes[fields[1]].push_back(bgpRouteInfo);
  }
  return routes;
}

} // namespace minion
} // namespace terragraph
} // namespace facebook
