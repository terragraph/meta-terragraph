/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>

#include <fbzmq/async/ZmqTimeout.h>
#include <folly/dynamic.h>

#include "BasePublisher.h"

namespace facebook {
namespace terragraph {
namespace stats {

class GraphPublisher final : public BasePublisher {
 public:
  GraphPublisher(
      fbzmq::Context& context,
      const std::string& macAddr,
      std::chrono::seconds statsReportInterval,
      std::chrono::seconds curlTimeout,
      const thrift::StatsAgentParams& statsAgentParams);

  // Process counter object
  void processCountersMessage(
      const fbzmq::thrift::CounterValuesResponse& counters) noexcept override;

  // Process event log object
  void processEventLogMessage(
      const fbzmq::thrift::EventLog& eventLog) noexcept override;

 private:
  // Push stats/logs to graph API
  void pushQueuedGraphRequests();
  void pushQueuedEventLogs();
  void pushQueuedCounters();

  // Create a single ODS data point with the right prefixes
  folly::dynamic createSingleOdsDataPoint(
      const std::string& key,
      const int64_t ts,
      double value,
      const std::string& entity);
  folly::dynamic createSinglePelicanLogMessage(
      const std::string& counterName,
      const fbzmq::thrift::Counter& thriftCounter,
      int64_t ts,
      const std::string& entity);

  // CURL helper function for graph api calls
  folly::dynamic graphApiRequest(
      const std::string& endpointUrl,
      const std::unordered_map<std::string, std::string>& reqParams);

  // CURL helper function for graph api ODS endpoint
  bool pushGraphApiStatsRequest(const folly::dynamic& datapoints);
  // CURL helper function for graph api logging endpoint
  bool pushGraphApiLogsRequest(const folly::dynamic& logMessages);
  // Helper function for pelican graph api
  bool pushGraphApiPelicanRequest(const folly::dynamic& logMessages);
  // Helper function for async curl request
  void pushAsyncOdsRequest(const folly::dynamic& dataPoints);

  // Remove all spaces
  void stripWhitespace(std::string& inStr);

  // Interval at which we report stats
  std::chrono::seconds statsReportInterval_{30};
  std::chrono::seconds curlTimeout_{10};

  // ZmqTimeout for performing periodic submission of stats reports
  std::unique_ptr<fbzmq::ZmqTimeout> statsReportTimer_{nullptr};

  // store the most recent value for each key
  std::unordered_map<std::string /* key */, fbzmq::thrift::Counter> prevValues_;
  std::unordered_map<std::string /* key */, fbzmq::thrift::Counter> curValues_;

  //  folly::dynamic scribeMessagesQueue_{};
  std::unordered_map<
      time_t,
      std::vector<std::pair<std::string, fbzmq::thrift::Counter>>>
      statsQueueByTimestamp_;

  // formatted messages got from counter store
  std::vector<fbzmq::thrift::EventLog> eventLogs_{};

  // Various parameters for ods, pelican and scribe. These are populated with
  // values from config.
  thrift::OdsParams odsParams_;
  thrift::PelicanParams pelicanParams_;
  thrift::ScribeParams scribeParams_;
};

} // namespace stats
} // namepsace terragraph
} // namespace facebook
