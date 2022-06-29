/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>
#include <vector>

class TerragraphNode {
 public:
  TerragraphNode();

  bool reboot() const;

  bool swapPrimaryAndSecondaryPartitions() const;

  // Extract SNR and RSSI from f/w stats
  void getLinkStats(
      int timeoutSec,
      const std::string& initiatorMac,
      const std::string& responderMac,
      std::vector<int>& snr,
      std::vector<int>& rssi);

  // Create a link
  bool assoc(const std::string& initiatorMac, const std::string& responderMac);

  // Destroy a link
  bool dissoc(const std::string& initiatorMac, const std::string& responderMac);

  // Send a command for execution
  static std::string exec(const char* cmd);

  // Send a command for execution and split output by lines
  static std::vector<std::string> sendCommand(const std::string& command);

  std::string osVersion;
  std::string secondaryOsVersion;
  std::string dtbVersion;
  std::string ubootVersion;
  std::string nic0Mac;
  std::string hostMac;
  std::vector<std::string> radioMac;

  // Wait times for snr and rssi samples to show up in f/w stats.
  // Roughly the number of samples in LinkStats messages.
  static const int minLinkStatsTimeoutSec{2};
  static const int maxLinkStatsTimeoutSec{20};
  static const int defaultLinkStatsTimeoutSec{3};
};
