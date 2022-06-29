/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Consts.h"

#include <folly/String.h>

namespace facebook {
namespace terragraph {

// --- Controller ---
const std::string E2EConsts::kMainCtrlId{"ctrl-MAIN"};
const std::string E2EConsts::kBrokerCtrlId{"ctrl-BROKER"};

// --- Minion ---
const std::string E2EConsts::kBrokerMinionId{"minion-BROKER"};
const std::string E2EConsts::kBroadcastSockMinionId{"minion-BROADCAST-PUB"};

// --- Apps ---
const std::string E2EConsts::kIgnitionAppCtrlId{"ctrl-app-IGNITION_APP"};
const std::string E2EConsts::kIgnitionAppMinionId{"minion-app-IGNITION_APP"};
const std::string E2EConsts::kStatusAppCtrlId{"ctrl-app-STATUS_APP"};
const std::string E2EConsts::kStatusAppMinionId{"minion-app-STATUS_APP"};
const std::string E2EConsts::kTopologyAppCtrlId{"ctrl-app-TOPOLOGY_APP"};
const std::string E2EConsts::kDriverAppMinionId{"minion-app-DRIVER_APP"};
const std::string E2EConsts::kUpgradeAppCtrlId{"ctrl-app-UPGRADE_APP"};
const std::string E2EConsts::kUpgradeAppMinionId{"minion-app-UPGRADE_APP"};
const std::string E2EConsts::kScanAppCtrlId{"ctrl-app-SCAN_APP"};
const std::string E2EConsts::kConfigAppCtrlId{"ctrl-app-CONFIG_APP"};
const std::string E2EConsts::kConfigAppMinionId{"minion-app-CONFIG_APP"};
const std::string E2EConsts::kSchedulerAppCtrlId{"ctrl-app-SCHEDULER_APP"};
const std::string E2EConsts::kOpenrClientAppMinionId{
    "minion-app-OPENRCLIENT_APP"};
const std::string E2EConsts::kTrafficAppCtrlId{"ctrl-app-TRAFFIC_APP"};
const std::string E2EConsts::kTrafficAppMinionId{"minion-app-TRAFFIC_APP"};
const std::string E2EConsts::kBinaryStarAppCtrlId{"ctrl-app-BINARYSTAR_APP"};
const std::string E2EConsts::kTopologyBuilderAppCtrlId{
    "ctrl-app-TOPOLOGYBUILDER_APP"};

// --- Provision ---
const std::string E2EConsts::kCtrlUrlKey{"e2e-ctrl-url"};
const std::string E2EConsts::kAggrUrlKey{"e2e-aggr-url"};

// --- Timeouts ---
const std::chrono::milliseconds E2EConsts::kPollTimeout{50};
const std::chrono::milliseconds E2EConsts::kReadTimeout{500};

// --- Socket Option ---
const int E2EConsts::kIpTos{0x22 << 2};
const std::string E2EConsts::kZmqZapDomain{"global"};
const std::string E2EConsts::kZmqIpAddressMetaProperty{"Ip-Address"};
const std::string E2EConsts::kZmqIdentityMetaProperty{"Identity"};

// --- Inproc Socket Url ---
const std::string E2EConsts::kZmqZapEndpoint{"inproc://zeromq.zap.01"};

// --- Upgrades ---
const unsigned short int E2EConsts::kTorrentTrackerPort{6969};
const std::string E2EConsts::kTorrentCreator{"TG-UpgradeApp"};
const std::string E2EConsts::kImageFileExtension{".bin"};
const int E2EConsts::kTorrentTos{0x12 << 2};

// --- Config ---
const std::string E2EConsts::kJsonFileExtension{".json"};
const std::string E2EConsts::kSwVersionDelimiter{"_"};
const std::string E2EConsts::kConfigPathDelimiter{"."};
const std::string E2EConsts::kConfigPathWildcard{"*"};

// --- API ---
const std::string E2EConsts::kApiEventSubId{"api-event-subscriber"};

// --- Events ---
const std::string E2EConsts::kEventCategory{"TG"};
const std::string E2EConsts::kEventScanResultCategory{"TG_scan_result"};
const std::string E2EConsts::kEventIperfResultCategory{"TG_iperf_result"};
const std::string E2EConsts::kEventPingResultCategory{"TG_ping_result"};

// --- Controller/Minion Shared ---
const double E2EConsts::kGpsAccuracyThresh{50};
const std::chrono::seconds E2EConsts::kUnixGpsEpochDifference{315964800};

} // namespace terragraph
} // namespace facebook
