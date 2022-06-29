/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqTimeout.h>
#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>
#include <fbzmq/service/monitor/ZmqMonitorClient.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/dynamic.h>
#include <openr/common/OpenrClient.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "BaseCounters.h"
#include "NetworkCounters.h"
#include "OpenrCounters.h"
#include "ProcessCounters.h"
#include "SensorCounters.h"
#include "SystemCounters.h"
#include "VppCounters.h"
#include "stats/if/gen-cpp2/Aggregator_types.h"

namespace facebook {
namespace terragraph {
namespace stats {

/**
 * Fetch system/network related statistics from the local host.
 * This should only be used for local system/driver calls. Other
 * stats should be sent to the monitor.
 */
class LocalStatsFetcher final : public fbzmq::ZmqEventLoop {
 public:
  LocalStatsFetcher(
      fbzmq::Context& context,
      const std::string& macAddr,
      std::chrono::seconds statsReportInterval,
      const std::string& monitorSubmitUrl,
      bool fetchVppCounters,
      bool fetchOpenrCounters);

 private:
  // Fetch counter stats from all known local sources (system, sensors, etc)
  // and push to monitor
  void fetchAndStoreCounterStats();

  // Fetch interface to link mapping from minion
  void getMinionLinks();

  // Initialize connection to Open/R, returns false if connection fails
  // and true otherwise
  bool openrConnect();

  fbzmq::Context& context_;
  std::string macAddr_;

  // ZmqTimeout for performing periodic submission of stats reports
  std::unique_ptr<fbzmq::ZmqTimeout> statsReportTimer_{nullptr};

  // All message exchanges get serialized with this serializer
  apache::thrift::CompactSerializer serializer_{};

  // Stats classes
  SensorCounters sensorCounters_{};
  SystemCounters systemCounters_{};
  NetworkCounters networkCounters_{};
  ProcessCounters processCounters_{};
  std::unique_ptr<VppCounters> vppCounters_{nullptr};
  std::unique_ptr<OpenrCounters> openrCounters_{nullptr};

  // client to interact with monitor
  std::unique_ptr<fbzmq::ZmqMonitorClient> zmqMonitorClient_;

  // Map from "terraX" network interface to peer radio + responder ID (MAC
  // address)
  std::unordered_map<
      std::string,
      std::pair<std::string /* radio mac */, std::string /* responder mac */>>
      interfaceToRadio_{};

  // EventBase to create Open/R client
  folly::EventBase evb_;

  // cached OpenrCtrlClient to talk to Open/R
  std::unique_ptr<openr::thrift::OpenrCtrlAsyncClient> openrCtrlClient_{
      nullptr};
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
