/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ConfigApp.h"

#include <fbzmq/zmq/Zmq.h>
#include <folly/Conv.h>
#include <folly/dynamic.h>
#include <folly/MapUtil.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include "SharedObjects.h"
#include "../common/Consts.h"
#include "e2e/if/gen-cpp2/Controller_types.h"

using namespace fbzmq;
using std::string;
using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {
namespace stats {

ConfigApp::ConfigApp(
    fbzmq::Context& context,
    const string& routerSockUrl,
    pid_t aggregatorPid)
    : AggrApp(context, routerSockUrl, NMSConsts::kConfigAppAggrId),
      context_(context),
      aggregatorPid_(aggregatorPid) {
  // Load config metadata file
  // On errors, default initialize to prevent segfaults
  try {
    aggregatorConfigMeta_ = std::make_unique<ConfigMetadata>(
        FLAGS_aggregator_config_metadata_file,
        true /* keepFullMetadata */,
        true /* hasFlags */);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Error loading aggregator config metadata: "
               << folly::exceptionStr(ex);
    folly::dynamic obj = folly::dynamic::object;
    aggregatorConfigMeta_ = std::make_unique<ConfigMetadata>(
        obj, true /* keepFullMetadata */, true /* hasFlags */);
  }
}

void
ConfigApp::processMessage(
    const string& agent,
    const string& senderApp,
    const thrift::AggrMessage& message) noexcept {
  switch (message.mType) {
    case thrift::AggrMessageType::GET_AGGR_CONFIG_REQ:
      processGetAggrConfigReq(senderApp);
      break;
    case thrift::AggrMessageType::SET_AGGR_CONFIG_REQ:
      processSetAggrConfigReq(senderApp, message);
      break;
    case thrift::AggrMessageType::GET_AGGR_CONFIG_METADATA_REQ:
      processGetAggrConfigMetadataReq(senderApp);
      break;
    default:
      LOG(ERROR) << "Wrong type of message ("
                 << folly::get_default(
                        TEnumMapFactory<thrift::AggrMessageType>::
                            makeValuesToNamesMap(),
                        message.mType,
                        "UNKNOWN")
                 << ") received from " << agent << ":" << senderApp;
  }
}

void
ConfigApp::processGetAggrConfigReq(const string& senderApp) {
  VLOG(4) << "Request for aggregator config from " << senderApp;
  auto configJson =
      SharedObjects::getAggrConfigWrapper()->rlock()->getConfigJson();
  thrift::AggrGetConfigResp resp{};
  resp.config = configJson;
  sendToAggrApp(senderApp, thrift::AggrMessageType::GET_AGGR_CONFIG_RESP, resp);
}

void
ConfigApp::processSetAggrConfigReq(
    const string& senderApp, const thrift::AggrMessage& message) {
  LOG(INFO) << "Request to set aggregator config from " << senderApp;
  auto request = maybeReadThrift<thrift::AggrSetConfigReq>(message);
  if (!request) {
    handleInvalidMessage("AggrSetConfigReq", senderApp);
    return;
  }

  // Try to set the config (wrapper performs all validation)
  auto maybeConfigDiff =
      SharedObjects::getAggrConfigWrapper()->wlock()->setConfig(
          request->config, aggregatorConfigMeta_.get());
  if (maybeConfigDiff.hasError()) {
    sendAggrAck(
        senderApp,
        false,
        "Could not set aggregator config: " + maybeConfigDiff.error());
    return;
  }
  VLOG(1) << "Changed or removed config values:\n"
          << folly::toJson(maybeConfigDiff.value());

  // Config was saved, so return success now
  sendAggrAck(senderApp, true, "Success.");

  // Determine what actions to perform based on the config diff
  auto aggregatorActions =
      aggregatorConfigMeta_->getActions(maybeConfigDiff.value());

  // Perform aggregator actions
  performAggregatorConfigActions(aggregatorActions);
}

void
ConfigApp::performAggregatorConfigActions(
    const std::unordered_map<thrift::CfgAction, std::vector<std::string>>&
        aggregatorActions) {
  if (aggregatorActions.count(thrift::CfgAction::UPDATE_GLOG_LEVEL)) {
    // Update glog's VLOG level
    const auto& flags =
        SharedObjects::getAggrConfigWrapper()->rlock()->getConfig()->flags;
    auto iter = flags.find("v");
    if (iter != flags.end()) {
      auto v = folly::tryTo<int32_t>(iter->second);
      if (v.hasValue() && FLAGS_v != v.value()) {
        LOG(INFO) << "Updating VLOG level to " << v.value() << "...";
        FLAGS_v = v.value();
      }
    }
  }

  if (aggregatorActions.count(thrift::CfgAction::REBOOT)) {
    // Schedule reboot
    LOG(INFO) << "Aggregator process rebooting...";
    scheduleTimeout(std::chrono::seconds(2), [&]() noexcept {
      kill(aggregatorPid_, SIGTERM);
    });
  }
}

void
ConfigApp::processGetAggrConfigMetadataReq(const string& senderApp) {
  VLOG(4) << "Request for aggregator config metadata from " << senderApp;

  thrift::AggrGetConfigMetadataResp response;
  try {
    response.metadata = folly::toJson(aggregatorConfigMeta_->get());
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not parse aggregator config metadata: "
               << folly::exceptionStr(ex);
    sendAggrAck(senderApp, false, "Could not parse aggregator config metadata");
    return;
  }

  sendToAggrApp(
      senderApp,
      thrift::AggrMessageType::GET_AGGR_CONFIG_METADATA_RESP,
      response);
}

} // namesapce stats
} // namespace terragraph
} // namespace facebook
