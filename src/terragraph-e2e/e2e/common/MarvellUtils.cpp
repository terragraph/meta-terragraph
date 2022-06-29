/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "MarvellUtils.h"
#include "SysUtils.h"

#include <regex>

#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/String.h>
#include <folly/Subprocess.h>
#include <folly/system/Shell.h>

using namespace folly::literals::shell_literals;

namespace facebook {
namespace terragraph {

const static std::string PORT_NAME_FORMAT = "port_{}";

// script used to collect switch counters
const static std::string COUNTERS_CMD =
    "show interfaces mac counters ethernet 0/0,4,12,20,24,25,26,27";
const static std::string COUNTERS_FILE = "/tmp/marvell_interface_counters";
const static std::regex COUNTERS_REGEX(
    "([0-9/]+)\\s+([0-9]+)\\s+([0-9]+)\\s+([0-9]+)\\s+([0-9]+)");

// script used to collect switch port status
const static std::string STATUS_CMD =
    "show interfaces status ethernet 0/0,4,12,20,24,25,26,27";
const static std::string STATUS_FILE = "/tmp/marvell_interface_status";
const static std::regex STATUS_REGEX(
    "([0-9/]+)\\s+([^\\s]+)\\s+(Up|Down)\\s+([0-9\\.]+)([A-Z])\\s+([^\\s]+)");

std::string
MarvellUtils::formatPortIdToName(const std::string& portId) {
  return folly::sformat(PORT_NAME_FORMAT, portId);
}

bool
MarvellUtils::outputCommandToFile(
    const std::string& script, const std::string& outputFile) {
  // Format marvell command into Folly::Subprocess argument
  const std::string marvellCmd =
    folly::sformat("echo -e \'{}\\rCLIexit\' | timeout 10 nc 0 12345", script);
  const std::vector<std::string> cmd = {"/bin/sh", "-c", marvellCmd};
  std::string interfaceCountersOut{};
  folly::Subprocess proc(cmd, folly::Subprocess::Options().pipeStdout());
  folly::readFile(proc.stdoutFd(), interfaceCountersOut);
  auto ret = proc.wait();
  if (!ret.exited() || ret.exitStatus() != 0) {
    LOG(ERROR) << "Failed to execute marvell command " << script;
    return false;
  }
  if (!folly::writeFile(interfaceCountersOut, outputFile.c_str())) {
    LOG(ERROR) << "Failed to write marvell command to " << outputFile;
    return false;
  }
  return true;
}

MarvellInterfaceStatsList
MarvellUtils::getInterfaceStatsFromString(
    const std::string& output, const std::regex& regex) {
  std::vector<std::string> outputLines;
  folly::split("\n", output, outputLines);

  // match script output with regex by line
  MarvellInterfaceStatsList matchedLines;
  for (const auto& outputLine : outputLines) {
    std::smatch regexMatch;
    if (std::regex_search(outputLine, regexMatch, regex)) {
      // regexMatch contains pointers to temporary strings,
      // so we have to create explicit copies of them to return
      std::vector<std::string> stringMatches;
      for (std::size_t i = 0; i < regexMatch.size(); i++) {
        stringMatches.emplace_back(regexMatch.str(i));
      }
      matchedLines.push_back(stringMatches);
    }
  }
  return matchedLines;
}

std::optional<MarvellInterfaceStatsList>
MarvellUtils::getInterfaceStatsFromScript(
    const std::string& outputFile, const std::regex& regex) {

  // read and parse cli output
  std::string scriptOutput;
  if (!folly::readFile(outputFile.c_str(), scriptOutput)) {
    LOG(ERROR) << "Failed getting marvell stats from " << outputFile;
    return std::nullopt;
  }
  return getInterfaceStatsFromString(scriptOutput, regex);
}

bool
MarvellUtils::updateInterfaceCounters() {
  return outputCommandToFile(COUNTERS_CMD, COUNTERS_FILE);
}

std::optional<MarvellInterfaceStatsList>
MarvellUtils::getInterfaceCounters() {
  return getInterfaceStatsFromScript(COUNTERS_FILE, COUNTERS_REGEX);
}

bool
MarvellUtils::updateInterfaceStatus() {
  return outputCommandToFile(STATUS_CMD, STATUS_FILE);
}

std::optional<MarvellInterfaceStatsList>
MarvellUtils::getInterfaceStatus() {
  return getInterfaceStatsFromScript(STATUS_FILE, STATUS_REGEX);
}

} // namespace terragraph
} // namespace facebook
