/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "LogPublisher.h"

#include <chrono>

#include <folly/ExceptionString.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/dynamic.h>
#include <folly/json.h>

#include "e2e/if/gen-cpp2/Controller_types.h"

DEFINE_string(aggregator_host, "", "The aggregator we talk to");
DEFINE_int32(aggregator_router_port, 8002, "The port aggregator listens on");
DEFINE_string(
    log_marker_suffix, ".marker.1", "LogPublisher marker file suffix");

using std::string;
using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::system_clock;

namespace facebook {
namespace terragraph {
namespace stats {

LogPublisher::LogPublisher(
    fbzmq::Context& context,
    const string& macAddr,
    const string& configFileName,
    int32_t submissionInterval,
    uint32_t bufferSize,
    const std::string& myNetworkInfoFile)
    : LogTailer(
          macAddr,
          configFileName,
          FLAGS_log_marker_suffix,
          submissionInterval,
          bufferSize),
      aggregatorSock_(
          context,
          // Note that stats_agent is using macAddr as Socket ID.
          fbzmq::IdentityString(folly::sformat("{}-logpublisher", macAddr))),
      myNetworkInfoFile_{myNetworkInfoFile} {

  syslogsBuffer_.reserve(bufferSize);

  macHashValue_ = std::hash<string>{}(macAddr);

  //
  // Overwrite default TCP_KEEPALIVE options to handle aggregator crash and
  // drop dead socket after 30 secs
  //

  const int keepAlive = 1;
  const int keepAliveIdle = 15;
  const int keepAliveCnt = 3;
  const int keepAliveIntvl = 5;
  aggregatorSock_
      .setKeepAlive(keepAlive, keepAliveIdle, keepAliveCnt, keepAliveIntvl)
      .value();

  // Allow a max of 2 messages to get buffered
  const int sndHwm = 2;
  aggregatorSock_.setSockOpt(ZMQ_SNDHWM, &sndHwm, sizeof(int)).value();

  const int sndTimeout = 0;
  aggregatorSock_.setSockOpt(ZMQ_SNDTIMEO, &sndTimeout, sizeof(int)).value();

  std::optional<string> url = FLAGS_aggregator_host.empty()
                                    ? getAggregatorUrl()
                                    : folly::sformat(
                                          "tcp://{}:{}",
                                          FLAGS_aggregator_host,
                                          FLAGS_aggregator_router_port);

  if (url) {
    connectToAggregator(*url);
  }
}

std::optional<string>
LogPublisher::getAggregatorUrl() noexcept {
  string contents;
  if (!folly::readFile(myNetworkInfoFile_.c_str(), contents)) {
    LOG(ERROR) << "Could not read NetworkInfo file";
    return std::nullopt;
  }

  thrift::NetworkInfo myNetworkInfo;
  auto jsonSerializer = apache::thrift::SimpleJSONSerializer();
  try {
    jsonSerializer.deserialize(contents, myNetworkInfo);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not parse NetworkInfo struct "
               << folly::exceptionStr(ex);
    return std::nullopt;
  }

  // Pick one Aggregator URL from pool
  auto aggrCount = myNetworkInfo.aggrUrl.size();
  if (aggrCount > 0) {
    auto aggrIndex = macHashValue_ % aggrCount;
    return myNetworkInfo.aggrUrl[aggrIndex];
  }
  return std::nullopt;
}

void
LogPublisher::connectToAggregator(const string& aggrUrl) {
  if (aggrUrl.empty()) {
    return;
  }

  // disconnect previous connection if necessary
  if (!aggregatorRouterUrl_.empty()) {
    LOG(INFO) << folly::format(
        "Disconnecting from aggregator on url '{}'", aggregatorRouterUrl_);
    if (aggregatorSock_.disconnect(fbzmq::SocketUrl{aggregatorRouterUrl_})) {
      aggregatorConnected_ = false;
    } else {
      LOG(ERROR)
          << folly::format("Error disconnecting '{}'", aggregatorRouterUrl_);
      return;
    }
  }

  LOG(INFO) << folly::format("Connecting to aggregator on url '{}'", aggrUrl);
  if (aggregatorSock_.connect(fbzmq::SocketUrl{aggrUrl})) {
    aggregatorRouterUrl_ = aggrUrl;
    aggregatorConnected_ = true;
  } else {
    LOG(ERROR) << folly::format("Error connecting to '{}'", aggrUrl);
  }
}

void
LogPublisher::checkAggregatorUrl() noexcept {
  if (!FLAGS_aggregator_host.empty()) {
    return;  // started with a fixed URL
  }

  auto url = getAggregatorUrl();
  if (url && url != aggregatorRouterUrl_) {
    connectToAggregator(*url);
  }
}

void
LogPublisher::fillSyslogBuffer() {
  for (auto& agentIt : tailAgents_) {
    if (syslogsBuffer_.size() >= bufferSize_) {
      return;
    }

    auto timestamp = (int64_t)duration_cast<microseconds>(
                         system_clock::now().time_since_epoch())
                         .count();

    uint32_t bufferCapacity = bufferSize_ - syslogsBuffer_.size();
    agentIt.second.fetchLogLines(logsBuffer_, bufferCapacity);

    for (const auto& line : logsBuffer_) {
      thrift::AggrSyslog syslog{};
      syslog.timestamp = timestamp++;
      syslog.index = agentIt.first;
      syslog.log = line;
      syslogsBuffer_.push_back(syslog);
    }

    logsBuffer_.clear();
  }
}

void
LogPublisher::flushSyslogs() {
  thrift::AggrSyslogReport report{};
  report.macAddr = macAddr_;
  report.syslogs = syslogsBuffer_;
  if (sendToAggregator(
          NMSConsts::kStatsAppAggrId,
          thrift::AggrMessageType::SYSLOG_REPORT,
          report,
          true /* compress */)) {
    LOG(INFO) << "Submitted " << syslogsBuffer_.size()
              << " syslog entries to Aggregator";
    syslogsBuffer_.clear();
  }
}

void
LogPublisher::monitor() noexcept {
  checkAggregatorUrl();
  // Hold off if we did not get our first connection to Aggregator or
  // the previously read logs did not make it to the socket buffer
  if (aggregatorConnected_ && syslogsBuffer_.empty()) {
    fillSyslogBuffer();
  }
  if (!syslogsBuffer_.empty()) {
    flushSyslogs();
  }
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
