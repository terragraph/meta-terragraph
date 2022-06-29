/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <chrono>
#include <csignal>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <fbzmq/async/StopEventLoopSignalHandler.h>
#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/init/Init.h>
#include <folly/system/ThreadName.h>
#include <gflags/gflags.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "Broker.h"
#include "ConfigApp.h"
#include "SharedObjects.h"
#include "StatsApp.h"
#include "StatusApp.h"
#include "e2e/common/ExceptionHandler.h"

using namespace fbzmq;
using namespace facebook::terragraph;
using namespace stats;

using apache::thrift::CompactSerializer;

// controller addr & ports
DEFINE_string(
    controller_ip,
    "",
    "[DEPRECATED] The IP of the controller to connect to");
DEFINE_string(
    controller_host,
    "localhost",
    "The hostname or IP of the controller to connect to");
DEFINE_int32(controller_port, 17077, "The port controller listens on");
DEFINE_int32(
    agent_router_port, 8002, "The port aggregator listens on for agents");
DEFINE_int32(app_router_port, 18100, "The port aggregator listens on for apps");
DEFINE_string(listen_addr, "*", "The IP address to bind to");

// aggregator config file
DEFINE_string(
    aggregator_config_file,
    "/data/cfg/aggregator_config.json",
    "The aggregator config file");

int
main(int argc, char** argv) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  ExceptionHandler::install();

  fbzmq::Context context;

  // Initialize shared objects
  SharedObjects::getAggrConfigWrapper()->wlock()->setE2EConfigFile(
      FLAGS_aggregator_config_file);

  // start signal handler before any thread
  ZmqEventLoop mainEventLoop;
  StopEventLoopSignalHandler handler(&mainEventLoop);
  handler.registerSignalHandler(SIGINT);
  handler.registerSignalHandler(SIGQUIT);
  handler.registerSignalHandler(SIGTERM);

  // Start the broker app
  Broker broker(
      context,
      folly::sformat("tcp://{}:{}", FLAGS_listen_addr, FLAGS_agent_router_port),
      folly::sformat("tcp://{}:{}", FLAGS_listen_addr, FLAGS_app_router_port));
  std::thread brokerThread([&broker]() {
    LOG(INFO) << "Starting Broker thread...";
    folly::setThreadName("Broker");
    broker.run();
    LOG(INFO) << "Broker thread got stopped";
  });
  broker.waitUntilRunning();

  // Start the status app
  StatusApp statusApp(
      context, folly::sformat("tcp://localhost:{}", FLAGS_app_router_port));
  std::thread statusAppThread([&statusApp]() {
    LOG(INFO) << "Starting StatusApp thread...";
    folly::setThreadName("StatusApp");
    statusApp.run();
    LOG(INFO) << "StatusApp thread got stopped";
  });
  statusApp.waitUntilRunning();

  std::string controller_host = FLAGS_controller_host;
  if (!google::GetCommandLineFlagInfoOrDie("controller_ip").is_default) {
    controller_host = folly::sformat("[{}]", FLAGS_controller_ip);
  }

  // Start the config app
  ConfigApp configApp(
      context,
      folly::sformat("tcp://localhost:{}", FLAGS_app_router_port),
      getpid() /* aggregatorPid */);
  std::thread configAppThread([&configApp]() {
    LOG(INFO) << "Starting ConfigApp thread...";
    folly::setThreadName("ConfigApp");
    configApp.run();
    LOG(INFO) << "ConfigApp thread got stopped";
  });
  configApp.waitUntilRunning();

  // Start the stats app
  StatsApp statsApp(
      context,
      folly::sformat("tcp://localhost:{}", FLAGS_app_router_port),
      folly::sformat(
          "tcp://{}:{}", controller_host, FLAGS_controller_port));
  std::thread statsAppThread([&statsApp]() {
    LOG(INFO) << "Starting StatsApp thread...";
    folly::setThreadName("StatsApp");
    statsApp.run();
    LOG(INFO) << "StatsApp thread got stopped";
  });
  statsApp.waitUntilRunning();

  LOG(INFO) << "Starting main event loop...";
  mainEventLoop.run();
  LOG(INFO) << "Main event loop got stopped";

  // Stop all threads (in reverse order of their creation)
  configApp.stop();
  configApp.waitUntilStopped();
  statsApp.stop();
  statsApp.waitUntilStopped();
  statusApp.stop();
  statusApp.waitUntilStopped();
  broker.stop();
  broker.waitUntilStopped();

  brokerThread.join();
  statusAppThread.join();
  statsAppThread.join();
  configAppThread.join();
  return 0;
}
