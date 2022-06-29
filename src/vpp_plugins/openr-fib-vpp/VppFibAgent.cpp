/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <csignal>

#include <folly/init/Init.h>
#include <folly/io/async/AsyncSignalHandler.h>
#include <folly/io/async/EventBase.h>
#include <folly/system/ThreadName.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <thrift/lib/cpp2/server/ThriftServer.h>

#include "VppClient.h"
#include "VppFibHandler.h"

DEFINE_int32 (fib_thrift_port, 60100,
              "Thrift server port for the VppFibHandler");

// VAPI config
DEFINE_int32 (vapi_max_outstanding_requests, 64,
              "Max number of outstanding requests queued by the VAPI client");
DEFINE_int32 (vapi_response_queue_size, 32,
              "Size of the VAPI client's response queue");

using openr::VppClient;
using openr::VppFibHandler;

class VppFibSignalHandler : public folly::AsyncSignalHandler
{
public:
  explicit VppFibSignalHandler (folly::EventBase *evb)
      : folly::AsyncSignalHandler (evb)
  {
  }

protected:
  void signalReceived (int signum) noexcept override
  {
    LOG (INFO) << "Caught signal: " << signum << ". Stopping EventBase.";
    getEventBase ()->terminateLoopSoon ();
  }
};

int main (int argc, char **argv)
{
  // Init everything
  folly::init (&argc, &argv);

  folly::EventBase mainEvb;

  // Register signal handler for stop
  VppFibSignalHandler signalHandler (&mainEvb);
  signalHandler.registerSignalHandler (SIGINT);
  signalHandler.registerSignalHandler (SIGQUIT);
  signalHandler.registerSignalHandler (SIGTERM);

  // Create VPP connection
  VppClient vppClient (std::nullopt, FLAGS_vapi_max_outstanding_requests,
                       FLAGS_vapi_response_queue_size);
  while (!vppClient.connect ())
    {
      LOG (INFO) << "Waiting for VPP to start...";
      std::this_thread::sleep_for (std::chrono::seconds (2));
    }
  LOG (INFO) << "Connected to VPP.";

  // Start FibService thread
  apache::thrift::ThriftServer vppFibAgentServer;
  auto fibHandler = std::make_shared<VppFibHandler> (&mainEvb, &vppClient);
  auto fibThriftThread = std::thread ([fibHandler, &vppFibAgentServer]() {
    folly::setThreadName ("FibService");
    vppFibAgentServer.setIOThreadPool (
        std::make_shared<folly::IOThreadPoolExecutor> (1));
    vppFibAgentServer.setPort (FLAGS_fib_thrift_port);
    vppFibAgentServer.setInterface (fibHandler);
    vppFibAgentServer.setDuplex (true);

    LOG (INFO) << "Vpp FibAgent starting...";
    vppFibAgentServer.serve ();
    LOG (INFO) << "Vpp FibAgent stopped.";
  });

  LOG (INFO) << "Main EventBase starting...";
  mainEvb.loopForever ();
  LOG (INFO) << "Main EventBase stopped.";

  // Stop fib-server and wait for server to finish
  vppFibAgentServer.stop ();
  fibThriftThread.join ();

  return 0;
}
