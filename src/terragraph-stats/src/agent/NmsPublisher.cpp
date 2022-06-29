/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "NmsPublisher.h"

#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/MapUtil.h>
#include <folly/io/async/EventBaseManager.h>
#include <future>
#include <gflags/gflags.h>

#include "e2e/common/Consts.h"
#include "e2e/common/JsonUtils.h"
#include "stats/common/StatInfo.h"

DEFINE_string(event_cache_file, "/tmp/events.json", "Event cache file");

using namespace fbzmq;

namespace {
// Default keepAlive values
// We intend to garbage collect connections after 30 seconds of inactivity
const int kKeepAliveEnable{1};
// Idle Time before sending keep alives
const std::chrono::seconds kKeepAliveTime{15};
// max keep alives before resetting connection
const int kKeepAliveCnt{3};
// interval between keep alives
const std::chrono::seconds kKeepAliveIntvl{5};
// microseconds per seconds
const int kUsPerS{1000000};
} // namespace

namespace facebook {
namespace terragraph {
namespace stats {

NmsPublisher::NmsPublisher(
    fbzmq::Context& context,
    const std::string& macAddr,
    const std::string& aggregatorRouterUrl,
    const std::string& myNetworkInfoFile,
    const thrift::StatsAgentParams& statsAgentParams)
    : BasePublisher(context, macAddr, "NmsPublisher", statsAgentParams),
      fixedAggregatorRouterUrl_(aggregatorRouterUrl),
      nmsDefaultReportInterval_(std::chrono::seconds(
          statsAgentParams.publisherParams.defaultStatsInterval)),
      nmsHighFrequencyReportInterval_(std::chrono::seconds(
          statsAgentParams.publisherParams.highFrequencyStatsInterval)),
      myNetworkInfoFile_{myNetworkInfoFile},
      zmqSndHwm_(
          statsAgentParams.endpointParams.nmsPublisherParams_ref()
              .value().zmqSndHwm)
  {
  // Load cached events
  loadEvents();

  // Hash our mac address (for sharding, if multiple aggregators are running)
  macHashValue_ = std::hash<std::string>{}(macAddr);

  // Create timers for periodically pushing stats/events to an aggregator
  const bool makePeriodic = true;
  nmsDefaultReportTimer_ =
      ZmqTimeout::make(this, [&]() noexcept { pushNmsReport(); });
  nmsDefaultReportTimer_->scheduleTimeout(
      nmsDefaultReportInterval_, makePeriodic);
  nmsHighFrequencyReportTimer_ =
      ZmqTimeout::make(this, [&]() noexcept { pushHighFrequencyStats(); });
  nmsHighFrequencyReportTimer_->scheduleTimeout(
      nmsHighFrequencyReportInterval_, makePeriodic);

  // Prepare dealer socket to talk to the aggregator
  initAggregatorSock();

  // Connect to an aggregator
  std::optional<std::string> url =
      aggregatorRouterUrl.empty() ? getAggrUrl() : aggregatorRouterUrl;
  if (url) {
    connectToAggregator(*url);
  }
}

void
NmsPublisher::initAggregatorSock() {
  aggregatorSock_ = fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>(
      context_,
      fbzmq::IdentityString{macAddr_},
      folly::none,  // Crypto keypair
      fbzmq::NonblockingFlag{true});

  //
  // Overwrite default TCP_KEEPALIVE options to handle aggregator crash and
  // drop dead socket after 30 secs
  //

  if (aggregatorSock_
          .setKeepAlive(
              kKeepAliveEnable,
              kKeepAliveTime.count(),
              kKeepAliveCnt,
              kKeepAliveIntvl.count())
          .hasError()) {
    LOG(FATAL) << "Could not set zmq keepAlive options.";
  }

  // tag all management messages as DSCP to differenciate with data traffic
  if (aggregatorSock_.setSockOpt(ZMQ_TOS, &NMSConsts::kIpTos, sizeof(int))
          .hasError()) {
    LOG(FATAL) << "Could not set ZMQ_TOS.";
  }

  // Limit the maximum number of messages buffered by ZMQ socket
  if (aggregatorSock_.setSockOpt(ZMQ_SNDHWM, &zmqSndHwm_, sizeof(int))
          .hasError()) {
    LOG(FATAL) << "Could not set ZMQ_SNDHWM.";
  }
}

std::optional<std::string>
NmsPublisher::getAggrUrl() noexcept {
  std::string contents;
  if (!folly::readFile(myNetworkInfoFile_.c_str(), contents)) {
    LOG(ERROR) << "Could not read NetworkInfo file";
    return std::nullopt;
  }

  std::optional<thrift::NetworkInfo> myNetworkInfo =
      JsonUtils::deserializeFromJson<thrift::NetworkInfo>(contents);
  if (!myNetworkInfo) {
    LOG(ERROR) << "Could not parse NetworkInfo struct";
    return std::nullopt;
  }

  // Pick one Aggregator URL from pool
  auto aggrCount = myNetworkInfo->aggrUrl.size();
  if (aggrCount > 0) {
    auto aggrIndex = macHashValue_ % aggrCount;
    return myNetworkInfo->aggrUrl[aggrIndex];
  }
  return std::nullopt;
}

void
NmsPublisher::processCountersMessage(
    const fbzmq::thrift::CounterValuesResponse& counters) noexcept {
  int64_t ts = std::time(nullptr);

  // Create datapoints from counters, add to queue
  int bufferFullDroppedKeys = 0;
  for (const auto& kv : counters.counters_ref().value()) {
    const StatInfo info(kv.first);

    // Check if blacklisted
    if (isBlacklisted(info.key)) {
      VLOG(4) << "Dropped blacklisted counter '" << info.key << "'";
      continue;
    }

    // Check max queue size
    size_t totalQueueSize = statsQueuesLF_.size() + statsQueuesHF_.size();
    if (totalQueueSize >= statsBufferSize_ && !statsQueuesLF_.count(kv.first)) {
      bufferFullDroppedKeys++;
      continue;
    }

    // Check if whitelisted as a high frequency stat
    bool isHighFreqKey = isWhitelisted(info.key);

    // Create stat key
    // 'key' and 'entity' will get filled before sending to aggregator
    thrift::AggrStat stat;
    // Normalize the reported timestamp
    stat.timestamp =
        getUnixUsTimestamp(kv.second.timestamp_ref().value(), ts) / kUsPerS;
    stat.isCounter =
        kv.second.valueType_ref() == fbzmq::thrift::CounterValueType::COUNTER;
    stat.value = kv.second.value_ref().value();

    // Update value in queues
    statsQueuesLF_[kv.first] = stat;
    if (isHighFreqKey) {
      statsQueuesHF_[kv.first] = stat;
    }
  }

  if (bufferFullDroppedKeys > 0) {
    LOG(WARNING) << "Stats buffer full (" << statsBufferSize_
                 << " keys), dropped " << bufferFullDroppedKeys << " new keys!";
  }
}

void
NmsPublisher::processEventLogMessage(
    const fbzmq::thrift::EventLog& eventLog) noexcept {
  // Only handle TG events
  if (eventLog.category_ref().value() != E2EConsts::kEventCategory) {
    return;
  }

  auto maybeEvent = parseTerragraphEventLog(eventLog);
  if (maybeEvent) {
    eventLog_.events.push_back(JsonUtils::serializeToJson(maybeEvent.value()));
  }
}

void
NmsPublisher::connectToAggregator(const std::string& aggrUrl) {
  if (aggrUrl.empty()) {
    return;
  }

  // disconnect previous connection if necessary
  if (!aggregatorRouterUrl_.empty()) {
    LOG(INFO) << "Disconnecting from aggregator on url '"
              << aggregatorRouterUrl_ << "'";

    // `disconnect()` causes runtime errors when aggregatorSock_ is connected to
    // an incompatible socket. To avoid this, use `close()` to destroy and
    // reinitialize the socket. The only drawbacks are that the socket has to be
    // reinitialized.

    aggregatorSock_.close();  // Destroy socket
    aggrConnected_ = false;
    initAggregatorSock();  // Reinitialize a new aggregatorSock_
  }

  LOG(INFO) << "Connecting to aggregator on url '" << aggrUrl << "'";
  auto res = aggregatorSock_.connect(fbzmq::SocketUrl{aggrUrl});
  if (res) {
    aggregatorRouterUrl_ = aggrUrl;
    aggrConnected_ = true;
  } else {
    LOG(ERROR) << "Error connecting to '" << aggrUrl << "'"
               << ". " << res.error();
  }
}

void
NmsPublisher::pushQueuedStatsAndEvents(
    const thrift::AggrMessageType& messageType,
    NmsPublisher::StatsMap& statsQueue,
    thrift::EventLog& eventLog) {
  // Construct the stats report
  thrift::AggrStatsReport statsReport;
  statsReport.stats.reserve(statsQueue.size());
  for (const auto& kv : statsQueue) {
    const StatInfo info(kv.first);
    thrift::AggrStat stat = kv.second;

    // Fill in empty 'key' and 'entity'
    stat.key = info.key;
    if (!info.entity.empty()) {
      stat.entity_ref() = info.entity;
    }
    statsReport.stats.push_back(stat);
  }
  statsReport.events = eventLog.events;

  // Do not call sendToAggregator() for empty high-frequency report
  if (messageType == thrift::AggrMessageType::HIGH_FREQUENCY_STATS_REPORT &&
      statsReport.stats.empty() && statsReport.events.empty()) {
    return;
  }

  // Send report to aggregator
  auto success = sendToAggregator(
      NMSConsts::kStatsAppAggrId,
      messageType,
      statsReport,
      true /* compress */);
  if (success) {
    LOG(INFO) << "Submitted " << statsReport.stats.size() << " stats"
              << " and " << statsReport.events.size() << " events"
              << " to Aggregator";
    statsQueue.clear();
    eventLog.events.clear();
  } else {
    LOG(ERROR) << "Failed to send stats report to Aggregator containing "
               << statsReport.stats.size() << " stats, "
               << statsReport.events.size() << " events";
  }
}

void
NmsPublisher::pushNmsReport() noexcept {
  checkAggregatorUrl();

  if (aggrConnected_) {
    // Push default-frequency stats and events
    pushQueuedStatsAndEvents(
        thrift::AggrMessageType::STATS_REPORT,
        statsQueuesLF_,
        eventLog_);
  } else {
    VLOG(1) << "Aggregator not connected. Current queue sizes: "
            << statsQueuesLF_.size() << " stats, "
            << eventLog_.events.size() << " events";

    // Limit size of event queue (remove the oldest elements)
    if (eventLog_.events.size() > eventsBufferSize_) {
      VLOG(1) << "Dropping " << (eventLog_.events.size() - eventsBufferSize_)
              << " events over capacity (" << eventsBufferSize_ << ")";
      eventLog_.events.erase(
          eventLog_.events.begin(),
          eventLog_.events.begin() +
              (eventLog_.events.size() - eventsBufferSize_));
    }
  }
}

void
NmsPublisher::pushHighFrequencyStats() noexcept {
  checkAggregatorUrl();

  if (aggrConnected_) {
    // Push high-frequency stats
    thrift::EventLog eventLog;
    pushQueuedStatsAndEvents(
        thrift::AggrMessageType::HIGH_FREQUENCY_STATS_REPORT,
        statsQueuesHF_,
        eventLog);
  } else {
    VLOG(4) << "Aggregator not connected. "
               "Current high-frequency stats queue size: "
            << statsQueuesHF_.size();
  }
}

void
NmsPublisher::checkAggregatorUrl() noexcept {
  if (!fixedAggregatorRouterUrl_.empty()) {
    return;  // started with a fixed URL
  }

  auto url = getAggrUrl();
  if (url && url != aggregatorRouterUrl_) {
    connectToAggregator(*url);
  }
}

void
NmsPublisher::cacheEvents() {
  if (eventLog_.events.empty()) {
    VLOG(2) << "No events to cache";
    return;
  }
  std::string contents = JsonUtils::serializeToJson(eventLog_);
  try {
    folly::writeFileAtomic(FLAGS_event_cache_file, contents);
  } catch (const std::exception&) {
    LOG(ERROR) << "Could not write events to " << FLAGS_event_cache_file;
  }
}

void
NmsPublisher::loadEvents() {
  try {
    std::string contents;
    auto jsonSerializer = apache::thrift::SimpleJSONSerializer();
    if (folly::readFile(FLAGS_event_cache_file.c_str(), contents)) {
      jsonSerializer.deserialize(contents, eventLog_);

      // Delete file
      std::remove(FLAGS_event_cache_file.c_str());
    }
  } catch (const std::exception&) {
    LOG(ERROR) << "Could not read events from " << FLAGS_event_cache_file;
  }

  if (eventLog_.events.empty()) {
    VLOG(2) << "No events found in cache";
    return;
  }
  VLOG(2) << "Loaded " << eventLog_.events.size() << " event(s) from cache...";
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
