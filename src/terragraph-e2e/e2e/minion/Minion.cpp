/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fbzmq/async/StopEventLoopSignalHandler.h>
#include <fbzmq/service/monitor/ZmqMonitor.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/String.h>
#include <folly/gen/Base.h>
#include <folly/gen/String.h>
#include <folly/init/Init.h>
#include <folly/system/ThreadName.h>
#include <gflags/gflags.h>
#include <sys/types.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <unistd.h>

#include "Broker.h"
#include "ConfigApp.h"
#include "DriverApp.h"
#include "IgnitionApp.h"
#include "OpenrClientApp.h"
#include "SharedObjects.h"
#include "StatusApp.h"
#include "TrafficApp.h"
#include "UpgradeApp.h"
#include "UpgradeStateCache.h"

#include "e2e/common/ExceptionHandler.h"
#include "e2e/common/Progress.h"
#include "e2e/driver-if/ArmDriverIf.h"
#include "e2e/driver-if/ArmNetlinkSocket.h"
#include "e2e/driver-if/BaseDriverIf.h"
#include "e2e/driver-if/X86DriverIf.h"

using namespace facebook::terragraph;

DEFINE_string(
    node_info_file,
    "/var/run/node_info",
    "File containing static node info");
DEFINE_string(
    node_config_file, "/data/cfg/node_config.json", "Node configuration file");
DEFINE_string(version_file, "/etc/tgversion", "Version file");
DEFINE_string(
    my_network_info_file, "/tmp/mynetworkinfo", "Network information file");
// controller ip & ports
DEFINE_string(controller_ip, "", "[DEPRECATED] The controller we talk to");
DEFINE_string(
    controller_host, "", "The hostname or IP of the controller we talk to");
DEFINE_int32(controller_router_port, 7007, "The port controller listens on");
// minion ip & ports
DEFINE_string(listen_ip, "[::1]", "The address to listen on");
DEFINE_int32(
    minion_app_router_port,
    17177,
    "The port minion listens on for apps");
DEFINE_int32(
    minion_broadcast_pub_port,
    17277,
    "The zmq publish port on which the minion broadcasts asynchronous messages "
    "(-1 to disable)");
DEFINE_int32(
    minion_monitor_router_port,
    17007,
    "The zmq router port on which the monitor listens on");
DEFINE_int32(
    minion_monitor_pub_port,
    18989,
    "The zmq publish port on which the app monitor binds");
DEFINE_bool(
    driver_if_only,
    false,
    "Enable only the DriverIf module and disable all minion functionality");
DEFINE_int32(
    driverif_monitor_router_port,
    17008,
    "The zmq router port on which the DriverIf monitor listens");
DEFINE_int32(
    driverif_monitor_pub_port,
    18990,
    "The zmq pub port on which the DriverIf publishes firmware stats");
DEFINE_int32(
    driverif_pair_port,
    17989,
    "The zmq pair port on which the DriverIf binds");
// ZmqMonitor
DEFINE_int32(
    monitor_counter_lifetime_s,
    300,
    "The lifetime of stale counters in ZmqMonitor (in seconds)");
// upgrade app
DEFINE_bool(
    use_https,
    false,
    "Only allow HTTPS (not HTTP) sessions to download minion images for "
    "upgrades");
// status app
DEFINE_string(
    ipv6_global_addressable_ifname,
    "lo",
    "The globally addressable ipv6 interface on minions");
DEFINE_int32(
    status_report_interval_s, 5, "Time period in seconds for status report");
DEFINE_int32(
    bgp_status_interval_s,
    30,
    "Time period in seconds for bgp status fetching");
// ignition app
DEFINE_int32(
    linkup_resp_wait_timeout_s,
    // NOTE: This value is chosen based on the vendor-specific IBF timeout
    // defined in wireless-fw (BF_TIMEOUT), which for QTI is 8192 superframes
    // (~13.1 seconds). We add some margin to compensate for processing delays.
    //
    // Controller should wait AT LEAST this long before sending subsequent
    // ignition commands to the same sector (if no response was received).
    15,
    "Timeout before we give up on unresponsive linkups");
