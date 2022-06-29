/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <chrono>

#include <fbzmq/async/StopEventLoopSignalHandler.h>
#include <fbzmq/service/monitor/ZmqMonitor.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/init/Init.h>
#include <folly/system/ThreadName.h>
#include <gflags/gflags.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "time/ChronoFlags.h"

#include "BinaryStarApp.h"
#include "Broker.h"
#include "ConfigApp.h"
#include "IgnitionApp.h"
#include "ScanApp.h"
#include "SchedulerApp.h"
#include "SharedObjects.h"
#include "StatusApp.h"
#include "TopologyApp.h"
#include "TopologyBuilderApp.h"
#include "TrafficApp.h"
#include "UpgradeApp.h"
#include "ZapHandler.h"
#include "e2e/common/Consts.h"
#include "e2e/common/ExceptionHandler.h"

using namespace fbzmq;
using namespace facebook::terragraph;

using apache::thrift::CompactSerializer;

DEFINE_int32(
    minion_router_port, 7007, "The port controller listens on for minions");
DEFINE_int32(app_router_port, 17077, "The port controller listens on for apps");
DEFINE_int32(event_pub_port, 17078, "The port controller publishes events on");
DEFINE_string(listen_addr, "*", "The IP address to bind to");
DEFINE_time_s(
    linkup_extended_dampen_interval,
    300_s,
    "The minimum time before re-igniting the same link after "
    "'linkup_extended_dampen_failure_interval' of failed ignition attempts");
DEFINE_time_s(
    linkup_extended_dampen_failure_interval,
    1800_s,
    "The minimum duration of successive failed ignition attempts across the "
    "same link before changing the dampen interval to "
    "'linkup_extended_dampen_interval'");
DEFINE_time_s(
    linkup_backup_cn_link_interval,
    300_s,
    "The minimum time that must elapse before trying to ignite using backup "
    "links (starting from when a DN-to-CN link could have been ignited from "
    "either a primary or backup link)");
DEFINE_bool(
    linkup_ignore_dampen_interval_after_resp,
    false,
    "Whether to ignore the regular dampen interval upon receiving a link-down "
    "event from a node, allowing for quicker ignition attempts (the extended "
    "dampen interval is not affected by this setting)");
DEFINE_time_s(
    status_reports_interval,
    5_s,
    "The time interval at which various controller apps sync with the latest "
    "status reports (i.e. heartbeats) received from nodes");
DEFINE_time_s(
    topology_info_sync_interval,
    300_s,
    "The time interval at which each node's topology info config is synced to "
    "keep neighbor information up to date");
DEFINE_time_s(
    topology_report_interval,
    30_s,
    "The time interval at which the controller records statistics for "
    "node/link status");
DEFINE_time_s(
    routing_adjacencies_dump_interval,
    30_s,
    "The time interval at which the controller requests the full dump of "
    "routing adjacencies from a minion's KvStore");
DEFINE_time_s(
    status_report_throttle_interval,
    5_s,
    "Do not process successive status reports (i.e. heartbeats) received from "
    "nodes within this 'throttle' interval");
DEFINE_time_s(
    full_status_report_interval,
    3600_s,
    "Request full status reports from minions at this interval "
    "(as a safeguard only)");
DEFINE_string(topology_file, "", "The config file containing the topology");
DEFINE_string(
    topology_dir,
    "/tmp/topology",
    "The directory to save timestamped topology files");
DEFINE_int32(
    monitor_router_port,
    27007,
    "The zmq router port on which the monitor listens on");
DEFINE_int32(
    monitor_pub_port, 28989, "The zmq publish port on which the monitor binds");
DEFINE_int32(
    monitor_counter_lifetime_s,
    300,
    "The lifetime of stale counters in ZmqMonitor (in seconds)");
DEFINE_time_s(
    node_alive_timeout,
    30_s,
    "Mark a node as offline if no heartbeat is received within this interval");
DEFINE_bool(
    enable_airtime_auto_alloc,
    false,
    "Whether to enable automatic fair airtime allocation");
DEFINE_time_s(
    airtime_alloc_update_interval,
    60_s,
    "The minimum time interval at which the controller will recompute the "
    "airtime allocations for the entire network");
