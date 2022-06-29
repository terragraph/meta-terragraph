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

static folly::Singleton<folly::Synchronized<
    E2EConfigWrapper<thrift::AggregatorConfig>>> aggrConfigWrapperSingleton;

std::shared_ptr<folly::Synchronized<E2EConfigWrapper<thrift::AggregatorConfig>>>
SharedObjects::getAggrConfigWrapper() {
  return aggrConfigWrapperSingleton.try_get();
}

} // namesapce stats
} // namespace terragraph
} // namespace facebook
