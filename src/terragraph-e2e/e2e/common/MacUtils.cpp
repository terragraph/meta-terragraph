/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "MacUtils.h"
#include <folly/MacAddress.h>

namespace facebook {
namespace terragraph {

std::string
MacUtils::standardizeMac(const std::string& nodeMac) {
  return folly::MacAddress(nodeMac).toString();
}

} // namespace terragraph
} // namespace facebook
