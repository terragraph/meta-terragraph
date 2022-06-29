/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Consts.h"

namespace facebook {
namespace terragraph {

// --- Stats matching
// key name to friendly/short name matching
const std::vector<StatFormat> WebUIConsts::kStatsFormat{
    StatFormat("phystatus.ssnrEst", "tgf", "snr"),
    StatFormat("phystatus.srssi", "tgf", "rssi"),
    StatFormat("staPkt.mcs", "tgf", "mcs")};

} // namespace terragraph
} // namespace facebook
