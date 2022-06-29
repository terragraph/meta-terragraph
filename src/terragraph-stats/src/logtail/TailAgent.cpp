/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TailAgent.h"

#include <folly/ExceptionString.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/dynamic.h>
#include <folly/json.h>
#include <gflags/gflags.h>
#include <sys/stat.h>

DEFINE_uint32(rotation_check_count, 10, "When to check for file rotation");

using std::string;

namespace facebook {
namespace terragraph {
namespace stats {

TailAgent::TailAgent(const string& filename, const string& markerSuffix)
    : filename_(filename),
      staleCount_(0),
      markerPosition_(0),
      markerFilename_(filename + markerSuffix),
      markerInode_(0) {}

void
TailAgent::fetchLogLines(std::vector<string>& logsBuffer, uint32_t logCount) {
  VLOG(4) << folly::format(
      "Processing '{}', staleCount: {}", filename_, staleCount_);

  if (staleCount_ <= 0) {
    struct stat currentStat;
    staleCount_ = FLAGS_rotation_check_count;

    // Open file and get file stat
    if (fileIfs_.is_open()) {
      VLOG(4) << "File was not updated for a while. Closing file ...";
      fileIfs_.close();
    }

    if (stat(filename_.c_str(), &currentStat) != 0) {
      // File does not exist
      VLOG(4) << folly::format("File '{}' does not exist", filename_);
      return;
    }

    VLOG(4) << folly::format("Opening file '{}'", filename_);
    fileIfs_.open(filename_);

    if (readMarkerFile() && markerInode_ == currentStat.st_ino) {
      // Same File. Seek to marker position
      fileIfs_.seekg(markerPosition_);
      VLOG(4) << "Opened same file as tracked >> seek to " << markerPosition_;
    } else {
      // Different file, or never tracked
      VLOG(4) << folly::format(
          "Opened different file than tracked. Inode (Tracked, New) ({}, {})",
          markerInode_,
          currentStat.st_ino);
      markerInode_ = currentStat.st_ino;
      markerPosition_ = 0;
    }
  }

  string line;
  while (logsBuffer.size() < logCount) {
    auto line = getNextLine();

    if (line.length() > 0) {
      // update pos
      markerPosition_ = fileIfs_.tellg();

      logsBuffer.push_back(line);

      // reset stale counter
      staleCount_ = FLAGS_rotation_check_count;
    } else {
      staleCount_--;
      break;
    }
  }

  writeMarkerFile();
}

bool
TailAgent::readMarkerFile() {
  string contents;
  if (!folly::readFile(markerFilename_.c_str(), contents)) {
    LOG(INFO)
        << folly::format("Could not read marker file '{}'", markerFilename_);
    return false;
  }

  folly::dynamic markerFileData = folly::dynamic::object;
  try {
    markerFileData = folly::parseJson(contents);
  } catch (const std::runtime_error& ex) {
    LOG(ERROR) << "Could not parse marker file struct: "
               << folly::exceptionStr(ex);
    return false;
  }

  if (markerFileData.isObject()) {
    if (markerFileData.count("inode") && markerFileData.count("position")) {
      markerInode_ = markerFileData.at("inode").getInt();
      markerPosition_ = markerFileData.at("position").getInt();
    } else {
      return false;
    }
  }

  return true;
}

bool
TailAgent::writeMarkerFile() {
  auto contents = folly::toJson(folly::dynamic::object("inode", markerInode_)(
      "position", markerPosition_));

  if (!folly::writeFile(contents, markerFilename_.c_str())) {
    LOG(INFO)
        << folly::format("Could not write marker file '{}'", markerFilename_);
    return false;
  }

  return true;
}

string
TailAgent::getNextLine() {
  string line;

  if (fileIfs_.is_open()) {
    std::getline(fileIfs_, line);
    if (fileIfs_.eof()) {
      fileIfs_.clear();
    }
  }

  return line;
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
