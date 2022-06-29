/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fbzmq/async/StopEventLoopSignalHandler.h>
#include <fbzmq/service/monitor/ZmqMonitor.h>
#include <folly/Format.h>
#include <folly/gen/Base.h>
#include <folly/gen/String.h>
#include <folly/init/Init.h>
#include <folly/system/ThreadName.h>
#include <gflags/gflags.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "ArmDriverIf.h"
#include "ArmNetlinkSocket.h"
#include "BaseDriverIf.h"
#include "DriverIfUtil.h"
#include "X86DriverIf.h"
#include "e2e/common/NodeInfoWrapper.h"

using namespace facebook::terragraph;

DEFINE_string(
    node_info_file,
    "/var/run/node_info",
    "File containing static node info");

DEFINE_string(listen_ip, "*", "The address to listen on");

DEFINE_int32(
    driver_pair_port, 17989, "The zmq pair port on which driverIf binds");
DEFINE_int32(
    driverif_monitor_pub_port,
    18990,
    "The zmq publish port on which monitor binds");
DEFINE_int32(
    driverif_monitor_router_port,
    17008,
    "The zmq router port on which driverIf binds");

using apache::thrift::CompactSerializer;

// Configure ASAN runtime options to limit memory usage
const char*
__asan_default_options() {
  return "malloc_context_size=10:quarantine_size_mb=8:max_redzone=256";
}

int
main(int argc, char** argv) {

  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  google::InstallFailureSignalHandler();

  // start signal handler before any thread
  fbzmq::ZmqEventLoop mainEventLoop;
  fbzmq::StopEventLoopSignalHandler handler(&mainEventLoop);
  handler.registerSignalHandler(SIGINT);
  handler.registerSignalHandler(SIGQUIT);
  handler.registerSignalHandler(SIGTERM);

  fbzmq::Context zmqContext;

  // initialize node info
  facebook::terragraph::NodeInfoWrapper nodeInfoW(FLAGS_node_info_file);
  auto radioMacToBusId = nodeInfoW.getRadioMacToBusId();

  std::unique_ptr<facebook::terragraph::BaseDriverIf> driverIf;
#ifdef ARM_ARCH
  bool isIf2if = nodeInfoW.isIf2if();
  driverIf = std::make_unique<facebook::terragraph::ArmDriverIf>(
      zmqContext,
      folly::sformat("tcp://{}:{}", FLAGS_listen_ip, FLAGS_driver_pair_port),
      folly::sformat("tcp://localhost:{}", FLAGS_driverif_monitor_router_port),
      std::make_unique<ArmNetlinkSocket>(),
      isIf2if,
      radioMacToBusId,
      true /* daemonMode */);
#elif x86_ARCH
  auto nodeId = nodeInfoW.getNodeId();
  if (!nodeId) {
    LOG(FATAL) << "Empty node id";
  }
  driverIf = std::make_unique<facebook::terragraph::X86DriverIf>(
      zmqContext,
      folly::sformat("tcp://{}:{}", FLAGS_listen_ip, FLAGS_driver_pair_port),
      folly::sformat("tcp://localhost:{}", FLAGS_driverif_monitor_router_port),
      nodeId.value(),
      radioMacToBusId,
      true /* daemonMode */);
#else
  LOG(FATAL) << "Undefined machine architecture";
#endif

  std::thread driverIfThread([&driverIf]() {
    LOG(INFO) << "Starting DriverIf thread...";
    folly::setThreadName("DriverIf");
    driverIf->run();
  });
  driverIf->waitUntilRunning();

  LOG(INFO) << "DriverIf running ...";

  // Start the DriverIf ZmqMonitor Server
  fbzmq::ZmqMonitor driverIfZmqMonitor(
      std::string{folly::sformat(
          "tcp://{}:{}", FLAGS_listen_ip, FLAGS_driverif_monitor_router_port)},
      std::string{folly::sformat(
          "tcp://{}:{}", FLAGS_listen_ip, FLAGS_driverif_monitor_pub_port)},
      zmqContext);
  std::thread driverIfZmqMonitorThread([&driverIfZmqMonitor]() noexcept {
    LOG(INFO) << "Starting ZmqMonitor thread...";
    folly::setThreadName("ZmqMonitor");
    driverIfZmqMonitor.run();
    LOG(INFO) << "ZmqMonitor thread got stopped";
  });
  driverIfZmqMonitor.waitUntilRunning();

  LOG(INFO) << "Starting main event loop...";
  mainEventLoop.run();
  LOG(INFO) << "Main event loop got stopped";

  driverIf->stop();
  driverIf->waitUntilStopped();
  driverIfThread.join();

  driverIfZmqMonitor.stop();
  driverIfZmqMonitor.waitUntilStopped();
  driverIfZmqMonitorThread.join();
}
