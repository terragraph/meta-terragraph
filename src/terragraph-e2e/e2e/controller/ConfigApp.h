/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqTimeout.h>
#include <folly/String.h>
#include <folly/dynamic.h>

#include "ConfigHelper.h"
#include "CtrlApp.h"
#include "StatusApp.h"
#include "e2e/common/ConfigMetadata.h"
#include "e2e/common/Consts.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/NodeConfig_types.h"

namespace facebook {
namespace terragraph {

/**
 * App that handles node configuration.
 *
 * This app primarily communicates with a separate ConfigApp on the E2E minion.
 */
class ConfigApp final : public CtrlApp {
 public:
  /**
   * Constructor.
   *
   * This will load the controller config metadata file from disk.
   *
   * @param zmqContext the ZMQ context
   * @param routerSockUrl the broker address for the E2E controller
   * @param monitorSockUrl the ZmqMonitor address for the E2E controller
   * @param statusReportsInterval the interval at which to sync config with
   *                              minion status reports
   * @param topologyInfoSyncInterval the interval at which to sync network-wide
   *                                 topologyInfo config
   * @param controllerPid the process ID for the E2E controller
   */
  ConfigApp(
      fbzmq::Context& zmqContext,
      const std::string& routerSockUrl,
      const std::string& monitorSockUrl,
      std::chrono::seconds statusReportsInterval,
      std::chrono::seconds topologyInfoSyncInterval,
      pid_t controllerPid);

