/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "LogTailer.h"

#include <chrono>

#include <folly/Format.h>

using std::string;

namespace facebook {
namespace terragraph {
namespace stats {

LogTailer::LogTailer(
    const string& macAddr,
    const string& configFileName,
    const string& markerSuffix,
    int32_t submissionInterval,
    uint32_t bufferSize)
    : macAddr_(macAddr), bufferSize_(bufferSize) {

  logsBuffer_.reserve(bufferSize);

  NodeConfigWrapper nodeConfigWrapper(configFileName);
  logTailParams_ = nodeConfigWrapper.getLogTailParams();
  for (const auto& sourcesIt : logTailParams_->sources) {
    if (sourcesIt.second.enabled) {
      string source = sourcesIt.first;
      string filename = sourcesIt.second.filename;

      LOG(INFO)
          << folly::format("Tailing file '{}' to source {}", filename, source);

      tailAgents_.emplace(source, TailAgent(filename, markerSuffix));
    }
  }

  const bool makePeriodic = true;
  periodicTimer_ = fbzmq::ZmqTimeout::make(this, [&]() noexcept { monitor(); });
  periodicTimer_->scheduleTimeout(
      std::chrono::seconds(submissionInterval), makePeriodic);
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
