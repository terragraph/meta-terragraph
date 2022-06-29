/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "SharedObjects.h"

namespace facebook {
namespace terragraph {

// Singleton declarations
static folly::Singleton<folly::Synchronized<
    TopologyWrapper>> topologyWrapperSingleton;
static folly::Singleton<folly::Synchronized<
    std::unordered_map<std::string, StatusApp::StatusReport>>>
        statusReportsSingleton;
static folly::Singleton<folly::Synchronized<
    thrift::RoutingAdjacencies>> routingAdjacenciesSingleton;
static folly::Singleton<folly::Synchronized<
    ConfigHelper>> configHelperSingleton;
static folly::Singleton<folly::Synchronized<
    E2EConfigWrapper<thrift::ControllerConfig>>> e2eConfigWrapperSingleton;
static folly::Singleton<folly::Synchronized<
    BinaryStarApp::SyncedAppData>> syncedAppDataSingleton;
static folly::Singleton<folly::Synchronized<std::string>> topologyName;

std::shared_ptr<folly::Synchronized<TopologyWrapper>>
SharedObjects::getTopologyWrapper() {
  return topologyWrapperSingleton.try_get();
}

std::shared_ptr<folly::Synchronized<
    std::unordered_map<std::string, StatusApp::StatusReport>>>
SharedObjects::getStatusReports() {
  return statusReportsSingleton.try_get();
}

std::shared_ptr<folly::Synchronized<thrift::RoutingAdjacencies>>
SharedObjects::getRoutingAdjacencies() {
  return routingAdjacenciesSingleton.try_get();
}

std::shared_ptr<folly::Synchronized<ConfigHelper>>
SharedObjects::getConfigHelper() {
  return configHelperSingleton.try_get();
}

std::shared_ptr<folly::Synchronized<E2EConfigWrapper<thrift::ControllerConfig>>>
SharedObjects::getE2EConfigWrapper() {
  return e2eConfigWrapperSingleton.try_get();
}

std::shared_ptr<folly::Synchronized<BinaryStarApp::SyncedAppData>>
SharedObjects::getSyncedAppData() {
  return syncedAppDataSingleton.try_get();
}

std::shared_ptr<folly::Synchronized<std::string>>
SharedObjects::getTopologyName() {
  return topologyName.try_get();
}

} // namespace terragraph
} // namespace facebook
