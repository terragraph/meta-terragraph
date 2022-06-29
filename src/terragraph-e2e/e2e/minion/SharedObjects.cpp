/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "SharedObjects.h"

namespace facebook {
namespace terragraph {
namespace minion {

// Singleton declarations
static folly::Singleton<folly::Synchronized<
    NodeConfigWrapper>> nodeConfigWrapperSingleton;
static folly::Singleton<folly::Synchronized<
    NodeInfoWrapper>> nodeInfoWrapperSingleton;
static folly::Singleton<folly::Synchronized<
    std::unordered_map<std::string, thrift::NodeType>>>
        peerNodeTypeMapSingleton;
static folly::Singleton<folly::Synchronized<std::string>>
    firmwareVersionSingleton;
static folly::Singleton<folly::Synchronized<
    std::unordered_map<std::string, thrift::LinkStatus>>>
        ignitedNeighborsSingleton;

std::shared_ptr<folly::Synchronized<NodeConfigWrapper>>
SharedObjects::getNodeConfigWrapper() {
  return nodeConfigWrapperSingleton.try_get();
}

std::shared_ptr<folly::Synchronized<NodeInfoWrapper>>
SharedObjects::getNodeInfoWrapper() {
  return nodeInfoWrapperSingleton.try_get();
}

std::shared_ptr<folly::Synchronized<
    std::unordered_map<std::string, thrift::NodeType>>>
SharedObjects::getPeerNodeTypeMap() {
  return peerNodeTypeMapSingleton.try_get();
}

std::shared_ptr<folly::Synchronized<std::string>>
SharedObjects::getFirmwareVersion() {
  return firmwareVersionSingleton.try_get();
}

std::shared_ptr<folly::Synchronized<
    std::unordered_map<std::string, thrift::LinkStatus>>>
SharedObjects::getIgnitedNeighbors() {
  return ignitedNeighborsSingleton.try_get();
}

} // namespace minion
} // namespace terragraph
} // namespace facebook
