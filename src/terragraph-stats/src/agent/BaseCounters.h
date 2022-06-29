/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "ZmqCounterUtils.h"

#include <unordered_map>

#include <boost/noncopyable.hpp>
#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>

namespace facebook {
namespace terragraph {
namespace stats {

/**
 * Base class for exposing entity/key combinations.
 *
 * The entity is equivalent to a category (system, app name, etc.).
 * The key is the specific item, usually with a prefix for similar
 * items (mount.total:/, mount.free:/, etc.).
 */
class BaseCounters : public ZmqCounterUtils, public boost::noncopyable {
 public:
  virtual ~BaseCounters() {}

  // update fb303-style stats
  virtual std::unordered_map<std::string, fbzmq::thrift::Counter>
      fetchStats() = 0;
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
