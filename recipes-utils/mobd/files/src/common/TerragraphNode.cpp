/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * This class stores and updates the status of a Terragraph node.
 */

#include "TerragraphNode.h"
#include "Utils.h"

#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/String.h>
#include <folly/dynamic.h>
#include <folly/json.h>
#include <glog/logging.h>

#include <math.h>
#include <string.h>

#include <chrono>
#include <inttypes.h>
#include <iostream>
#include <numeric>
#include <regex>
#include <sstream>
#include <thread>

using namespace facebook::terragraph;

namespace {
const std::string kLinkStatusFile{"/tmp/TerragraphNodeLinkStatus"};
const std::string kLinkStatusCmd{
    "/usr/sbin/tg2 minion links --json 2>/dev/null | sed '/^Connecting/d' > " +
    kLinkStatusFile};
const int kLinkWaitSec{5};
} // namespace

TerragraphNode::TerragraphNode() {
  folly::readFile("/etc/tgversion", osVersion);
  secondaryOsVersion = "unknown";
  folly::readFile("/tmp/secondaryImageVersion", secondaryOsVersion);
  folly::readFile("/proc/device-tree/model", dtbVersion);

  osVersion = folly::trimWhitespace(osVersion).toString();
  dtbVersion = folly::trimWhitespace(dtbVersion).toString();
  ubootVersion =
      folly::trimWhitespace(sendCommand("/usr/bin/ubootversion").front())
          .toString();
  nic0Mac =
      folly::trimWhitespace(
          sendCommand("fw_printenv | grep nic0 | cut -d \"=\" -f2").front())
          .toString();
  hostMac = folly::trimWhitespace(
                sendCommand("fw_printenv | grep tg_wlanmac | cut -d \"=\" -f2")
                    .front())
                .toString();
  folly::toLowerAscii(hostMac);

  std::stringstream ss;
  ss << exec("timeout 5 grep MAC_.*= /tmp/node_info | cut -d'\"' -f 2");
  while (!ss.eof()) {
    std::string mac;
    if (ss >> mac) {
      radioMac.push_back(mac);
    }
  }
}

bool
TerragraphNode::reboot() const {
  return mobd::system("/sbin/reboot") == 0;
}

bool
TerragraphNode::swapPrimaryAndSecondaryPartitions() const {
  return mobd::system("/usr/sbin/testcode x") == 0;
}

// Command example
//
// timeout 6 tg2 stats driver-if --radio_mac fe:19:44:06:00:3e | grep
// --line-buffered -i -m 4
// 'fe:19:44:06:00:65.*phystatus.ssnrEst\|fe:19:44:06:00:65.*phystatus.srssi' |
// cut -d' ' -f 3
//
// tg2 stats output to grep
//
// 1277622558998635, tgf.fe:19:44:06:00:65.phystatus.ssnrEst, 20
// 1277622558998635, tgf.fe:19:44:06:00:65.phystatus.srssi, -49
// 1277622559998660, tgf.fe:19:44:06:00:65.phystatus.ssnrEst, 20
// 1277622559998660, tgf.fe:19:44:06:00:65.phystatus.srssi, -49
//
static std::string
getLinkStatsCommand(
    int timeoutSec,
    const std::string& initiatorMac,
    const std::string& responderMac) {
  int maxMatches = 2 * timeoutSec; // expecting snr + rssi sample every sec
  return std::string("timeout ") + std::to_string(timeoutSec + 2) +
         " tg2 stats driver-if --radio_mac " + initiatorMac +
         " | grep --line-buffered -i -m " + std::to_string(maxMatches) + " '" +
         responderMac + ".*phystatus.ssnrEst\\|" + responderMac +
         ".*phystatus.srssi' | cut -d' ' -f 3";
}

