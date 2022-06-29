/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <folly/MPMCQueue.h>
#include <folly/dynamic.h>

namespace facebook {
namespace terragraph {
namespace api {

/**
 * Non-blocking audit log manager that will record network changes to a file
 */
class AuditLogger {
 public:
  explicit AuditLogger(const std::string& path, uint32_t bufferSize);

  ~AuditLogger();

  // Send network change message to audit logger
  void logNetworkChange(folly::dynamic msg);

 private:
  // Path to access log file
  std::string path_;

  // Thread that handles file I/O
  std::thread worker_;

  // Thread safe buffer for worker thread to process logs from
  folly::MPMCQueue<folly::dynamic> buffer_;

  // Atomic bool to trigger worker thread shut-down
  std::atomic_bool keepAlive_{true};
};

} // namespace api
} // namespace terragraph
} // namespace facebook