DEFINE_bool(
    disable_driver_if,
    false,
    "We disable driver if in X86 emulation and run a separate driver daemon");
// Broker
DEFINE_int32(
    ctrl_socket_timeout_s,
    20,  // NOTE: This should be a multiple of status_report_interval_s!
    "Timeout in seconds before disconnecting and reconnecting to the "
    "controller dealer socket if no message has been received");

using apache::thrift::CompactSerializer;

// Configure ASAN runtime options to limit memory usage
const char *__asan_default_options() {
  return "malloc_context_size=10:quarantine_size_mb=8:max_redzone=256";
}

namespace {
const std::string kMinionProgressTouchFile = "minion";
const std::chrono::seconds kMinionProgressReportPeriodSeconds{1};

#ifdef ARM_ARCH
const bool kReportProgress = true;
#else
const bool kReportProgress = false;
#endif
} // namespace

int
main(int argc, char** argv) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  ExceptionHandler::install();

  // start signal handler before any thread
  fbzmq::ZmqEventLoop mainEventLoop;
  fbzmq::StopEventLoopSignalHandler handler(&mainEventLoop);
  handler.registerSignalHandler(SIGINT);
  handler.registerSignalHandler(SIGQUIT);
  handler.registerSignalHandler(SIGTERM);

  // the zmq context - IO pool.
  fbzmq::Context zmqContext;

  std::vector<std::thread> allThreads{};

  // initialize node config
  auto lockedNodeConfigWrapper =
      minion::SharedObjects::getNodeConfigWrapper()->wlock();
  lockedNodeConfigWrapper->setNodeConfigFile(FLAGS_node_config_file);
  int64_t wsecEnable =
      lockedNodeConfigWrapper->getRadioParams().fwParams.wsecEnable_ref()
          .value_or(0);
  lockedNodeConfigWrapper.unlock();  // lockedNodeConfigWrapper -> NULL

  // initialize node info
  auto lockedNodeInfoW = minion::SharedObjects::getNodeInfoWrapper()->wlock();
  lockedNodeInfoW->setNodeInfoFile(FLAGS_node_info_file);
  auto nodeId = lockedNodeInfoW->getNodeId();
  auto hwModel = lockedNodeInfoW->getHwModel();
  auto hwBoardId = lockedNodeInfoW->getHwBoardId();
  auto radioMacToBusId = lockedNodeInfoW->getRadioMacToBusId();
#ifdef ARM_ARCH
  bool isIf2if = lockedNodeInfoW->isIf2if();
#endif
  lockedNodeInfoW.unlock();  // lockedNodeInfoW -> NULL

  // Validate certain minion-specific parameters
  if (!FLAGS_driver_if_only) {
    if (!nodeId || folly::trimWhitespace(nodeId.value()).empty()) {
      LOG(FATAL) << "Empty node ID";
    }
    LOG(INFO) << folly::sformat("Using node ID {}", nodeId.value());

    if (hwModel) {
      LOG(INFO)
          << folly::sformat("Current Hardware Model: {}", hwModel.value());
    } else {
      LOG(ERROR) << "Unknown Hardware Model";
    }
    if (hwBoardId) {
      LOG(INFO) << folly::sformat(
          "Current Hardware Board ID: {}", hwBoardId.value());
    } else {
      LOG(ERROR) << "Unknown Hardware Board ID";
    }
  }

  // Start DriverIf thread before other threads which send messages to DriverIf
  std::unique_ptr<facebook::terragraph::BaseDriverIf> driverIf;
#ifdef ARM_ARCH
  driverIf = std::make_unique<facebook::terragraph::ArmDriverIf>(
      zmqContext,
      folly::sformat("tcp://{}:{}", FLAGS_listen_ip, FLAGS_driverif_pair_port),
      folly::sformat("tcp://localhost:{}", FLAGS_driverif_monitor_router_port),
      std::make_unique<ArmNetlinkSocket>(),
      isIf2if,
      radioMacToBusId,
      FLAGS_driver_if_only /* daemonMode */);
