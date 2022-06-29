/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <regex>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>
#include <fbzmq/zmq/Zmq.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "e2e/if/gen-cpp2/Event_types.h"
#include "e2e/if/gen-cpp2/NodeConfig_types.h"
#include "stats/if/gen-cpp2/Aggregator_types.h"

namespace facebook {
namespace terragraph {
namespace stats {

class BasePublisher : public fbzmq::ZmqEventLoop {
 public:
  BasePublisher(
      fbzmq::Context& context,
      const std::string& macAddr,
      const std::string& className,
      const thrift::StatsAgentParams& statsAgentParams);

 protected:
  // Return whether the given stat key is blacklisted
  bool isBlacklisted(const std::string& key);

  // Return whether the given stat key is whitelisted
  bool isWhitelisted(const std::string& key);

  // Parse an event and fill in missing fields.
  // The event MUST be of type/category "TG"
  std::optional<thrift::Event> parseTerragraphEventLog(
      const fbzmq::thrift::EventLog& eventLog) const;

  // Convert the given raw counter value into a rate (using previous values).
  //
  // This will return `std::nullopt` if the counter cannot be converted into a
  // valid rate, for example when:
  // - it is actually a gauge (not monotonically increasing)
  // - this is the first occurrence seen (prevCounter is null)
  // - counter overflowed (curValue < prevValue)
  // - system clock jumped backwards (curTime <= prevTime)
  std::optional<double> getCounterRate(
      const std::string& key,
      const fbzmq::thrift::Counter& counter,
      const std::unordered_map<std::string, fbzmq::thrift::Counter>&
          prevValues) const;

  // The ZMQ context
  fbzmq::Context& context_;

  // The node ID (MAC address)
  std::string macAddr_;

  // The topology name (read from node config)
  std::string topologyName_;

  // The node name (read from node config)
  std::string nodeName_;

  // The maximum queue sizes
  size_t statsBufferSize_;
  size_t eventsBufferSize_;

  // Whether to convert counter-type stats into rates before publishing
  bool convertToRate_{true};

  // Whether to publish BOTH raw values and rates for counter-type stats
  bool publishValueWithRate_{false};

  // All message exchanges get serialized with this serializer
  apache::thrift::CompactSerializer serializer_{};

 private:
  // Process counter object
  virtual void processCountersMessage(
      const fbzmq::thrift::CounterValuesResponse& counters) noexcept = 0;

  // Process event log object
  virtual void processEventLogMessage(
      const fbzmq::thrift::EventLog& eventLog) noexcept = 0;

  // Initializes ZMQ sockets
  void prepare(const thrift::StatsAgentParams& statsAgentParams) noexcept;

  // Check if the key matches any regex in the given list.
  bool regexMatches(
      const std::string& key, const std::vector<std::regex>& regexes);

  // The subclass name (for internal use)
  std::string className_;

  // The sockets used for communicating with the command processor thread
  std::vector<fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT>> csSubSockList_;

  // Stats blacklisted regular expressions
  std::vector<std::regex> statsBlacklist_;

  // High-frequency stats whitelisted regular expressions
  std::vector<std::regex> highFrequencyStatsWhitelist_;
};

} // namespace stats
} // namepsace terragraph
} // namespace facebook
