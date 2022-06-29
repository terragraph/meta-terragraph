/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>

#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {

/**
 * Utility to encapsulate interactions with the Terragraph watchdog.
 */
class WatchdogUtils {
 public:
  /** Disable watchdog for a given time length, in seconds */
  static bool disable(int disableSec);

  /** Disable watchdog for a predefined time, based on UpgradeReqType */
  static bool disableForUpgrade(const thrift::UpgradeReqType& upgradeReqType);

  /** Prevent Watchdog config from fallbacking on reboot */
  static bool preventConfigFallbackOnReboot();

  /** Designate the current, active config as the fallback config */
  static bool setConfigFallbackFile(const std::string& nodeConfigFile);

  /** Start config fallback monitoring */
  static bool startConfigFallbackMonitor();
};

} // namespace terragraph
} // namespace facebook
