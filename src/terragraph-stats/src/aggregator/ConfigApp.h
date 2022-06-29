/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "AggrApp.h"

#include <fbzmq/zmq/Zmq.h>

#include "e2e/common/ConfigMetadata.h"
#include "e2e/common/E2EConfigWrapper.h"
#include "stats/if/gen-cpp2/Aggregator_types.h"

namespace facebook {
namespace terragraph {
namespace stats {

/*
 *  ConfigApp manages NMS aggregator config.
 */
class ConfigApp final : public AggrApp {
 public:
  ConfigApp(
      fbzmq::Context& context,
      const std::string& routerSockUrl,
      pid_t aggregatorPid);

 private:
  // from AggrApp
  void processMessage(
      const std::string& agent,
      const std::string& senderApp,
      const thrift::AggrMessage& message) noexcept override;

  // Process a request to get the aggregator config
  void processGetAggrConfigReq(const std::string& senderApp);
  // Process a request to set the aggregator config
  void processSetAggrConfigReq(
      const std::string& senderApp, const thrift::AggrMessage& message);
  // Process a request to get the aggregator config metadata
  void processGetAggrConfigMetadataReq(const std::string& senderApp);

  // Performs the given aggregator config actions.
  void performAggregatorConfigActions(
      const std::unordered_map<thrift::CfgAction, std::vector<std::string>>&
          aggregatorActions);

  fbzmq::Context& context_;

  // Config metadata
  std::unique_ptr<ConfigMetadata> aggregatorConfigMeta_;

  // The process ID of the aggregator
  pid_t aggregatorPid_;
};

} // namesapce stats
} // namespace terragraph
} // namespace facebook
