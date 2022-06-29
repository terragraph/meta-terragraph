/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <folly/Expected.h>
#include <folly/Subprocess.h>

namespace facebook {
namespace terragraph {

/**
 * System-related utilities.
 */
class SysUtils {
 public:
  /**
   * Execute the given command, and log all errors.
   *
   * Returns the exit status, or -1 on failure.
   */
  static int system(const std::string& command);

  /**
   * Execute the given command, and log all errors.
   *
   * Returns true only if the command returns the expected exit status.
   */
  static bool system(const std::string& command, int expectedExitStatus);

  /**
   * Run a command and return the output if successful (return code = 0), else
   * the return code itself.
   */
  static folly::Expected<std::string, folly::ProcessReturnCode> runCommand(
      const std::vector<std::string>& command);

  /**
   * Returns a vector of path and filename pairs within a directory that match
   * the given file extension.
   */
  static std::vector<std::pair<std::string, std::string>> findFilesInDirectory(
      const std::string& directory, const std::string& extension);

  /** Return a vector of path and subdirectory name pairs within a directory. */
  static std::vector<std::pair<std::string, std::string>>
      findSubdirectoriesInDirectory(const std::string& directory);

 private:
  /** Traverse entries of a directory. */
  static std::vector<std::pair<std::string, std::string>>
      getEntriesFromDirectory(
          const std::string& directory,
          const std::string extension,
          bool getDirectories);
};

} // namespace terragraph
} // namespace facebook
