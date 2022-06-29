/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Progress.h"

#include <folly/Format.h>

#include <glog/logging.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

DEFINE_string(
    progress_file_directory,
    "/var/volatile/progress",
    "Directory for the progress reporting utility, used by the watchdog "
    "(empty to disable)");

namespace facebook {
namespace terragraph {

void
Progress::logErr(const char* syscall, const std::string& progressFile) const {
  if (logErrors_) {
    logErrors_ = false;
    LOG(ERROR) << "ERROR: " << syscall << " " << progressFile << ": "
               << folly::errnoStr(errno);
  }
}

void
Progress::report(const std::string& file) const {
  if (FLAGS_progress_file_directory.empty()) {
    return; // disabled
  }

  // Construct full file path
  std::string progressFile = FLAGS_progress_file_directory;
  if (progressFile.back() != '/') {
    progressFile += '/';
  }
  progressFile += file;

  // Check for file existence (first time only)
  auto fileIter = files_.find(file);
  if (fileIter == files_.end()) {
    struct stat statbuf;
    if (stat(progressFile.c_str(), &statbuf) != 0) {
      if (errno != ENOENT) {
        logErr("stat", progressFile);
        return; // unexpected stat error
      }
      LOG(INFO) << "Creating progress file: " << progressFile;
      FILE* fp = fopen(progressFile.c_str(), "w");
      if (fp == 0) {
        logErr("fopen", progressFile);
        files_[file] = false;
        return;
      }
      if (fclose(fp) != 0) {
        logErr("fclose", progressFile);
        files_[file] = false;
        return;
      }
    }
    files_[file] = true; // Don't stat this progress file again.
  } else if (fileIter->second == false) {
    return; // File could not be created, don't keep trying
  }

  struct timespec times[2]; // [0] -last access, [1] -last modification
  if (clock_gettime(CLOCK_MONOTONIC_RAW, times) != 0) {
    logErr("clock_gettime", progressFile);
  } else {
    times[1] = times[0];
    if (utimensat(AT_FDCWD, progressFile.c_str(), times, 0) != 0) {
      logErr("utimensat", progressFile);
    }
  }
}

} // namespace terragraph
} // namespace facebook
