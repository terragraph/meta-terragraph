/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>
#include <regex>
#include <thread>

#include <cppkafka/utils/buffered_producer.h>
#include <cppkafka/configuration.h>
#include <fbzmq/async/ZmqTimeout.h>

#include "BasePublisher.h"
#include "e2e/if/gen-cpp2/NodeConfig_types.h"
#include "stats/if/gen-cpp2/Aggregator_types.h"

namespace facebook {
namespace terragraph {
namespace stats {

/*
 *  KafkaPublisher publishes stats and node info to Kafka brokers.
 */
class KafkaPublisher final : public BasePublisher {
 public:
  KafkaPublisher(
      fbzmq::Context& context,
      const std::string& macAddr,
      const thrift::StatsAgentParams& statsAgentParams);

  ~KafkaPublisher();

  // Process counter object
  void processCountersMessage(
      const fbzmq::thrift::CounterValuesResponse& counters) noexcept override;

  // Process event log object
  void processEventLogMessage(
      const fbzmq::thrift::EventLog& eventLog) noexcept override;

  // Cache event queue to disk
  void cacheEvents();

 private:
  // Stats queue type
  typedef std::unordered_map<std::string /* key */, fbzmq::thrift::Counter>
      StatsMap;

  // Initializes Kafka-related structures
  void kafkaInit(const thrift::KafkaParams& kafkaParams);

  // Push queued stats to Kafka.
  void pushQueuedStats(
      StatsMap& curValues, StatsMap& prevValues, const std::string& statsTopic);

  // Push a single stat to Kafka.
  void publishStat(
      const std::string& statsTopic,
      const std::string& key,
      int64_t timestamp,
      double val,
      bool isCounter);

  // Push eventsDropped_ to Kafka.
  void pushDroppedEvents();

  // Load event log from disk and remove cache file
  void loadEvents();

  // Interval at which we report stats
  std::chrono::seconds nmsDefaultReportInterval_;

  // Interval at which we report high-frequency stats
  std::chrono::seconds nmsHighFrequencyReportInterval_;

  // ZmqTimeout for performing periodic submission of stats
  std::unique_ptr<fbzmq::ZmqTimeout> nmsDefaultReportTimer_;

  // ZmqTimeout for performing periodic submission of high-frequency stats
  std::unique_ptr<fbzmq::ZmqTimeout> nmsHighFrequencyReportTimer_;

  // ZmqTimeout for retransmitting dropped events to Kafka
  std::unique_ptr<fbzmq::ZmqTimeout> kafkaDroppedEventsTimer_;

  // Stat queues
  // - store the most recent value for each key (low frequency)
  StatsMap prevValuesLF_;
  StatsMap curValuesLF_;
  // - high-frequency counters
  StatsMap prevValuesHF_;
  StatsMap curValuesHF_;

  // Event queues (JSON payloads)
  // - In-flight events
  std::unordered_set<std::string> eventsInFlight_;
  // - Dropped events
  std::unordered_set<std::string> eventsDropped_;

  // Kafka configuration
  cppkafka::Configuration kafkaConfig_;
  // Kafka producer
  std::unique_ptr<cppkafka::BufferedProducer<std::string>> kafkaProducer_;
  // Kafka topics
  thrift::KafkaTopics kafkaTopics_;
  // Max Kafka buffer size
  size_t kafkaMaxBufferSize_ = 0;

  // Thread to periodically flush the Kafka producer
  std::thread kafkaFlushThread_;

  // Simple loop-breaker in kafkaFlushThread_
  std::atomic_bool kafkaFlushThreadStop_{false};
};

} // namespace stats
} // namepsace terragraph
} // namespace facebook
