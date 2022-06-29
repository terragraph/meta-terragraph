/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Singleton.h>
#include <folly/Synchronized.h>

#include "e2e/common/E2EConfigWrapper.h"
#include "stats/if/gen-cpp2/Aggregator_types.h"

namespace facebook {
namespace terragraph {
namespace stats {

class SharedObjects {
 public:
  // Returns the single shared config wrapper instance.
  // Note that this object needs to be manually initialized by the caller
  static std::shared_ptr<folly::Synchronized<
      E2EConfigWrapper<thrift::AggregatorConfig>>>
          getAggrConfigWrapper();
};

} // namesapce stats
} // namespace terragraph
} // namespace facebook