DEFINE_bool(
    enable_centralized_prefix_alloc,
    true,
    "Whether to enable centralized prefix allocation or not");
DEFINE_bool(
    enable_deterministic_prefix_alloc,
    false,
    "Whether to enable deterministic prefix allocation or not");
DEFINE_time_s(
    centralized_prefix_update_interval,
    15_s,
    "The time interval at which the controller will propogate any allocated "
    "network prefixes to a minion's KvStore (when centralized or deterministic "
    "prefix allocation mode is enabled)");
DEFINE_string(
    node_config_overrides_file,
    "/data/cfg/node_config_overrides.json",
    "Config file with node specific overrides");
DEFINE_string(
    auto_node_config_overrides_file,
    "/data/cfg/auto_node_config_overrides.json",
    "Config file with automated node specific overrides");
DEFINE_string(
    network_config_overrides_file,
    "/data/cfg/network_config_overrides.json",
    "Config file with network-wide overrides");
DEFINE_string(version_file, "/etc/tgversion", "Version file");
DEFINE_string(
    config_backup_dir,
    "/tmp/cfg_backup/",
    "Directory to save config backups");
DEFINE_string(
    base_config_dir,
    "/etc/e2e_config/base_versions/",
    "Directory with base config JSON files");
DEFINE_string(
    fw_base_config_dir,
    "/etc/e2e_config/base_versions/fw_versions/",
    "Directory with firmware base config JSON files");
DEFINE_string(
    hw_base_config_dir,
    "/etc/e2e_config/base_versions/hw_versions/",
    "Directory with hardware base config JSON files");
DEFINE_string(
    hw_config_types_file,
    "/etc/e2e_config/base_versions/hw_versions/hw_types.json",
    "JSON file mapping hardware config types to hardware board IDs");
DEFINE_string(
    controller_config_file,
    "/data/cfg/controller_config.json",
    "The controller config file");
DEFINE_bool(
    enable_zap_apps_sock,
    false,
    "Whether to enable ZAP on the apps sock, which will log IP addresses and "
    "ZMQ IDs for each request");
DEFINE_bool(
    enable_zap_minions_sock,
    false,
    "Whether to enable ZAP on the minions sock, which will log IP addresses "
    "and ZMQ IDs for each request");
DEFINE_bool(
    enable_create_intrasite_links,
    true,
    "Whether to enable automatic intrasite wired link creation");

// Flags for primary-backup replication (disabled unless bstar_peer_host or
// bstar_peer_ip is set).
DEFINE_bool(
    bstar_primary,
    true,
    "The primary (true) or backup (false) controller in the high availability "
    "configuration");
DEFINE_int32(
    bstar_pub_port,
    55555,
    "The port that the controller publishes primary-backup state information "
    "on in the high availability configuration");
DEFINE_string(
    bstar_peer_ip,
    "",
    "[DEPRECATED] The IP address of the peer controller in the high "
    "availability configuration; if empty, this feature will be disabled");
DEFINE_string(
    bstar_peer_host,
    "",
    "The hostname or IP address of the peer controller in the high "
    "availability configuration; if empty, this feature will be disabled");
DEFINE_int32(
    bstar_peer_pub_port,
    55555,
    "The publisher port on the peer controller in the high availability "
    "configuration");
DEFINE_bool(
    disable_bstar,
    false,
    "Whether to disable the high availability feature (even if a peer IP "
    "address is provided)");