// Extract SNR and RSSI from f/w stats
void
TerragraphNode::getLinkStats(
    int timeoutSec,
    const std::string& initiatorMac,
    const std::string& responderMac,
    std::vector<int>& snr,
    std::vector<int>& rssi) {

  timeoutSec = std::max(timeoutSec, minLinkStatsTimeoutSec);
  timeoutSec = std::min(timeoutSec, maxLinkStatsTimeoutSec);

  snr.clear();
  rssi.clear();

  std::stringstream ss;
  auto cmd = getLinkStatsCommand(timeoutSec, initiatorMac, responderMac);
  ss << exec(cmd.c_str());
  while (!ss.eof()) {
    std::string t;
    int sample;
    ss >> t;
    if (std::stringstream(t) >> sample) {
      if (sample >= 0) {
        snr.push_back(sample);
      } else {
        rssi.push_back(sample);
      }
    }
  }
}

// Bring a link down.
// See assoc() for the json expected from the link status command
// (kLinkStatusCmd)
bool
TerragraphNode::dissoc(
    const std::string& initiatorMac, const std::string& responderMac) {
  const std::string dissocCmd = std::string("tg2 minion dissoc -i ") +
                                initiatorMac + " -m " + responderMac;
  if (mobd::system(dissocCmd) != 0) {
    return false;
  }
  // Wait a few seconds for the link to go down.
  for (int i = 0; i < kLinkWaitSec; i++) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (mobd::system(kLinkStatusCmd) != 0) {
      continue;
    }
    std::string jsonString;
    if (!folly::readFile(kLinkStatusFile.c_str(), jsonString)) {
      continue;
    }
    try {
      auto status = folly::parseJson(jsonString);
      if (status["linkStatusDump"].empty()) {
        return true;
      }
      auto dump = status["linkStatusDump"];
      if (dump[responderMac].empty()) {
        return true;
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Caught " << e.what();
    }
  }
  return false;
}

// Create a link
/*
  json expected from the link status command (kLinkStatusCmd)
  {
    "linkStatusDump": {
      "fe:19:44:06:00:3e": {
        "ifname": "terra0",
        "linkStatusType": 1,
        "radioMac": "fe:19:44:06:00:50", <-- initiator
        "responderMac": "fe:19:44:06:00:3e"
      },
      "fe:19:44:06:00:65": {
        "ifname": "terra1",
        "linkStatusType": 1,
        "radioMac": "fe:19:44:06:00:50", <-- initiator
        "responderMac": "fe:19:44:06:00:65"
      }
    }
  }
*/
bool
TerragraphNode::assoc(
    const std::string& initiatorMac, const std::string& responderMac) {
  const std::string assocCmd = std::string("tg2 minion assoc -i ") +
                               initiatorMac + " -m " + responderMac;
  if (mobd::system(assocCmd) != 0) {
    return false;
  }
  // Wait a few seconds for link to come up.
  for (int i = 0; i < kLinkWaitSec; i++) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (mobd::system(kLinkStatusCmd) != 0) {
      continue;
    }
    std::string jsonString;
    if (!folly::readFile(kLinkStatusFile.c_str(), jsonString)) {
      continue;
    }
    try {
      auto status = folly::parseJson(jsonString);
      if (status["linkStatusDump"].empty()) {
        continue;
      }
      auto dump = status["linkStatusDump"];
      if (dump[responderMac].empty()) {
        continue;
      }
      auto linkDump = dump[responderMac];
      if (linkDump["radioMac"].empty()) {
        continue;
      }
      const std::string linkInitMac = linkDump["radioMac"].getString();
      if (linkInitMac != initiatorMac) {
        continue;
      }
      if (linkDump["linkStatusType"].isInt() &&
          linkDump["linkStatusType"].getInt() == 1) {
        return true;
      }
    } catch (const std::exception& e) {
      LOG(ERROR) << "Caught " << e.what();
    }
  }
  return false;
}

// Execute a shell command and get output
std::string
TerragraphNode::exec(const char* cmd) {
  std::array<char, 128> buffer;
  std::string result;
  std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }
  while (!feof(pipe.get())) {
    if (fgets(buffer.data(), 128, pipe.get()) != nullptr) {
      result += buffer.data();
    }
  }
  return result;
}

// Send a command for execution and split output by lines
std::vector<std::string>
TerragraphNode::sendCommand(const std::string& command) {
  std::string output = exec(command.c_str());
  std::vector<std::string> splitedOutput;
  folly::split("\n", output, splitedOutput);
  return splitedOutput;
}
