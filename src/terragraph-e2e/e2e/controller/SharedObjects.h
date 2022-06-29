/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Singleton.h>
#include <folly/Synchronized.h>

#include "BinaryStarApp.h"
#include "ConfigHelper.h"
#include "StatusApp.h"
#include "e2e/common/E2EConfigWrapper.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "topology/TopologyWrapper.h"

/**
 * \def LOCK_TOPOLOGY_AND_CONFIG(lockedTopologyW, fn, lockedConfigHelper, fn)
 *
 * Macro to acquire dual locks on TopologyWrapper and ConfigHelper.
 *
 * 'fn' is the folly::Synchronized method to call, usually rlock() or wlock().
 *
 * These objects are commonly needed together. By locking in a fixed order,
 * deadlocks are avoided.
 */
#define LOCK_TOPOLOGY_AND_CONFIG(                              \
    lockedTopologyW,                                           \
    topologyLockType,                                          \
    lockedConfigHelper,                                        \
    configHelperLockType)                                      \
  auto lockedTopologyW =                                       \
      SharedObjects::getTopologyWrapper()->topologyLockType(); \
  auto lockedConfigHelper =                                    \
      SharedObjects::getConfigHelper()->configHelperLockType();

namespace facebook {
namespace terragraph {

/**
 * Synchronized globals for the E2E controller.
 *
 * This class manages globally-shared objects, which can be accessed
 * concurrently by different apps/threads. The shared objects are singletons
 * that are lazy-loaded; the internal data can only be accessed by acquiring
 * read/write locks.
 */
class SharedObjects {
 public:
  /** Returns the single shared topology wrapper instance. */
  static std::shared_ptr<folly::Synchronized<TopologyWrapper>>
      getTopologyWrapper();

  /** Returns the mapping from minion MAC to latest status report received. */
  static std::shared_ptr<folly::Synchronized<
      std::unordered_map<std::string, StatusApp::StatusReport>>>
          getStatusReports();

  /** Returns the single shared routing adjacencies structure. */
  static std::shared_ptr<folly::Synchronized<thrift::RoutingAdjacencies>>
      getRoutingAdjacencies();

  /**
   * Returns the single shared node config helper instance.
   *
   * Note that this object needs to be manually initialized by the caller.
   */
  static std::shared_ptr<folly::Synchronized<ConfigHelper>>
      getConfigHelper();

  /**
   * Returns the single shared E2E config helper instance.
   *
   * Note that this object needs to be manually initialized by the caller.
   */
  static std::shared_ptr<folly::Synchronized<
      E2EConfigWrapper<thrift::ControllerConfig>>>
          getE2EConfigWrapper();

  /** Returns the synced app data in the "Binary Star" replication scheme. */
  static std::shared_ptr<folly::Synchronized<BinaryStarApp::SyncedAppData>>
      getSyncedAppData();

  /** Returns the topology name. */
  static std::shared_ptr<folly::Synchronized<std::string>> getTopologyName();
};

} // namespace terragraph
} // namespace facebook
