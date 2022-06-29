/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>

#include <openr/common/OpenrClient.h>

#include "BaseCounters.h"

namespace facebook {
namespace terragraph {
namespace stats {

class OpenrCounters : BaseCounters {
 public:
  OpenrCounters() = default;

  std::unordered_map<std::string, fbzmq::thrift::Counter> fetchStats()
      override;
  std::unordered_map<std::string, fbzmq::thrift::Counter> fetchStats(
      openr::thrift::OpenrCtrlAsyncClient* openrCtrlClient);
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