int
main(int argc, char** argv) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  ExceptionHandler::install();

  // the zmq context - IO pool.
  Context zmqContext;

  // start signal handler before any thread
  ZmqEventLoop mainEventLoop;
  StopEventLoopSignalHandler handler(&mainEventLoop);
  handler.registerSignalHandler(SIGINT);
  handler.registerSignalHandler(SIGQUIT);
  handler.registerSignalHandler(SIGTERM);

  // is high availability mode enabled?
  std::string bstar_peer_host = FLAGS_bstar_peer_host;
  if (!google::GetCommandLineFlagInfoOrDie("bstar_peer_ip").is_default) {
    bstar_peer_host = folly::sformat("[{}]", FLAGS_bstar_peer_ip);
  }

  bool isBstarEnabled = !(bstar_peer_host.empty() || FLAGS_disable_bstar);

  std::vector<std::thread> allThreads{};

  // Start the ZMQ ZAP handler
  std::unique_ptr<ZapHandler> zapHandler;
  if (FLAGS_enable_zap_apps_sock || FLAGS_enable_zap_minions_sock) {
    zapHandler = std::make_unique<ZapHandler>(zmqContext);
    std::thread zapHandlerThread([&zapHandler]() {
      LOG(INFO) << "Starting ZapHandler thread...";
      folly::setThreadName("ZapHandler");
      zapHandler->run();
      LOG(INFO) << "ZapHandler thread got stopped";
    });
    allThreads.emplace_back(std::move(zapHandlerThread));
    zapHandler->waitUntilRunning();
  }

  // Start the broker app
  Broker broker(
      zmqContext,
      folly::sformat(
          "tcp://{}:{}", FLAGS_listen_addr, FLAGS_minion_router_port),
      folly::sformat("tcp://{}:{}", FLAGS_listen_addr, FLAGS_app_router_port),
      folly::sformat("tcp://{}:{}", FLAGS_listen_addr, FLAGS_event_pub_port),
      FLAGS_enable_zap_apps_sock,
      FLAGS_enable_zap_minions_sock,
      isBstarEnabled,
      FLAGS_bstar_primary);
  std::thread brokerThread([&broker]() {
    LOG(INFO) << "Starting Broker thread...";
    folly::setThreadName("Broker");
    broker.run();
    LOG(INFO) << "Broker thread got stopped";
  });
  allThreads.emplace_back(std::move(brokerThread));
  broker.waitUntilRunning();

  // Start the ZmqMonitor Server
  fbzmq::ZmqMonitor zmqMonitor(
      folly::sformat(
          "tcp://{}:{}", FLAGS_listen_addr, FLAGS_monitor_router_port),
      folly::sformat("tcp://{}:{}", FLAGS_listen_addr, FLAGS_monitor_pub_port),
      zmqContext,
      folly::none,
      std::chrono::seconds(FLAGS_monitor_counter_lifetime_s));
  std::thread zmqMonitorThread([&zmqMonitor]() {
    LOG(INFO) << "Starting ZmqMonitor thread...";
    folly::setThreadName("ZmqMonitor");
    zmqMonitor.run();
    LOG(INFO) << "ZmqMonitor thread got stopped";
  });
  allThreads.emplace_back(std::move(zmqMonitorThread));
  zmqMonitor.waitUntilRunning();

  const std::string routerSockUrl =
      folly::sformat("tcp://localhost:{}", FLAGS_app_router_port);
  const std::string monitorSockUrl =
      folly::sformat("tcp://localhost:{}", FLAGS_monitor_router_port);

  // Create event publisher for main thread
  auto zmqMonitorClient = std::make_shared<fbzmq::ZmqMonitorClient>(
      zmqContext, monitorSockUrl, E2EConsts::kMainCtrlId);
  auto eventClient = EventClient(E2EConsts::kMainCtrlId, zmqMonitorClient);

  // Initialize topology and config shared objects.
  // This also performs basic validation
  {
    // Lock TopologyW and ConfigHelper
    LOCK_TOPOLOGY_AND_CONFIG(lockedTopologyW, wlock, lockedConfigHelper, wlock);

    // Initialize topology
    lockedTopologyW->setTopologyFromFile(FLAGS_topology_file);
    lockedTopologyW->sanitizeState();

    // Get all nodes in topology
    auto topologyNodeNames =
        folly::gen::from(lockedTopologyW->getAllNodes()) |
        folly::gen::field(&facebook::terragraph::thrift::Node::name) |
        folly::gen::as<std::unordered_set<std::string>>();

    // Initialize node config helper
    lockedConfigHelper->setConfigFiles(
        FLAGS_base_config_dir,
        FLAGS_fw_base_config_dir,
        FLAGS_hw_base_config_dir,
        FLAGS_hw_config_types_file,
        FLAGS_node_config_overrides_file,
        FLAGS_auto_node_config_overrides_file,
        FLAGS_network_config_overrides_file,
        FLAGS_node_config_metadata_file,
        FLAGS_config_backup_dir,
        topologyNodeNames);
  }

  // initialize E2E config
  SharedObjects::getE2EConfigWrapper()
      ->wlock()->setE2EConfigFile(FLAGS_controller_config_file);

  // Start the status app
  StatusApp statusApp(
      zmqContext,
      routerSockUrl,
      monitorSockUrl,
      FLAGS_status_report_throttle_interval_s,
      FLAGS_full_status_report_interval_s,
      FLAGS_version_file);
  std::thread statusAppThread([&statusApp]() {
    LOG(INFO) << "Starting StatusApp thread...";
    folly::setThreadName("StatusApp");
    statusApp.run();
    LOG(INFO) << "StatusApp thread got stopped";
  });
  allThreads.emplace_back(std::move(statusAppThread));
  statusApp.waitUntilRunning();

  // Start the topology app
  facebook::terragraph::TopologyApp topologyApp(
      zmqContext,
      routerSockUrl,
      monitorSockUrl,
      FLAGS_status_reports_interval_s,
      FLAGS_topology_report_interval_s,
      FLAGS_routing_adjacencies_dump_interval_s,
      FLAGS_node_alive_timeout_s,
      FLAGS_airtime_alloc_update_interval_s,
      FLAGS_centralized_prefix_update_interval_s,
      FLAGS_topology_file,
      FLAGS_topology_dir,
      FLAGS_enable_airtime_auto_alloc,
      FLAGS_enable_centralized_prefix_alloc,
      FLAGS_enable_deterministic_prefix_alloc,
      FLAGS_enable_create_intrasite_links);
  std::thread topologyAppThread([&topologyApp]() {
    LOG(INFO) << "Starting TopologyApp thread...";
    folly::setThreadName("TopologyApp");
    topologyApp.run();
    LOG(INFO) << "TopologyApp thread got stopped";
  });
  allThreads.emplace_back(std::move(topologyAppThread));
  topologyApp.waitUntilRunning();

  // Start the ignition app
  facebook::terragraph::IgnitionApp ignitionApp(
      zmqContext,
      routerSockUrl,
      monitorSockUrl,
      FLAGS_linkup_extended_dampen_interval_s,
      FLAGS_linkup_extended_dampen_failure_interval_s,
      FLAGS_linkup_backup_cn_link_interval_s,
      FLAGS_linkup_ignore_dampen_interval_after_resp);
  std::thread ignitionAppThread([&ignitionApp]() {
    LOG(INFO) << "Starting IgnitionApp thread...";
    folly::setThreadName("IgnitionApp");
    ignitionApp.run();
    LOG(INFO) << "IgnitionApp thread got stopped";
  });
  allThreads.emplace_back(std::move(ignitionAppThread));
  ignitionApp.waitUntilRunning();

  // Start the scheduler app
  SchedulerApp schedulerApp(zmqContext, routerSockUrl, monitorSockUrl);
  std::thread schedulerAppThread([&schedulerApp]() {
    LOG(INFO) << "Starting SchedulerApp thread...";
    folly::setThreadName("SchedulerApp");
    schedulerApp.run();
    LOG(INFO) << "SchedulerApp thread got stopped";
  });
  allThreads.emplace_back(std::move(schedulerAppThread));
  schedulerApp.waitUntilRunning();

  // Start the scan app
  ScanApp scanApp(zmqContext, routerSockUrl, monitorSockUrl, schedulerApp);
  std::thread scanAppThread([&scanApp]() {
    LOG(INFO) << "Starting ScanApp thread...";
    folly::setThreadName("ScanApp");
    scanApp.run();
    LOG(INFO) << "ScanApp thread got stopped";
  });
  allThreads.emplace_back(std::move(scanAppThread));
  scanApp.waitUntilRunning();

  // Start the upgrade app
  UpgradeApp upgradeApp(
      zmqContext,
      routerSockUrl,
      monitorSockUrl,
      FLAGS_status_reports_interval_s);
  std::thread upgradeAppThread([&upgradeApp]() {
    LOG(INFO) << "Starting UpgradeApp thread...";
    folly::setThreadName("UpgradeApp");
    upgradeApp.run();
    LOG(INFO) << "UpgradeApp thread got stopped";
  });
  allThreads.emplace_back(std::move(upgradeAppThread));
  upgradeApp.waitUntilRunning();

  // Start the config app
  ConfigApp configApp(
      zmqContext,
      routerSockUrl,
      monitorSockUrl,
      FLAGS_status_reports_interval_s,
      FLAGS_topology_info_sync_interval_s,
      getpid() /* controllerPid */);
  std::thread configAppThread([&configApp]() {
    LOG(INFO) << "Starting ConfigApp thread...";
    folly::setThreadName("ConfigApp");
    configApp.run();
    LOG(INFO) << "ConfigApp thread got stopped";
  });
  allThreads.emplace_back(std::move(configAppThread));
  configApp.waitUntilRunning();

  // Start the traffic app
  TrafficApp trafficApp(zmqContext, routerSockUrl, monitorSockUrl);
  std::thread trafficAppThread([&trafficApp]() {
    LOG(INFO) << "Starting TrafficApp thread...";
    folly::setThreadName("TrafficApp");
    trafficApp.run();
    LOG(INFO) << "TrafficApp thread got stopped";
  });
  allThreads.emplace_back(std::move(trafficAppThread));
  trafficApp.waitUntilRunning();

  // Start the topology builder app
  TopologyBuilderApp topologyBuilderApp(
      zmqContext, routerSockUrl, monitorSockUrl);
  std::thread topologyBuilderAppThread([&topologyBuilderApp]() {
    LOG(INFO) << "Starting TopologyBuilderApp thread...";
    folly::setThreadName("TopologyBuilderApp");
    topologyBuilderApp.run();
    LOG(INFO) << "TopologyBuilderApp thread got stopped";
  });
  allThreads.emplace_back(std::move(topologyBuilderAppThread));
  topologyBuilderApp.waitUntilRunning();

  // Start the Binary Star app
  BinaryStarApp binaryStarApp(
      zmqContext,
      routerSockUrl,
      monitorSockUrl,
      isBstarEnabled,
      FLAGS_bstar_primary,
      folly::sformat(
          "tcp://{}:{}", FLAGS_listen_addr, FLAGS_bstar_pub_port),
      folly::sformat(
          "tcp://{}:{}", bstar_peer_host, FLAGS_bstar_peer_pub_port),
      FLAGS_version_file);
  std::thread binaryStarAppThread([&binaryStarApp]() {
    LOG(INFO) << "Starting BinaryStarApp thread...";
    folly::setThreadName("BinaryStarApp");
    binaryStarApp.run();
    LOG(INFO) << "BinaryStarApp thread got stopped";
  });
  allThreads.emplace_back(std::move(binaryStarAppThread));
  binaryStarApp.waitUntilRunning();

  LOG(INFO) << "Starting main event loop...";
  mainEventLoop.run();
  LOG(INFO) << "Main event loop got stopped";

  // Stop all threads (in reverse order of their creation)
  binaryStarApp.stop();
  binaryStarApp.waitUntilStopped();
  topologyBuilderApp.stop();
  topologyBuilderApp.waitUntilStopped();
  trafficApp.stop();
  trafficApp.waitUntilStopped();
  configApp.stop();
  configApp.waitUntilStopped();
  upgradeApp.stop();
  upgradeApp.waitUntilStopped();
  scanApp.stop();
  scanApp.waitUntilStopped();
  schedulerApp.stop();
  schedulerApp.waitUntilStopped();
  ignitionApp.stop();
  ignitionApp.waitUntilStopped();
  topologyApp.stop();
  topologyApp.waitUntilStopped();
  statusApp.stop();
  statusApp.waitUntilStopped();
  zmqMonitor.stop();
  zmqMonitor.waitUntilStopped();
  broker.stop();
  broker.waitUntilStopped();
  if (FLAGS_enable_zap_apps_sock || FLAGS_enable_zap_minions_sock) {
    zapHandler->stop();
    zapHandler->waitUntilStopped();
  }

  // Wait for all threads to finish
  for (auto& t : allThreads) {
    t.join();
  }

  return 0;
}
