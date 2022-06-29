/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>
#include <string>

namespace facebook {
namespace terragraph {
namespace stats {

class NMSConsts {
 public:
  // zmq identity of the broker on nms aggregator
  const static std::string kBrokerAggrId;
  const static std::string kConfigAppAggrId;
  const static std::string kStatusAppAggrId;
  const static std::string kStatsAppAggrId;

  // --- Stats Agent ---
  // zmq identity of the nms publisher
  const static std::string kNmsPublisherId;
  const static std::string kInputListenerId;
  const static std::string kFibAgentHost;
  const static int kFibAgentClientId;

  // --- LogTail ---
  // zmq identity of LogPublisher
  const static std::string kLogPublisherId;

  // --- Timeouts ---
  const static std::chrono::milliseconds kPollTimeout;
  const static std::chrono::milliseconds kReadTimeout;
  const static std::chrono::milliseconds kFibAgentProcTimeout;
  const static std::chrono::milliseconds kFibAgentConnTimeout;

  // --- Socket Option ---
  // DSCP = 48 (first 6 bits), ECN = 0 (last 2 bits). Total 192
  const static int kIpTos;
};
} // namespace stats
} // namespace terragraph
} // namespace facebook
