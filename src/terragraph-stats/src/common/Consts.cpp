/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Consts.h"

namespace facebook {
namespace terragraph {
namespace stats {

// --- Aggregator ---
const std::string NMSConsts::kBrokerAggrId{"aggr-BROKER"};
const std::string NMSConsts::kConfigAppAggrId{"aggr-app-CONFIG_APP"};
const std::string NMSConsts::kStatusAppAggrId{"aggr-app-STATUS_APP"};
const std::string NMSConsts::kStatsAppAggrId{"aggr-app-STATS_APP"};

// --- Stats agent ---
const std::string NMSConsts::kNmsPublisherId{"nms-PUBLISHER"};
const std::string NMSConsts::kInputListenerId{"zmq_monitor_input_listener"};
const std::string NMSConsts::kFibAgentHost{"localhost"};
const int NMSConsts::kFibAgentClientId{786};

// --- logtail ---
const std::string NMSConsts::kLogPublisherId{"nms-LOGPUBLISHER"};

// --- Timeouts ---
const std::chrono::milliseconds NMSConsts::kPollTimeout{50};
const std::chrono::milliseconds NMSConsts::kReadTimeout{500};
const std::chrono::milliseconds NMSConsts::kFibAgentProcTimeout{1000};
const std::chrono::milliseconds NMSConsts::kFibAgentConnTimeout{100};

// --- Socket Option ---
// Should stats use TC0 Green?
const int NMSConsts::kIpTos{0x22 << 2};

} // namespace stats
} // namespace terragraph
} // namespace facebook
