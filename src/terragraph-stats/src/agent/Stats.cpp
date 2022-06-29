/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <boost/asio.hpp>
#include <csignal>
#include <curl/curl.h>
#include <fbzmq/async/AsyncSignalHandler.h>
#include <fbzmq/service/monitor/ZmqMonitor.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/String.h>
#include <folly/init/Init.h>
#include <folly/system/ThreadName.h>
#include <gflags/gflags.h>

#include "GraphPublisher.h"
#include "InputListener.h"
#include "KafkaPublisher.h"
#include "LocalStatsFetcher.h"
#include "NmsPublisher.h"
#include "SharedObjects.h"
#include "e2e/common/ExceptionHandler.h"
#include "e2e/common/SysUtils.h"

// General options
DEFINE_string(mac_addr, "", "The MAC address");
DEFINE_string(
    aggregator_ip, "", "[DEPRECATED] The IP of the aggregator we talk to");
DEFINE_string(
    aggregator_host, "", "The hostname or IP of the aggregator we talk to");
DEFINE_int32(aggregator_router_port, 8002, "The port aggregator listens on");

DEFINE_string(
    input_router_listen_ip,
    "[::1]",
    "The IP address to bind to for input messages (e.g. CLI events, commands)");
DEFINE_int32(
    input_router_port,
    4231,
    "The port that stats agent will listen on for input messages");

DEFINE_string(
    node_info_file,
    "/var/run/node_info",
    "File containing static node info");
DEFINE_string(
    node_config_file,
    "/data/cfg/node_config.json",
    "Node configuration file");
DEFINE_string(
    my_network_info_file,
    "/tmp/mynetworkinfo",
    "Network information file");

// local monitor
DEFINE_string(
    monitor_listen_ip,
    "[::1]",
    "The IP address to bind to for the monitor");
DEFINE_int32(
    monitor_router_port,
    17009,
    "The zmq router port on which the monitor listens on");
DEFINE_int32(
    monitor_pub_port,
    18991,
    "The zmq publish port on which the monitor binds");
DEFINE_int32(
    monitor_counter_lifetime_s,
    300,
    "The lifetime of stale counters in ZmqMonitor (in seconds)");

DEFINE_bool(use_graph_publisher, true, "Push metrics to graph publisher");
DEFINE_bool(use_nms_publisher, true, "Push metrics to NMS aggregator");
DEFINE_bool(use_local_stats_fetcher, true, "Collect local stats");
// The submission intervals should be divisible
DEFINE_int32(ods_submission_interval_s, 30, "Submission interval for ODS");
DEFINE_int32(curl_timeout_s, 10, "cURL timeout for the entire request");

using namespace facebook::terragraph;

// Configure ASAN runtime options to limit memory usage
const char *__asan_default_options() {
    return "malloc_context_size=10:quarantine_size_mb=8:max_redzone=256";
}

namespace {
// StatsStopSignalHandler is a copy of fbzmq::StopEventLoopSignalHandler that
// allows callbacks to be registered and called when signals are received.
// Callbacks are invoked with a received signal, but before stopping the event
// loop. Callbacks must have the type `void callback(int sig)`.
class StatsStopSignalHandler final : public fbzmq::AsyncSignalHandler {
 public:
  explicit StatsStopSignalHandler(fbzmq::ZmqEventLoop* evl)
      : fbzmq::AsyncSignalHandler(evl) {}

  // Callback function type
  using CallbackFunction = std::function<void(int)>;

  // Register callback that is called when a signal is received
  void registerCallback(CallbackFunction cb) {
    callbacks_.push_back(cb);
  }
 private:
  void signalReceived(int sig) noexcept override {
    LOG(INFO) << folly::format("Invoking {} callback(s)...", callbacks_.size());

    // Invoke all the callbacks
    for (auto& cb : callbacks_) {
      cb(sig);
    }

    // Stop the event loop for this handler
    LOG(INFO) << "Stopping event loop...";
    auto evl = getZmqEventLoop();
    evl->stop();
  }