 private:
  // from CtrlApp
  void processMessage(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

  /** Process thrift::GetMinionConfigActionsResp. */
  void processGetMinionConfigActionsResp(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::GetCtrlConfigReq. */
  void processGetConfigReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::GetCtrlConfigNodeOverridesReq. */
  void processGetConfigNodeOverridesReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::SetCtrlConfigNodeOverridesReq. */
  void processSetConfigNodeOverridesReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::ModifyCtrlConfigNodeOverridesReq. */
  void processModifyConfigNodeOverridesReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::GetCtrlConfigNodeOverridesActionsReq. */
  void processGetConfigNodeOverridesActionsReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::GetCtrlConfigBaseReq. */
  void processGetConfigBaseReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::GetCtrlConfigFirmwareBaseReq. */
  void processGetConfigFirmwareBaseReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::GetCtrlConfigHardwareBaseReq. */
  void processGetConfigHardwareBaseReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::GetCtrlConfigNetworkOverridesReq. */
  void processGetConfigNetworkOverridesReq(const std::string& senderApp);
  /** Process thrift::SetCtrlConfigNetworkOverridesReq. */
  void processSetConfigNetworkOverridesReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::ModifyCtrlConfigNetworkOverridesReq. */
  void processModifyConfigNetworkOverridesReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::GetCtrlConfigNetworkOverridesActionsReq. */
  void processGetConfigNetworkOverridesActionsReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::GetCtrlConfigActionsResultsReq. */
  void processGetConfigActionsResultsReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::GetCtrlConfigMetadata. */
  void processGetConfigMetadata(const std::string& senderApp);
  /** Process thrift::GetCtrlControllerConfigReq. */
  void processGetControllerConfigReq(const std::string& senderApp);
  /** Process thrift::SetCtrlControllerConfigReq. */
  void processSetControllerConfigReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::GetCtrlControllerConfigMetadata. */
  void processGetControllerConfigMetadata(const std::string& senderApp);
  /** Process thrift::GetCtrlConfigAutoNodeOverridesReq. */
  void processGetConfigAutoNodeOverridesReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::ClearAutoNodeOverridesConfig. */
  void processClearAutoNodeOverridesConfig(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::GetCtrlConfigPathsReq. */
  void processGetCtrlConfigPathsReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::SetFwLogConfig. */
  void processSetFwLogConfig(
      const std::string& senderApp, const thrift::Message& message);

  /** Process thrift::EditNode from TopologyApp. */
  void processNodeChanged(
      const std::string& senderApp, const thrift::Message& message);

  /** Process a request for app data from BinaryStarApp. */
  void processBstarGetAppData(
      const std::string& senderApp, const thrift::Message& message);
  /** Process new app data from BinaryStarApp. */
  void processBstarAppData(
      const std::string& senderApp, const thrift::Message& message);

  /** Process thrift::TriggerPolarityOptimization. */
  void processTriggerPolarityOptimization(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::TriggerControlSuperframeOptimization. */
  void processTriggerControlSuperframeOptimization(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::TriggerGolayOptimization. */
  void processTriggerGolayOptimization(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::TriggerChannelOptimization. */
  void processTriggerChannelOptimization(
      const std::string& senderApp, const thrift::Message& message);

  /** Process thrift::MinionBaseConfig. */
  void processMinionBaseConfig(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process updating tunnel config. */
  void processUpdateTunnelConfig(
      const std::string& senderApp, const thrift::Message& message);

  /**
   * Read all minion status reports and update internal config state.
   *
   * If necessary, new config will be sent to nodes.
   */
  void syncWithStatusReports();

  /**
   * Update the network-wide topology info config with the latest information
   * about each node's neighbors.
   */
  void syncTopologyInfo();

  /**
   * Iterate over all minion status reports and return a set of all nodes
   * pending a configuration change.
   */
  std::unordered_set<std::string> getNodesPendingConfig(
      const std::unordered_map<std::string, StatusApp::StatusReport>&
          statusReports);

  /** Monitor the progress of a batch configuration change. */
  void monitorCurrentBatchRollout(
      const std::unordered_map<std::string, StatusApp::StatusReport>&
          statusReports);

  /**
   * Send a config actions request to node, using the given config state and
   * overrides.
   *
   * GetMinionConfigActionsReq is constructed with the given ID.
   */
  bool sendConfigActionsRequestToMinion(
      const std::string& id,
      const std::string& nodeName,
      const ConfigHelper::NodeConfigState& configState,
      const folly::dynamic& nodeConfig);

  /**
   * Build a config actions response to 'senderApp', generating a unique ID and
   * setting up the timeouts for it.
   */
  thrift::GetCtrlConfigOverridesActionsResp generateGetConfigActionsResp(
      const std::string& overrides);

  /**
   * Set the given controller config and performs any associated actions.
   *
   * Returns true if successful, otherwise puts failure cause in 'errorMsg' and
   * returns false.
   */
  bool setControllerConfig(
      const std::string& controllerConfig,
      bool fromBstarSync,
      std::string& errorMsg);

  /** Perform the given controller config actions. */
  void performControllerConfigActions(
      const std::unordered_map<thrift::CfgAction, std::vector<std::string>>&
          controllerActions);

  /**
   * Optimize the network-wide polarity.
   *
   * Returns true upon success or false upon failure, and fills 'errorMsgs' with
   * any errors that occurred.
   */
  bool performPolarityOptimization(
      bool clearUserPolarityConfig, std::vector<std::string>& errMsgs);

  /** Returns true if the network overrides were set successfully. */
  bool setNetworkOverrides(
      const std::string& overrides,
      const std::string& senderApp,
      bool overwrite);

  /**
   * Process config 'hooks' for a given node name.
   *
   * This allows modification of config layers when a user changes node
   * overrides.
   */
  void processConfigHooks(
      const std::string& nodeName, const folly::dynamic& newOverrides);

  /**
   * Process tunnel config hooks if a tunnelConfig node override is changed by
   * a user.
   */
  void processTunnelConfigHook(
      const std::string& nodeName,
      const folly::dynamic& newOverrides,
      const std::optional<std::string> ipv6Address = std::nullopt);

  /** Returns true if the node overrides were set successfully. */
  bool setNodeOverrides(
      const std::string& overrides,
      const std::string& senderApp,
      bool overwrite);

  /** The ZMQ `DEALER` socket connected to the stats agent input socket. */
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> agentSock_;

  /** Timer to sync internal config state with minion status reports. */
  std::unique_ptr<fbzmq::ZmqTimeout> statusReportsSyncTimeout_{nullptr};

  /** Timer to periodically sync the network-wide topologyInfo config. */
  std::unique_ptr<fbzmq::ZmqTimeout> topologyInfoSyncTimeout_{nullptr};

  /** Controller config metadata. */
  std::unique_ptr<ConfigMetadata> controllerConfigMeta_;

  /** Config action results map. */
  using ConfigActionsResults = std::unordered_map<
      std::string /* node name */, thrift::NodeConfigActionsResult>;
  /** Aggregated node reports for config actions requests, indexed by ID. */
  std::unordered_map<std::string /* id */, ConfigActionsResults>
      configActionsResults_{};

  /** The process ID of the controller. */
  pid_t controllerPid_;

  /** The current batch of nodes being configured. */
  std::unordered_set<std::string> currBatch_;

  /**
   * For nodes we see with unrecognized hardware, map between each board ID and
   * some details of when we requested base configs (minion and monotonic time).
   */
  std::unordered_map<
      std::string /* hwBoardId */,
      std::pair<std::string /* nodeId */, int64_t /* requestTs */>>
          unknownHardwareReqMap_;

  /** The monotonic time when the current batch began configuration. */
  int64_t batchStartTime_;
};

} // namespace terragraph
} // namespace facebook
