/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "WatchdogUtils.h"

#include <iostream>
#include <unordered_map>

#include <folly/Format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "SysUtils.h"

DEFINE_string(
    watchdog_path,
    "/etc/init.d/watchdog.sh",
    "The path to the watchdog.sh script file (empty to disable)");

namespace ft = ::facebook::terragraph;

namespace {
// TG watchdog disable parameters.
// Note that the prepare upgrade step needs wdog-free time to download the
// image and also to physically flash it. The latter can take several
// minutes when the secondary partition needs to be scrubbed and the
// image write retried.
const std::unordered_map<ft::thrift::UpgradeReqType, int> kWdogDisableMinutes =
    {{ft::thrift::UpgradeReqType::PREPARE_UPGRADE, 15},
     {ft::thrift::UpgradeReqType::COMMIT_UPGRADE, 5},
     {ft::thrift::UpgradeReqType::RESET_STATUS, 2}};
} // namespace

namespace facebook {
namespace terragraph {

bool
WatchdogUtils::disable(int disableSec) {
  if (FLAGS_watchdog_path.empty()) {
    return true; // disabled
  }

  const std::string disableCmd = folly::sformat(
      "{} dis {}",
      FLAGS_watchdog_path,
      disableSec);
  return SysUtils::system(disableCmd, 0);
}

bool
WatchdogUtils::disableForUpgrade(const thrift::UpgradeReqType& upgradeReqType) {
  if (FLAGS_watchdog_path.empty()) {
    return true; // disabled
  }

  auto wdogDisableMinutes = kWdogDisableMinutes.find(upgradeReqType);
  if (wdogDisableMinutes != kWdogDisableMinutes.end()) {
    return WatchdogUtils::disable(wdogDisableMinutes->second);
  }

  return true;
}

bool
WatchdogUtils::preventConfigFallbackOnReboot() {
  if (FLAGS_watchdog_path.empty()) {
    return true; // disabled
  }

  const std::string preventConfigFallbackOnRebootCmd = folly::sformat(
      "{} prevent_config_fallback_on_reboot >/dev/null 2>/dev/null",
      FLAGS_watchdog_path);
  return SysUtils::system(preventConfigFallbackOnRebootCmd, 0);
}

bool
WatchdogUtils::setConfigFallbackFile(const std::string& nodeConfigFile) {
  if (FLAGS_watchdog_path.empty()) {
    return true; // disabled
  }

  const std::string setConfigFallbackFileCmd = folly::sformat(
      "{} set_config_fallback_file {} >/dev/null 2>/dev/null",
      FLAGS_watchdog_path,
      nodeConfigFile);
  return SysUtils::system(setConfigFallbackFileCmd, 0);
}

bool
WatchdogUtils::startConfigFallbackMonitor() {
  if (FLAGS_watchdog_path.empty()) {
    return true; // disabled
  }

  const std::string startConfigFallbackMonitorCmd = folly::sformat(
      "{} start_config_fallback_monitor >/dev/null 2>/dev/null",
      FLAGS_watchdog_path);
  return SysUtils::system(startConfigFallbackMonitorCmd, 0);
}

} // namespace terragraph
} // namespace facebook