  std::vector<CallbackFunction> callbacks_{};  // registered callbacks
};
} // namespace

int
main(int argc, char** argv) {
  folly::init(&argc, &argv);
  ExceptionHandler::install();

  // start signal handler before any thread
  fbzmq::ZmqEventLoop mainEventLoop;
  StatsStopSignalHandler handler(&mainEventLoop);
  handler.registerSignalHandler(SIGINT);
  handler.registerSignalHandler(SIGQUIT);
  handler.registerSignalHandler(SIGTERM);

  fbzmq::Context context;

  // initialize node config
  stats::SharedObjects::getNodeConfigWrapper()
      ->wlock()->setNodeConfigFile(FLAGS_node_config_file);
  auto lockedConfigWrapper =
      stats::SharedObjects::getNodeConfigWrapper()->rlock();
  auto nmsConfig = *lockedConfigWrapper->getStatsAgentParams();
  const auto& endpointParams = nmsConfig.endpointParams;
  const bool dpdkEnabled =
      lockedConfigWrapper->getEnvConfig()->DPDK_ENABLED_ref().value_or("0")
          == "1";
  lockedConfigWrapper.unlock();  // lockedConfigWrapper -> NULL

  // initialize node info
  auto lockedNodeInfoW = stats::SharedObjects::getNodeInfoWrapper()->wlock();
  lockedNodeInfoW->setNodeInfoFile(FLAGS_node_info_file);
  auto nodeId = lockedNodeInfoW->getNodeId();
  if (!FLAGS_mac_addr.empty()) {
    nodeId = FLAGS_mac_addr;
  }
  lockedNodeInfoW.unlock();  // lockedNodeInfoW -> NULL
  if (!nodeId || folly::trimWhitespace(nodeId.value()).empty()) {
    LOG(FATAL) << "Empty node ID";
  }
  LOG(INFO) << folly::sformat("Stats agent using node ID {}", nodeId.value());

  // Enable/disable publisher threads...
  // - Enable graph publisher only if at least one fb graph endpoint is enabled
  bool graphPublisherEnabled =
      (endpointParams.odsParams_ref().has_value()
          && endpointParams.odsParams_ref().value().enabled)
      || (endpointParams.pelicanParams_ref().has_value()
          && endpointParams.pelicanParams_ref().value().enabled)
      || (endpointParams.scribeParams_ref().has_value()
          && endpointParams.scribeParams_ref().value().enabled);
  if (!FLAGS_use_graph_publisher) {
    graphPublisherEnabled = false;
  }
  // - Only allow NMS aggregator publishing if enabled in config
  bool nmsPublisherEnabled =
      endpointParams.nmsPublisherParams_ref().has_value() &&
      endpointParams.nmsPublisherParams_ref().value().enabled;
  if (!FLAGS_use_nms_publisher) {
    nmsPublisherEnabled = false;
  }
  // - Only allow Kafka publishing if all topics and broker endpoint defined
  const auto& kafkaParams = endpointParams.kafkaParams_ref();
  bool kafkaPublisherEnabled =
      kafkaParams.has_value() &&
      kafkaParams.value().enabled &&
      !kafkaParams.value().config.brokerEndpointList.empty() &&
      !kafkaParams.value().topics.statsTopic.empty() &&
      !kafkaParams.value().topics.hfStatsTopic.empty() &&
      !kafkaParams.value().topics.eventsTopic.empty();

  // init curl once, as it could be used by multiple publishers
  curl_global_init(CURL_GLOBAL_ALL);

  // Start the ZMQ ZmqMonitor
  fbzmq::ZmqMonitor zmqMonitor(
      folly::sformat(
          "tcp://{}:{}", FLAGS_monitor_listen_ip, FLAGS_monitor_router_port),
      folly::sformat(
          "tcp://{}:{}", FLAGS_monitor_listen_ip, FLAGS_monitor_pub_port),
      context,
      folly::none,
      std::chrono::seconds(FLAGS_monitor_counter_lifetime_s));
  std::thread zmqMonitorThread([&zmqMonitor]() {
    LOG(INFO) << "Starting ZmqMonitor thread...";
    folly::setThreadName("ZmqMonitor");
    zmqMonitor.run();
    LOG(INFO) << "ZmqMonitor thread got stopped";
  });
  zmqMonitor.waitUntilRunning();

  // Start input listener
  facebook::terragraph::stats::InputListener inputListener(
      context,
      folly::sformat(
          "tcp://{}:{}",
          FLAGS_input_router_listen_ip,
          FLAGS_input_router_port),
      folly::sformat(
          "tcp://{}:{}", FLAGS_monitor_listen_ip, FLAGS_monitor_router_port),
      getpid());
  std::thread inputListenerThread([&inputListener]() {
    LOG(INFO) << "Starting InputListener thread...";
    folly::setThreadName("InputListener");
    inputListener.run();
    LOG(INFO) << "InputListener thread got stopped";
  });
  inputListener.waitUntilRunning();

  // Start FB graph publisher (if enabled)
  std::unique_ptr<facebook::terragraph::stats::GraphPublisher> graphPublisher;
  std::thread graphPublisherThread;
  if (graphPublisherEnabled) {
    graphPublisher =
      std::make_unique<facebook::terragraph::stats::GraphPublisher>(
        context,
        nodeId.value(),
        std::chrono::seconds(FLAGS_ods_submission_interval_s),
        std::chrono::seconds(FLAGS_curl_timeout_s),
        nmsConfig);

    graphPublisherThread = std::thread([&context, &graphPublisher]() {
      LOG(INFO) << "Starting GraphPublisher thread...";
      folly::setThreadName("GraphPublisher");
      graphPublisher->run();
      LOG(INFO) << "GraphPublisher thread got stopped";
    });
    graphPublisher->waitUntilRunning();
  } else {
    LOG(INFO) << "GraphPublisher thread is disabled";
  }

  // Start NMS publisher (if enabled)
  std::unique_ptr<facebook::terragraph::stats::NmsPublisher> nmsPublisher;
  std::thread nmsPublisherThread;
  if (nmsPublisherEnabled) {
    std::string aggregator_host = FLAGS_aggregator_host;
    if (!google::GetCommandLineFlagInfoOrDie("aggregator_ip").is_default) {
      aggregator_host = folly::sformat("[{}]", FLAGS_aggregator_ip);
    }

    nmsPublisher = std::make_unique<facebook::terragraph::stats::NmsPublisher>(
        context,
        nodeId.value(),
        (aggregator_host.empty()) ? "" : folly::sformat("tcp://{}:{}",
            aggregator_host, FLAGS_aggregator_router_port),
        FLAGS_my_network_info_file,
        nmsConfig);

    // This will cache events before allowing the process to be killed.
    // It will work with reboot, sv stop stats_agent,
    // or kill $(pgrep stats_agent).
    handler.registerCallback([&nmsPublisher](int /*sig*/) {
      LOG(INFO) << "Caching events...";
      nmsPublisher->cacheEvents();
    });

    nmsPublisherThread = std::thread([&context, &nmsPublisher]() {
      LOG(INFO) << "Starting NmsPublisher thread...";
      folly::setThreadName("NmsPublisher");
      nmsPublisher->run();
      LOG(INFO) << "NmsPublisher thread got stopped";
    });
    nmsPublisher->waitUntilRunning();
  } else {
    LOG(INFO) << "NmsPublisher thread is disabled";
  }

  // Start Kafka publisher (if enabled)
  std::unique_ptr<facebook::terragraph::stats::KafkaPublisher> kafkaPublisher;
  std::thread kafkaPublisherThread;
  if (kafkaPublisherEnabled) {
    kafkaPublisher =
      std::make_unique<facebook::terragraph::stats::KafkaPublisher>(
          context, nodeId.value(), nmsConfig);

    // This will cache events before allowing the process to be killed.
    // It will work with reboot, sv stop stats_agent,
    // or kill $(pgrep stats_agent).
    handler.registerCallback([&kafkaPublisher](int /*sig*/) {
      LOG(INFO) << "Caching events...";
      kafkaPublisher->cacheEvents();
    });

    kafkaPublisherThread = std::thread([&context, &kafkaPublisher]() {
      LOG(INFO) << "Starting KafkaPublisher thread...";
      folly::setThreadName("KafkaPublisher");
      kafkaPublisher->run();
      LOG(INFO) << "KafkaPublisher thread got stopped";
    });
    kafkaPublisher->waitUntilRunning();
  } else {
    LOG(INFO) << "KafkaPublisher thread is disabled";
  }

  // Start local stats runner/fetcher (if enabled)
  std::unique_ptr<facebook::terragraph::stats::LocalStatsFetcher> statsRunner;
  std::thread localStatsThread;
  bool openrStatsEnabled = nmsConfig.collectors.openrStatsEnabled;
  if (openrStatsEnabled && nmsConfig.sources.count("openr") > 0) {
    LOG(WARNING)
        << "Open/R is configured to collect stats both via the ZMQ"
           " socket in .statsAgentParams.sources and via"
           " .statsAgentParams.collectors, disabling collection through"
           " .statsAgentParams.collectors";
    openrStatsEnabled = false;
  }
  if (FLAGS_use_local_stats_fetcher &&
      nmsConfig.collectors.systemStatsCollectionInterval > 0) {
    // Run at the system stats interval
    statsRunner =
        std::make_unique<facebook::terragraph::stats::LocalStatsFetcher>(
            context,
            nodeId.value(),
            std::chrono::seconds(
                nmsConfig.collectors.systemStatsCollectionInterval),
            folly::sformat(
                "tcp://{}:{}",
                FLAGS_monitor_listen_ip,
                FLAGS_monitor_router_port),
            dpdkEnabled,
            openrStatsEnabled);

    localStatsThread = std::thread([&context, &statsRunner]() {
      LOG(INFO) << "Starting LocalStatsFetcher thread...";
      folly::setThreadName("LocalStatsFetcher");
      statsRunner->run();
      LOG(INFO) << "LocalStatsFetcher thread got stopped";
    });
    statsRunner->waitUntilRunning();
  } else {
    LOG(INFO) << "LocalStatsFetcher thread is disabled";
  }

  LOG(INFO) << "Starting main event loop...";
  mainEventLoop.run();
  LOG(INFO) << "Main event loop got stopped";

  // Stop all threads (in reverse order of their creation)
  if (FLAGS_use_local_stats_fetcher) {
    statsRunner->stop();
    statsRunner->waitUntilStopped();
  }
  if (kafkaPublisherEnabled) {
    kafkaPublisher->stop();
    kafkaPublisher->waitUntilStopped();
  }
  if (nmsPublisherEnabled) {
    nmsPublisher->stop();
    nmsPublisher->waitUntilStopped();
  }
  if (graphPublisherEnabled) {
    graphPublisher->stop();
    graphPublisher->waitUntilStopped();
  }

  inputListener.stop();
  inputListener.waitUntilStopped();
  zmqMonitor.stop();
  zmqMonitor.waitUntilStopped();

  // wait for threads to stop
  zmqMonitorThread.join();
  inputListenerThread.join();
  if (graphPublisherEnabled) {
    graphPublisherThread.join();
  }
  if (nmsPublisherEnabled) {
    nmsPublisherThread.join();
  }
  if (kafkaPublisherEnabled) {
    kafkaPublisherThread.join();
  }
  if (FLAGS_use_local_stats_fetcher) {
    localStatsThread.join();
  }

  // cleanup curl
  curl_global_cleanup();

  return 0;
}
