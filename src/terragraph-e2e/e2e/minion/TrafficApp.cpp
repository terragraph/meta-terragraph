/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TrafficApp.h"

#include <chrono>
#include <csignal>
#include <sys/wait.h>
#include <thread>

#include <folly/IPAddress.h>
#include <folly/MapUtil.h>
#include <folly/String.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "e2e/common/Consts.h"

using namespace fbzmq;
using apache::thrift::detail::TEnumMapFactory;

DEFINE_int32(iperf_server_port_min, 60101, "Start of iperf port range.");
DEFINE_int32(iperf_server_port_max, 60150, "End of iperf port range.");

namespace facebook {
namespace terragraph {
namespace minion {

TrafficApp::TrafficApp(
    fbzmq::Context& zmqContext,
    const std::string& brokerRouterUrl,
    const std::string& monitorSockUrl,
    const std::string& macAddr)
    : MinionApp(
          zmqContext,
          brokerRouterUrl,
          monitorSockUrl,
          macAddr,
          E2EConsts::kTrafficAppMinionId) {

  auto lockedIperfAvailablePorts = iperfAvailablePorts_.wlock();
  // Initialize available ports
  for (int32_t i = FLAGS_iperf_server_port_min;
       i <= FLAGS_iperf_server_port_max;
       i++) {
    lockedIperfAvailablePorts->insert(i);
  }
    lockedIperfAvailablePorts.unlock();
}

void
TrafficApp::processMessage(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::START_IPERF_SERVER:
      processStartIperfServer(senderApp, message);
      break;
    case thrift::MessageType::START_IPERF_CLIENT:
      processStartIperfClient(senderApp, message);
      break;
    case thrift::MessageType::STOP_IPERF:
      processStopIperf(senderApp, message);
      break;
    case thrift::MessageType::START_PING:
      processStartPing(senderApp, message);
      break;
    case thrift::MessageType::STOP_PING:
      processStopPing(senderApp, message);
      break;
    default:
      LOG(ERROR)
          << "Wrong type of message ("
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                     message.mType, "UNKNOWN")
          << ") received from " << senderApp;
  }
}

void
TrafficApp::processStartIperfServer(
    const std::string& senderApp, const thrift::Message& message) {
  auto startServer = maybeReadThrift<thrift::StartMinionIperf>(message);
  if (!startServer) {
    handleInvalidMessage("StartMinionIperf", senderApp);
    return;
  }

  std::string startMsg =
    folly::sformat("Starting iperf server for session ID: {}", startServer->id);
  LOG(INFO) << startMsg;
  eventClient_->logEventThrift(
      thrift::EventCategory::TRAFFIC,
      thrift::EventId::IPERF_INFO,
      thrift::EventLevel::INFO,
      startMsg,
      startServer.value());

  auto lockedIperfAvailablePorts = iperfAvailablePorts_.rlock();
  // Find an unused port
  if (lockedIperfAvailablePorts->empty()) {
    lockedIperfAvailablePorts.unlock();
    LOG(ERROR) << "No unused ports to start iperf server";
    return;
  }
  lockedIperfAvailablePorts.unlock();

  auto availablePorts = iperfAvailablePorts_.wlock();
  auto iter = availablePorts->begin();
  int32_t serverPort = *iter;
  availablePorts->erase(iter);
  thrift::StartMinionIperf startClient = startServer.value();
  startClient.serverPort = serverPort;
  availablePorts.unlock();

  // With JSON output, nothing gets printed until iperf completes.
  if (startClient.iperfConfig.options_ref().has_value() &&
      startClient.iperfConfig.options_ref().value().json_ref().value_or(false))
  {
    // Start server after X ms, where X is either specified through
    // configuration or set to 500ms by default
    int32_t clientDelay =
      startClient.iperfConfig.options_ref().value()
          .clientDelayMs_ref().value_or(500);
    scheduleTimeout(
        std::chrono::milliseconds(clientDelay),
        [startClient, senderApp, this]() noexcept {
          sendToCtrlApp(
              senderApp,
              thrift::MessageType::START_IPERF_SERVER_RESP,
              startClient);
        });
  }

  // Wrap in a thread so we know when iperf server exits
  std::thread iperfServerThread([startClient, senderApp, this]() {
    // Choose the right iperf executable depending on configuration.
    std::string iperfExecutable = "/usr/bin/iperf3";
    if (startClient.iperfConfig.options_ref().has_value() &&
        startClient.iperfConfig.options_ref().value().useVppDataPath_ref()
            .value_or(false)) {
      iperfExecutable = "/usr/bin/iperf_wrapper.sh";
    }

    // Start iperf server
    std::vector<std::string> command = {
        iperfExecutable,
        "-s",
        "-p", std::to_string(startClient.serverPort),
        "--one-off",
        "--forceflush"
    };
    if (startClient.iperfConfig.options_ref().has_value()) {
      getIperfCliOptionsString(
          command, startClient.iperfConfig.options_ref().value(), true);
    }

    // Fork the iperf server
    std::function<void(pid_t)> pidCallback = [startClient, this](pid_t pid) {
      auto lockedIperfProcess = this->iperfProcesses_.wlock();
      lockedIperfProcess->emplace(startClient.id,pid);
      lockedIperfProcess.unlock();
    };
    std::function<void()> initialDataCallback =
        [startClient, senderApp, this]() {
          sendToCtrlApp(
              senderApp,
              thrift::MessageType::START_IPERF_SERVER_RESP,
              startClient);
        };
    std::optional<std::string> output;
    if (startClient.iperfConfig.options_ref().has_value() &&
        startClient.iperfConfig.options_ref().value().json_ref().has_value()) {
      // The notification to the controller is sent in the event loop.
      output = forkCommand(command, pidCallback);
    } else {
      // With regular text output, a header gets printed when the server starts.
      // Notify the controller when we read the first byte of this.
      output = forkCommand(command, pidCallback, initialDataCallback);
    }
    auto lockedIperfAvailablePorts = this->iperfAvailablePorts_.wlock();
    if (!output.has_value()) {
      lockedIperfAvailablePorts->insert(startClient.serverPort);
      lockedIperfAvailablePorts.unlock();
      return;
    }
    auto lockedIperfProcess = this->iperfProcesses_.rlock();
    // Log the output
    if (lockedIperfProcess->count(startClient.id)) {
      LOG(INFO) << "iperf session " << startClient.id << " finished, "
                   "sending output to controller...";
      thrift::IperfOutput iperfOutput;
      iperfOutput.output = output.value();
      iperfOutput.startIperf = startClient;
      iperfOutput.isServer = true;
      this->sendToCtrlApp(
          senderApp,
          thrift::MessageType::IPERF_OUTPUT,
          iperfOutput);
      this->eventClient_->logEvent(
          thrift::EventCategory::TRAFFIC,
          thrift::EventId::IPERF_INFO,
          thrift::EventLevel::INFO,
          folly::sformat("iperf session finished: {}", startClient.id));
    } else {
      LOG(INFO) << "iperf session " << startClient.id << " was killed";
      this->eventClient_->logEvent(
          thrift::EventCategory::TRAFFIC,
          thrift::EventId::IPERF_INFO,
          thrift::EventLevel::INFO,
          folly::sformat("iperf session was killed: {}", startClient.id));
    }
      lockedIperfProcess.unlock();
    auto lockedIperfProcessErase = this->iperfProcesses_.wlock();
    lockedIperfProcessErase->erase(startClient.id);
    lockedIperfProcessErase.unlock();
    lockedIperfAvailablePorts->insert(startClient.serverPort);
    lockedIperfAvailablePorts.unlock();
  });
  iperfServerThread.detach();
}

void
TrafficApp::processStartIperfClient(
    const std::string& senderApp, const thrift::Message& message) {
  auto startClient = maybeReadThrift<thrift::StartMinionIperf>(message);
  if (!startClient.has_value()) {
    handleInvalidMessage("StartMinionIperf", senderApp);
    return;
  }

  std::string startMsg =
    folly::sformat("Starting iperf client for session ID: {}", startClient->id);
  LOG(INFO) << startMsg;
  eventClient_->logEventThrift(
      thrift::EventCategory::TRAFFIC,
      thrift::EventId::IPERF_INFO,
      thrift::EventLevel::INFO,
      startMsg,
      startClient.value());

  // Validate IP address
  if (!startClient->iperfConfig.dstNodeIpv6_ref().has_value() ||
      !folly::IPAddress::validate(
          startClient->iperfConfig.dstNodeIpv6_ref().value())) {
    LOG(ERROR) << "Invalid IP address: "
               << startClient->iperfConfig.dstNodeIpv6_ref().value_or("");
    return;
  }

  // Wrap in a thread so we know when iperf client exits
  std::thread iperfClientThread([startClient, senderApp, this]() {
    // Select the right iperf executable depending on configuration.
    std::string iperfExecutable = "/usr/bin/iperf3";
    if (startClient->iperfConfig.options_ref().has_value() &&
        startClient->iperfConfig.options_ref().value().useVppDataPath_ref()
            .value_or(false)) {
      iperfExecutable = "/usr/bin/iperf_wrapper.sh";
    }

    // Start iperf client
    std::string addr = startClient->iperfConfig.dstNodeIpv6_ref().value();
    if (startClient->iface_ref().has_value()) {
      addr += "%" + startClient->iface_ref().value();
    }
    std::vector<std::string> command = {
        iperfExecutable,
        "-c", addr,
        "-p", std::to_string(startClient->serverPort)
    };
    if (startClient->iperfConfig.options_ref().has_value()) {
      getIperfCliOptionsString(
          command, startClient->iperfConfig.options_ref().value(), false);
    }

    // Fork the iperf client
    std::function<void(pid_t)> pidCallback = [startClient, this](pid_t pid) {
      auto lockedIperfProcess = this->iperfProcesses_.wlock();
      lockedIperfProcess->emplace(startClient->id,pid);
      lockedIperfProcess.unlock();
    };
    auto output = forkCommand(command, pidCallback);
    if (!output.has_value()) {
      return;
    }
    auto lockedIperfProcess = this->iperfProcesses_.rlock();
    // Log the output
    if (lockedIperfProcess->count(startClient->id)) {
      LOG(INFO) << "iperf session " << startClient->id << " finished, "
                   "sending output to controller...";
      thrift::IperfOutput iperfOutput;
      iperfOutput.output = output.value();
      iperfOutput.startIperf = startClient.value();
      iperfOutput.isServer = false;
      this->sendToCtrlApp(
          senderApp,
          thrift::MessageType::IPERF_OUTPUT,
          iperfOutput);
      this->eventClient_->logEvent(
          thrift::EventCategory::TRAFFIC,
          thrift::EventId::IPERF_INFO,
          thrift::EventLevel::INFO,
          folly::sformat("iperf session finished: {}", startClient->id));
    } else {
      LOG(INFO) << "iperf session " << startClient->id << " was killed";
      this->eventClient_->logEvent(
          thrift::EventCategory::TRAFFIC,
          thrift::EventId::IPERF_INFO,
          thrift::EventLevel::INFO,
          folly::sformat("iperf session was killed: {}", startClient->id));
    }
     lockedIperfProcess.unlock();

    auto lockedIperfProcessErase = this->iperfProcesses_.wlock();
    lockedIperfProcessErase->erase(startClient->id);
    lockedIperfProcessErase.unlock();
  });
  iperfClientThread.detach();
}

void
TrafficApp::processStopIperf(
    const std::string& senderApp, const thrift::Message& message) {
  auto lockedIperfProcess = this->iperfProcesses_.rlock();
  auto stopIperf = maybeReadThrift<thrift::StopIperf>(message);
  if (!stopIperf) {
    handleInvalidMessage("StopIperf", senderApp);
    return;
  }

  LOG(INFO) << "Stopping iperf process for session ID: " << stopIperf->id;

  auto iter = lockedIperfProcess->find(stopIperf->id);
  if (iter != lockedIperfProcess->end()) {
    pid_t pid = iter->second;
    // Delete this map entry first so that the iperf wrapper thread knows the
    // process terminated abnormally
    auto lockedIperfProcessErase = this->iperfProcesses_.wlock();
    lockedIperfProcessErase->erase(iter);
    // Kill the process (only SIGKILL works :/)
    kill(pid, SIGKILL);
    lockedIperfProcessErase.unlock();
  }
 lockedIperfProcess.unlock();
}

void
TrafficApp::getIperfCliOptionsString(
    std::vector<std::string>& command,
    const thrift::IperfOptions& options,
    bool isServer) {
  // ---- Shared server/client commands ----
  // seconds between periodic bandwidth reports
  if (options.intervalSec_ref().has_value()) {
    command.push_back("-i");
    command.push_back(std::to_string(options.intervalSec_ref().value()));
  }
  // more detailed output
  if (options.verbose_ref().has_value() && options.verbose_ref().value()) {
    command.push_back("-V");
  }
  // output in JSON format
  if (options.json_ref().has_value() && options.json_ref().value()) {
    command.push_back("-J");
  }
  // format to report
  if (options.format_ref().has_value()) {
    switch (options.format_ref().value()) {
      case thrift::IperfFormat::KILOBITS:
        command.push_back("-f");
        command.push_back("k");
        break;
      case thrift::IperfFormat::MEGABITS:
        command.push_back("-f");
        command.push_back("m");
        break;
      case thrift::IperfFormat::GIGABITS:
        command.push_back("-f");
        command.push_back("g");
        break;
      case thrift::IperfFormat::KILOBYTES:
        command.push_back("-f");
        command.push_back("K");
        break;
      case thrift::IperfFormat::MEGABYTES:
        command.push_back("-f");
        command.push_back("M");
        break;
      case thrift::IperfFormat::GIGABYTES:
        command.push_back("-f");
        command.push_back("G");
        break;
    }
  }

  // ---- Client-only commands ----
  if (!isServer) {
    // time in seconds to transmit for (default 10 secs)
    if (options.timeSec_ref().has_value()) {
      command.push_back("-t");
      command.push_back(std::to_string(options.timeSec_ref().value()));
    }
    // target bandwidth in bits/sec (0 for unlimited)
    // (default 1 Mbit/sec for UDP, unlimited for TCP)
    if (options.bitrate_ref().has_value()) {
      command.push_back("-b");
      command.push_back(std::to_string(options.bitrate_ref().value()));
    }
    // transport protocol (default TCP)
    if (options.protocol_ref().has_value()) {
      switch (options.protocol_ref().value()) {
        case thrift::IperfTransportProtocol::UDP:
          command.push_back("-u");
          break;
        case thrift::IperfTransportProtocol::TCP:
          break;
      }
    }
    // window size / socket buffer size
    if (options.windowSize_ref().has_value()) {
      command.push_back("-w");
      command.push_back(std::to_string(options.windowSize_ref().value()));
    }
    // TCP maximum segment size (MTU - 40 bytes)
    if (options.mss_ref().has_value()) {
      command.push_back("-M");
      command.push_back(std::to_string(options.mss_ref().value()));
    }
    // TCP disable Nagle's Algorithm
    if (options.noDelay_ref().has_value() && options.noDelay_ref().value()) {
      command.push_back("-N");
    }
    // omit the first n seconds
    if (options.omitSec_ref().has_value()) {
      command.push_back("-O");
      command.push_back(std::to_string(options.omitSec_ref().value()));
    }
    // length of buffer to read or write
    // (default 128 KB for TCP, dynamic or 1 for UDP)
    if (options.bufferLength_ref().has_value()) {
      command.push_back("-l");
      command.push_back(std::to_string(options.bufferLength_ref().value()));
    }
    // number of parallel client streams to run
    if (options.parallelStreams_ref().has_value()) {
      command.push_back("-P");
      command.push_back(std::to_string(options.parallelStreams_ref().value()));
    }
  }
}

void
TrafficApp::processStartPing(
    const std::string& senderApp, const thrift::Message& message) {
  auto startPing = maybeReadThrift<thrift::StartMinionPing>(message);
  if (!startPing) {
    handleInvalidMessage("StartMinionPing", senderApp);
    return;
  }

  std::string startMsg =
    folly::sformat("Starting ping for session ID: {}", startPing->id);
  LOG(INFO) << startMsg;
  eventClient_->logEventThrift(
      thrift::EventCategory::TRAFFIC,
      thrift::EventId::PING_INFO,
      thrift::EventLevel::INFO,
      startMsg,
      startPing.value());

  // Validate IP address
  if (!startPing->pingConfig.dstNodeIpv6_ref().has_value() ||
      !folly::IPAddress::validate(startPing->pingConfig.dstNodeIpv6_ref()
      .value())) {
    LOG(ERROR) << "Invalid IP address: "
               << startPing->pingConfig.dstNodeIpv6_ref().value_or("");
    return;
  }

  // Wrap in a thread so we know when ping exits
  std::thread pingThread([startPing, senderApp, this]() {
    // Start ping process
    std::vector<std::string> command = {"ping6"};
    getPingCliOptionsString(
        command,
        startPing->pingConfig.options_ref().has_value()
            ? startPing->pingConfig.options_ref().value()
            : thrift::PingOptions());
    std::string addr = startPing->pingConfig.dstNodeIpv6_ref().value();
    if (startPing->iface_ref().has_value()) {
      addr += "%" + startPing->iface_ref().value();
    }
    command.push_back(addr);

    // Fork the ping process
    std::function<void(pid_t)> pidCallback = [startPing,this](pid_t pid) {
      auto lockedPingProcess = this->pingProcesses_.wlock();
      lockedPingProcess->emplace(startPing->id, pid);
      lockedPingProcess.unlock();
    };
    auto output = forkCommand(command, pidCallback);
    if (!output.has_value()) {
      return;
    }

    auto lockedPingProcess = pingProcesses_.rlock();
    // Log the output
    if (lockedPingProcess->count(startPing->id)) {
      LOG(INFO) << "ping session " << startPing->id << " finished, "
                   "sending output to controller...";
      thrift::PingOutput pingOutput;
      pingOutput.output = output.value();
      pingOutput.startPing = startPing.value();
      this->sendToCtrlApp(
          senderApp,
          thrift::MessageType::PING_OUTPUT,
          pingOutput);
      this->eventClient_->logEvent(
          thrift::EventCategory::TRAFFIC,
          thrift::EventId::PING_INFO,
          thrift::EventLevel::INFO,
          folly::sformat("ping session finished: {}", startPing->id));
    } else {
      LOG(INFO) << "ping session " << startPing->id << " was killed";
      this->eventClient_->logEvent(
          thrift::EventCategory::TRAFFIC,
          thrift::EventId::PING_INFO,
          thrift::EventLevel::INFO,
          folly::sformat("ping session was killed: {}", startPing->id));
    }
     lockedPingProcess.unlock();

    auto lockedPingProcessErase = pingProcesses_.wlock();
    lockedPingProcessErase->erase(startPing->id);
    lockedPingProcessErase.unlock();
  });
  pingThread.detach();
}

void
TrafficApp::processStopPing(
    const std::string& senderApp, const thrift::Message& message) {
  auto stopPing = maybeReadThrift<thrift::StopPing>(message);
  auto lockedPingProcess = pingProcesses_.rlock();
  if (!stopPing) {
    handleInvalidMessage("StopPing", senderApp);
    return;
  }

  LOG(INFO) << "Stopping ping process for session ID: " << stopPing->id;

  auto iter = lockedPingProcess->find(stopPing->id);
  if (iter != lockedPingProcess->end()) {
    pid_t pid = iter->second;

    // Delete this map entry first so that the ping wrapper thread knows the
    // process terminated abnormally
    auto lockedPingProcessErase = pingProcesses_.wlock();
    lockedPingProcessErase->erase(iter);

    // Kill the process
    kill(pid, SIGTERM);
    lockedPingProcessErase.unlock();
  }
  lockedPingProcess.unlock();
}

void
TrafficApp::getPingCliOptionsString(
    std::vector<std::string>& command, const thrift::PingOptions& options) {
  // adaptive ping
  if (options.adaptive_ref().has_value() && options.adaptive_ref().value()) {
    command.push_back("-A");
  }
  // stop after sending count ECHO_REQUEST packets
  // don't allow infinite pings: default to "-c 10"
  command.push_back("-c");
  command.push_back(std::to_string(options.count_ref().value_or(10)));
  // print timestamp before each line
  if (options.timestamp_ref().has_value() && options.timestamp_ref().value()) {
    command.push_back("-D");
  }
  // flood ping
  if (options.flood_ref().has_value() && options.flood_ref().value()) {
    command.push_back("-f");
  }
  // wait interval seconds between sending each packet
  if (options.interval_ref().has_value()) {
    command.push_back("-i");
    command.push_back(std::to_string(options.interval_ref().value()));
  }
  // sends this many packets not waiting for a reply
  if (options.preload_ref().has_value()) {
    command.push_back("-l");
    command.push_back(std::to_string(options.preload_ref().value()));
  }
  // numeric output only
  if (options.numeric_ref().has_value() && options.numeric_ref().value()) {
    command.push_back("-n");
  }
  // report outstanding ICMP ECHO reply before sending next packet
  if (options.outstanding_ref().has_value() &&
      options.outstanding_ref().value()) {
    command.push_back("-O");
  }
  // quiet output
  if (options.quiet_ref().has_value() && options.quiet_ref().value()) {
    command.push_back("-q");
  }
  // specifies the number of data bytes to be sent
  if (options.packetSize_ref().has_value()) {
    command.push_back("-s");
    command.push_back(std::to_string(options.packetSize_ref().value()));
  }
  // set socket sndbuf
  if (options.sndbuf_ref().has_value()) {
    command.push_back("-S");
    command.push_back(std::to_string(options.sndbuf_ref().value()));
  }
  // set the IP time-to-live
  if (options.ttl_ref().has_value()) {
    command.push_back("-t");
    command.push_back(std::to_string(options.ttl_ref().value()));
  }
  // verbose output
  if (options.verbose_ref().has_value() && options.verbose_ref().value()) {
    command.push_back("-v");
  }
  // seconds before exit regardless of how many packets sent or received
  if (options.deadline_ref().has_value()) {
    command.push_back("-w");
    command.push_back(std::to_string(options.deadline_ref().value()));
  }
  // time to wait for a response, in seconds
  if (options.timeout_ref().has_value()) {
    command.push_back("-W");
    command.push_back(std::to_string(options.timeout_ref().value()));
  }
}

std::optional<std::string>
TrafficApp::forkCommand(
    const std::vector<std::string>& command,
    std::optional<std::function<void(pid_t)>> pidCallback,
    std::optional<std::function<void()>> initialDataCallback) {
  // Create a pipe (child-to-parent) to capture child process output
  int fd[2];
  if (pipe(fd) != 0) {
    LOG(ERROR) << "Failed to create pipe";
    return std::nullopt;
  }

  pid_t pid = fork();
  if (pid == 0) {  // child process
    dup2(fd[1], 1);  // redirect stdout to fd[1]
    close(fd[0]);    // close new stdin (unused)
    close(fd[1]);    // close new stdout (copied)

    // Build command vector
    std::vector<char*> argv(command.size() + 1);  // terminate with nullptr
    for (size_t i = 0; i < command.size(); i++) {
      // Casting away 'const' is necessary and OK for execvp() because it won't
      // try to modify its arguments
      argv[i] = const_cast<char*>(command[i].c_str());
    }

    LOG(INFO) << "Executing command: " << folly::join(" ", command);
    execvp(argv[0], argv.data());
    _exit(0);
  } else if (pid > 0) {  // parent process
    close(fd[1]);  // close new stdout (unused)
    if (pidCallback) {
      pidCallback.value()(pid);
    }

    // Read process output
    char buf[512];
    std::string output;
    // initially read a single byte if a callback function is provided
    size_t nbyte = initialDataCallback ? 1 : sizeof(buf);
    while (true) {
      ssize_t count = read(fd[0], buf, nbyte);
      if (count == -1) {
        // read() failed
        if (errno == EINTR) {
          continue;  // interrupted, try again
        } else {
          LOG(ERROR) << "Failed to read process output";
          break;
        }
      } else if (count == 0) {
        break;  // we're done
      } else {
        if (nbyte == 1) {
          initialDataCallback.value()();  // notify that first byte was read
          nbyte = sizeof(buf);  // start buffering reads
        }
        output.append(buf, count);  // append to output
      }
    }
    close(fd[0]);  // close new stdin (finished)

    // Wait for child to exit
    while (waitpid(pid, nullptr, 0) > 0);

    // Return process output
    return output;
  } else {
    LOG(ERROR) << "Failed to fork process";
    return std::nullopt;
  }
}

} // namespace minion
} // namespace terragraph
} // namespace facebook
