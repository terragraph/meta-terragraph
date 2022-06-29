/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <cstdlib>
#include <thread>

extern "C" {
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>
}

#include <folly/init/Init.h>
#include <folly/system/ThreadName.h>
#include <folly/io/async/EventBase.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "SquireHandler.h"

static const auto nonEmpty = [](const char*, const std::string& value) {
  return !value.empty();
};

DEFINE_string(
    prefix,
    "",
    "IPv6 prefix to advertise to CPEs. If empty, deduce prefix from IP "
    "assigned to the interface set with -prefix_from_interface");

DEFINE_string(
    prefix_from_interface,
    "",
    "Interface to deduce prefix from. If empty, prefix must be manually "
    "chosen with -prefix");

DEFINE_string(nic, "", "Network interface to use, e.g. 'nic3'");
DEFINE_validator(nic, nonEmpty);

DEFINE_string(wireless, "", "Wireless interface to use, e.g. 'terra0'");

DEFINE_string(
    non_default_route,
    "",
    "Route prefix to advertise (RFC 4191). "
    "If empty, advertises a default route");

DEFINE_string(
    node_config_file,
    "/data/cfg/node_config.json",
    "Node configuration file");

using namespace facebook::terragraph;

// Configure ASAN runtime options to limit memory usage
const char *__asan_default_options() {
    return "malloc_context_size=10:quarantine_size_mb=8:max_redzone=256";
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;

  if (FLAGS_prefix.empty() == FLAGS_prefix_from_interface.empty()) {
    // If both are empty or both are set
    LOG(FATAL) << "One and only one of "
                  "-prefix and -prefix_from_interface must be set";
  }

  sigset_t signalMask;
  sigemptyset(&signalMask);
  sigaddset(&signalMask, SIGINT);
  sigaddset(&signalMask, SIGQUIT);
  sigaddset(&signalMask, SIGTERM);
  int err = pthread_sigmask(SIG_BLOCK, &signalMask, nullptr);
  if (err != 0) {
    LOG(ERROR) << "Could not set signal masks, error=" << err << ": "
               << strerror(err);
    return 1;
  }

  int sigfd = signalfd(-1, &signalMask, SFD_CLOEXEC);
  if (sigfd == -1) {
    PLOG(ERROR) << "Could not set signal fd";
    return 1;
  }

  fbzmq::ZmqEventLoop zmqLoop;
  std::shared_ptr<SquireNlHandler> nlHandler =
      std::make_shared<SquireNlHandler>(
      FLAGS_nic,
      FLAGS_wireless,
      zmqLoop,
      FLAGS_prefix_from_interface,
      FLAGS_non_default_route,
      FLAGS_node_config_file);

  // create Netlink Protocol object in a new thread
  auto evb = std::make_unique<folly::EventBase>();
  openr::messaging::ReplicateQueue<openr::fbnl::NetlinkEvent> netlinkEventsQ;

  auto nlProtocolSocket =
      std::make_unique<openr::fbnl::NetlinkProtocolSocket>(evb.get(),
      netlinkEventsQ);
  auto nlProtocolSocketThread = std::thread([&]() {
    LOG(INFO) << "Starting netlink thread ...";
    folly::setThreadName(pthread_self(), "netlink");
    LOG(INFO) << "netlink thread got stopped.";
  });

  SquireNlThread nlThreadObj(
      std::move(nlHandler),
      std::move(nlProtocolSocket),
      FLAGS_nic,
      FLAGS_wireless,
      sigfd,
      zmqLoop,
      FLAGS_prefix_from_interface,
      FLAGS_prefix);

  LOG(INFO) << "starting threads..";

  std::thread nlThread([&nlThreadObj] {
      nlThreadObj.preRun();
      nlThreadObj.run();
      });

  nlThread.join();

  LOG(INFO) << "Exit requested, cleaning up";
  nlProtocolSocketThread.join();

  return 0;
}
