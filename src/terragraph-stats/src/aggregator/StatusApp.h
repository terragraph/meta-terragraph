/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "AggrApp.h"

#include <fbzmq/zmq/Zmq.h>

#include "stats/if/gen-cpp2/Aggregator_types.h"

namespace facebook {
namespace terragraph {
namespace stats {

/*
 *  StatusApp currently only reports the aggregator software version.
 */
class StatusApp final : public AggrApp {
 public:
  StatusApp(fbzmq::Context& context, const std::string& routerSockUrl);

 private:
  // from AggrApp
  void processMessage(
      const std::string& agent,
      const std::string& senderApp,
      const thrift::AggrMessage& message) noexcept override;

  // Process a request for status dump
  void processGetStatusDump(
      const std::string& senderApp, const thrift::AggrMessage& message);

  // Process an "is alive" health check of the aggregator
  void processIsAlive(const std::string& senderApp);

  fbzmq::Context& context_;

  // The aggregator software version string
  std::string version_;
};

} // namesapce stats
} // namespace terragraph
} // namespace facebook
