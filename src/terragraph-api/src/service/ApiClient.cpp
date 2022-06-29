/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ApiClient.h"

#include <algorithm>
#include <chrono>

#include "e2e/common/CompressionUtil.h"
#include "e2e/common/Consts.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/Topology_types.h"
#include "stats/common/CompressionUtil.h"
#include "stats/common/Consts.h"
#include "stats/if/gen-cpp2/Aggregator_types.h"

using apache::thrift::detail::TEnumMapFactory;
using namespace facebook::terragraph::api;
using facebook::terragraph::E2EConsts;
using facebook::terragraph::stats::NMSConsts;

// from RequestFunction signature
using CLIENT = ApiClient*;
using JSON = const std::string&;
using RESPONSE = std::optional<std::string>;

DEFINE_int32(
    zmq_rcv_timeout_ms,
    15000,
    "The amount of time to wait for ZMQ responses (in milliseconds)");

DEFINE_string(
    api_role_prefix,
    "tg_",
    "Prefix for namespacing terragraph API roles, e.g. 'TG_'");

namespace facebook {
namespace terragraph {
namespace api {

ApiClient::RequestFunction::RequestFunction(
    std::function<std::optional<std::string>(
        ApiClient* apiClient, const std::string& json)> function,
    thrift::ApiCategory category,
    thrift::ApiLevel level,
    RequestFunction::HTTPMethod method)
    : function_(function),
      category_(category),
      level_(level),
      method_(method) {}

ApiClient::RequestFunction::RequestFunction(
    std::function<std::optional<std::string>(
        ApiClient* apiClient, const std::string& json)> function,
    RequestFunction::HTTPMethod method)
    : function_(function),
      category_(std::nullopt),
      level_(std::nullopt),
      method_(method) {}

bool
ApiClient::RequestFunction::hasPermission(
    const std::vector<std::string>& roles) {
  if (isAuthless()) {
    return true;
  }

  // Valid roles are of the form: <api_role_prefix><ApiCategory>_<ApiLevel>
  for (const auto& role : roles) {
    if (!FLAGS_api_role_prefix.empty() &&
        role.substr(0, FLAGS_api_role_prefix.length()) !=
            FLAGS_api_role_prefix) {
      continue;
    }

    auto pos = role.rfind('_');
    if (pos == std::string::npos) {
      continue;
    }

    // Extract the ApiCategory. It is only valid if it exists and is either
    // 'ALL' or an exact match to the RequestFunction's category.
    std::string categoryName = role.substr(
        FLAGS_api_role_prefix.length(), pos - FLAGS_api_role_prefix.length());
    std::transform(
        categoryName.begin(),
        categoryName.end(),
        categoryName.begin(),
        ::toupper);
    auto apiCategoryMap =
        TEnumMapFactory<thrift::ApiCategory>::makeNamesToValuesMap();
    auto categoryIt = apiCategoryMap.find(categoryName.c_str());
    if (categoryIt == apiCategoryMap.end()) {
      continue;
    }
    if (categoryIt->second != thrift::ApiCategory::ALL &&
        categoryIt->second != *category_) {
      continue;
    }

    // Extract the ApiLevel. It is only valid if it exists and is greater
    // than or equal to the RequestFunction's base level.
    std::string levelName = role.substr(pos + 1);
    std::transform(
        levelName.begin(), levelName.end(), levelName.begin(), ::toupper);

    auto apiLevelMap =
        TEnumMapFactory<thrift::ApiLevel>::makeNamesToValuesMap();
    auto levelIt = apiLevelMap.find(levelName.c_str());
    if (levelIt != apiLevelMap.end() && levelIt->second >= *level_) {
      return true;
    }
  }

  return false;
}

std::optional<std::string>
ApiClient::RequestFunction::applyFunction(
    ApiClient* apiClient, const std::string& body) {
  return function_(apiClient, body);
}

bool
ApiClient::RequestFunction::isWriteOperation() const {
  return level_ == thrift::ApiLevel::WRITE;
}

bool
ApiClient::RequestFunction::isAuthless() const {
  return !category_ && !level_;
}

ApiClient::RequestFunction::HTTPMethod
ApiClient::RequestFunction::method() const {
  return method_;
}

//
// This function populates the map of supported API methods.
// The RequestFunction for each method is expected to invoke either
// makeCtrlRequest() or makeAggrRequest() with the appropriate arguments/types.
//
// apidoc conventions:
// - Define any Thrift struct used with @apiDefine, then @apiUse it as needed
// - Include Thrift enum values inside @apiParam descriptions
// - Add "_GROUP" suffix for nested structs using the "@apiParam (Group)" syntax
// - Add "_SUCCESS" suffix for any @apiSuccess response structs
//
ApiClient::MethodMap ApiClient::methods_ = [] {
  MethodMap map;

  /**
   * @api {get} /isCtrlAlive Is Controller Alive
   * @apiVersion 2.0.0
   * @apiName IsCtrlAlive
   * @apiGroup Management
   *
   * @apiDescription E2E controller aliveness check.
   *
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/isCtrlAlive
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "success":true,"message":"Up"
   * }
   */
  map.insert({"isCtrlAlive",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::Empty,
                        thrift::E2EAck>(
                        json,
                        E2EConsts::kStatusAppCtrlId,
                        thrift::MessageType::IS_ALIVE);
                  },
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {get} /isAggrAlive Is Aggregator Alive
   * @apiVersion 2.0.0
   * @apiName IsAggrAlive
   * @apiGroup Management
   *
   * @apiDescription NMS aggregator aliveness check.
   *
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/isAggrAlive
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "success":true,"message":"Up"
   * }
   */
  map.insert({"isAggrAlive",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeAggrRequest<
                        thrift::Empty,
                        thrift::AggrAck>(
                        json,
                        NMSConsts::kStatusAppAggrId,
                        thrift::AggrMessageType::IS_ALIVE);
                  },
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @apiDefine NodeConfiguration Node Configuration
   */

  /**
   * @api {post} /getNodeConfig Get Node Config
   * @apiVersion 2.0.0
   * @apiName GetNodeConfig
   * @apiPermission CONFIG_READ
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Retrieves the full configuration for the given node.
   *
   * @apiUse GetCtrlConfigReq
   * @apiExample {curl} Example:
   *    curl -id '{"node": "terra111.f5.tb.a404-if", "swVersion": "RELEASE_M78"}' http://localhost:443/api/v2/getNodeConfig
   * @apiUse GetCtrlConfigResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "config": "{\"sysParams\":{<...>},\"envParams\":{<...>},\"fwParams\":{<...>},\"logTailParams\":{<...>},\"statsAgentParams\":{<...>}}"
   * }
   */
  map.insert({"getNodeConfig",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetCtrlConfigReq,
                        thrift::GetCtrlConfigResp>(
                        json,
                        E2EConsts::kConfigAppCtrlId,
                        thrift::MessageType::GET_CTRL_CONFIG_REQ);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /getBaseConfig Get Base Config
   * @apiVersion 2.0.0
   * @apiName GetBaseConfig
   * @apiPermission CONFIG_READ
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Retrieves the base configuration objects for the given software versions.
   *
   * @apiUse GetCtrlConfigBaseReq
   * @apiExample {curl} Example:
   *    curl -id '{"swVersions": ["Facebook Terragraph Release RELEASE_M15_1-michaelcallahan (michaelcallahan@devbig730 Mon Oct 9 16:50:07 PDT 2017)"]}' http://localhost:443/api/v2/getBaseConfig
   * @apiUse GetCtrlConfigBaseResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "config": "\"Facebook Terragraph Release RELEASE_M15_1-michaelcallahan (michaelcallahan@devbig730 Mon Oct 9 16:50:07 PDT 2017)\":{\"sysParams\":{<...>},\"envParams\":{<...>},\"fwParams\":{<...>},\"logTailParams\":{<...>},\"statsAgentParams\":{<...>}}"
   * }
   */
  map.insert({"getBaseConfig",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetCtrlConfigBaseReq,
                        thrift::GetCtrlConfigBaseResp>(
                        json,
                        E2EConsts::kConfigAppCtrlId,
                        thrift::MessageType::GET_CTRL_CONFIG_BASE_REQ);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /getFirmwareBaseConfig Get Firmware Base Config
   * @apiVersion 2.0.0
   * @apiName GetFirmwareBaseConfig
   * @apiPermission CONFIG_READ
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Retrieves the firmware base configuration objects for the given firmware versions.
   *
   * @apiUse GetCtrlConfigFirmwareBaseReq
   * @apiExample {curl} Example:
   *    curl -id '{"fwVersions": []}' http://localhost:443/api/v2/getFirmwareBaseConfig
   * @apiUse GetCtrlConfigFirmwareBaseResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "config": "{\"10.6.0\":{<...>}}"
   * }
   */
  map.insert({"getFirmwareBaseConfig",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetCtrlConfigFirmwareBaseReq,
                        thrift::GetCtrlConfigFirmwareBaseResp>(
                        json,
                        E2EConsts::kConfigAppCtrlId,
                        thrift::MessageType::GET_CTRL_CONFIG_FIRMWARE_BASE_REQ);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /getHardwareBaseConfig Get Hardware Base Config
   * @apiVersion 2.0.0
   * @apiName GetHardwareBaseConfig
   * @apiPermission CONFIG_READ
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Retrieves the hardware base configuration objects for the given hardware board IDs and software versions.
   *
   * @apiUse GetCtrlConfigHardwareBaseReq
   * @apiExample {curl} Example:
   *    curl -id '{"hwBoardIds": [], "swVersions": []}' http://localhost:443/api/v2/getHardwareBaseConfig
   * @apiUse GetCtrlConfigHardwareBaseResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "config": "{\"NXP\":{\"RELEASE_M78\":{<...>}}}"
   * }
   */
  map.insert({"getHardwareBaseConfig",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetCtrlConfigHardwareBaseReq,
                        thrift::GetCtrlConfigHardwareBaseResp>(
                        json,
                        E2EConsts::kConfigAppCtrlId,
                        thrift::MessageType::GET_CTRL_CONFIG_HARDWARE_BASE_REQ);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /getNetworkOverridesConfig Get Network Overrides Config
   * @apiVersion 2.0.0
   * @apiName GetNetworkOverridesConfig
   * @apiPermission CONFIG_READ
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Retrieves the network-wide override configuration.
   *
   * The network overrides are applied on top of the base configuration objects for each node.
   *
   * @apiUse GetCtrlConfigNetworkOverridesReq
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getNetworkOverridesConfig
   * @apiUse GetCtrlConfigNetworkOverridesResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "overrides": "{\"envParams\":{\"FW_IF2IF\":\"1\",\"OOB_NETNS\":\"1\"}}"
   * }
   */
  map.insert(
      {"getNetworkOverridesConfig",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<
                 thrift::GetCtrlConfigNetworkOverridesReq,
                 thrift::GetCtrlConfigNetworkOverridesResp>(
                 json,
                 E2EConsts::kConfigAppCtrlId,
                 thrift::MessageType::GET_CTRL_CONFIG_NETWORK_OVERRIDES_REQ);
           },
           thrift::ApiCategory::CONFIG,
           thrift::ApiLevel::READ,
           RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /getAutoNodeOverridesConfig Get Automated Node(s) Overrides Config
   * @apiVersion 2.0.0
   * @apiName GetAutoNodeOverridesConfig
   * @apiPermission CONFIG_READ
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Retrieves the automated node-specific override configuration for the given nodes.
   *
   * The automated node overrides are applied on top of the base configuration objects for each node.
   *
   * @apiUse GetCtrlConfigAutoNodeOverridesReq
   * @apiExample {curl} Example:
   *    curl -id '{"nodes": ["terra111.f5.tb.a404-if", "terra114.f5.tb.a404-if"]}' http://localhost:443/api/v2/getAutoNodeOverridesConfig
   * @apiUse GetCtrlConfigAutoNodeOverridesResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "overrides": "{\"terra111.f5.tb.a404-if\":{\"envParams\":{\"FW_IF2IF\":\"1\"}},\"terra114.f5.tb.a404-if\":{\"envParams\":{\"FW_IF2IF\":\"1\"}}}"
   * }
   */
  map.insert(
      {"getAutoNodeOverridesConfig",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<
                 thrift::GetCtrlConfigAutoNodeOverridesReq,
                 thrift::GetCtrlConfigAutoNodeOverridesResp>(
                 json,
                 E2EConsts::kConfigAppCtrlId,
                 thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_REQ);
           },
           thrift::ApiCategory::CONFIG,
           thrift::ApiLevel::READ,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /getNodeOverridesConfig Get Node(s) Overrides Config
   * @apiVersion 2.0.0
   * @apiName GetNodeOverridesConfig
   * @apiPermission CONFIG_READ
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Retrieves the node-specific override configuration for the given nodes.
   *
   * The node overrides are applied on top of both the base configuration objects and network overrides for each node.
   *
   * @apiUse GetCtrlConfigNodeOverridesReq
   * @apiExample {curl} Example:
   *    curl -id '{"nodes": ["terra111.f5.tb.a404-if", "terra114.f5.tb.a404-if"]}' http://localhost:443/api/v2/getNodeOverridesConfig
   * @apiUse GetCtrlConfigNodeOverridesResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "overrides": "{\"terra111.f5.tb.a404-if\":{\"envParams\":{\"FW_IF2IF\":\"1\"}},\"terra114.f5.tb.a404-if\":{\"envParams\":{\"FW_IF2IF\":\"1\"}}}"
   * }
   */
  map.insert(
      {"getNodeOverridesConfig",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<
                 thrift::GetCtrlConfigNodeOverridesReq,
                 thrift::GetCtrlConfigNodeOverridesResp>(
                 json,
                 E2EConsts::kConfigAppCtrlId,
                 thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_REQ);
           },
           thrift::ApiCategory::CONFIG,
           thrift::ApiLevel::READ,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /setNetworkOverridesConfig Set Network Overrides Config
   * @apiVersion 2.0.0
   * @apiName SetNetworkOverridesConfig
   * @apiPermission CONFIG_WRITE
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Sets the network-wide override configuration. This is applied on top of the base configuration objects for each node.
   *
   * The network configuration provided will completely replace the existing network configuration.
   *
   * @apiUse SetCtrlConfigNetworkOverridesReq
   * @apiExample {curl} Example:
   *    curl -id '{"overrides": "{\"envParams\":{\"FW_IF2IF\":\"1\",\"OOB_NETNS\":\"1\"}}"}' http://localhost:443/api/v2/setNetworkOverridesConfig
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"setNetworkOverridesConfig",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<
                 thrift::SetCtrlConfigNetworkOverridesReq,
                 thrift::E2EAck>(
                 json,
                 E2EConsts::kConfigAppCtrlId,
                 thrift::MessageType::SET_CTRL_CONFIG_NETWORK_OVERRIDES_REQ);
           },
           thrift::ApiCategory::CONFIG,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /modifyNetworkOverridesConfig Modify Network Overrides Config
   * @apiVersion 2.0.0
   * @apiName ModifyNetworkOverridesConfig
   * @apiPermission CONFIG_WRITE
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Modifies the network-wide override configuration. This is applied on top of the base configuration objects for each node.
   *
   * Existing keys not included in the provided network configuration request will be not changed.
   *
   * @apiUse ModifyCtrlConfigNetworkOverridesReq
   * @apiExample {curl} Example:
   *    curl -id '{"overrides": "{\"envParams\":{\"FW_IF2IF\":\"1\",\"OOB_NETNS\":\"1\"}}"}' http://localhost:443/api/v2/modifyNetworkOverridesConfig
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"modifyNetworkOverridesConfig",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<
                 thrift::ModifyCtrlConfigNetworkOverridesReq,
                 thrift::E2EAck>(
                 json,
                 E2EConsts::kConfigAppCtrlId,
                 thrift::MessageType::MODIFY_CTRL_CONFIG_NETWORK_OVERRIDES_REQ);
           },
           thrift::ApiCategory::CONFIG,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /setNodeOverridesConfig Set Node(s) Overrides Config
   * @apiVersion 2.0.0
   * @apiName SetNodeOverridesConfig
   * @apiPermission CONFIG_WRITE
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Sets the node-specific override configuration for the given nodes. This is applied on top of both the base configuration objects and network overrides for each node.
   *
   * This operation will only affect the given nodes. To erase overrides for a node, its configuration must be explicitly set to an empty object.
   *
   * Each node configuration provided will completely replace the existing node configuration.
   *
   * @apiUse SetCtrlConfigNodeOverridesReq
   * @apiExample {curl} Example:
   *    curl -id '{"overrides": "{\"terra111.f5.tb.a404-if\":{\"envParams\":{\"FW_IF2IF\":\"1\"}}}"}' http://localhost:443/api/v2/setNodeOverridesConfig
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"setNodeOverridesConfig",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<
                 thrift::SetCtrlConfigNodeOverridesReq,
                 thrift::E2EAck>(
                 json,
                 E2EConsts::kConfigAppCtrlId,
                 thrift::MessageType::SET_CTRL_CONFIG_NODE_OVERRIDES_REQ);
           },
           thrift::ApiCategory::CONFIG,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /modifyNodeOverridesConfig Modify Node(s) Overrides Config
   * @apiVersion 2.0.0
   * @apiName ModifyNodeOverridesConfig
   * @apiPermission CONFIG_WRITE
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Modifies the node-specific override configuration for the given nodes. This is applied on top of both the base configuration objects and network overrides for each node.
   *
   * This operation will only affect the given nodes. Overrides cannot be erased using this operation.
   *
   * Existing keys not included in the provided node configuration request will be not changed.
   *
   * @apiUse ModifyCtrlConfigNodeOverridesReq
   * @apiExample {curl} Example:
   *    curl -id '{"overrides": "{\"terra111.f5.tb.a404-if\":{\"envParams\":{\"FW_IF2IF\":\"1\"}}}"}' http://localhost:443/api/v2/modifyNodeOverridesConfig
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"modifyNodeOverridesConfig",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<
                 thrift::ModifyCtrlConfigNodeOverridesReq,
                 thrift::E2EAck>(
                 json,
                 E2EConsts::kConfigAppCtrlId,
                 thrift::MessageType::MODIFY_CTRL_CONFIG_NODE_OVERRIDES_REQ);
           },
           thrift::ApiCategory::CONFIG,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /getNetworkOverridesConfigActions Get Network Overrides Config Actions
   * @apiVersion 2.0.0
   * @apiName GetNetworkOverridesConfigActions
   * @apiPermission CONFIG_READ
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Gets the node actions for the network-wide override configuration. Each node will report what action it would take upon this config change, but the config overrides will not actually be applied.
   *
   * The returned ID should be used to poll for results, which will expire after the returned time.
   *
   * @apiUse GetCtrlConfigNetworkOverridesActionsReq
   * @apiExample {curl} Example:
   *    curl -id '{"overrides": "{\"envParams\":{\"FW_IF2IF\":\"1\",\"OOB_NETNS\":\"1\"}}"}' http://localhost:443/api/v2/getNetworkOverridesConfigActions
   * @apiUse GetCtrlConfigOverridesActionsResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "availableUntil": 1516670306,
   *     "id": "12582986238321455010",
   *     "overrides": "{\"envParams\":{\"FW_IF2IF\":\"1\",\"OOB_NETNS\":\"1\"}}"
   * }
   */
  map.insert({"getNetworkOverridesConfigActions",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetCtrlConfigNetworkOverridesActionsReq,
                        thrift::GetCtrlConfigOverridesActionsResp>(
                        json,
                        E2EConsts::kConfigAppCtrlId,
                        thrift::MessageType::
                            GET_CTRL_CONFIG_NETWORK_OVERRIDES_ACTIONS_REQ);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /getNodeOverridesConfigActions Get Node(s) Overrides Config Actions
   * @apiVersion 2.0.0
   * @apiName GetNodeOverridesConfigActions
   * @apiPermission CONFIG_READ
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Gets the node actions for the node-specific override configuration for the given nodes. Each node will report what action it would take upon this config change, but the config overrides will not actually be applied.
   *
   * The returned ID should be used to poll for results, which will expire after the returned time.
   *
   * @apiUse SetCtrlConfigNodeOverridesReq
   * @apiExample {curl} Example:
   *    curl -id '{"overrides": "{\"terra111.f5.tb.a404-if\":{\"envParams\":{\"FW_IF2IF\":\"1\"}}}"}' http://localhost:443/api/v2/getNodeOverridesConfigActions
   * @apiUse GetCtrlConfigOverridesActionsResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "availableUntil": 1516670773,
   *     "id": "10876931660539482246",
   *     "overrides": "{\"envParams\":{\"FW_IF2IF\":\"1\",\"OOB_NETNS\":\"1\"}}"
   * }
   */
  map.insert({"getNodeOverridesConfigActions",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetCtrlConfigNodeOverridesActionsReq,
                        thrift::GetCtrlConfigOverridesActionsResp>(
                        json,
                        E2EConsts::kConfigAppCtrlId,
                        thrift::MessageType::
                            GET_CTRL_CONFIG_NODE_OVERRIDES_ACTIONS_REQ);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /getConfigActionsResults Get Config Actions Results
   * @apiVersion 2.0.0
   * @apiName GetConfigActionsResults
   * @apiPermission CONFIG_READ
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Retrieves the per-node results for the given config actions request ID.
   *
   * @apiUse GetCtrlConfigActionsResultsReq
   * @apiExample {curl} Example:
   *    curl -id '{"id": "10876931660539482246"}' http://localhost:443/api/v2/getConfigActionsResults
   * @apiUse GetCtrlConfigActionsResultsResp_SUCCESS
   * @apiUse NodeConfigActionsResult_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *   "results": {
   *     "terra111.f5.tb.a404-if": {
   *       "actions": [
   *         0,
   *         10
   *       ]
   *   }
   * }
   */
  map.insert(
      {"getConfigActionsResults",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<
                 thrift::GetCtrlConfigActionsResultsReq,
                 thrift::GetCtrlConfigActionsResultsResp>(
                 json,
                 E2EConsts::kConfigAppCtrlId,
                 thrift::MessageType::GET_CTRL_CONFIG_ACTIONS_RESULTS_REQ);
           },
           thrift::ApiCategory::CONFIG,
           thrift::ApiLevel::READ,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /getConfigMetadata Get Config Metadata
   * @apiVersion 2.0.0
   * @apiName GetConfigMetadata
   * @apiPermission CONFIG_READ
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Retrieves the configuration metadata.
   *
   * @apiUse GetCtrlConfigMetadata
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getConfigMetadata
   * @apiUse GetCtrlConfigMetadataResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "metadata": "... <json>"
   * }
   */
  map.insert({"getConfigMetadata",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetCtrlConfigMetadata,
                        thrift::GetCtrlConfigMetadataResp>(
                        json,
                        E2EConsts::kConfigAppCtrlId,
                        thrift::MessageType::GET_CTRL_CONFIG_METADATA_REQ);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /getNodeConfigPaths Get Node Config Paths
   * @apiVersion 2.0.0
   * @apiName GetNodeConfigPaths
   * @apiPermission CONFIG_READ
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Retrieves specific node configuration data at the provided config paths.
   *
   * User provides list of nodes and list of requested configPaths.
   * configPaths are textual traversals of the JSON tree with dot delimiters and '*' wildcards.
   * Response is a map of node names to expanded config JSON string.
   *
   * @apiUse GetCtrlConfigPathsReq
   * @apiExample {curl} Example:
   *    curl -id '{"nodeNames": ["node1"], "configPaths": ["radioParamsOverride.*.fwParams.polarity"]}' http://localhost:443/api/v2/getNodeConfigPaths
   * @apiUse GetCtrlConfigPathsResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *   "config": {
   *     "node1": "{\"radioParamsOverride\":{\"01:01:01:01:01:01\":{\"fwParams\":{\"polarity\":1}},\"02:02:02:02:02:02\":{\"fwParams\":{\"polarity\":2}}}}"
   *   }
   * }
   */
  map.insert({"getNodeConfigPaths",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetCtrlConfigPathsReq,
                        thrift::GetCtrlConfigPathsResp>(
                        json,
                        E2EConsts::kConfigAppCtrlId,
                        thrift::MessageType::GET_CTRL_CONFIG_PATHS_REQ);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @apiDefine E2EConfiguration E2E Configuration
   */

  /**
   * @api {get} /getControllerConfig Get Controller Config
   * @apiVersion 2.0.0
   * @apiName GetControllerConfig
   * @apiPermission CONFIG_READ
   * @apiGroup E2EConfiguration
   *
   * @apiDescription Retrieves the controller configuration.
   *
   * @apiUse GetCtrlControllerConfigReq
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getControllerConfig
   * @apiUse GetCtrlControllerConfigResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "config": "{\"flags\":{\"v\":\"3\"}}"
   * }
   */
  map.insert({"getControllerConfig",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetCtrlControllerConfigReq,
                        thrift::GetCtrlControllerConfigResp>(
                        json,
                        E2EConsts::kConfigAppCtrlId,
                        thrift::MessageType::GET_CTRL_CONFIG_CONTROLLER_REQ);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /setControllerConfig Set Controller Config
   * @apiVersion 2.0.0
   * @apiName SetControllerConfig
   * @apiPermission CONFIG_WRITE
   * @apiGroup E2EConfiguration
   *
   * @apiDescription Sets the controller configuration.
   *
   * The controller configuration provided will completely replace the existing controller configuration.
   *
   * @apiUse SetCtrlControllerConfigReq
   * @apiExample {curl} Example:
   *    curl -id '{"config": "{\"flags\":{\"v\":\"3\"}}"}' http://localhost:443/api/v2/setControllerConfig
   * @apiUse E2EAck_SUCCESS
   */
  map.insert({"setControllerConfig",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::SetCtrlControllerConfigReq,
                        thrift::E2EAck>(
                        json,
                        E2EConsts::kConfigAppCtrlId,
                        thrift::MessageType::SET_CTRL_CONFIG_CONTROLLER_REQ);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::WRITE,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /getControllerConfigMetadata Get Controller Config Metadata
   * @apiVersion 2.0.0
   * @apiName GetControllerConfigMetadata
   * @apiPermission CONFIG_READ
   * @apiGroup E2EConfiguration
   *
   * @apiDescription Retrieves the controller configuration metadata.
   *
   * @apiUse GetCtrlControllerConfigMetadata
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getControllerConfigMetadata
   * @apiUse GetCtrlControllerConfigMetadataResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "metadata": "... <json>"
   * }
   */
  map.insert(
      {"getControllerConfigMetadata",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<
                 thrift::GetCtrlControllerConfigMetadata,
                 thrift::GetCtrlControllerConfigMetadataResp>(
                 json,
                 E2EConsts::kConfigAppCtrlId,
                 thrift::MessageType::GET_CTRL_CONFIG_CONTROLLER_METADATA_REQ);
           },
           thrift::ApiCategory::CONFIG,
           thrift::ApiLevel::READ,
           RequestFunction::HTTPMethod::GET)});

  /**
   * @api {get} /getAggregatorConfig Get Aggregator Config
   * @apiVersion 2.0.0
   * @apiName GetAggregatorConfig
   * @apiPermission CONFIG_READ
   * @apiGroup E2EConfiguration
   *
   * @apiDescription Retrieves the aggregator configuration.
   *
   * @apiUse AggrGetConfigReq
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getAggregatorConfig
   * @apiUse AggrGetConfigResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "config": "{\"flags\":{\"v\":\"4\"}}"
   * }
   */
  map.insert({"getAggregatorConfig",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeAggrRequest<
                        thrift::AggrGetConfigReq,
                        thrift::AggrGetConfigResp>(
                        json,
                        NMSConsts::kConfigAppAggrId,
                        thrift::AggrMessageType::GET_AGGR_CONFIG_REQ);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /setAggregatorConfig Set Aggregator Config
   * @apiVersion 2.0.0
   * @apiName SetAggregatorConfig
   * @apiPermission CONFIG_WRITE
   * @apiGroup E2EConfiguration
   *
   * @apiDescription Sets the aggregator configuration.
   *
   * The aggregator configuration provided will completely replace the existing aggregator configuration.
   *
   * @apiUse AggrSetConfigReq
   * @apiExample {curl} Example:
   *    curl -id '{"config": "{\"flags\":{\"v\":\"4\"}}"}' http://localhost:443/api/v2/setAggregatorConfig
   * @apiUse AggrAck_SUCCESS
   */
  map.insert(
      {"setAggregatorConfig",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeAggrRequest<thrift::AggrSetConfigReq, thrift::AggrAck>(
                     json,
                     NMSConsts::kConfigAppAggrId,
                     thrift::AggrMessageType::SET_AGGR_CONFIG_REQ);
           },
           thrift::ApiCategory::CONFIG,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /getAggregatorConfigMetadata Get Aggregator Config Metadata
   * @apiVersion 2.0.0
   * @apiName GetAggregatorConfigMetadata
   * @apiPermission CONFIG_READ
   * @apiGroup E2EConfiguration
   *
   * @apiDescription Retrieves the aggregator configuration metadata.
   *
   * @apiUse AggrGetConfigMetadata
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getAggregatorConfigMetadata
   * @apiUse AggrGetConfigMetadataResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "metadata": "... <json>"
   * }
   */
  map.insert({"getAggregatorConfigMetadata",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeAggrRequest<
                        thrift::AggrGetConfigMetadata,
                        thrift::AggrGetConfigMetadataResp>(
                        json,
                        NMSConsts::kConfigAppAggrId,
                        thrift::AggrMessageType::GET_AGGR_CONFIG_METADATA_REQ);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /triggerPolarityOptimization Trigger Polarity Optimization
   * @apiVersion 2.0.0
   * @apiName TriggerPolarityOptimization
   * @apiPermission CONFIG_WRITE
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Trigger polarity optimization using the Odd Cycle Cover (OCC) algorithm.
   *
   * When the "clearUserPolarityConfig" flag is set, the controller will reset
   * user-configured polarities before computing a new polarity assignment.
   *
   * @apiUse TriggerPolarityOptimization
   * @apiExample {curl} Example:
   *    curl -id '{"clearUserPolarityConfig": true}' http://localhost:443/api/v2/triggerPolarityOptimization
   * @apiUse E2EAck_SUCCESS
   */
  map.insert({"triggerPolarityOptimization",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::TriggerPolarityOptimization,
                        thrift::E2EAck>(
                        json,
                        E2EConsts::kConfigAppCtrlId,
                        thrift::MessageType::TRIGGER_POLARITY_OPTIMIZATION);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::WRITE,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /triggerChannelOptimization Trigger Channel Optimization
   * @apiVersion 2.0.0
   * @apiName TriggerChannelOptimization
   * @apiPermission CONFIG_WRITE
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Trigger channel optimization across topology. Currently
   * assigns channels to link groups (groups that must have the same channel)
   * at random.
   * When the "clearUserChannelConfig" flag is set, the controller will reset
   * user-configured channels before computing a new channel assignment.
   *
   * @apiUse TriggerChannelOptimization
   * @apiExample {curl} Example:
   *    curl -id '{"clearUserChannelConfig": true}' http://localhost:443/api/v2/triggerChannelOptimization
   * @apiUse E2EAck_SUCCESS
   */
  map.insert({"triggerChannelOptimization",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::TriggerChannelOptimization,
                        thrift::E2EAck>(
                        json,
                        E2EConsts::kConfigAppCtrlId,
                        thrift::MessageType::TRIGGER_CHANNEL_OPTIMIZATION);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::WRITE,
                  RequestFunction::HTTPMethod::POST)});
  /**
   * @api {post} /triggerControlSuperframeOptimization Trigger Control Superframe Optimization
   * @apiVersion 2.0.0
   * @apiName TriggerControlSuperframeOptimization
   * @apiPermission CONFIG_WRITE
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Trigger control superframe optimization.
   *
   * When the "clearUserConfig" flag is set, the controller will reset
   * user-configured control superframes before computing a new assignment.
   *
   * @apiUse TriggerControlSuperframeOptimization
   * @apiExample {curl} Example:
   *    curl -id '{"clearUserConfig": true}' http://localhost:443/api/v2/triggerControlSuperframeOptimization
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"triggerControlSuperframeOptimization",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<
                 thrift::TriggerControlSuperframeOptimization,
                 thrift::E2EAck>(
                 json,
                 E2EConsts::kConfigAppCtrlId,
                 thrift::MessageType::TRIGGER_CONTROL_SUPERFRAME_OPTIMIZATION);
           },
           thrift::ApiCategory::CONFIG,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /triggerGolayOptimization Trigger Golay Optimization
   * @apiVersion 2.0.0
   * @apiName TriggerGolayOptimization
   * @apiPermission CONFIG_WRITE
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Trigger network-wide Golay optimization.
   *
   * When the "clearUserConfig" flag is set, the controller will reset
   * user-configured Golay codes before computing a new assignment.
   *
   * @apiUse TriggerGolayOptimization
   * @apiExample {curl} Example:
   *    curl -id '{"clearUserConfig": true}' http://localhost:443/api/v2/triggerGolayOptimization
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"triggerGolayOptimization",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<
                 thrift::TriggerGolayOptimization,
                 thrift::E2EAck>(
                 json,
                 E2EConsts::kConfigAppCtrlId,
                 thrift::MessageType::TRIGGER_GOLAY_OPTIMIZATION);
           },
           thrift::ApiCategory::CONFIG,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /setFwLogConfig Set Firmware Log Config
   * @apiVersion 2.0.0
   * @apiName SetFwLogConfig
   * @apiPermission CONFIG_WRITE
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Temporarily changes the per-module firmware logging levels.
   *
   * @apiUse SetFwLogConfig
   * @apiExample {curl} Example:
   *    curl -id '{"nodes": [], "level": 0}' http://localhost:443/api/v2/setFwLogConfig
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"setFwLogConfig",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<
                thrift::SetFwLogConfig, thrift::E2EAck>(
                    json,
                    E2EConsts::kConfigAppCtrlId,
                    thrift::MessageType::FW_SET_LOG_CONFIG);
           },
           thrift::ApiCategory::CONFIG,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /clearAutoNodeOverridesConfig Clear Auto Node Overrides Config
   * @apiVersion 2.0.0
   * @apiName ClearAutoNodeOverridesConfig
   * @apiPermission CONFIG_WRITE
   * @apiGroup NodeConfiguration
   *
   * @apiDescription Removes the automated node-specific override configuration data at the provided config paths.
   *
   * User provides list of nodes and list of configPaths to be cleared.
   * configPaths are textual traversals of the JSON tree with dot delimiters and '*' wildcards.
   * Response indicates whether request was successfully received by controller.
   *
   * @apiUse ClearAutoNodeOverridesConfig
   * @apiExample {curl} Example:
   *    curl -id '{"nodeNames": ["node1"], "configPaths": ["radioParamsOverride.*.fwParams.polarity"]}' http://localhost:443/api/v2/clearAutoNodeOverridesConfig
   * @apiUse E2EAck_SUCCESS
   */
   map.insert({"clearAutoNodeOverridesConfig",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::ClearAutoNodeOverridesConfig,
                        thrift::E2EAck>(
                        json,
                        E2EConsts::kConfigAppCtrlId,
                        thrift::MessageType::CLEAR_AUTO_NODE_OVERRIDES_CONFIG);
                  },
                  thrift::ApiCategory::CONFIG,
                  thrift::ApiLevel::WRITE,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /rebootNode Reboot Node
   * @apiVersion 2.0.0
   * @apiName RebootNode
   * @apiPermission MANAGEMENT_WRITE
   * @apiGroup Management
   *
   * @apiDescription Sends a reboot request to the given node(s), which can either reboot immediately or schedule the reboot at a later time.
   *
   * Unless the "force" flag is set, the reboot request can be silently rejected by a node under certain circumstances (e.g. if currently undergoing an upgrade).
   *
   * @apiUse RebootReq
   * @apiExample {curl} Example:
   *    curl -id '{"nodes": ["terra114.f5.td.a404-if"], "force": true, "secondsToReboot": 60}' http://localhost:443/api/v2/rebootNode
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"rebootNode",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::RebootReq, thrift::E2EAck>(
                 json,
                 E2EConsts::kStatusAppCtrlId,
                 thrift::MessageType::REBOOT_REQUEST);
           },
           thrift::ApiCategory::MANAGEMENT,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /restartMinion Restart Minion
   * @apiVersion 2.0.0
   * @apiName RestartMinion
   * @apiPermission MANAGEMENT_WRITE
   * @apiGroup Management
   *
   * @apiDescription  Restart the E2E minion process on the given node(s). This restart can occur immediately or be scheduled for a later time.
   *
   * @apiUse RestartMinionRequest
   * @apiExample {curl} Example:
   *    curl -id '{"nodes": ["terra114.f5.td.a404-if"], "secondsToRestart": 60}' http://localhost:443/api/v2/restartMinion
   * @apiUse E2EAck_SUCCESS
   */
  map.insert({"restartMinion",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::RestartMinionRequest,
                        thrift::E2EAck>(
                        json,
                        E2EConsts::kStatusAppCtrlId,
                        thrift::MessageType::RESTART_MINION_REQUEST);
                  },
                  thrift::ApiCategory::MANAGEMENT,
                  thrift::ApiLevel::WRITE,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /getSysdump Get Sysdump
   * @apiVersion 2.0.0
   * @apiName GetSysdump
   * @apiPermission MANAGEMENT_WRITE
   * @apiGroup Management
   *
   * @apiDescription  Fetch a sysdump from the given node.
   * @apiUse GetSysdump
   * @apiExample {curl} Example:
   *    curl -id '{"node": "terra114.f5.td.a404-if"} http://localhost:442/api/v2/getSysdump
   * @apiUse GetSysdump_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "success": true,
   *     "filename": "sysdump-38:3a:21:b0:11:67-1596474697.tgz",
   *     "error": ""
   * }
   */
  map.insert({"getSysdump",
              RequestFunction(
                [](CLIENT client, JSON json) -> RESPONSE {
                  return client->makeCtrlRequest<
                      thrift::GetSysdump,
                      thrift::GetSysdumpResp>(
                      json,
                      E2EConsts::kStatusAppCtrlId,
                      thrift::MessageType::GET_SYSDUMP);
                },
                thrift::ApiCategory::MANAGEMENT,
                thrift::ApiLevel::WRITE,
                RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /getCtrlStatusDump Get Controller Status Dump
   * @apiVersion 2.0.0
   * @apiName GetCtrlStatusDump
   * @apiPermission MANAGEMENT_READ
   * @apiGroup Management
   *
   * @apiDescription Retrieves the latest status reports to the controller from all nodes in the network.
   *
   * @apiUse GetStatusDump
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getCtrlStatusDump
   * @apiUse StatusDump_SUCCESS
   * @apiUse StatusReport_SUCCESS
   * @apiUse UpgradeStatus_SUCCESS
   * @apiUse ImageMeta_SUCCESS
   * @apiUse BgpInfo_SUCCESS
   * @apiUse BgpRouteInfo_SUCCESS
   * @apiUse RadioStatus_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "statusReports": {
   *         "00:00:00:11:22:33": {
   *             "configMd5": "60f72d8eb83ba1b99fbcf200a7294aae",
   *             "firmwareVersion": "10.11.0.92",
   *             "hardwareBoardId": "NXP_LS1048A_PUMA",
   *             "hardwareModel": "NXP TG Board LS1048A (PUMA)",
   *             "ipv6Address": "2001::1",
   *             "lastAckGpsTimestamp": 1.3242414705326e+15,
   *             "neighborConnectionStatus": {},
   *             "networkInterfaceMacs": {},
   *             "nodeReachability": 1,
   *             "nodeType": 2,
   *             "radioStatus": {
   *               "aa:bb:cc:dd:ee:ff": {
   *                 "gpsSync": true,
   *                 "initialized": true,
   *                 "nodeParamsSet": true
   *               }
   *             },
   *             "sentGpsTimestamp": 1.3242414755734e+15,
   *             "status": 3,
   *             "timeStamp": 1640206257,
   *             "ubootVersion": "Terragraph Uboot Version: 1.0",
   *             "upgradeStatus": {
   *                 "nextImage": {
   *                     "hardwareBoardIds": {},
   *                     "md5": "",
   *                     "model": "",
   *                     "version": ""
   *                 },
   *                 "reason": "",
   *                 "upgradeReqId": "",
   *                 "usType": 10,
   *                 "whenToCommit": 0
   *             },
   *             "version": "Facebook Terragraph Release RELEASE_M78_PRE-127-gb0e7b3870-jeffreyhan jeffreyhan@devvm1523 2021-12-20T22:25:33"
   *         },
   *         ... <more nodes>
   *     },
   *     "timeStamp": 1640206257,
   *     "version": "Facebook Terragraph Release RELEASE_M78_PRE-127-gb0e7b3870-jeffreyhan jeffreyhan@devvm1523 2021-12-20T22:25:33"
   * }
   */
  map.insert(
      {"getCtrlStatusDump",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::GetStatusDump, thrift::StatusDump>(
                     json,
                     E2EConsts::kStatusAppCtrlId,
                     thrift::MessageType::GET_STATUS_DUMP);
           },
           thrift::ApiCategory::MANAGEMENT,
           thrift::ApiLevel::READ,
           RequestFunction::HTTPMethod::GET)});

  /**
   * @api {get} /getAggrStatusDump Get Aggregator Status Dump
   * @apiVersion 2.0.0
   * @apiName GetAggrStatusDump
   * @apiPermission MANAGEMENT_READ
   * @apiGroup Management
   *
   * @apiDescription Retrieves the aggregator status dump.
   *
   * @apiUse AggrGetStatusDump
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getAggrStatusDump
   * @apiUse AggrStatusDump_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "version": "Facebook Terragraph Release RELEASE_M78_PRE-127-gb0e7b3870-jeffreyhan jeffreyhan@devvm1523 2021-12-20T22:25:33"
   * }
   */
  map.insert({"getAggrStatusDump",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeAggrRequest<
                        thrift::AggrGetStatusDump,
                        thrift::AggrStatusDump>(
                        json,
                        NMSConsts::kStatusAppAggrId,
                        thrift::AggrMessageType::GET_STATUS_DUMP);
                  },
                  thrift::ApiCategory::MANAGEMENT,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {get} /getRoutingAdjacencies Get Routing Adjacencies
   * @apiVersion 2.0.0
   * @apiName GetRoutingAdjacencies
   * @apiPermission MANAGEMENT_READ
   * @apiGroup Management
   *
   * @apiDescription Retrieves a dump of adjacencies and prefixes for the entire network.
   *
   * @apiUse GetRoutingAdjacencies
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getRoutingAdjacencies
   * @apiUse RoutingAdjacencies_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "adjacencyMap": {
   *          "node-00.00.00.10.0b.40": {
   *              "adjacencies": [
   *                  {
   *                      "adjLabel": 0,
   *                      "ifName": "terra0",
   *                      "isOverloaded": false,
   *                      "metric": 1,
   *                      "nextHopV4": {
   *                          "addr": "AAAAAA"
   *                      },
   *                      "nextHopV6": {
   *                          "addr": "/oAAAAAAAAACAAD//hALRQ"
   *                      },
   *                      "otherIfName": "terra0",
   *                      "otherNodeName": "node-00.00.00.10.0b.45",
   *                      "rtt": 19857,
   *                      "timestamp": 1528413412,
   *                      "weight": 1
   *                  },
   *                  ... <more adjacencies>
   *              ],
   *              "isOverloaded": false,
   *              "nodeLabel": 12844,
   *              "thisNodeName": "node-00.00.00.10.0b.40"
   *          },
   *          ... <more nodes>
   *      },
   *      "prefixMap": {
   *          "node-00.00.00.10.0b.40": {
   *              "prefixEntries": [
   *                  {
   *                      "data": "",
   *                      "prefix": {
   *                          "prefixAddress": {
   *                              "addr": "AAAAAAAAAAAAAAAAAAAAAA"
   *                          },
   *                          "prefixLength": 0
   *                      },
   *                      "type": 5
   *                  },
   *                  {
   *                      "data": "",
   *                      "prefix": {
   *                          "prefixAddress": {
   *                              "addr": "JiABDcCJIWQAAAAAAAAAAA"
   *                          },
   *                          "prefixLength": 64
   *                      },
   *                      "type": 4
   *                  }
   *              ],
   *              "thisNodeName": "node-00.00.00.10.0b.40"
   *          },
   *          ... <more nodes>
   *      },
   *      "network": "2620:10d:c089:af00::/56"
   * }
   */
  map.insert({"getRoutingAdjacencies",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetRoutingAdjacencies,
                        thrift::RoutingAdjacencies>(
                        json,
                        E2EConsts::kTopologyAppCtrlId,
                        thrift::MessageType::GET_ROUTING_ADJACENCIES);
                  },
                  thrift::ApiCategory::MANAGEMENT,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /getRoutes Get Routes
   * @apiVersion 2.0.0
   * @apiName GetRoutes
   * @apiPermission MANAGEMENT_READ
   * @apiGroup Management
   *
   * @apiDescription Returns the list of routes between a source and destination node.
   *
   * @apiUse GetRoutes
   * @apiExample {curl} Example:
   *    curl -id '{"srcNode": "terra114.f5.tb.a404-if", "dstNode": "terra223.f5.tb.a404-if"}' http://localhost:443/api/v2/getRoutes
   * @apiUse GetRoutesResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "routes": [
   *         [
   *             "terra114.f5.tb.a404-if",
   *             "terra111.f5.tb.a404-if",
   *             "terra212.f5.tb.a404-if",
   *             "terra214.f5.tb.a404-if",
   *             "terra223.f5.tb.a404-if"
   *         ],
   *         [
   *             "terra114.f5.tb.a404-if",
   *             "terra123.f5.tb.a404-if",
   *             "terra121.f5.tb.a404-if",
   *             "terra222.f5.tb.a404-if",
   *             "terra223.f5.tb.a404-if"
   *         ]
   *     ]
   * }
   */
  map.insert(
      {"getRoutes",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::GetRoutes, thrift::GetRoutesResp>(
                     json,
                     E2EConsts::kTopologyAppCtrlId,
                     thrift::MessageType::GET_ROUTES);
           },
           thrift::ApiCategory::MANAGEMENT,
           thrift::ApiLevel::READ,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /getDefaultRoutes Get Default Routes
   * @apiVersion 2.0.0
   * @apiName GetDefaultRoutes
   * @apiPermission MANAGEMENT_READ
   * @apiGroup Management
   *
   * @apiDescription Returns a map of node names to list of default routes.
   *
   * @apiUse GetDefaultRoutes
   * @apiExample {curl} Example:
   *    curl -id '{"nodes": ["terra221.f3.tg.a404-if", "terra524.f5.tg.a404-if"]}' http://localhost:443/api/v2/getDefaultRoutes
   * @apiUse GetDefaultRoutesResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "defaultRoutes": {
   *         "terra221.f3.tg.a404-if": [
   *             [
   *                 "terra221.f3.tg.a404-if",
   *                 "terra223.f3.tg.a404-if",
   *                 "terra324.f1.tg.a404-if",
   *                 "terra322.f1.tg.a404-if",
   *                 "terra221.f1.tg.a404-if",
   *                 "terra222.f1.tg.a404-if",
   *                 "terra121.f1.tg.a404-if"
   *             ]
   *         ],
   *         "terra524.f5.tg.a404-if": [
   *             [
   *                 "terra524.f5.tg.a404-if",
   *                 "terra513.f7.tg.a404-if",
   *                 "terra514.f7.tg.a404-if",
   *                 "terra523.f7.tg.a404-if",
   *                 "terra522.f7.tg.a404-if",
   *                 "terra421.f7.tg.a404-if",
   *                 "terra423.f7.tg.a404-if",
   *                 "terra414.f7.tg.a404-if",
   *                 "terra411.f7.tg.a404-if"
   *             ],
   *             [
   *                 "terra524.f5.tg.a404-if",
   *                 "terra513.f7.tg.a404-if",
   *                 "terra514.f7.tg.a404-if",
   *                 "terra523.f7.tg.a404-if",
   *                 "terra522.f7.tg.a404-if",
   *                 "terra421.f7.tg.a404-if",
   *                 "terra423.f7.tg.a404-if",
   *                 "terra414.f7.tg.a404-if",
   *                 "terra121.f1.tg.a404-if"
   *             ]
   *         ]
   *     }
   * }
   */
  map.insert({"getDefaultRoutes",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetDefaultRoutes,
                        thrift::GetDefaultRoutesResp>(
                        json,
                        E2EConsts::kTopologyAppCtrlId,
                        thrift::MessageType::GET_DEFAULT_ROUTES);
                  },
                  thrift::ApiCategory::MANAGEMENT,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /getHighAvailabilityState Get High Availability State
   * @apiVersion 2.0.0
   * @apiName GetHighAvailabilityState
   * @apiPermission MANAGEMENT_READ
   * @apiGroup Management
   *
   * @apiDescription Returns the controller's state in the High Availability state machine.
   *
   * @apiUse BinaryStarGetState
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getHighAvailabilityState
   * @apiUse BinaryStar_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "peerExpiry": 1517860326320,
   *     "state": 3
   * }
   */
  map.insert({"getHighAvailabilityState",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::BinaryStarGetState,
                        thrift::BinaryStar>(
                        json,
                        E2EConsts::kBinaryStarAppCtrlId,
                        thrift::MessageType::BSTAR_GET_STATE);
                  },
                  thrift::ApiCategory::MANAGEMENT,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {get} /getTopology Get Topology
   * @apiVersion 2.0.0
   * @apiName GetTopology
   * @apiPermission TOPOLOGY_READ
   * @apiGroup Topology
   *
   * @apiDescription Retrieves the network topology.
   *
   * @apiUse GetTopology
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getTopology
   * @apiUse Topology_SUCCESS
   * @apiUse Config_SUCCESS
   * @apiUse DeterministicPrefixAllocParams_SUCCESS
   * @apiUse Zone_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "config": {
   *         "channel": 2,
   *         "deterministic_prefix_alloc_params": {
   *             "zones": {
   *                 "A": {
   *                     "node_names": [
   *                         "terra111.f5.tb.a404-if",
   *                         "terra212.f5.tb.a404-if"
   *                     ],
   *                     "zone_prefixes": [
   *                         "face:b00c:cafe:ba00::/56"
   *                     ]
   *                 }
   *             }
   *         }
   *     },
   *     "links": [
   *         {
   *             "a_node_mac": "00:00:00:10:0b:40",
   *             "a_node_name": "terra111.f5.tb.a404-if",
   *             "is_alive": false,
   *             "link_type": 1,
   *             "linkup_attempts": 3,
   *             "name": "link-terra111.f5.tb.a404-if-terra212.f5.tb.a404-if",
   *             "z_node_mac": "00:00:00:10:0b:45",
   *             "z_node_name": "terra212.f5.tb.a404-if"
   *         },
   *         ... <more links>
   *     ],
   *     "name": "Lab F8 B",
   *     "nodes": [
   *         {
   *             "ant_azimuth": 0,
   *             "ant_elevation": 0,
   *             "mac_addr": "00:00:00:10:0b:40",
   *             "name": "terra111.f5.tb.a404-if",
   *             "node_type": 2,
   *             "pop_node": true,
   *             "prefix": "face:b00c:cafe:ba00::/64",
   *             "wlan_mac_addrs": [],
   *             "site_name": "A",
   *             "status": 1
   *         },
   *         ... <more nodes>
   *     ],
   *     "sites": [
   *         {
   *             "location": {
   *                 "accuracy": 1.2327728906818156,
   *                 "altitude": -12.804,
   *                 "latitude": 37.4848884,
   *                 "longitude": -122.1473886
   *             },
   *             "name": "A"
   *         },
   *         ... <more sites>
   *     ]
   * }
   */
  map.insert(
      {"getTopology",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::GetTopology, thrift::Topology>(
                     json,
                     E2EConsts::kTopologyAppCtrlId,
                     thrift::MessageType::GET_TOPOLOGY);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::READ,
           RequestFunction::HTTPMethod::GET)});

  /**
   * @api {get} /getNetworkAirtime Get Network Airtime
   * @apiVersion 2.0.0
   * @apiName GetNetworkAirtime
   * @apiPermission TOPOLOGY_READ
   * @apiGroup Topology
   *
   * @apiDescription Retrieves the network airtime allocation map.
   *
   * @apiUse GetNetworkAirtime
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getNetworkAirtime
   * @apiUse NetworkAirtime_SUCCESS
   * @apiUse NodeAirtime_SUCCESS
   * @apiUse LinkAirtime_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "nodeAirtimeMap": {
   *         "terra111.f5.tb.a404-if": {
   *             "linkAirtimes": [
   *                 {
   *                     "macAddress": "00:00:00:10:0b:45",
   *                     "rxIdeal": -1,
   *                     "rxMax": 8000,
   *                     "rxMin": 200,
   *                     "txIdeal": 10000,
   *                     "txMax": 10000,
   *                     "txMin": 200
   *                 }
   *             ]
   *         },
   *         ... <more nodes>
   *     }
   * }
   */
  map.insert({"getNetworkAirtime",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetNetworkAirtime,
                        thrift::NetworkAirtime>(
                        json,
                        E2EConsts::kTopologyAppCtrlId,
                        thrift::MessageType::GET_NETWORK_AIRTIME);
                  },
                  thrift::ApiCategory::TOPOLOGY,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /getSite Get Site
   * @apiVersion 2.0.0
   * @apiName GetSite
   * @apiPermission TOPOLOGY_READ
   * @apiGroup Topology
   *
   * @apiDescription Finds a site structure by name from the topology.
   *
   * @apiUse GetSite
   * @apiExample {curl} Example:
   *    curl -id '{"name": "A"}' http://localhost:443/api/v2/getSite
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "location": {
   *         "accuracy": 1.2327728906818156,
   *         "altitude": -12.804,
   *         "latitude": 37.4848884,
   *         "longitude": -122.1473886
   *     },
   *     "name": "A"
   * }
   */
  map.insert(
      {"getSite",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::GetSite, thrift::Site>(
                 json,
                 E2EConsts::kTopologyAppCtrlId,
                 thrift::MessageType::GET_SITE);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::READ,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /addSite Add Site
   * @apiVersion 2.0.0
   * @apiName AddSite
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Adds a site to the topology.
   *
   * @apiUse AddSite
   * @apiUse Site_GROUP
   * @apiUse Location_GROUP
   * @apiExample {curl} Example:
   *    curl -id '{"site": {"name": "Test Site", "location": {"latitude": 37.4848, "longitude": -122.1472, "altitude": 30.5, "accuracy": 40000000}}}' http://localhost:443/api/v2/addSite
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"addSite",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::AddSite, thrift::E2EAck>(
                 json,
                 E2EConsts::kTopologyAppCtrlId,
                 thrift::MessageType::ADD_SITE);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /delSite Delete Site
   * @apiVersion 2.0.0
   * @apiName DelSite
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Removes a site from the topology.
   *
   * @apiUse DelSite
   * @apiExample {curl} Example:
   *    curl -id '{"siteName": "Test Site"}' http://localhost:443/api/v2/delSite
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"delSite",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::DelSite, thrift::E2EAck>(
                 json,
                 E2EConsts::kTopologyAppCtrlId,
                 thrift::MessageType::DEL_SITE);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /editSite Edit Site
   * @apiVersion 2.0.0
   * @apiName EditSite
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Edits an existing site in the topology.
   *
   * @apiUse EditSite
   * @apiUse Site_GROUP
   * @apiUse Location_GROUP
   * @apiExample {curl} Example:
   *    curl -id '{"siteName": "A", "newSite": {"name": "Z", "location": {"latitude": 37.484946, "longitude": -122.1475237, "altitude": 17.92, "accuracy": 10.0}}}' http://localhost:443/api/v2/editSite
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"editSite",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::EditSite, thrift::E2EAck>(
                 json,
                 E2EConsts::kTopologyAppCtrlId,
                 thrift::MessageType::EDIT_SITE);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /getLink Get Link
   * @apiVersion 2.0.0
   * @apiName GetLink
   * @apiPermission TOPOLOGY_READ
   * @apiGroup Topology
   *
   * @apiDescription Finds a link structure by name from the topology.
   *
   * @apiUse GetLink
   * @apiExample {curl} Example:
   *    curl -id '{"name": "link-terra111.f5.tb.a404-if-terra212.f5.tb.a404-if"}' http://localhost:443/api/v2/getLink
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "a_node_mac": "00:00:00:10:0b:40",
   *     "a_node_name": "terra111.f5.tb.a404-if",
   *     "is_alive": false,
   *     "link_type": 1,
   *     "linkup_attempts": 3,
   *     "name": "link-terra111.f5.tb.a404-if-terra212.f5.tb.a404-if",
   *     "z_node_mac": "00:00:00:10:0b:45",
   *     "z_node_name": "terra212.f5.tb.a404-if"
   * }
   */
  map.insert(
      {"getLink",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::GetLink, thrift::Link>(
                 json,
                 E2EConsts::kTopologyAppCtrlId,
                 thrift::MessageType::GET_LINK);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::READ,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /addLink Add Link
   * @apiVersion 2.0.0
   * @apiName AddLink
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Adds a link between two nodes to the topology.
   *
   * @apiUse AddLink
   * @apiUse Link_GROUP
   * @apiExample {curl} Example:
   *    curl -id '{"link": {"a_node_name": "terra212.f5.td.a404-if", "a_node_mac": "00:00:00:10:0b:45", "z_node_name": "terra214.f5.td.a404-if", "z_node_mac": "00:00:00:10:0b:46", "link_type": 2, "is_alive": false, "linkup_attempts": 0}}' http://localhost:443/api/v2/addLink
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"addLink",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::AddLink, thrift::E2EAck>(
                 json,
                 E2EConsts::kTopologyAppCtrlId,
                 thrift::MessageType::ADD_LINK);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /delLink Delete Link
   * @apiVersion 2.0.0
   * @apiName DelLink
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Removes a link between two nodes from the topology.
   *
   * Unless the "force" flag is set, deletion can fail under certain circumstances (e.g. if the link is still alive).
   *
   * @apiUse DelLink
   * @apiExample {curl} Example:
   *    curl -id '{"aNodeName": "terra212.f5.td.a404-if", "zNodeName": "terra214.f5.td.a404-if", "force": true}' http://localhost:443/api/v2/delLink
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"delLink",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::DelLink, thrift::E2EAck>(
                 json,
                 E2EConsts::kTopologyAppCtrlId,
                 thrift::MessageType::DEL_LINK);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /getNode Get Node
   * @apiVersion 2.0.0
   * @apiName GetNode
   * @apiPermission TOPOLOGY_READ
   * @apiGroup Topology
   *
   * @apiDescription Finds a node structure by name from the topology.
   *
   * @apiUse GetNode
   * @apiExample {curl} Example:
   *    curl -id '{"name": "terra111.f5.tb.a404-if"}' http://localhost:443/api/v2/getNode
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "ant_azimuth": 0,
   *     "ant_elevation": 0,
   *     "mac_addr": "00:00:00:10:0b:40",
   *     "name": "terra111.f5.tb.a404-if",
   *     "node_type": 2,
   *     "pop_node": true,
   *     "prefix": "face:b00c:cafe:ba00::/64",
   *     "wlan_mac_addrs": [],
   *     "site_name": "A",
   *     "status": 1
   * }
   */
  map.insert(
      {"getNode",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::GetNode, thrift::Node>(
                 json,
                 E2EConsts::kTopologyAppCtrlId,
                 thrift::MessageType::GET_NODE);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::READ,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /addNode Add Node
   * @apiVersion 2.0.0
   * @apiName AddNode
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Adds a node at the given site to the topology.
   *
   * @apiUse AddNode
   * @apiUse Node_GROUP
   * @apiExample {curl} Example:
   *    curl -id '{"node": {"name": "terra999.f5.td.a404-if", "node_type": 2, "mac_addr": "", "wlan_mac_addrs": [], "pop_node" : false, "status": 1, "site_name": "Test Site", "ant_azimuth": 100.0, "ant_elevation": 999.99}}' http://localhost:443/api/v2/addNode
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"addNode",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::AddNode, thrift::E2EAck>(
                 json,
                 E2EConsts::kTopologyAppCtrlId,
                 thrift::MessageType::ADD_NODE);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /delNode Delete Node
   * @apiVersion 2.0.0
   * @apiName DelNode
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Removes a node from the topology.
   *
   * Unless the "force" flag is set, deletion can fail under certain circumstances (e.g. if any link associated with the node is still alive).
   *
   * @apiUse DelNode
   * @apiExample {curl} Example:
   *    curl -id '{"nodeName": "terra212.f5.td.a404-if", "force": false}' http://localhost:443/api/v2/delNode
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"delNode",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::DelNode, thrift::E2EAck>(
                 json,
                 E2EConsts::kTopologyAppCtrlId,
                 thrift::MessageType::DEL_NODE);
           },
           thrift::ApiCategory::CONFIG,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /editNode Edit Node
   * @apiVersion 2.0.0
   * @apiName EditNode
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Edits an existing node in the topology.
   *
   * Currently, the only supported changes are "name", "node_type", "pop_node", "ant_azimuth", "ant_elevation", and "has_cpe". When changing node type, no existing links are allowed.
   *
   * @apiUse EditNode
   * @apiUse Node_GROUP
   * @apiExample {curl} Example:
   *    curl -id '{"nodeName": "terra212.f5.td.a404-if", "newNode": {"name": "terra999.f5.td.a404-if", "node_type": 2, "mac_addr": "00:00:00:10:0d:45", "wlan_mac_addrs": [], "pop_node" : false, "status": 1, "site_name": "B", "ant_azimuth": 100.0, "ant_elevation": 999.99}}' http://localhost:443/api/v2/editNode
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"editNode",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::EditNode, thrift::E2EAck>(
                 json,
                 E2EConsts::kTopologyAppCtrlId,
                 thrift::MessageType::EDIT_NODE);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /bulkAdd Bulk Add Topology
   * @apiVersion 2.0.0
   * @apiName BulkAdd
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Adds the given sites, nodes, and links to the topology (in that order).
   *
   * This command is equivalent to invoking the individual operations in sequence, and will proceed even if any of the topology changes fails.
   *
   * @apiUse BulkAdd
   * @apiUse Site_GROUP
   * @apiUse Node_GROUP
   * @apiUse Link_GROUP
   * @apiUse Location_GROUP
   * @apiExample {curl} Example:
   *    curl -id '{"sites": [{"name": "Test Site", "location": {"latitude": 37.4848, "longitude": -122.1472, "altitude": 30.5, "accuracy": 40000000}}], "nodes": [{"name": "terra999.f5.td.a404-if", "node_type": 2, "mac_addr": "", "wlan_mac_addrs": [], "pop_node" : false, "status": 1, "site_name": "Test Site", "ant_azimuth": 100.0, "ant_elevation": 999.99}]}' http://localhost:443/api/v2/bulkAdd
   * @apiUse BulkAddResult_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "success": true,
   *     "message": "Bulk add successful",
   *     "addedSites": ["Test Site"],
   *     "failedSites": {},
   *     "addedNodes": ["terra999.f5.td.a404-if"],
   *     "failedNodes": {},
   *     "addedLinks": [],
   *     "failedLinks": {}
   * }
   */
  map.insert(
      {"bulkAdd",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<
                 thrift::BulkAdd, thrift::BulkAddResult>(
                     json,
                     E2EConsts::kTopologyAppCtrlId,
                     thrift::MessageType::BULK_ADD);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /setNodeMacAddress Set Node Mac Address
   * @apiVersion 2.0.0
   * @apiName SetNodeMacAddress
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Sets the MAC address of the given node.
   *
   * Unless the "force" flag is set, this operation can be rejected under certain circumstances (e.g. if the node is currently ignited).
   *
   * @apiUse SetNodeMac
   * @apiExample {curl} Example:
   *    curl -id '{"nodeName": "terra111.f5.td.a404-if", "nodeMac": "99:00:00:10:0d:40", "force": true}' http://localhost:443/api/v2/setNodeMacAddress
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"setNodeMacAddress",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::SetNodeMac, thrift::E2EAck>(
                 json,
                 E2EConsts::kTopologyAppCtrlId,
                 thrift::MessageType::SET_NODE_MAC);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /bulkSetNodeMacAddress Bulk Set Node Mac Address
   * @apiVersion 2.0.0
   * @apiName BulkSetNodeMacAddress
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Sets the MAC addresses of the given nodes (in order).
   *
   * This command is equivalent to setting the MAC addresses in sequence, and will proceed even if any of the operations fails.
   *
   * Unless the "force" flag is set, an operation can be rejected under certain circumstances (e.g. if a node is currently ignited).
   *
   * @apiUse SetNodeMacList
   * @apiUse SetNodeMac_GROUP
   * @apiExample {curl} Example:
   *    curl -id '{"setNodeMacList": [{"nodeName": "terra111.f5.td.a404-if", "nodeMac": "99:00:00:10:0d:40", "force": true}]}' http://localhost:443/api/v2/bulkSetNodeMacAddress
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"bulkSetNodeMacAddress",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::SetNodeMacList, thrift::E2EAck>(
                     json,
                     E2EConsts::kTopologyAppCtrlId,
                     thrift::MessageType::SET_NODE_MAC_LIST);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /addNodeWlanMacAddresses Add Node Wlan Mac Addresses
   * @apiVersion 2.0.0
   * @apiName AddNodeWlanMacAddresses
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Adds wlan MAC addresses to the given node
   *
   * @apiUse AddNodeWlanMacs
   * @apiExample {curl} Example:
   *    curl -id '{"nodeName": "terra111.f5.td.a404-if", "wlanMacs": ["99:00:00:10:0d:40"]}' http://localhost:443/api/v2/addNodeWlanMacAddresses
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"addNodeWlanMacAddresses",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::AddNodeWlanMacs, thrift::E2EAck>(
                     json,
                     E2EConsts::kTopologyAppCtrlId,
                     thrift::MessageType::ADD_NODE_WLAN_MACS);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /deleteNodeWlanMacAddresses Delete Node Wlan Mac Addresses
   * @apiVersion 2.0.0
   * @apiName DeleteNodeWlanMacAddresses
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Deletes wlan MAC addresses from the given node
   *
   * Unless the "force" flag is set, this operation can be rejected under certain circumstances (e.g. if associated links are alive).
   *
   * @apiUse DelNodeWlanMacs
   * @apiExample {curl} Example:
   *    curl -id '{"nodeName": "terra111.f5.td.a404-if", "wlanMacs": ["99:00:00:10:0d:40"], "force": true}' http://localhost:443/api/v2/deleteNodeWlanMacAddresses
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"deleteNodeWlanMacAddresses",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::DelNodeWlanMacs, thrift::E2EAck>(
                     json,
                     E2EConsts::kTopologyAppCtrlId,
                     thrift::MessageType::DEL_NODE_WLAN_MACS);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /changeNodeWlanMacAddress Change Node Wlan Mac Address
   * @apiVersion 2.0.0
   * @apiName ChangeNodeWlanMacAddress
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Changes one wlan MAC address of the given node
   *
   * Unless the "force" flag is set, an operation can be rejected under certain circumstances (e.g. if a node is currently ignited).
   *
   * @apiUse ChangeNodeWlanMac
   * @apiExample {curl} Example:
   *    curl -id '{"nodeName": "terra111.f5.td.a404-if", "oldWlanMac": "99:00:00:10:0d:40", "newWlanMac": "99:00:00:10:0d:41"}' http://localhost:443/api/v2/changeNodeWlanMacAddress
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"changeNodeWlanMacAddress",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::ChangeNodeWlanMac, thrift::E2EAck>(
                     json,
                     E2EConsts::kTopologyAppCtrlId,
                     thrift::MessageType::CHANGE_NODE_WLAN_MAC);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /getNodePrefixes Get Node Prefixes
   * @apiVersion 2.0.0
   * @apiName GetNodePrefixes
   * @apiPermission TOPOLOGY_READ
   * @apiGroup Topology
   *
   * @apiDescription Retrieves the prefixes allocated to the nodes in the topology.
   *
   * @apiUse GetNodePrefixes
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getNodePrefixes
   * @apiUse GetNodePrefixesResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "nodePrefixes": {
   *         "node-1": "face:b00c:0:1::/64",
   *         "node-2": "face:b00c::/64"
   *     }
   * }
   */
  map.insert({"getNodePrefixes",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetNodePrefixes,
                        thrift::GetNodePrefixesResp>(
                        json,
                        E2EConsts::kTopologyAppCtrlId,
                        thrift::MessageType::GET_NODE_PREFIXES);
                  },
                  thrift::ApiCategory::TOPOLOGY,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {get} /getZonePrefixes Get Zone Prefixes
   * @apiVersion 2.0.0
   * @apiName GetZonePrefixes
   * @apiPermission TOPOLOGY_READ
   * @apiGroup Topology
   *
   * @apiDescription Retrieves the prefixes allocated to the zones in the topology.
   *
   * @apiUse GetZonePrefixes
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getZonePrefixes
   * @apiUse GetZonePrefixesResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "zonePrefixes": {
   *         "A": [
   *             "face:b00c::/56"
   *         ]
   *     }
   * }
   */
  map.insert({"getZonePrefixes",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetZonePrefixes,
                        thrift::GetZonePrefixesResp>(
                        json,
                        E2EConsts::kTopologyAppCtrlId,
                        thrift::MessageType::GET_ZONE_PREFIXES);
                  },
                  thrift::ApiCategory::TOPOLOGY,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /allocatePrefixes Allocate Prefixes
   * @apiVersion 2.0.0
   * @apiName AllocatePrefixes
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Reallocates prefixes for all nodes in the network.
   *
   * @apiUse AllocatePrefixes
   * @apiExample {curl} Example:
   *    curl -id '{}' http://localhost:443/api/v2/allocatePrefixes
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"allocatePrefixes",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::AllocatePrefixes, thrift::E2EAck>(
                     json,
                     E2EConsts::kTopologyAppCtrlId,
                     thrift::MessageType::ALLOCATE_PREFIXES);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /setTopologyName Set Topology Name
   * @apiVersion 2.0.0
   * @apiName SetTopologyName
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup Topology
   *
   * @apiDescription Sets the name of the topology.
   *
   * @apiUse SetTopologyName
   * @apiExample {curl} Example:
   *    curl -id '{"name": "Test Topology"}' http://localhost:443/api/v2/setTopologyName
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"setTopologyName",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::SetTopologyName, thrift::E2EAck>(
                     json,
                     E2EConsts::kTopologyAppCtrlId,
                     thrift::MessageType::SET_TOPOLOGY_NAME);
           },
           thrift::ApiCategory::TOPOLOGY,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @apiDefine TopologyBuilder Topology Builder
   */

  /**
   * @api {post} /startTopologyScan Start Topology Scan
   * @apiVersion 2.0.0
   * @apiName StartTopologyScan
   * @apiPermission SCAN_WRITE
   * @apiGroup TopologyBuilder
   *
   * @apiDescription Initiates a topology scan at a given node, returning information about nearby nodes.
   *
   * @apiUse StartTopologyScan
   * @apiExample {curl} Example:
   *    curl -id '{"txNode": "15-46.p1"}' http://localhost:443/api/v2/startTopologyScan
   * @apiUse StartTopologyScanResp_SUCCESS
   * @apiUse TopologyScanInfo_SUCCESS
   * @apiUse TopoResponderInfo_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "responders": [
   *         {
   *             "bestRxAngle": 11.25,
   *             "bestSnr": 14,
   *             "bestTxAngle": 5.625,
   *             "nearestSite": "18-9",
   *             "nearestSiteDistance": "0.0717740869",
   *             "responderInfo": {
   *                 "addr": "38:3a:21:b0:0e:89",
   *                 "adjs": [
   *                     "74:6f:f7:ca:1a:f8",
   *                     "38:3a:21:b0:11:e2",
   *                     "2c:dc:ad:2f:e8:36"
   *                 ],
   *                 "itorLqmMat": {
   *                     "0": {
   *                         "0": 250,
   *                         "16": 267
   *                     },
   *                     ... <more beams>
   *                 },
   *                 "pos": {
   *                     "accuracy": 7.23619955501505,
   *                     "altitude": -24.779,
   *                     "latitude": 37.4846565,
   *                     "longitude": -122.1457912
   *                 },
   *                 "rtoiLqmMat": {
   *                     "12": {
   *                         "19": 252,
   *                         "20": 264,
   *                         "21": 261,
   *                         "23": 244
   *                     },
   *                     ... <more beams>
   *                 }
   *             }
   *         },
   *         ... <more responders>
   *     ],
   *     "txNode": "15-46.p1"
   * }
   */
  map.insert({"startTopologyScan",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::StartTopologyScan,
                        thrift::StartTopologyScanResp>(
                        json,
                        E2EConsts::kTopologyBuilderAppCtrlId,
                        thrift::MessageType::START_TOPOLOGY_SCAN);
                  },
                  thrift::ApiCategory::SCAN,
                  thrift::ApiLevel::WRITE,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /startNetworkTopologyScan Start Network Topology Scan
   * @apiVersion 2.0.0
   * @apiName StartNetworkTopologyScan
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup TopologyBuilder
   *
   * @apiDescription Initiates a network-wide topology scan.
   *
   * @apiUse StartNetworkTopologyScan
   * @apiUse SiteLink_GROUP
   * @apiExample {curl} Example:
   *    curl -id '{"siteLinks": [{"aSite": "A", "zSite": "B"}, {"aSite": "B", "zSite": "C"}, {"aSite": "C", "zSite": "D"}, {"aSite": "D", "zSite": "A"}], "macAddrs": [], "cnSites": [], "yStreetSites": []}' http://localhost:443/api/v2/startNetworkTopologyScan
   * @apiUse E2EAck_SUCCESS
   */
  map.insert({"startNetworkTopologyScan",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::StartNetworkTopologyScan,
                        thrift::E2EAck>(
                        json,
                        E2EConsts::kTopologyBuilderAppCtrlId,
                        thrift::MessageType::START_NETWORK_TOPOLOGY_SCAN);
                  },
                  thrift::ApiCategory::TOPOLOGY,
                  thrift::ApiLevel::WRITE,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /stopNetworkTopologyScan Stop Network Topology Scan
   * @apiVersion 2.0.0
   * @apiName StopNetworkTopologyScan
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup TopologyBuilder
   *
   * @apiDescription Stops the current network-wide topology scan (if running).
   *
   * @apiUse StopNetworkTopologyScan
   * @apiExample {curl} Example:
   *    curl -id '{}' http://localhost:443/api/v2/stopNetworkTopologyScan
   * @apiUse E2EAck_SUCCESS
   */
  map.insert({"stopNetworkTopologyScan",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::StopNetworkTopologyScan,
                        thrift::E2EAck>(
                        json,
                        E2EConsts::kTopologyBuilderAppCtrlId,
                        thrift::MessageType::STOP_NETWORK_TOPOLOGY_SCAN);
                  },
                  thrift::ApiCategory::TOPOLOGY,
                  thrift::ApiLevel::WRITE,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /getNetworkTopologyScanStatus Get Network Topology Scan Status
   * @apiVersion 2.0.0
   * @apiName GetNetworkTopologyScanStatus
   * @apiPermission TOPOLOGY_READ
   * @apiGroup TopologyBuilder
   *
   * @apiDescription Retrieves the status of the network-wide topology scan.
   *
   * @apiUse GetNetworkTopologyScanStatus
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getNetworkTopologyScanStatus
   * @apiUse NetworkTopologyScanStatus_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "active": false,
   *     "lastUpdateTime": 1540594973,
   *     "currentScanNode": "",
   *     "queuedSites": [],
   *     "emptySites": [],
   *     "visitedSites": [
   *         "B",
   *         "E",
   *         "A",
   *         "C",
   *         "F",
   *         "D"
   *     ],
   *     "newNodes": [
   *         {
   *             "name": "E.1",
   *             "node_type": 2,
   *             "mac_addr": "38:3a:21:b0:04:9d",
   *             "pop_node": false,
   *             "status": 0,
   *             "wlan_mac_addrs": [],
   *             "site_name": "E",
   *             "ant_azimuth": 0,
   *             "ant_elevation": 0
   *         },
   *         ... <more nodes>
   *     ],
   *     "newLinks": [
   *         {
   *             "name": "link-E.1-MPK.11-02.S2",
   *             "a_node_name": "E.1",
   *             "z_node_name": "MPK.11-02.S2",
   *             "link_type": 1,
   *             "is_alive": false,
   *             "linkup_attempts": 0,
   *             "a_node_mac": "",
   *             "z_node_mac": ""
   *         },
   *         ... <more links>
   *     ]
   * }
   */
  map.insert({"getNetworkTopologyScanStatus",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetNetworkTopologyScanStatus,
                        thrift::NetworkTopologyScanStatus>(
                        json,
                        E2EConsts::kTopologyBuilderAppCtrlId,
                        thrift::MessageType::GET_NETWORK_TOPOLOGY_SCAN_STATUS);
                  },
                  thrift::ApiCategory::TOPOLOGY,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /startLinkDiscoveryScan Start Link Discovery Scan
   * @apiVersion 2.0.0
   * @apiName StartLinkDiscoveryScan
   * @apiPermission TOPOLOGY_WRITE
   * @apiGroup TopologyBuilder
   *
   * @apiDescription Use topology scans to find the best link for a node.
   *
   * @apiUse StartLinkDiscoveryScan
   * @apiExample {curl} Example:
   *    curl -id '{"targetMac": "2c:dc:ad:28:e7:2b", "scanRadius": 200}' http://localhost:443/api/v2/startLinkDiscoveryScan
   * @apiUse E2EAck_SUCCESS
   */
  map.insert({"startLinkDiscoveryScan",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::StartLinkDiscoveryScan,
                        thrift::E2EAck>(
                        json,
                        E2EConsts::kTopologyBuilderAppCtrlId,
                        thrift::MessageType::START_LINK_DISCOVERY_SCAN);
                  },
                  thrift::ApiCategory::TOPOLOGY,
                  thrift::ApiLevel::WRITE,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /getLinkDiscoveryScanStatus Get Link Discovery Scan Status
   * @apiVersion 2.0.0
   * @apiName GetLinkDiscoveryScanStatus
   * @apiPermission TOPOLOGY_READ
   * @apiGroup TopologyBuilder
   *
   * @apiDescription Get the status of the link discovery scan.
   *
   * @apiUse GetLinkDiscoveryScanStatus
   * @apiExample {curl} Example:
   *    curl -i http://localhost::443/api/v2/getLinkDiscoveryScanStatus
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "active": false,
   *     "lastUpdateTime": 1540594973,
   *     "responders": {
   *         "15-46.p1": [
   *             {
   *                 "bestRxAngle": 11.25,
   *                 "bestSnr": 14,
   *                 "bestTxAngle": 5.625,
   *                 "nearestSite": "18-9",
   *                 "nearestSiteDistance": "0.0717740869",
   *                 "responderInfo": {
   *                     "addr": "38:3a:21:b0:0e:89",
   *                     "adjs": [
   *                         "74:6f:f7:ca:1a:f8",
   *                         "38:3a:21:b0:11:e2",
   *                         "2c:dc:ad:2f:e8:36"
   *                     ],
   *                     "itorLqmMat": {
   *                         "0": {
   *                             "0": 250,
   *                             "16": 267
   *                         }
   *                     },
   *                     ... <more beams>
   *                 },
   *                 "pos": {
   *                     "accuracy": 7.23619955501505,
   *                     "altitude": -24.779,
   *                     "latitude": 37.4846565,
   *                     "longitude": -122.1457912
   *                 },
   *                 "rtoiLqmMat": {
   *                     "12": {
   *                         "19": 252,
   *                         "20": 264,
   *                         "21": 261,
   *                         "23": 244
   *                     },
   *                     ... <more beams>
   *                 },
   *             },
   *         ],
   *         ... <more responders>
   *     }
   * }
   */
   map.insert({"getLinkDiscoveryScanStatus",
               RequestFunction(
                   [](CLIENT client, JSON json) -> RESPONSE {
                     return client->makeCtrlRequest<
                         thrift::GetLinkDiscoveryScanStatus,
                         thrift::LinkDiscoveryScanStatus>(
                         json,
                         E2EConsts::kTopologyBuilderAppCtrlId,
                         thrift::MessageType::GET_LINK_DISCOVERY_SCAN_STATUS);
                   },
                   thrift::ApiCategory::TOPOLOGY,
                   thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /startContinuousTopologyScan Start Continuous Topology Scan
   * @apiVersion 2.0.0
   * @apiName StartContinuousTopologyScan
   * @apiPermission SCAN_WRITE
   * @apiGroup TopologyBuilder
   *
   * @apiDescription Run a continuous topology scan for a given duration on a single node.
   *
   * @apiUse StartContinuousTopoScan
   * @apiExample {curl} Example:
   *    curl -id '{"radioMac": "11:22:33:44:55:66", "durationSec": 60}' http://localhost:443/api/v2/startContinuousTopologyScan
   * @apiUse E2EAck_SUCCESS
   */
  map.insert({"startContinuousTopologyScan",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::StartContinuousTopoScan,
                        thrift::E2EAck>(
                        json,
                        E2EConsts::kTopologyBuilderAppCtrlId,
                        thrift::MessageType::START_CONTINUOUS_TOPO_SCAN);
                  },
                  thrift::ApiCategory::SCAN,
                  thrift::ApiLevel::WRITE,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /getIgnitionState Get Ignition State
   * @apiVersion 2.0.0
   * @apiName GetIgnitionState
   * @apiPermission IGNITION_READ
   * @apiGroup Ignition
   *
   * @apiDescription Retrieves the ignition state of the network.
   *
   * @apiUse GetIgnitionState
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getIgnitionState
   * @apiUse IgnitionState_SUCCESS
   * @apiUse IgnitionCandidate_SUCCESS
   * @apiUse IgnitionParams_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "igCandidates": [],
   *     "igParams": {
   *         "enable": true,
   *         "linkUpDampenInterval": 35,
   *         "linkUpInterval": 5,
   *         "linkAutoIgnite": {}
   *     },
   *     "lastIgCandidates": []
   * }
   */
  map.insert({"getIgnitionState",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetIgnitionState,
                        thrift::IgnitionState>(
                        json,
                        E2EConsts::kIgnitionAppCtrlId,
                        thrift::MessageType::GET_IGNITION_STATE);
                  },
                  thrift::ApiCategory::IGNITION,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /setIgnitionState Set Ignition State
   * @apiVersion 2.0.0
   * @apiName SetIgnitionState
   * @apiPermission IGNITION_WRITE
   * @apiGroup Ignition
   *
   * @apiDescription Sets the ignition parameters for the network.
   *
   * @apiUse IgnitionParams
   * @apiExample {curl} Example:
   *    curl -id '{"enable": true, "linkAutoIgnite": {"link-terra121.f5.td.a404-if-terra222.f5.td.a404-if": false}}' http://localhost:443/api/v2/setIgnitionState
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"setIgnitionState",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::IgnitionParams, thrift::E2EAck>(
                     json,
                     E2EConsts::kIgnitionAppCtrlId,
                     thrift::MessageType::SET_IGNITION_PARAMS);
           },
           thrift::ApiCategory::IGNITION,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /setLinkStatus Set Link Status
   * @apiVersion 2.0.0
   * @apiName SetLinkStatus
   * @apiPermission IGNITION_WRITE
   * @apiGroup Ignition
   *
   * @apiDescription Sets the status of a link.
   *
   * @apiUse SetLinkStatusReq
   * @apiExample {curl} Example:
   *    curl -id '{"action": 2, "initiatorNodeName": "terra111.f5.td.a404-if", "responderNodeName": "terra212.f5.td.a404-if"}' http://localhost:443/api/v2/setLinkStatus
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"setLinkStatus",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::SetLinkStatusReq, thrift::E2EAck>(
                     json,
                     E2EConsts::kIgnitionAppCtrlId,
                     thrift::MessageType::SET_LINK_STATUS_REQ);
           },
           thrift::ApiCategory::IGNITION,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /forceDissoc Force Dissoc
   * @apiVersion 2.0.0
   * @apiName ForceDissoc
   * @apiPermission IGNITION_WRITE
   * @apiGroup Ignition
   *
   * @apiDescription Forcefully disassociate a link to a given responder MAC address (which does not need to exist in the current topology).
   *
   * @apiUse ForceDissocReq
   * @apiExample {curl} Example:
   *    curl -id '{"initiatorMac": "11:22:33:44:55:66", "responderMac": "aa:bb:cc:dd:ee"}' http://localhost:443/api/v2/forceDissoc
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"forceDissoc",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::ForceDissocReq, thrift::E2EAck>(
                     json,
                     E2EConsts::kIgnitionAppCtrlId,
                     thrift::MessageType::FORCE_DISSOC_REQ);
           },
           thrift::ApiCategory::IGNITION,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /startScan Start Scan
   * @apiVersion 2.0.0
   * @apiName StartScan
   * @apiPermission SCAN_WRITE
   * @apiGroup Scan
   *
   * @apiDescription Starts a one-time scan.
   *
   * @apiUse StartScan
   * @apiUse BeamIndices_GROUP
   * @apiExample {curl} Example:
   *    curl -id '{"scanType": 1, "scanMode": 2, "startTime": 1535403177, "txNode": "terra111.f5.td.a404-if", "rxNodes": ["terra212.f5.td.a404-if"], "txPwrIndex": 255}' http://localhost:443/api/v2/startScan
   * @apiUse StartScanResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "message": "Scheduled topology scan 1",
   *     "success": true,
   *     "token": 1
   * }
   */
  map.insert(
      {"startScan",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::StartScan, thrift::StartScanResp>(
                     json,
                     E2EConsts::kScanAppCtrlId,
                     thrift::MessageType::START_SCAN);
           },
           thrift::ApiCategory::SCAN,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /getScanStatus Get Scan Status
   * @apiVersion 2.0.0
   * @apiName GetScanStatus
   * @apiPermission SCAN_READ
   * @apiGroup Scan
   *
   * @apiDescription Retrieves the scan status.
   *
   * @apiUse GetScanStatus
   * @apiExample {curl} Example:
   *    curl -id '{"isConcise": true}' http://localhost:443/api/v2/getScanStatus
   * @apiUse ScanStatus_SUCCESS
   * @apiUse ScanData_SUCCESS
   * @apiUse ScanResp_SUCCESS
   * @apiUse RouteInfo_SUCCESS
   * @apiUse MicroRoute_SUCCESS
   * @apiUse TopoResponderInfo_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "scans": {
   *         "1": {
   *             "responses": {
   *                 "terra114.f5.tb.a404-if": {
   *                     "curSuperframeNum": 747415639887,
   *                     "routeInfoList": [],
   *                     "token": 1
   *                 },
   *                 ... <more responses>
   *             },
   *             "startBwgdIdx": 46713477424,
   *             "txNode": "terra111.f5.tb.a404-if"
   *         },
   *         "10": {
   *             "responses": {},
   *             "startBwgdIdx": 46713478144,
   *             "txNode": "terra223.f5.tb.a404-if"
   *         },
   *         ... <more tokens>
   *     }
   * }
   */
  map.insert(
      {"getScanStatus",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::GetScanStatus, thrift::ScanStatus>(
                     json,
                     E2EConsts::kScanAppCtrlId,
                     thrift::MessageType::GET_SCAN_STATUS);
           },
           thrift::ApiCategory::SCAN,
           thrift::ApiLevel::READ,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /resetScanStatus Reset Scan Status
   * @apiVersion 2.0.0
   * @apiName ResetScanStatus
   * @apiPermission SCAN_WRITE
   * @apiGroup Scan
   *
   * @apiDescription Resets the scan status.
   *
   * @apiUse ResetScanStatus
   * @apiExample {curl} Example:
   *    curl -id '{}' http://localhost:443/api/v2/resetScanStatus
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"resetScanStatus",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::ResetScanStatus, thrift::E2EAck>(
                     json,
                     E2EConsts::kScanAppCtrlId,
                     thrift::MessageType::RESET_SCAN_STATUS);
           },
           thrift::ApiCategory::SCAN,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /setScanSchedule Set Scan Schedule
   * @apiVersion 2.0.0
   * @apiName SetScanSchedule
   * @apiPermission SCAN_WRITE
   * @apiGroup Scan
   *
   * @apiDescription Sets the scan schedule.
   *
   * @apiUse ScanSchedule
   * @apiExample {curl} Example:
   *    curl -id '{"imScanTimeoutSec": 2700, "combinedScanTimeoutSec": 3600, "pbfEnable": true, "rtcalEnable": true, "cbfEnable": true, "imEnable": true}' http://localhost:443/api/v2/setScanSchedule
   * @apiUse E2EAck_SUCCESS
   */
  map.insert({"setScanSchedule",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client
                        ->makeCtrlRequest<thrift::ScanSchedule, thrift::E2EAck>(
                            json,
                            E2EConsts::kScanAppCtrlId,
                            thrift::MessageType::SET_SCAN_SCHEDULE);
                  },
                  thrift::ApiCategory::SCAN,
                  thrift::ApiLevel::WRITE,
                  RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /getScanSchedule Get Scan Schedule
   * @apiVersion 2.0.0
   * @apiName GetScanSchedule
   * @apiPermission SCAN_READ
   * @apiGroup Scan
   *
   * @apiDescription Retrieves the scan schedule.
   *
   * @apiUse GetScanSchedule
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getScanSchedule
   * @apiUse GetScanScheduleResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "nextBwgdIdx": 0,
   *     "scanSchedule": {
   *         "cbfEnable": true,
   *         "combinedScanTimeoutSec": 3600,
   *         "imEnable": true,
   *         "imScanTimeoutSec": 2700,
   *         "pbfEnable": true,
   *         "rtcalEnable": true
   *     }
   * }
   */
  map.insert({"getScanSchedule",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::GetScanSchedule,
                        thrift::GetScanScheduleResp>(
                        json,
                        E2EConsts::kScanAppCtrlId,
                        thrift::MessageType::GET_SCAN_SCHEDULE);
                  },
                  thrift::ApiCategory::SCAN,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /startTraffic Start Iperf Traffic
   * @apiVersion 2.0.0
   * @apiName StartTraffic
   * @apiPermission PERFORMANCE_WRITE
   * @apiGroup Performance
   *
   * @apiDescription Starts an iperf traffic measurement between two nodes.
   *
   * @apiUse StartIperf
   * @apiUse IperfOptions_GROUP
   * @apiExample {curl} Example:
   *    curl -id '{"srcNodeId": "00:00:00:10:0d:40", "dstNodeId": "00:00:00:10:0d:42", "options": {"timeSec": 100}}' http://localhost:443/api/v2/startTraffic
   * @apiUse StartIperfResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "id": "14367480570677722902"
   * }
   */
  map.insert(
      {"startTraffic",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::StartIperf, thrift::StartIperfResp>(
                     json,
                     E2EConsts::kTrafficAppCtrlId,
                     thrift::MessageType::START_IPERF);
           },
           thrift::ApiCategory::PERFORMANCE,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /stopTraffic Stop Iperf Traffic
   * @apiVersion 2.0.0
   * @apiName StopTraffic
   * @apiPermission PERFORMANCE_WRITE
   * @apiGroup Performance
   *
   * @apiDescription Stops an iperf traffic measurement session.
   *
   * @apiUse StopIperf
   * @apiExample {curl} Example:
   *    curl -id '{"id": "14367480570677722902"}' http://localhost:443/api/v2/stopTraffic
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"stopTraffic",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::StopIperf, thrift::E2EAck>(
                 json,
                 E2EConsts::kTrafficAppCtrlId,
                 thrift::MessageType::STOP_IPERF);
           },
           thrift::ApiCategory::PERFORMANCE,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /statusTraffic Iperf Session Status
   * @apiVersion 2.0.0
   * @apiName StatusTraffic
   * @apiPermission PERFORMANCE_READ
   * @apiGroup Performance
   *
   * @apiDescription Retrieves information about all running iperf measurements.
   *
   * @apiUse GetIperfStatus
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/statusTraffic
   * @apiUse IperfStatus_SUCCESS
   * @apiUse StartMinionIperf_SUCCESS
   * @apiUse StartIperf_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "sessions": {
   *         "14367480570677722902": {
   *             "id": "14367480570677722902",
   *             "iperfConfig": {
   *                 "dstNodeId": "00:00:00:10:0d:42",
   *                 "dstNodeIpv6": "2001::1",
   *                 "options": {
   *                     "timeSec": 100
   *                 },
   *                 "srcNodeId": "00:00:00:10:0d:40",
   *             },
   *             "senderApp": "api-service-6777026746000997038",
   *             "serverPort": 70050
   *         }
   *     }
   * }
   */
  map.insert(
      {"statusTraffic",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::GetIperfStatus, thrift::IperfStatus>(
                     json,
                     E2EConsts::kTrafficAppCtrlId,
                     thrift::MessageType::GET_IPERF_STATUS);
           },
           thrift::ApiCategory::PERFORMANCE,
           thrift::ApiLevel::READ,
           RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /startPing Start Ping
   * @apiVersion 2.0.0
   * @apiName StartPing
   * @apiPermission PERFORMANCE_WRITE
   * @apiGroup Performance
   *
   * @apiDescription Starts a ping measurement between two nodes.
   *
   * @apiUse StartPing
   * @apiUse PingOptions_GROUP
   * @apiExample {curl} Example:
   *    curl -id '{"srcNodeId": "00:00:00:10:0d:40", "dstNodeId": "00:00:00:10:0d:42", "options": {"count": 10}}' http://localhost:443/api/v2/startPing
   * @apiUse StartPingResp_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "id": "5223465126179148667"
   * }
   */
  map.insert(
      {"startPing",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::StartPing, thrift::StartPingResp>(
                     json,
                     E2EConsts::kTrafficAppCtrlId,
                     thrift::MessageType::START_PING);
           },
           thrift::ApiCategory::PERFORMANCE,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /stopPing Stop Ping
   * @apiVersion 2.0.0
   * @apiName StopPing
   * @apiPermission PERFORMANCE_WRITE
   * @apiGroup Performance
   *
   * @apiDescription Stops a ping measurement session.
   *
   * @apiUse StopPing
   * @apiExample {curl} Example:
   *    curl -id '{"id": "5223465126179148667"}' http://localhost:443/api/v2/stopPing
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"stopPing",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client->makeCtrlRequest<thrift::StopPing, thrift::E2EAck>(
                 json,
                 E2EConsts::kTrafficAppCtrlId,
                 thrift::MessageType::STOP_PING);
           },
           thrift::ApiCategory::PERFORMANCE,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /statusPing Ping Session Status
   * @apiVersion 2.0.0
   * @apiName StatusPing
   * @apiPermission PERFORMANCE_READ
   * @apiGroup Performance
   *
   * @apiDescription Retrieves information about all running ping measurements.
   *
   * @apiUse GetPingStatus
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/statusPing
   * @apiUse PingStatus_SUCCESS
   * @apiUse StartMinionPing_SUCCESS
   * @apiUse StartPing_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "sessions": {
   *         "5223465126179148667": {
   *             "id": "5223465126179148667",
   *             "pingConfig": {
   *                 "dstNodeId": "00:00:00:10:0d:42",
   *                 "dstNodeIpv6": "2001::1",
   *                 "options": {
   *                     "count": 10
   *                 },
   *                 "srcNodeId": "00:00:00:10:0d:40",
   *             },
   *             "senderApp": "api-service-14942789954493701194"
   *         }
   *     }
   * }
   */
  map.insert(
      {"statusPing",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::GetPingStatus, thrift::PingStatus>(
                     json,
                     E2EConsts::kTrafficAppCtrlId,
                     thrift::MessageType::GET_PING_STATUS);
           },
           thrift::ApiCategory::PERFORMANCE,
           thrift::ApiLevel::READ,
           RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /sendUpgradeRequest Send Upgrade Request
   * @apiVersion 2.0.0
   * @apiName SendUpgradeRequest
   * @apiPermission UPGRADE_WRITE
   * @apiGroup Upgrade
   *
   * @apiDescription Sends an upgrade request (prepare, commit, or reset status).
   *
   * @apiUse UpgradeGroupReq
   * @apiUse UpgradeReq_GROUP
   * @apiUse UpgradeTorrentParams_GROUP
   * @apiExample {curl} Example:
   *    curl -id '{"ugType": 20, "nodes": [], "excludeNodes": [], "urReq": {"urType": 10, "upgradeReqId": "20171127162252", "md5": "176454de5884bbe173c2015e5145fc97", "imageUrl": "magnet:?xt=urn:btih:3427351029eebbb923570440de57e738d892d996&dn=tg-update-qoriq.bin&tr=http%3a%2f%2f%5b2001%3a%3a1%5d%3a6969%2fannounce", "torrentParams": {"downloadTimeout": 180}}, "timeout": 180, "skipFailure": false, "version": "", "skipLinks": [], "limit": 0, "retryLimit": 3}' http://localhost:443/api/v2/sendUpgradeRequest
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"sendUpgradeRequest",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::UpgradeGroupReq, thrift::E2EAck>(
                     json,
                     E2EConsts::kUpgradeAppCtrlId,
                     thrift::MessageType::UPGRADE_GROUP_REQ);
           },
           thrift::ApiCategory::UPGRADE,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /abortUpgrade Abort Upgrade
   * @apiVersion 2.0.0
   * @apiName AbortUpgrade
   * @apiPermission UPGRADE_WRITE
   * @apiGroup Upgrade
   *
   * @apiDescription Aborts an upgrade.
   *
   * @apiUse UpgradeAbortReq
   * @apiExample {curl} Example:
   *    curl -id '{"abortAll": true, "reqIds": [], "resetStatus": true}' http://localhost:443/api/v2/abortUpgrade
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"abortUpgrade",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::UpgradeAbortReq, thrift::E2EAck>(
                     json,
                     E2EConsts::kUpgradeAppCtrlId,
                     thrift::MessageType::UPGRADE_ABORT_REQ);
           },
           thrift::ApiCategory::UPGRADE,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /getUpgradeState Get Upgrade State
   * @apiVersion 2.0.0
   * @apiName GetUpgradeState
   * @apiPermission UPGRADE_READ
   * @apiGroup Upgrade
   *
   * @apiDescription Retrieves the current upgrade state from the controller.
   *
   * @apiUse UpgradeStateReq
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/getUpgradeState
   * @apiUse UpgradeStateDump_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "curBatch": [
   *         "terra312.f5.tb.a404-if",
   *         "terra223.f5.tb.a404-if",
   *         "terra222.f5.tb.a404-if",
   *         "terra314.f5.tb.a404-if",
   *         "terra214.f5.tb.a404-if",
   *         "terra212.f5.tb.a404-if",
   *         "terra211.f5.tb.a404-if",
   *         "terra322.f5.tb.a404-if",
   *         "terra123.f5.tb.a404-if",
   *         "terra323.f5.tb.a404-if",
   *         "terra221.f5.tb.a404-if",
   *         "terra114.f5.tb.a404-if",
   *         "terra121.f5.tb.a404-if",
   *         "terra111.f5.tb.a404-if"
   *     ],
   *     "curReq": {
   *         "excludeNodes": [],
   *         "limit": 0,
   *         "nodes": [],
   *         "skipFailure": false,
   *         "skipLinks": [],
   *         "timeout": 180,
   *         "retryLimit": 3,
   *         "ugType": 20,
   *         "urReq": {
   *             "imageUrl": "magnet:?xt=urn:btih:3427351029eebbb923570440de57e738d892d996&dn=tg-update-qoriq.bin&tr=http%3a%2f%2f%5b2001%3a%3a1%5d%3a6969%2fannounce",
   *             "md5": "176454de5884bbe173c2015e5145fc97",
   *             "torrentParams": {
   *                 "downloadTimeout": 180
   *             },
   *             "upgradeReqId": "20171127162252",
   *             "urType": 10
   *         },
   *         "version": ""
   *     },
   *     "pendingBatches": [],
   *     "pendingReqs": []
   * }
   */
  map.insert({"getUpgradeState",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::UpgradeStateReq,
                        thrift::UpgradeStateDump>(
                        json,
                        E2EConsts::kUpgradeAppCtrlId,
                        thrift::MessageType::UPGRADE_STATE_REQ);
                  },
                  thrift::ApiCategory::UPGRADE,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /addUpgradeImage Add Upgrade Image
   * @apiVersion 2.0.0
   * @apiName AddUpgradeImage
   * @apiPermission UPGRADE_WRITE
   * @apiGroup Upgrade
   *
   * @apiDescription Adds an image to be hosted by the controller.
   *
   * @apiUse UpgradeAddImageReq
   * @apiExample {curl} Example:
   *    curl -id '{"imageUrl": "https://example.com/tg-update-qoriq.bin"}' http://localhost:443/api/v2/addUpgradeImage
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"addUpgradeImage",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::UpgradeAddImageReq, thrift::E2EAck>(
                     json,
                     E2EConsts::kUpgradeAppCtrlId,
                     thrift::MessageType::UPGRADE_ADD_IMAGE_REQ);
           },
           thrift::ApiCategory::UPGRADE,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {post} /delUpgradeImage Delete Upgrade Image
   * @apiVersion 2.0.0
   * @apiName DelUpgradeImage
   * @apiPermission UPGRADE_WRITE
   * @apiGroup Upgrade
   *
   * @apiDescription Deletes an image from the controller.
   *
   * @apiUse UpgradeDelImageReq
   * @apiExample {curl} Example:
   *    curl -id '{"name": "Facebook Terragraph Release RELEASE_M16_1-michaelcallahan (michaelcallahan@devbig730 Wed Nov 22 18:27:41 PST 2017)"}' http://localhost:443/api/v2/delUpgradeImage
   * @apiUse E2EAck_SUCCESS
   */
  map.insert(
      {"delUpgradeImage",
       RequestFunction(
           [](CLIENT client, JSON json) -> RESPONSE {
             return client
                 ->makeCtrlRequest<thrift::UpgradeDelImageReq, thrift::E2EAck>(
                     json,
                     E2EConsts::kUpgradeAppCtrlId,
                     thrift::MessageType::UPGRADE_DEL_IMAGE_REQ);
           },
           thrift::ApiCategory::UPGRADE,
           thrift::ApiLevel::WRITE,
           RequestFunction::HTTPMethod::POST)});

  /**
   * @api {get} /listUpgradeImages List Upgrade Images
   * @apiVersion 2.0.0
   * @apiName ListUpgradeImages
   * @apiPermission UPGRADE_READ
   * @apiGroup Upgrade
   *
   * @apiDescription Retrieves the list of all images hosted by the controller.
   *
   * @apiUse UpgradeListImagesReq
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/api/v2/listUpgradeImages
   * @apiUse UpgradeListImagesResp_SUCCESS
   * @apiUse UpgradeImage_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "images": [
   *         {
   *             "magnetUri": "magnet:?xt=urn:btih:f8b37b30c31f201645c37a19f3fd2f08dde162b2&dn=176454de5884bbe173c2015e5145fc97.bin&tr=http%3a%2f%2f%5b%3a%3a1%5d%3a6969%2fannounce",
   *             "httpUri": "http://[fd00::1]/node_image/network_name/md5.bin",
   *             "md5": "176454de5884bbe173c2015e5145fc97",
   *             "name": "Facebook Terragraph Release RELEASE_M16_1-michaelcallahan (michaelcallahan@devbig730 Wed Nov 22 18:27:41 PST 2017)"
   *         }
   *     ]
   * }
   */
  map.insert({"listUpgradeImages",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::UpgradeListImagesReq,
                        thrift::UpgradeListImagesResp>(
                        json,
                        E2EConsts::kUpgradeAppCtrlId,
                        thrift::MessageType::UPGRADE_LIST_IMAGES_REQ);
                  },
                  thrift::ApiCategory::UPGRADE,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::GET)});

  /**
   * @api {post} /getUpgradeCommitPlan Get Upgrade Commit Plan
   * @apiVersion 2.0.0
   * @apiName GetUpgradeCommitPlan
   * @apiPermission UPGRADE_READ
   * @apiGroup Upgrade
   *
   * @apiDescription Retrieves the upgrade commit plan containing the batching order at which nodes will be upgraded.
   *
   * @apiUse UpgradeCommitPlanReq
   * @apiExample {curl} Example:
   *    curl -id '{"limit": 10, "excludeNodes": ["terra411.f7.tg.a404-if"]}' http://localhost:443/api/v2/getUpgradeCommitPlan
   * @apiUse UpgradeCommitPlan_SUCCESS
   * @apiSuccessExample {json} Success-Response:
   * {
   *     "commitBatches": [
   *         [
   *             "terra121.f3.tg.a404-if",
   *             "terra324.f3.tg.a404-if",
   *             "terra322.f3.tg.a404-if",
   *             "terra123.f3.tg.a404-if",
   *             "terra611.f1.tg.a404-if"
   *         ],
   *         [
   *             "terra623.f5.tg.a404-if",
   *             "terra622.f5.tg.a404-if"
   *         ],
   *         [
   *             "terra221.f1.tg.a404-if",
   *             "terra224.f1.tg.a404-if",
   *             "terra413.f1.tg.a404-if"
   *         ]
   *     ]
   * }
   */
  map.insert({"getUpgradeCommitPlan",
              RequestFunction(
                  [](CLIENT client, JSON json) -> RESPONSE {
                    return client->makeCtrlRequest<
                        thrift::UpgradeCommitPlanReq,
                        thrift::UpgradeCommitPlan>(
                        json,
                        E2EConsts::kUpgradeAppCtrlId,
                        thrift::MessageType::UPGRADE_COMMIT_PLAN_REQ);
                  },
                  thrift::ApiCategory::UPGRADE,
                  thrift::ApiLevel::READ,
                  RequestFunction::HTTPMethod::POST)});

  return map;
}();

bool
ApiClient::contains(const std::string& methodName) {
  return !!methods_.count(methodName);
}

std::optional<ApiClient::RequestFunction>
ApiClient::get(const std::string& methodName) {
  auto iter = methods_.find(methodName);
  if (iter == methods_.end()) {
    return std::nullopt;
  }
  return iter->second;
}

ApiClient::ApiClient(
    fbzmq::Context& context,
    const std::string& ctrlRouterUrl,
    const std::string& aggrRouterUrl,
    const std::string& zmqId)
    : ctrlSock_{context, fbzmq::IdentityString{zmqId}},
      aggrSock_{context, fbzmq::IdentityString{zmqId}},
      ctrlRouterUrl_(ctrlRouterUrl),
      aggrRouterUrl_(aggrRouterUrl),
      zmqId_(zmqId) {
}

template <class ThriftRequestType, class ThriftResponseType>
std::optional<std::string>
ApiClient::makeCtrlRequest(
    const std::string& json,
    const std::string& receiverId,
    const thrift::MessageType& mType) {
  // Try to deserialize the request (JSON -> Thrift)
  auto thriftRequest = deserializeFromJson<ThriftRequestType>(json);
  if (!thriftRequest) {
    return std::nullopt;
  }

  // Connect to the controller (if necessary)
  connectToCtrl();
  if (!ctrlConnected_) {
    return std::nullopt;
  }

  // Send the ZMQ request to the controller
  sendToCtrl(receiverId, mType, thriftRequest.value());

  // Wait for a reply
  auto thriftResponse = receiveFromCtrl();
  if (!thriftResponse) {
    return std::nullopt;
  }

  // Deserialize the response and serialize it to the client (Thrift -> JSON)
  if (thriftResponse->mType == thrift::MessageType::E2E_ACK) {
    // Check if the response is an E2EAck (the default failure class)
    auto ack = deserializeFromThrift<thrift::E2EAck>(thriftResponse->value);
    if (ack) {
      return serializeToJson<thrift::E2EAck>(ack.value());
    }
  } else {
    // Try the supplied Thrift struct
    auto msg = deserializeFromThrift<ThriftResponseType>(thriftResponse->value);
    if (msg) {
      return serializeToJson<ThriftResponseType>(msg.value());
    }
  }
  LOG(ERROR) << "Thrift deserialization failed.";
  return std::nullopt;
}

template <class ThriftRequestType, class ThriftResponseType>
std::optional<std::string>
ApiClient::makeAggrRequest(
    const std::string& json,
    const std::string& receiverId,
    const thrift::AggrMessageType& mType) {
  // Try to deserialize the request (JSON -> Thrift)
  auto thriftRequest = deserializeFromJson<ThriftRequestType>(json);
  if (!thriftRequest) {
    return std::nullopt;
  }

  // Connect to the aggregator (if necessary)
  connectToAggr();
  if (!aggrConnected_) {
    return std::nullopt;
  }

  // Send the ZMQ request to the aggregator
  sendToAggr(receiverId, mType, thriftRequest.value());

  // Wait for a reply
  auto thriftResponse = receiveFromAggr();
  if (!thriftResponse) {
    return std::nullopt;
  }

  // Deserialize the response and serialize it to the client (Thrift -> JSON)
  if (thriftResponse->mType == thrift::AggrMessageType::AGGR_ACK) {
    // Check if the response is an AggrAck (the default failure class)
    auto ack = deserializeFromThrift<thrift::AggrAck>(thriftResponse->value);
    if (ack) {
      return serializeToJson<thrift::AggrAck>(ack.value());
    }
  } else {
    // Try the supplied Thrift struct
    auto msg = deserializeFromThrift<ThriftResponseType>(thriftResponse->value);
    if (msg) {
      return serializeToJson<ThriftResponseType>(msg.value());
    }
  }
  LOG(ERROR) << "Thrift deserialization failed.";
  return std::nullopt;
}

template <class T>
void
ApiClient::sendToCtrl(
    const std::string& receiverId,
    const thrift::MessageType& mType,
    const T& obj) {
  thrift::Message msg;
  msg.mType = mType;
  msg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);

  const auto res = ctrlSock_.sendMultiple(
      fbzmq::Message(),
      fbzmq::Message::from(receiverId).value(),
      fbzmq::Message::from(zmqId_).value(),
      fbzmq::Message::fromThriftObj(msg, serializer_).value());

  if (res.hasError()) {
    LOG(FATAL) << "Error sending "
               << TEnumMapFactory<thrift::MessageType>
               ::makeValuesToNamesMap().at(mType) << " to :" << receiverId
               << " from " << zmqId_ << ": "
               << res.error();
  }
}

template <class T>
void
ApiClient::sendToAggr(
    const std::string& receiverId,
    const thrift::AggrMessageType& mType,
    const T& obj) {
  thrift::AggrMessage msg;
  msg.mType = mType;
  msg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);

  const auto res = aggrSock_.sendMultiple(
      fbzmq::Message(),
      fbzmq::Message::from(receiverId).value(),
      fbzmq::Message::from(zmqId_).value(),
      fbzmq::Message::fromThriftObj(msg, serializer_).value());

  if (res.hasError()) {
    LOG(FATAL) << "Error sending "
               << TEnumMapFactory<thrift::AggrMessageType>
                  ::makeValuesToNamesMap().at(mType) << " to " << receiverId
                  << " from " << zmqId_ << ": " << res.error();
  }
}

std::optional<thrift::Message>
ApiClient::receiveFromCtrl() {
  fbzmq::Message minion, senderApp, data;
  auto res = ctrlSock_.recvMultipleTimeout(
      std::chrono::milliseconds(FLAGS_zmq_rcv_timeout_ms),
      minion,
      senderApp,
      data);
  if (res.hasError()) {
    LOG(ERROR) << "Error reading message from controller: " << res.error();
    return std::nullopt;
  }

  auto message = data.readThriftObj<thrift::Message>(serializer_);
  if (message.hasError()) {
    LOG(ERROR) << "Error parsing message from controller: " << res.error();
    return std::nullopt;
  }

  // Decompress the message (if needed)
  std::string error;
  if (!facebook::terragraph::CompressionUtil::decompress(
        message.value(), error)) {
    LOG(ERROR) << "Error decompressing message from controller: " << error;
    return std::nullopt;
  }

  return message.value();
}

std::optional<thrift::AggrMessage>
ApiClient::receiveFromAggr() {
  fbzmq::Message agent, senderApp, data;
  auto res = aggrSock_.recvMultipleTimeout(
      std::chrono::milliseconds(FLAGS_zmq_rcv_timeout_ms),
      agent,
      senderApp,
      data);
  if (res.hasError()) {
    LOG(ERROR) << "Error reading message from aggregator: " << res.error();
    return std::nullopt;
  }

  auto message = data.readThriftObj<thrift::AggrMessage>(serializer_);
  if (message.hasError()) {
    LOG(ERROR) << "Error parsing message from aggregator: " << res.error();
    return std::nullopt;
  }

  // Decompress the message (if needed)
  std::string error;
  if (!facebook::terragraph::stats::CompressionUtil::decompress(
        message.value(), error)) {
    LOG(ERROR) << "Error decompressing message from aggregator: " << error;
    return std::nullopt;
  }

  return message.value();
}

template <class T>
std::string
ApiClient::serializeToJson(const T& obj) {
  return apache::thrift::SimpleJSONSerializer::serialize<std::string>(obj);
}

template <class T>
std::optional<T>
ApiClient::deserializeFromJson(const std::string& s) {
  try {
    return apache::thrift::SimpleJSONSerializer::deserialize<T>(s);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "JSON deserialization failed: " << folly::exceptionStr(ex);
    return std::nullopt;
  }
}

template <class T>
std::optional<T>
ApiClient::deserializeFromThrift(const std::string& buf) {
  try {
    return fbzmq::util::readThriftObjStr<T>(buf, serializer_);
  } catch (const std::exception& ex) {
    return std::nullopt;
  }
}

void
ApiClient::connectToCtrl() {
  if (ctrlConnected_ || ctrlRouterUrl_.empty()) {
    return;
  }
  auto res = ctrlSock_.connect(fbzmq::SocketUrl{ctrlRouterUrl_});
  if (res) {
    ctrlConnected_ = true;
  } else {
    LOG(ERROR) << "Error connecting to controller URL '" << ctrlRouterUrl_
               << "': " << res.error();
  }
}

void
ApiClient::connectToAggr() {
  if (aggrConnected_ || aggrRouterUrl_.empty()) {
    return;
  }
  auto res = aggrSock_.connect(fbzmq::SocketUrl{aggrRouterUrl_});
  if (res) {
    aggrConnected_ = true;
  } else {
    LOG(ERROR) << "Error connecting to aggregator URL '" << aggrRouterUrl_
               << "': " << res.error();
  }
}

} // namesapce api
} // namespace terragraph
} // namespace facebook
