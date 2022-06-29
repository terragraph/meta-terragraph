/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <gflags/gflags.h>

#include <string>
#include <unordered_map>

namespace facebook {
namespace terragraph {

/**
 * Progress reporting utility, used by the watchdog.
 */
class Progress {
 public:
  /**
   * Report progress by touching the specified file with a monotonic timestamp.
   *
   * Creates the file if necessary.
   *
   * Note:
   * - The watchdog needs to be aware that progress files created here may
   *   briefly have a non-monotonic timestamp.
   * - Watchdog will take action if report() keeps failing.
   * - The watchdog monitoring scripts and report() both use the
   *   same clock: CLOCK_MONOTONIC_RAW. See also: monotonic-touch app.
   */
  void report(const std::string& file) const;

 private:
  /** Log the first system call failure. */
  void logErr(const char* syscall, const std::string& progressFile) const;
  /** Whether to log errors (set to "false" after first logErr() call). */
  mutable bool logErrors_{true};

  /**
   * Progress files, added via report().
   *
   * Each file is mapped to "true" if the file exists, or "false" if it could
   * not be created (and we should not keep trying to create it).
   */
  mutable std::unordered_map<std::string, bool> files_;
};

} // namespace terragraph
} // namespace facebook
