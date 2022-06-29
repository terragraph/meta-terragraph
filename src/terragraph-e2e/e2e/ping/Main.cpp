/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <ifaddrs.h>
#include <netdb.h>

#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <folly/ExceptionString.h>
#include <folly/Format.h>
#include <folly/IPAddress.h>
#include <folly/init/Init.h>
#include <folly/system/ThreadName.h>
#include <gflags/gflags.h>

#include "TargetThread.h"

using namespace facebook::terragraph;

DEFINE_int32(num_ping_threads, 2, "Number of ping thread pairs to start");
DEFINE_int32(ping_port, 31338, "UDP port to listen for ping agent probes");
DEFINE_int32(ping_queue_cap, 64000, "Capacity of ping shared queue");
DEFINE_int32(
    ping_recv_timeout_m,
    10,
    "Max number of minutes to wait for a probe. Set to 0 to block "
    "indefinitely");
DEFINE_string(src_if, "lo", "Interface to bind the UDP server to");

std::optional<std::string>
getHost() {
  struct ifaddrs *ifaddr, *ifa;
  if (getifaddrs(&ifaddr) > 0) {
    throw std::runtime_error(
        folly::sformat("getifaddrs() failed: {}", std::strerror(errno)));
  }

  std::optional<std::string> host = std::nullopt;
  for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr) {
      continue;
    }

    if (ifa->ifa_addr->sa_family != AF_INET6 ||
        FLAGS_src_if.compare(ifa->ifa_name) != 0) {
      continue;
    }

    char tmpHost[NI_MAXHOST];
    if (int res = getnameinfo(
            ifa->ifa_addr,
            sizeof(struct sockaddr_in6),
            tmpHost,
            NI_MAXHOST,
            nullptr,
            0,
            NI_NUMERICHOST)) {
      LOG(ERROR) << "get nameinfo() failed: " << gai_strerror(res);
      continue;
    }

    try {
      auto ipAddress = folly::IPAddress(tmpHost);
      if (ipAddress.isLoopback()) {
        VLOG(2) << "Found loopback address on " << FLAGS_src_if;
      } else {
        host = tmpHost;
        break;
      }
    } catch (const folly::IPAddressFormatException& e) {
      LOG(ERROR) << tmpHost << " is not a valid IP address";
    }
  }

  freeifaddrs(ifaddr);
  return host;
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv, true);

  auto host = getHost();
  while (!host) {
    LOG(ERROR) << "Could not find global address on " << FLAGS_src_if
               << ", retrying in 10s...";
    std::this_thread::sleep_for(std::chrono::seconds(10));
    host = getHost();
  }

  std::vector<std::thread> receiverThreads;
  std::vector<std::thread> senderThreads;
  auto timeout_m = FLAGS_ping_recv_timeout_m <= 0
                       ? std::nullopt
                       : std::optional<int32_t>{FLAGS_ping_recv_timeout_m};

  for (int i = 0; i < FLAGS_num_ping_threads; i++) {
    int socket;

    try {
      socket = initUdpServer(*host, FLAGS_ping_port, timeout_m);
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Error starting server: " << folly::exceptionStr(ex);
      continue;
    }

    if (socket == kSockFdInvalid) {
      continue;
    }

    LOG(INFO) << "UDP server initialized, listening on "
              << "[" << *host << "]"
              << ":" << FLAGS_ping_port;

    auto queue = std::make_shared<folly::MPMCQueue<std::unique_ptr<Probe>>>(
        FLAGS_ping_queue_cap);

    receiverThreads.emplace_back(std::thread([queue, socket, i]() {
      auto receiver = std::make_shared<TargetReceiverThread>(socket, queue);
      folly::setThreadName(
          pthread_self(), folly::sformat("Ping Receiver {}", i));
      receiver->run();
    }));

    senderThreads.emplace_back(std::thread([queue, socket, i, timeout_m]() {
      auto sender = std::make_shared<TargetSenderThread>(socket, queue);
      folly::setThreadName(pthread_self(), folly::sformat("Ping Sender {}", i));
      sender->run(timeout_m);
    }));
  }

  for (auto& thread : receiverThreads) {
    thread.join();
  }

  for (auto& thread : senderThreads) {
    thread.join();
  }

  LOG(WARNING) << "Threads finished, stopping server...";
  return 0;
}
