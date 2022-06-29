/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "TailAgent.h"

#include <memory>
#include <string>
#include <vector>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqTimeout.h>

#include "e2e/common/NodeConfigWrapper.h"

namespace facebook {
namespace terragraph {
namespace stats {

class LogTailer : public fbzmq::ZmqEventLoop {
 public:
  LogTailer(
      const std::string& macAddr,
      const std::string& configFileName,
      const std::string& markerSuffix,
      int32_t submissionInterval,
      uint32_t bufferSize);

 protected:
  std::string macAddr_;

  // Size of buffer holding file tail data
  uint32_t bufferSize_;

  // Log sources to tail
  std::unordered_map<std::string /* source name */, TailAgent> tailAgents_{};

  // Internal buffer for unprocessed log lines
  std::vector<std::string> logsBuffer_{};

  // LogTail params read from the node config wrapper
  std::shared_ptr<const thrift::LogTailParams> logTailParams_{nullptr};

 private:
  // ZmqTimeout for performing periodic submission
  std::unique_ptr<fbzmq::ZmqTimeout> periodicTimer_{nullptr};

  virtual void monitor() noexcept = 0;
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
