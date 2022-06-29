/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqTimeout.h>

#include "MinionApp.h"
#include "e2e/common/ConfigMetadata.h"
#include "e2e/common/Consts.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/FwOptParams_types.h"

namespace facebook {
namespace terragraph {
namespace minion {

/**
 * App that handles node configuration.
 *
 * This app primarily communicates with a separate ConfigApp on the E2E
 * controller.
 */
class ConfigApp final : public MinionApp {
 public:
  /**
   * Constructor.
   *
   * This will load the config metadata file from disk.
   *
   * @param zmqContext the ZMQ context
   * @param brokerRouterUrl the broker address for the E2E minion
   * @param monitorSockUrl the ZmqMonitor address for the E2E minion
   * @param macAddr our MAC address
   */
  ConfigApp(
      fbzmq::Context& zmqContext,
      const std::string& brokerRouterUrl,
      const std::string& monitorSockUrl,
      const std::string& macAddr);

  void processMessage(
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

 private:
  /** Process a request to retrieve the current node config. */
  void processConfigGetReq(const std::string& senderApp) noexcept;
  /** Process a request to set new node config. */
  void processConfigSetReq(
      const std::string& senderApp, const thrift::Message& message) noexcept;
  /**
   * Process a request for the post-config actions associated with a new config
   * blob.
   */
  void processGetMinionConfigActionsReq(
      const std::string& senderApp, const thrift::Message& message) noexcept;
  /** Process a request to set firmware log config dynamically. */
  void processSetFwLogConfig(
      const std::string& senderApp, const thrift::Message& message) noexcept;
  /** Process a request for this minion's base configs. */
  void processGetMinionBaseConfig(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /**
   * Look up the hardware config type associated with a hardware board ID in the
   * given file.
   */
  std::optional<std::string> getHardwareConfigType(
      const std::string& hwConfigTypesFile, const std::string& hwBoardId);

  /** Performs the given node actions. */
  void performNodeActions(
      const std::unordered_map<thrift::CfgAction, std::vector<std::string>>&
          nodeActions,
      int64_t bwgdIdx);

  /**
   * Dynamically change firmware parameters for the given keys.
   *
   * Returns false if this operation is not possible.
   *
   * @param keys the list of period-delimited config keys
   * @param bwgdIdx if given, changes will be scheduled for this BWGD index
   */
  bool setFwParams(
      const std::vector<std::string>& keys, std::optional<int64_t> bwgdIdx);

  /** Check if config update affects any links or radios. */
  bool affectsLinksOrRadios(const std::vector<std::string>& keys);

  /** Build firmware config requests for each radio. */
  std::unordered_map<std::string, thrift::FwConfigParams> getFwParamsForConfig(
      const std::vector<std::string>& keys, std::optional<int64_t> bwgdIdx);

  /**
   * Returns the value of the given firmware parameter, or std::nullopt if it
   * does not exist.
   */
  std::optional<int64_t> getFwParam(
      const std::string& key, const thrift::FwOptParams& fwParams);

  /** Dynamically change the airtime allocation parameters. */
  void setAirtimeParams();

  /**
   * Write the rsyslog config from the node configuration to the given file,
   * or deletes the given file if logging is disabled.
   *
   * Returns true if a write/delete occurred, or false otherwise.
   */
  bool writeRsyslogConfig(
      const std::string& outputDir, const std::string& outputFile);

  /** Update glog's VLOG level to the value in the node configuration. */
  void updateVlogLevel();

  /**
   * Call the tzdata settz init script to ensure we have configured TIMEZONE
   * set.
   */
  void symlinkTimezone();

  /** Timer to start delayed actions after receiving new config. */
  std::unique_ptr<fbzmq::ZmqTimeout> delayedActionTimer_{nullptr};

  /** The config metadata object. */
  std::unique_ptr<ConfigMetadata> configMeta_;
};

} // namespace minion
} // namespace terragraph
} // namespace facebook
