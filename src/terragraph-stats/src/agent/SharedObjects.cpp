/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "SharedObjects.h"

namespace facebook {
namespace terragraph {
namespace stats {

// Singleton declarations
static folly::Singleton<folly::Synchronized<
    NodeConfigWrapper>> nodeConfigWrapperSingleton;
static folly::Singleton<folly::Synchronized<
    NodeInfoWrapper>> nodeInfoWrapperSingleton;

std::shared_ptr<folly::Synchronized<NodeConfigWrapper>>
SharedObjects::getNodeConfigWrapper() {
  return nodeConfigWrapperSingleton.try_get();
}

std::shared_ptr<folly::Synchronized<NodeInfoWrapper>>
SharedObjects::getNodeInfoWrapper() {
  return nodeInfoWrapperSingleton.try_get();
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
