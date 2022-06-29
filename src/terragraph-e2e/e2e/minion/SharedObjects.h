/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Singleton.h>
#include <folly/Synchronized.h>

#include "IgnitionApp.h"
#include "e2e/common/NodeConfigWrapper.h"
#include "e2e/common/NodeInfoWrapper.h"
#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {
namespace minion {

/**
 * Synchronized globals for the E2E minion.
 *
 * This class manages globally-shared objects, which can be accessed
 * concurrently by different apps/threads. The shared objects are singletons
 * that are lazy-loaded; the internal data can only be accessed by acquiring
 * read/write locks.
 */
class SharedObjects {
 public:
  /**
   * Returns the single shared node config wrapper instance.
   *
   * Note that this object needs to be manually initialized by the caller.
   */
  static std::shared_ptr<folly::Synchronized<NodeConfigWrapper>>
      getNodeConfigWrapper();

  /**
   * Returns the single shared node info wrapper instance.
   *
   * Note that this object needs to be manually initialized by the caller.
   */
  static std::shared_ptr<folly::Synchronized<NodeInfoWrapper>>
      getNodeInfoWrapper();

  /** Returns the mapping from peer MAC to its node type. */
  static std::shared_ptr<folly::Synchronized<
      std::unordered_map<std::string, thrift::NodeType>>>
          getPeerNodeTypeMap();

  /** Returns the firmware version string. */
  static std::shared_ptr<folly::Synchronized<std::string>> getFirmwareVersion();

  /**
   * Returns information about ignited neighbors (neighbors to whom a wireless
   * link is up from our perspective).
   */
  static std::shared_ptr<folly::Synchronized<
      std::unordered_map<
          std::string /* responderMac */,
          thrift::LinkStatus>>> getIgnitedNeighbors();
};

} // namespace minion
} // namespace terragraph
} // namespace facebook
