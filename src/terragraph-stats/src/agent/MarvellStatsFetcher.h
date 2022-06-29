/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>
#include <future>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqTimeout.h>
#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>
#include <fbzmq/service/monitor/ZmqMonitorClient.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/Format.h>
#include <folly/MapUtil.h>
#include <folly/json.h>
#include <map>
#include <openr/if/gen-cpp2/Lsdb_types.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "e2e/common/EventClient.h"
#include "../common/Consts.h"

namespace facebook {
namespace terragraph {
namespace stats {

struct MarvellSwitchStats {
  MarvellSwitchStats()
      : unicastPkts(0), multicastPkts(0), broadcastPkts(0), octets(0) {}
  long unicastPkts;
  long multicastPkts;
  long broadcastPkts;
  long octets;
};

class MarvellStatsFetcher : public fbzmq::ZmqEventLoop {
 public:
  MarvellStatsFetcher(
    fbzmq::Context& context,
    std::chrono::seconds statsReportInterval,
    const std::string& monitorSubmitUrl);

  void fetchAndStoreCounterStats();

 private:
   fbzmq::Context& context_;

   // ZmqTimeout for performing periodic submission of stats reports
   std::unique_ptr<fbzmq::ZmqTimeout> statsReportTimer_{nullptr};

  // All message exchanges get serialized with this serializer
  apache::thrift::CompactSerializer serializer_{};

  // client to interact with monitor
  std::shared_ptr<fbzmq::ZmqMonitorClient> zmqMonitorClient_;

  // execute a script and output stdout to file
  bool outputCommandToFile(
      const std::string& script, const std::string& outputFile);

  // fetch the latest interface counters and insert them into the retMap
  bool fetchInterfaceCounters(fbzmq::CounterMap& retMap);
  // fetch the latest interface status and insert them into the retMap
  bool fetchInterfaceStatus(fbzmq::CounterMap& retMap);

  // switch counters for the receive (rx) side of the switch
  std::unordered_map<std::string /* port name */, MarvellSwitchStats>
      rxSwitchCounters_;
  // switch counters for the transmit (tx) side of the switch
  std::unordered_map<std::string /* port name */, MarvellSwitchStats>
      txSwitchCounters_;

  // Flag to denote status of marvell switch stat functionality (used for
  // sending events)
  bool marvellSwitchStatus_{true};

  // Event client
  std::unique_ptr<EventClient> eventClient_;
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
