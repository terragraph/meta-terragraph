/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "e2e/if/gen-cpp2/NodeConfig_types.h"

namespace facebook {
namespace terragraph {
namespace stats {

class TailAgent {
 public:
  TailAgent(const std::string& filename, const std::string& markerSuffix);

  // Fill "logsBuffer" with up to "logCount" lines
  void fetchLogLines(std::vector<std::string>& logsBuffer, uint32_t logCount);

 private:
  // Path + file name of the log source
  std::string filename_;

  // File stream for reading from the log file
  std::ifstream fileIfs_;

  // The number of log files that the thread is behind on analyzing
  int staleCount_;

  // Line number for how far we have read into the file belonging to
  // markerInode_
  int32_t markerPosition_;

  // Preset string to save the log file inode and position in case connection is
  // interrupted or the thread crashes
  std::string markerFilename_;

  // The serial number of the most recently accessed log file
  ino_t markerInode_;

  // Read the inode and position (line number) of the most recent log file
  bool readMarkerFile();

  // Update the inode and position in markerFilename_
  bool writeMarkerFile();

  // Read the next line from the file stream
  std::string getNextLine();
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
