/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "AggrApp.h"

#include <thread>

#include <fbzmq/async/ZmqTimeout.h>
#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>

#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/Topology_types.h"
#include "e2e/if/gen-cpp2/Event_types.h"
#include "stats/if/gen-cpp2/Aggregator_types.h"

namespace facebook {
namespace terragraph {
namespace stats {

/*
 *  StatsApp collects stats reports from agents, tags them and
 *  publishes them to a remote writer endpoint.
 */
class StatsApp final : public AggrApp {
 public:
  StatsApp(
      fbzmq::Context& context,
      const std::string& routerSockUrl,
      const std::string& controllerSockUrl);

  ~StatsApp();

 private:
  // Queue typedefs
  using StatsQueues = std::unordered_map<
      std::string /* agent */,
      std::unordered_map<std::string /* key */, thrift::AggrStat>>;
  using SysLogsQueue = folly::dynamic;
  using EventsQueues = std::unordered_map<
      std::string /* agent */,
      std::vector<thrift::Event>>;

  // from AggrApp
  void processMessage(
      const std::string& agent,
      const std::string& senderApp,
      const thrift::AggrMessage& message) noexcept override;
  void processStatsReport(
      const std::string& agent,
      const std::string& senderApp,
      const thrift::AggrMessage& message);
  void processHighFrequencyStatsReport(
      const std::string& agent,
      const std::string& senderApp,
      const thrift::AggrMessage& message);
  void processSyslogReport(
      const std::string& agent,
      const std::string& senderApp,
      const thrift::AggrMessage& message);
  void processGetTopology(const std::string& senderApp);

  // Fetches the topology from the controller, updates the topology mappings.
  // Called periodically by periodicTopologyFetchTimer_.
  void periodicTopologyFetch();

  // Fetches the topology from the controller.
  thrift::Topology controllerFetchTopology(const std::string& controllerAddr);

  // Updates the mappings based on topology_.
  void processTopology();

  // Process and publish data at data_publish_interval_s.
  void periodicDataPublish();
  void periodicHighFrequencyDataPublish();
  void pushQueuedStats(
      const StatsQueues& statsQueues,
      StatsQueues& prevStatsQueues,
      int interval,
      std::atomic_bool& publisherStop,
      const std::vector<std::string> endpoints);
  void pushQueuedSysLogs(const SysLogsQueue& sysLogsQueue);
  void pushQueuedEvents(const EventsQueues& eventsQueues);

  // Helper function for handling a new agent stats report.
  // Enqueues the stats and events and pushes debug stats if enabled.
  void handleStatsReport(
      const std::string& agent, const thrift::AggrStatsReport& statsReport);

  // Sends a POST request, using curl, to the given URL in a new thread.
  void pushCurlPostRequest(
      const std::vector<std::string>& endpoints,
      const std::string& postData,
      const bool useProxy = false,
      const bool jsonType = false);

  // Helper function that returns once the given data processing thread finishes
  void stopPublisherThread(
      std::thread& publisher, std::atomic_bool& publisherStop);

  // The ZMQ context
  fbzmq::Context& context_;

  // The timer to periodically fetch the topology from the controller
  std::unique_ptr<fbzmq::ZmqTimeout> periodicTopologyFetchTimer_{nullptr};

  // The timer to periodically push queued stats, syslogs, and events
  std::unique_ptr<fbzmq::ZmqTimeout> periodicDataPublishTimer_{nullptr};
  std::unique_ptr<fbzmq::ZmqTimeout>
      periodicHighFrequencyDataPublishTimer_{nullptr};

  // The controller app socket URL
  std::string controllerSockUrl_{};

  // The latest topology retrieved from the controller
  thrift::Topology topology_{};

  // Topology mappings
  std::unordered_map<std::string, std::string> nodeMacToName_;
  std::unordered_map<std::string, std::string> nodeMacToSite_;
  std::unordered_map<std::string, std::string> nodeNameToMac_;

  // Raw data since the last publish
  StatsQueues statsQueues_{};
  StatsQueues highFrequencyStatsQueues_{};
  SysLogsQueue sysLogsQueue_ = folly::dynamic::array;
  EventsQueues eventsQueues_{};

  // The previous interval's stats queues
  StatsQueues prevStatsQueues_{};
  StatsQueues prevHighFrequencyStatsQueues_{};

  // Data endpoints defined in aggregator config
  std::unordered_map<std::string, thrift::AggrDataEndpoint> dataEndpoints_;

  // Separate thread used in periodicDataPublish()
  std::thread dataPublisherThread_;
  std::thread hfDataPublisherThread_;

  // Simple loop-breaker in dataPublisherThread_
  std::atomic_bool dataPublisherStop_{false};
  std::atomic_bool hfDataPublisherStop_{false};
};

} // namesapce stats
} // namespace terragraph
} // namespace facebook
