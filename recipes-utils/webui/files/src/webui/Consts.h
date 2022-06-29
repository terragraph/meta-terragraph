/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "e2e/clients/StatCache.h"

namespace facebook {
namespace terragraph {

class WebUIConsts {
 public:
  // --- Stats matching
  const static std::vector<StatFormat> kStatsFormat;
};
} // namespace terragraph
} // namespace facebook