#elif x86_ARCH
  if (not FLAGS_disable_driver_if) {
    driverIf = std::make_unique<facebook::terragraph::X86DriverIf>(
        zmqContext,
        folly::sformat(
            "tcp://{}:{}", FLAGS_listen_ip, FLAGS_driverif_pair_port),
        folly::sformat(
            "tcp://localhost:{}", FLAGS_driverif_monitor_router_port),
        nodeId.value_or("00:00:00:00:00:00") /* arbitrary default */,
        radioMacToBusId,
        FLAGS_driver_if_only /* daemonMode */);
  }
#else
  LOG(FATAL) << "Undefined machine architecture";
#endif
  if (not FLAGS_disable_driver_if) {
    std::thread driverIfThread([&driverIf]() noexcept {
      LOG(INFO) << "Starting DriverIf thread...";
      folly::setThreadName("DriverIf");
      driverIf->run();
      LOG(INFO) << "DriverIf thread got stopped";
    });
    allThreads.emplace_back(std::move(driverIfThread));
    driverIf->waitUntilRunning();
  }

  // Start the DriverIf ZmqMonitor Server
  fbzmq::ZmqMonitor driverIfZmqMonitor(
      folly::sformat(
          "tcp://{}:{}", FLAGS_listen_ip, FLAGS_driverif_monitor_router_port),
      folly::sformat(
          "tcp://{}:{}", FLAGS_listen_ip, FLAGS_driverif_monitor_pub_port),
      zmqContext,
      folly::none,
      std::chrono::seconds(FLAGS_monitor_counter_lifetime_s));
  std::thread driverIfZmqMonitorThread([&driverIfZmqMonitor]() noexcept {
    LOG(INFO) << "Starting DriverIf ZmqMonitor thread...";
    folly::setThreadName("DriverIfZmqMonitor");
    driverIfZmqMonitor.run();
    LOG(INFO) << "DriverIf ZmqMonitor thread got stopped";
  });
  allThreads.emplace_back(std::move(driverIfZmqMonitorThread));
  driverIfZmqMonitor.waitUntilRunning();

  // Minion functionality is below... (disabled with FLAGS_driver_if_only)
  std::unique_ptr<minion::DriverApp> driverApp;
  std::unique_ptr<minion::Broker> minionBroker;
  std::unique_ptr<fbzmq::ZmqMonitor> minionZmqMonitor;
  std::unique_ptr<minion::IgnitionApp> ignitionApp;
  std::unique_ptr<minion::StatusApp> statusApp;
  std::unique_ptr<minion::UpgradeApp> upgradeApp;
  std::unique_ptr<minion::ConfigApp> configApp;
  std::unique_ptr<minion::OpenrClientApp> openrClientApp;
  std::unique_ptr<minion::TrafficApp> trafficApp;
  if (!FLAGS_driver_if_only) {
    // Broker's ZMQ router socket
    const std::string brokerAppRouterUrl = folly::sformat(
        "tcp://{}:{}",
        FLAGS_listen_ip,
        FLAGS_minion_app_router_port);

    // Start the DriverApp thread
    driverApp = std::make_unique<minion::DriverApp>(
        zmqContext,
        brokerAppRouterUrl,
        folly::sformat("tcp://localhost:{}", FLAGS_minion_monitor_router_port),
        folly::sformat("tcp://localhost:{}", FLAGS_driverif_pair_port),
        nodeId.value());
    std::thread driverAppThread([&driverApp]() noexcept {
      LOG(INFO) << "Starting DriverApp thread...";
      folly::setThreadName("DriverApp");
      driverApp->run();
      LOG(INFO) << "DriverApp thread got stopped";
    });
    allThreads.emplace_back(std::move(driverAppThread));
    driverApp->waitUntilRunning();

    std::string controller_host = FLAGS_controller_host;
    if (!google::GetCommandLineFlagInfoOrDie("controller_ip").is_default) {
      controller_host = FLAGS_controller_ip;
    }

    // Start the broker thread
    minionBroker = std::make_unique<minion::Broker>(
        zmqContext,
        nodeId.value(),
        controller_host.empty()
          ? ""
          : folly::sformat(
              "tcp://{}:{}", controller_host, FLAGS_controller_router_port),
        brokerAppRouterUrl,
        FLAGS_minion_broadcast_pub_port == -1
          ? ""
          : folly::sformat(
              "tcp://{}:{}", FLAGS_listen_ip, FLAGS_minion_broadcast_pub_port),
        std::chrono::seconds(FLAGS_ctrl_socket_timeout_s),
        FLAGS_my_network_info_file);
    std::thread minionBrokerThread([&minionBroker]() noexcept {
      LOG(INFO) << "Starting Minion Broker thread...";
      folly::setThreadName("Broker");
      minionBroker->run();
      LOG(INFO) << "Minion Broker thread got stopped";
    });
    allThreads.emplace_back(std::move(minionBrokerThread));
    minionBroker->waitUntilRunning();

    // Start the Minion ZmqMonitor server
    minionZmqMonitor = std::make_unique<fbzmq::ZmqMonitor>(
        folly::sformat(
            "tcp://{}:{}", FLAGS_listen_ip, FLAGS_minion_monitor_router_port),
        folly::sformat(
            "tcp://{}:{}", FLAGS_listen_ip, FLAGS_minion_monitor_pub_port),
        zmqContext,
        folly::none,
        std::chrono::seconds(FLAGS_monitor_counter_lifetime_s));
    std::thread minionZmqMonitorThread([&minionZmqMonitor]() noexcept {
      LOG(INFO) << "Starting Minion ZmqMonitor thread...";
      folly::setThreadName("MinionZmqMonitor");
      minionZmqMonitor->run();
      LOG(INFO) << "Minion ZmqMonitor thread got stopped";
    });
    allThreads.emplace_back(std::move(minionZmqMonitorThread));
    minionZmqMonitor->waitUntilRunning();

    // Start the ignition app
    ignitionApp = std::make_unique<minion::IgnitionApp>(
        zmqContext,
        brokerAppRouterUrl,
        folly::sformat("tcp://localhost:{}", FLAGS_minion_monitor_router_port),
        nodeId.value(),
        std::chrono::seconds(FLAGS_linkup_resp_wait_timeout_s),
        wsecEnable);
    std::thread ignitionAppThread([&ignitionApp]() noexcept {
      LOG(INFO) << "Starting IgnitionApp thread...";
      folly::setThreadName("IgnitionApp");
      ignitionApp->run();
      LOG(INFO) << "IgnitionApp thread got stopped";
    });
    allThreads.emplace_back(std::move(ignitionAppThread));
    ignitionApp->waitUntilRunning();

    minion::UpgradeStateCache upgradeStateCache(FLAGS_version_file);

    // Start the status app
    statusApp = std::make_unique<minion::StatusApp>(
        zmqContext,
        brokerAppRouterUrl,
        folly::sformat("tcp://localhost:{}", FLAGS_minion_monitor_router_port),
        nodeId.value(),
        std::chrono::seconds(FLAGS_status_report_interval_s),
        std::chrono::seconds(FLAGS_bgp_status_interval_s),
        FLAGS_ipv6_global_addressable_ifname,
        upgradeStateCache.getUpgradeStatus(),
        FLAGS_version_file);
    std::thread statusAppThread([&statusApp]() noexcept {
      LOG(INFO) << "Starting StatusApp thread...";
      folly::setThreadName("StatusApp");
      statusApp->run();
      LOG(INFO) << "StatusApp thread got stopped";
    });
    allThreads.emplace_back(std::move(statusAppThread));
    statusApp->waitUntilRunning();

    // Start the upgrade app
    upgradeApp = std::make_unique<minion::UpgradeApp>(
        zmqContext,
        brokerAppRouterUrl,
        folly::sformat("tcp://localhost:{}", FLAGS_minion_monitor_router_port),
        nodeId.value(),
        FLAGS_use_https,
        FLAGS_ipv6_global_addressable_ifname,
        upgradeStateCache);
    std::thread upgradeAppThread([&upgradeApp]() noexcept {
      LOG(INFO) << "Starting UpgradeApp thread...";
      folly::setThreadName("UpgradeApp");
      upgradeApp->run();
      LOG(INFO) << "UpgradeApp thread got stopped";
    });
    allThreads.emplace_back(std::move(upgradeAppThread));
    upgradeApp->waitUntilRunning();

    // Start the config app
    configApp = std::make_unique<minion::ConfigApp>(
        zmqContext,
        brokerAppRouterUrl,
        folly::sformat("tcp://localhost:{}", FLAGS_minion_monitor_router_port),
        nodeId.value());
    std::thread configAppThread([&configApp]() noexcept {
      LOG(INFO) << "Starting ConfigApp thread...";
      folly::setThreadName("ConfigApp");
      configApp->run();
      LOG(INFO) << "ConfigApp thread got stopped";
    });
    allThreads.emplace_back(std::move(configAppThread));
    configApp->waitUntilRunning();

    // Start the Open/R client app
    openrClientApp = std::make_unique<minion::OpenrClientApp>(
        zmqContext,
        brokerAppRouterUrl,
        folly::sformat("tcp://localhost:{}", FLAGS_minion_monitor_router_port),
        nodeId.value(),
        FLAGS_my_network_info_file);
    std::thread openrClientAppThread([&openrClientApp]() noexcept {
      LOG(INFO) << "Starting OpenrClientApp thread...";
      folly::setThreadName("OpenrClientApp");
      openrClientApp->run();
      LOG(INFO) << "OpenrClientApp thread got stopped";
    });
    allThreads.emplace_back(std::move(openrClientAppThread));
    openrClientApp->waitUntilRunning();

    // Start the traffic app
    trafficApp = std::make_unique<minion::TrafficApp>(
        zmqContext,
        brokerAppRouterUrl,
        folly::sformat("tcp://localhost:{}", FLAGS_minion_monitor_router_port),
        nodeId.value());
    std::thread trafficAppThread([&trafficApp]() noexcept {
      LOG(INFO) << "Starting TrafficApp thread...";
      folly::setThreadName("TrafficApp");
      trafficApp->run();
      LOG(INFO) << "TrafficApp thread got stopped";
    });
    allThreads.emplace_back(std::move(trafficAppThread));
    trafficApp->waitUntilRunning();

    // Minion liveness reporting for watchdog
    if (kReportProgress) {
      Progress progress;
      progress.report(kMinionProgressTouchFile);
      auto reportProgressTimer =
          fbzmq::ZmqTimeout::make(&mainEventLoop, [&progress]() {
            VLOG(4) << "Reporting minion progress ...";
            progress.report(kMinionProgressTouchFile);
          });
      reportProgressTimer->scheduleTimeout(
          kMinionProgressReportPeriodSeconds, true /* periodic */);
    }
  }

  LOG(INFO) << "Starting main event loop...";
  mainEventLoop.run();
  LOG(INFO) << "Main event loop got stopped";

  // Stop all threads (in reverse order of their creation)
  if (!FLAGS_driver_if_only) {
    trafficApp->stop();
    trafficApp->waitUntilStopped();
    openrClientApp->stop();
    openrClientApp->waitUntilStopped();
    configApp->stop();
    configApp->waitUntilStopped();
    upgradeApp->stop();
    upgradeApp->waitUntilStopped();
    statusApp->stop();
    statusApp->waitUntilStopped();
    ignitionApp->stop();
    ignitionApp->waitUntilStopped();
    minionZmqMonitor->stop();
    minionZmqMonitor->waitUntilStopped();
    minionBroker->stop();
    minionBroker->waitUntilStopped();
    driverApp->stop();
    driverApp->waitUntilStopped();
  }
  driverIfZmqMonitor.stop();
  driverIfZmqMonitor.waitUntilStopped();
  if (not FLAGS_disable_driver_if) {
    driverIf->stop();
    driverIf->waitUntilStopped();
  }

  // Wait for all threads to finish
  for (auto& t : allThreads) {
    t.join();
  }

  return 0;
}
