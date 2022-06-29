/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "SysUtils.h"

#include <cstdlib>
#include <sstream>
#include <sys/stat.h>

#include <boost/filesystem.hpp>
#include <glog/logging.h>

namespace facebook {
namespace terragraph {

namespace {
const int kErr{-1};
}

int
SysUtils::system(const std::string& command) {
  const int status = std::system(command.c_str());

  if (status < 0) {
    LOG(ERROR) << "std::system(" << command << ") failed. status " << status;
    return kErr;
  }

  if (!WIFEXITED(status)) {
    std::stringstream err;
    err << "std::system(" << command << ") abnormal exit.";
    if (WIFSIGNALED(status)) {
      err << " Killed by signal " << WTERMSIG(status);
    }
    LOG(ERROR) << err.str();
    return kErr;
  }

  return WEXITSTATUS(status);
}

bool
SysUtils::system(const std::string& command, int expectedExitStatus) {
  CHECK(expectedExitStatus >= 0) << "invalid expectedExitStatus";

  const int status = SysUtils::system(command);

  if (status != kErr && status != expectedExitStatus) {
    LOG(ERROR) << "std::system(" << command << ") bad exit status " << status;
  }

  return status == expectedExitStatus;
}

folly::Expected<std::string, folly::ProcessReturnCode>
SysUtils::runCommand(const std::vector<std::string>& command) {
  try {
    // Create subprocess to run command, capture stdout
    folly::Subprocess proc(
        command,
        folly::Subprocess::Options().usePath().pipeStdout());
    std::string output = proc.communicate().first;
    folly::ProcessReturnCode returnCode = proc.wait();

    if (returnCode.exitStatus() == 0) {
      return output;
    }
    return folly::makeUnexpected(returnCode);
  } catch (const folly::SubprocessSpawnError&) {
    return folly::makeUnexpected(folly::ProcessReturnCode::makeNotStarted());
  }
}

// Helper to traverse all entries of directory
std::vector<std::pair<std::string, std::string>>
SysUtils::getEntriesFromDirectory(
    const std::string& directory,
    const std::string extension,
    bool getDirectories) {
  std::vector<std::pair<std::string, std::string>> entries;

  boost::filesystem::path dir(directory);
  if (!boost::filesystem::is_directory(dir)) {
    // Invalid directory arg, return nothing
    LOG(ERROR) << "Could not find directory: " << directory;
    return entries;
  }

  for (const auto& entry : boost::filesystem::directory_iterator(dir)) {
    std::string filename = entry.path().filename().string();
    std::string pathname = entry.path().string();
    if (getDirectories) {
      if (!boost::filesystem::is_directory(
              boost::filesystem::canonical(entry))) {
        continue;
      }
      if (filename == "." || filename == "..") {
        continue;
      }
      // entry is a directory or link to a directory that is not the current or
      // parent directory
      entries.push_back(std::make_pair(pathname, filename));
    } else {
      if (!boost::filesystem::is_regular_file(entry)) {
        continue;
      }
      if (extension.size() > filename.size() ||
          (filename.compare(
               filename.size() - extension.size(),
               extension.size(),
               extension) != 0)) {
        continue;
      }
      // entry is a file that ends with the given extension
      entries.push_back(std::make_pair(pathname, filename));
    }
  }

  return entries;
}

std::vector<std::pair<std::string, std::string>>
SysUtils::findFilesInDirectory(
    const std::string& directory, const std::string& extension) {
  return getEntriesFromDirectory(directory, extension, false);
}

std::vector<std::pair<std::string, std::string>>
SysUtils::findSubdirectoriesInDirectory(const std::string& directory) {
  return getEntriesFromDirectory(directory, "", true);
}

} // namespace terragraph
} // namespace facebook
