/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "NodeInfoWrapper.h"

#include <regex>

#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/String.h>
#include <glog/logging.h>

namespace facebook {
namespace terragraph {

NodeInfoWrapper::NodeInfoWrapper() {
}

NodeInfoWrapper::NodeInfoWrapper(const std::string& nodeInfoFile)
    : nodeInfoFile_(nodeInfoFile) {
  readNodeInfoFile();
}

void
NodeInfoWrapper::setNodeInfoFile(const std::string& nodeInfoFile) {
  nodeInfoFile_ = nodeInfoFile;
  readNodeInfoFile();
}

void
NodeInfoWrapper::readNodeInfoFile() {
  // Read info file
  std::string contents;
  if (!folly::readFile(nodeInfoFile_.c_str(), contents)) {
    LOG(ERROR) << "Could not read node info file "
               << nodeInfoFile_;
    return;
  }

  // Temp struct to store bus ID corresponding to radio MAC
  struct BusProps {
    std::string busId;
    std::string macAddr;
  };
  std::unordered_map<size_t, BusProps> busMap;
  std::regex re("(MAC|BUS)_([0-9]+)");

  // Iterate over lines...
  std::vector<std::string> lines;
  folly::split("\n", contents, lines);
  for (const std::string& line : lines) {
    // Parse the line
    std::vector<std::string> tokens;
    folly::split("=", line, tokens);
    if (tokens.size() < 2) {
      continue;
    }
    std::string key = folly::trimWhitespace(tokens[0]).str();
    std::string value = folly::trimWhitespace(tokens[1]).str();
    // Remove all double-quote characters
    value.erase(remove(value.begin(), value.end(), '\"' ), value.end());

    // Store this pair
    nodeInfoStore_[key] = value;

    // Handle fields we care about
    std::smatch match;
    if (std::regex_match(key, match, re)) {
      BusProps& busProps = busMap[folly::to<int>(match.str(2))];
      if (match.str(1) == "BUS") {
        busProps.busId = value;
      } else {
        busProps.macAddr = value;
      }
    }
  }
  for (const auto& kv : busMap) {
    wlanMac2BusId_[kv.second.macAddr] = kv.second.busId;
  }
}

std::optional<std::string>
NodeInfoWrapper::getNodeId() const {
  auto nodeIdIt = nodeInfoStore_.find("NODE_ID");
  if (nodeIdIt != nodeInfoStore_.end()) {
    return nodeIdIt->second;
  }
  return std::nullopt;
}

bool
NodeInfoWrapper::wlanMacExists(const std::string& mac) const {
  return wlanMac2BusId_.count(mac) ? true : false;
}

std::unordered_map<std::string, std::string>
NodeInfoWrapper::getRadioMacToBusId() const {
  return wlanMac2BusId_;
}

std::optional<std::string>
NodeInfoWrapper::getBusId(const std::string& mac) const {
  auto iter = wlanMac2BusId_.find(mac);
  if (iter == wlanMac2BusId_.end()) {
    return std::nullopt;
  }
  return iter->second;
}

std::optional<std::string>
NodeInfoWrapper::getHwModel() const {
  auto modelIt = nodeInfoStore_.find("HW_MODEL");
  if (modelIt != nodeInfoStore_.end()) {
    return modelIt->second;
  }
  return std::nullopt;
}

std::optional<std::string>
NodeInfoWrapper::getHwBoardId() const {
  auto boardIdIt = nodeInfoStore_.find("HW_BOARD_ID");
  if (boardIdIt != nodeInfoStore_.end()) {
    return boardIdIt->second;
  }
  return std::nullopt;
}

std::optional<std::string>
NodeInfoWrapper::getPciOrder() const {
  auto pciOrderIt = nodeInfoStore_.find("PCI_ORDER");
  if (pciOrderIt != nodeInfoStore_.end()) {
    return pciOrderIt->second;
  }
  return std::nullopt;
}

bool
NodeInfoWrapper::isIf2if() const {
  auto iter = nodeInfoStore_.find("TG_IF2IF");
  if (iter != nodeInfoStore_.end()) {
    return iter->second == "1";
  }
  return false;
}

} // namespace terragraph
} // namespace facebook
