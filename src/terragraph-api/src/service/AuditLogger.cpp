/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "AuditLogger.h"

#include <fstream>

#include <boost/filesystem.hpp>
#include <folly/Format.h>
#include <folly/String.h>
#include <folly/json.h>

namespace fs = boost::filesystem;

namespace facebook {
namespace terragraph {
namespace api {

AuditLogger::AuditLogger(const std::string& path, uint32_t bufferSize)
    : path_{path}, buffer_(bufferSize) {
  worker_ = std::thread([this]() {
    std::ofstream logStream;
    // Create log file with any non-existing parent directories
    try {
      fs::path logFilePath(this->path_);
      fs::create_directories(logFilePath.parent_path());
      logStream =
          std::ofstream(this->path_, std::ofstream::out | std::ofstream::app);
      fs::permissions(
          logFilePath, fs::owner_all | fs::group_read | fs::others_read);
    } catch (std::runtime_error& e) {
      LOG(FATAL) << "Failed to create log file " << this->path_
                 << ": " << e.what();
    }
    if (!logStream.good()) {
      LOG(FATAL) << "Unable to open file " << this->path_ << " for writing";
    }

    while (this->keepAlive_) {
      folly::dynamic msg;
      this->buffer_.blockingRead(msg);
      if (msg.empty()) {
        // Treat empty message as kill signifier and return from worker thread
        this->keepAlive_ = false;
        return;
      }
      if (!logStream.good()) {
        LOG(ERROR)
            << "Could not successfully write to audit log file, retrying";
        logStream =
            std::ofstream(this->path_, std::ofstream::out | std::ofstream::app);
        if (!logStream.good()) {
          LOG(FATAL) << "Unable to get new file descriptor after retry";
        }
      }
      logStream << folly::toJson(msg) << std::endl;
    }
  });
}

AuditLogger::~AuditLogger() {
  keepAlive_ = false;
  // Write empty object to buffer as kill signifier
  buffer_.write(folly::dynamic::object());
  if (worker_.joinable()) {
    worker_.join();
  }
}

void
AuditLogger::logNetworkChange(folly::dynamic msg) {
  if (msg.empty()) {
    LOG(WARNING) << "Refusing to send empty message to audit logger";
    return;
  }
  if (keepAlive_) {
    buffer_.write(std::move(msg));
  }
}

} // namespace api
} // namespace terragraph
} // namespace facebook
