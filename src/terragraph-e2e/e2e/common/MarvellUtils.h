/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <regex>
#include <string>
#include <unordered_map>
#include <vector>


#include "e2e/if/gen-cpp2/DriverMessage_types.h"

namespace facebook {
namespace terragraph {

typedef std::vector<std::vector<std::string>> MarvellInterfaceStatsList;

/**
 * Marvell-specific utilities.
 */
class MarvellUtils {
 public:
  // TODO: document this file
  /** \{ */

  static std::string formatPortIdToName(const std::string& portId);

  static bool outputCommandToFile(
    const std::string& script, const std::string& outputFile);

  static MarvellInterfaceStatsList getInterfaceStatsFromString(
      const std::string& output, const std::regex& regex);

  static std::optional<MarvellInterfaceStatsList> getInterfaceStatsFromScript(
      const std::string& outputFile, const std::regex& regex);

  static bool updateInterfaceCounters();

  static bool updateInterfaceStatus();

  static std::optional<MarvellInterfaceStatsList> getInterfaceCounters();

  static std::optional<MarvellInterfaceStatsList> getInterfaceStatus();

  /** \} **/
};

} // namespace terragraph
} // namespace facebook
