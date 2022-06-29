/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>
#include <vector>
#include <string>

namespace facebook {
namespace terragraph {

/**
 * Wrapper class for accessing a Terragraph node info file.
 */
class NodeInfoWrapper {
 public:
  /**
   * Empty constructor. The caller is expected to invoke setNodeInfoFile()
   * before using any other methods.
   */
  NodeInfoWrapper();

  /** Construct NodeInfoWrapper by reading the given info file. */
  explicit NodeInfoWrapper(const std::string& nodeInfoFile);

  /** Set the node info file to read from and write to. */
  void setNodeInfoFile(const std::string& nodeInfoFile);

  /**
   * Initialize (or re-initialize) all internal state by reading the original
   * info file.
   */
  void readNodeInfoFile();

  /** Check if a WLAN MAC address exists. */
  bool wlanMacExists(const std::string& mac) const;

  /** Get the node ID (a MAC address). */
  std::optional<std::string> getNodeId() const;

  /**
   * Get the bus ID for the given WLAN MAC address.
   *
   * Returns std::nullopt if the MAC address is not found.
   */
  std::optional<std::string> getBusId(const std::string& mac) const;

  /** Get a mapping of all WLAN MACs to bus IDs. */
  std::unordered_map<std::string, std::string> getRadioMacToBusId() const;

  /** Get the hardware model string. */
  std::optional<std::string> getHwModel() const;

  /** Get the hardware board identifier string. */
  std::optional<std::string> getHwBoardId() const;

  /** Get the PCI order string. */
  std::optional<std::string> getPciOrder() const;

  /** Check if we are operating in IF2IF mode. */
  bool isIf2if() const;

 private:
  /** Location of node info file. */
  std::string nodeInfoFile_;

  /** Node info key/value map. */
  std::unordered_map<std::string, std::string> nodeInfoStore_;

  /** Map of WLAN MACs to bus IDs. */
  std::unordered_map<std::string, std::string> wlanMac2BusId_;
};

} // namespace terragraph
} // namespace facebook
