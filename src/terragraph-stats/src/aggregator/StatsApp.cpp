/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "StatsApp.h"

#include <future>

#include <curl/curl.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/IPAddress.h>
#include <folly/MacAddress.h>
#include <folly/MapUtil.h>
#include <folly/json.h>
#include <thrift/lib/cpp/protocol/TProtocolTypes.h>

#include "SharedObjects.h"
#include "../common/Consts.h"
#include "e2e/common/JsonUtils.h"

using namespace fbzmq;
using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::system_clock;
using apache::thrift::CompactSerializer;
using apache::thrift::detail::TEnumMapFactory;
using std::map;
using std::string;
using std::invalid_argument;

DEFINE_int32(topology_fetch_interval_s, 15, "Topology fetching interval");
DEFINE_int32(data_publish_interval_s, 30, "Stats publish interval");
DEFINE_int32(
    high_frequency_data_publish_interval_s,
    1,
    "High-frequency stats publish interval");

DEFINE_string(
    zmq_topology_app_identity,
    "ctrl-app-TOPOLOGY_APP",
    "ZMQ socket identity of TopologyApp on the controller");
DEFINE_string(
    zmq_sender_app_identity,
    "NMS_AGGREGATOR",
    "ZMQ socket identity used when fetching the topology from the controller");
DEFINE_int32(
    zmq_read_timeout_ms,
    2000,
    "ZMQ read timeout in milliseconds for fetching the topology from the "
    "controller");
DEFINE_int32(curl_timeout_s, 10, "cURL timeout for the entire request");
DEFINE_string(proxy_url, "", "IPv6 to IPv4 proxy URL (ex. 'http://[2001::1]')");
DEFINE_int32(proxy_port, 8080, "IPv6 to IPv4 proxy port");

extern "C" {
struct HTTPDataStruct {
  char* data;
  size_t size;
};

static size_t
curlWriteCb(void* content, size_t size, size_t nmemb, void* userp) {
  size_t realSize = size * nmemb;
  struct HTTPDataStruct* httpData = (struct HTTPDataStruct*)userp;
  httpData->data =
      (char*)realloc(httpData->data, httpData->size + realSize + 1);
  if (httpData->data == nullptr) {
    printf("Unable to allocate memory (realloc failed)\n");
    return 0;
  }
  memcpy(&(httpData->data[httpData->size]), content, realSize);
  httpData->size += realSize;
  httpData->data[httpData->size] = 0;
  return realSize;
}
}

namespace facebook {
namespace terragraph {
namespace stats {

StatsApp::StatsApp(
    fbzmq::Context& context,
    const string& routerSockUrl,
    const string& controllerSockUrl)
    : AggrApp(context, routerSockUrl, NMSConsts::kStatsAppAggrId),
      context_(context),
      controllerSockUrl_(controllerSockUrl) {
  const bool makePeriodic = true;
  std::chrono::seconds periodicFetchInterval{FLAGS_topology_fetch_interval_s};
  std::chrono::seconds periodicPublishInterval{FLAGS_data_publish_interval_s};
  std::chrono::seconds periodicHighFrequencyPublishInterval{
      FLAGS_high_frequency_data_publish_interval_s};

  // Fetch data endpoints from aggregator config
  dataEndpoints_ = SharedObjects::getAggrConfigWrapper()->rlock()
      ->getConfig()->dataEndpoints;

  // Create periodic call to fetch data timer
  periodicTopologyFetchTimer_ =
      ZmqTimeout::make(this, [this]() noexcept { periodicTopologyFetch(); });
  periodicTopologyFetchTimer_->scheduleTimeout(
      periodicFetchInterval, makePeriodic);
  periodicTopologyFetch();  // fetch right away

  // Create periodic calls to publish data to endpoints
  periodicDataPublishTimer_ =
      ZmqTimeout::make(this, [this]() noexcept { periodicDataPublish(); });
  periodicDataPublishTimer_->scheduleTimeout(
      periodicPublishInterval, makePeriodic);
  periodicHighFrequencyDataPublishTimer_ =
      ZmqTimeout::make(this, [this]() noexcept {
        periodicHighFrequencyDataPublish();
      });
  periodicHighFrequencyDataPublishTimer_->scheduleTimeout(
      periodicHighFrequencyPublishInterval, makePeriodic);
}

StatsApp::~StatsApp() {
  stopPublisherThread(dataPublisherThread_, dataPublisherStop_);
  stopPublisherThread(hfDataPublisherThread_, hfDataPublisherStop_);
}

void
StatsApp::processTopology() {
  for (const auto& node : topology_.nodes) {
    try {
      string macAddr = folly::MacAddress(node.mac_addr).toString();
      nodeMacToName_[macAddr] = node.name;
      nodeMacToSite_[macAddr] = node.site_name;
      nodeNameToMac_[node.name] = macAddr;
    } catch (std::invalid_argument& ex) {
      LOG(ERROR) << "Invalid MAC address: " << node.mac_addr << " for node "
                 << node.name;
    }
  }
}

thrift::Topology
StatsApp::controllerFetchTopology(const std::string& controllerAddr) {
  fbzmq::Socket<ZMQ_DEALER, ZMQ_CLIENT> zmqSocket(
      context_, fbzmq::IdentityString{FLAGS_zmq_sender_app_identity});

  VLOG(3) << "Connecting to controller: " << controllerAddr;
  zmqSocket.connect(fbzmq::SocketUrl{controllerAddr}).value();

  thrift::Message msg;
  msg.mType = thrift::MessageType::GET_TOPOLOGY;
  terragraph::thrift::GetTopology req;
  msg.value = fbzmq::util::writeThriftObjStr(req, serializer_);

  // request topology
  zmqSocket
      .sendMultiple(
          fbzmq::Message::from(std::string("")).value(),
          fbzmq::Message::from(std::string(FLAGS_zmq_topology_app_identity))
              .value(),
          fbzmq::Message::from(std::string(FLAGS_zmq_sender_app_identity))
              .value(),
          fbzmq::Message::fromThriftObj(msg, serializer_).value())
      .value();

  Message firstFrame, senderApp /* sender app */, data /* thrift msg */;
  zmqSocket
      .recvMultipleTimeout(
          std::chrono::milliseconds(FLAGS_zmq_read_timeout_ms),
          firstFrame,
          senderApp,
          data)
      .value();

  // try receiving a thrift::Message
  auto tMsg = data.readThriftObj<thrift::Message>(serializer_).value();

  // verify response type is expected
  if (tMsg.mType == thrift::MessageType::TOPOLOGY) {
    auto tResp = fbzmq::util::readThriftObjStr<thrift::Topology>(
        tMsg.value, serializer_);
    VLOG(2) << "Got topology from controller for: '" << tResp.name << "'";
    return tResp;
  }

  // request/response error
  throw std::runtime_error(folly::sformat(
      "Unexpected response: {}",
      folly::get_default(
          TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
          tMsg.mType, "UNKNOWN")));
}

void
StatsApp::processMessage(
    const string& agent,
    const string& senderApp,
    const thrift::AggrMessage& message) noexcept {
  switch (message.mType) {
    case thrift::AggrMessageType::STATS_REPORT:
      processStatsReport(agent, senderApp, message);
      break;
    case thrift::AggrMessageType::HIGH_FREQUENCY_STATS_REPORT:
      processHighFrequencyStatsReport(agent, senderApp, message);
      break;
    case thrift::AggrMessageType::SYSLOG_REPORT:
      processSyslogReport(agent, senderApp, message);
      break;
    case thrift::AggrMessageType::GET_TOPOLOGY:
      processGetTopology(senderApp);
      break;
    default:
      LOG(ERROR) << "Wrong type of message ("
                 << folly::get_default(
                        TEnumMapFactory<thrift::AggrMessageType>::
                            makeValuesToNamesMap(),
                        message.mType,
                        "UNKNOWN")
                 << ") received from " << agent << ":" << senderApp;
  }
}

void
StatsApp::processGetTopology(const string& senderApp) {
  VLOG(2) << "Request to get topology from " << senderApp;
  sendToAggrApp(
      senderApp,
      thrift::AggrMessageType::TOPOLOGY,
      topology_);
}

void
StatsApp::processStatsReport(
    const string& agent,
    const string& senderApp,
    const thrift::AggrMessage& message) {
  auto statsReport = maybeReadThrift<thrift::AggrStatsReport>(message);
  if (!statsReport) {
    handleInvalidMessage("AggrStatsReport", senderApp, agent, false);
    return;
  }

  VLOG(2) << "Stats Report received from " << agent << ":" << senderApp
          << " with " << statsReport->stats.size() << " stats and "
          << statsReport->events.size() << " events";

  handleStatsReport(agent, statsReport.value());
}

void
StatsApp::processHighFrequencyStatsReport(
    const string& agent,
    const string& senderApp,
    const thrift::AggrMessage& message) {
  auto statsReport = maybeReadThrift<thrift::AggrStatsReport>(message);
  if (!statsReport) {
    handleInvalidMessage("AggrStatsReport", senderApp, agent, false);
    return;
  }

  VLOG(2) << "High-Frequency Stats Report received from " << agent << ":"
          << senderApp << " with " << statsReport->stats.size() << " stats and "
          << statsReport->events.size() << " events";

  // Put these stats in both the normal and high-frequency queues, since they
  // get processed separately at different intervals
  handleStatsReport(agent, statsReport.value());
  auto& highFrequencyStatsQueue = highFrequencyStatsQueues_[agent];
  for (const auto& stat : statsReport->stats) {
    // Use the entity if given (e.g. a specific baseband MAC)
    if (stat.entity_ref().has_value()) {
      highFrequencyStatsQueues_[stat.entity_ref().value()][stat.key] = stat;
    } else {
      highFrequencyStatsQueue[stat.key] = stat;
    }
  }
}

void
StatsApp::handleStatsReport(
    const string& agent, const thrift::AggrStatsReport& statsReport) {
  // Add stats to agent's queue
  // We only care about the latest stat for each key during every interval
  // (data_publish_interval_s), so overwrite any previous stats
  auto& statsQueue = statsQueues_[agent];
  for (const auto& stat : statsReport.stats) {
    // Use the entity if given (e.g. a specific baseband MAC)
    if (stat.entity_ref().has_value()) {
      statsQueues_[stat.entity_ref().value()][stat.key] = stat;
    } else {
      statsQueue[stat.key] = stat;
    }
  }

  // Add events to agent's queue
  for (const std::string& eventStr : statsReport.events) {
    auto event = JsonUtils::deserializeFromJson<thrift::Event>(eventStr);
    if (!event) {
      LOG(ERROR) << "Failed to deserialize event: " << eventStr;
      continue;
    }

    // Use the nodeId if given (should always be present)
    const std::string& nodeId = event->nodeId_ref().value_or(agent);
    eventsQueues_[nodeId].push_back(event.value());
  }
}

void
StatsApp::processSyslogReport(
    const string& agent,
    const string& senderApp,
    const thrift::AggrMessage& message) {
  VLOG(2) << "Syslog Report received from " << agent << ":" << senderApp;
  auto syslogReport = maybeReadThrift<thrift::AggrSyslogReport>(message);
  if (!syslogReport) {
    handleInvalidMessage("AggrSyslogReport", senderApp, agent, false);
    return;
  }

  string macAddr = folly::MacAddress(syslogReport->macAddr).toString();

  // Process Syslogs
  if (!syslogReport->syslogs.empty()) {
    folly::dynamic logMsgs = folly::dynamic::array;
    for (const auto& syslog : syslogReport->syslogs) {
      logMsgs.push_back(folly::dynamic::object("ts", syslog.timestamp)(
          "file", syslog.index)("log", syslog.log));
    }
    if (logMsgs.empty()) {
      return;
    }

    auto nodeNameIt = nodeMacToName_.find(macAddr);
    auto siteIt = nodeMacToSite_.find(macAddr);
    if (nodeNameIt != nodeMacToName_.end() && siteIt != nodeMacToSite_.end()) {
      // format json for the remote logs writer
      // push agent logs into queue
      sysLogsQueue_.push_back(
          folly::dynamic::object("mac", macAddr)("name", nodeNameIt->second)(
              "site", siteIt->second)("logs", logMsgs));
    }
  }
}

void
StatsApp::pushCurlPostRequest(
    const std::vector<std::string>& endpoints,
    const std::string& postData,
    const bool useProxy,
    const bool jsonType) {
  // start a new thread for the curl request to each endpoint so we don't block
  for (const auto& endpoint : endpoints) {
    std::thread asyncCurlThread([=]() {
      bool success = false;
      try {
        curl_slist* slist = nullptr;
        CURL* curl;
        CURLcode res;
        curl = curl_easy_init();
        if (!curl) {
          throw std::runtime_error("Unable to initialize CURL");
        }

        // add form parameters
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
        // we can't verify the peer with our current image/lack of certs
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, postData.length());
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, FLAGS_curl_timeout_s);

        if (jsonType != false) {
          slist = curl_slist_append(slist, "Content-type: application/json");
          curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
        }

        if (useProxy != false) {
          if (FLAGS_proxy_url.empty()) {
            LOG(WARNING) << "Not enabling CURL proxy (proxy_url is empty!)";
          } else {
            curl_easy_setopt(curl, CURLOPT_PROXY, FLAGS_proxy_url.c_str());
            curl_easy_setopt(curl, CURLOPT_PROXYPORT, FLAGS_proxy_port);
          }
        }

        // read data from request
        struct HTTPDataStruct dataChunk;
        dataChunk.data = (char*)malloc(1);
        dataChunk.size = 0;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curlWriteCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&dataChunk);
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
          long response_code;
          curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
          // response code 204 is a success
          if (response_code >= 200L || response_code < 300L) {
            success = true;
          }
        }
        // cleanup
        if (slist != nullptr) {
          curl_slist_free_all(slist);
        }
        curl_easy_cleanup(curl);
        free(dataChunk.data);
        if (res != CURLE_OK) {
          LOG(WARNING) << "CURL error for endpoint " << endpoint << ": "
                       << curl_easy_strerror(res);
          return false;
        }

        if (success) {
          LOG(INFO) << "Submitted data points to " << endpoint;
        } else {
          LOG(WARNING) << "Failed submitting data points to " << endpoint;
        }
      } catch (const std::runtime_error& ex) {
        LOG(ERROR) << ex.what();
      }
      return success;
    });
    asyncCurlThread.detach();
  }
}

void
StatsApp::periodicTopologyFetch() {
  try {
    topology_ = controllerFetchTopology(controllerSockUrl_);
    processTopology();
  } catch (const std::exception& ex) {
    LOG(WARNING) << "Unable to get topology from controller at '"
                 << controllerSockUrl_ << "': " << ex.what();
  }
}

void
StatsApp::pushQueuedStats(
    const StatsApp::StatsQueues& statsQueues,
    StatsApp::StatsQueues& prevStatsQueues,
    int interval,
    std::atomic_bool& publisherStop,
    const std::vector<std::string> endpoints) {
  folly::dynamic dataPointsQueue = folly::dynamic::array;

  LOG(INFO) << "Processing queued stats from " << statsQueues.size()
            << " agents...";

  // Process stats from each agent
  for (const auto& kv : statsQueues) {
    std::string agent = kv.first;
    auto& statsQueue = kv.second;

    LOG(INFO) << "Processing " << statsQueue.size() << " stats from " << agent;

    // If we've taken too long (overlapped with next publish interval),
    // drop the rest of the stats
    if (publisherStop) {
      LOG(ERROR) << "Dropping " << statsQueue.size() << " queued stats from "
                 << agent << " (processing time limit exceeded)";
      continue;
    }

    // Calculate rate using the previous (processed) sample for this agent
    auto& prevStatsQueue = prevStatsQueues[agent];

    folly::dynamic statsMsgs = folly::dynamic::array;
    for (const auto& currStatKv : statsQueue) {
      double value = currStatKv.second.value;

      // Calculate rate for counters
      if (currStatKv.second.isCounter) {
        // Skip processing this iteration if no data
        auto prevCounter = prevStatsQueue.find(currStatKv.first);
        if (prevCounter == prevStatsQueue.end()) {
          continue;
        }

        double prevValue = prevCounter->second.value;
        int64_t prevTime = prevCounter->second.timestamp;
        double curValue = currStatKv.second.value;
        int64_t curTime = currStatKv.second.timestamp;

        // Only compute a rate if the current data point is newer
        if (curTime <= prevTime) {
          continue;
        }

        // Compute rate if the value changed
        value = 0;
        if (curValue > prevValue) {
          value = (curValue - prevValue) /
                  ((double)(curTime - prevTime) / 1000 /* ms */);
        }
      }

      // Push computed data point
      folly::dynamic stat = folly::dynamic::object("key", currStatKv.first)(
          "ts", currStatKv.second.timestamp)("value", value);
      statsMsgs.push_back(stat);
    }
    prevStatsQueue = statsQueue;
    if (statsMsgs.empty()) {
      continue;
    }

    // Add stats to queues
    auto nodeNameIt = nodeMacToName_.find(agent);
    auto siteIt = nodeMacToSite_.find(agent);
    if (nodeNameIt != nodeMacToName_.end() && siteIt != nodeMacToSite_.end()) {
      // Push stats from a known node
      dataPointsQueue.push_back(folly::dynamic::object(
          "mac", agent)("name", nodeNameIt->second)("site", siteIt->second)(
          "stats", statsMsgs));
    } else {
      // Push stats from another source (e.g. controller)
      dataPointsQueue.push_back(folly::dynamic::object(
          "mac", agent)("name", "")("site", "")("stats", statsMsgs));
    }
  }

  // Push the processed stats
  if (!dataPointsQueue.empty()) {
    folly::dynamic statsWriterRequest = folly::dynamic::object(
        "topology", folly::dynamic::object("name", topology_.name))(
        "agents", dataPointsQueue)("interval", interval);

    string postString = folly::toJson(statsWriterRequest);
    pushCurlPostRequest(endpoints, postString);
  }
}

void
StatsApp::pushQueuedSysLogs(const StatsApp::SysLogsQueue& sysLogsQueue) {
  folly::dynamic sysLogWriterRequest = folly::dynamic::object(
      "topology",
      folly::dynamic::object("name", topology_.name))("agents", sysLogsQueue);

  string postString = folly::toJson(sysLogWriterRequest);
  std::vector<std::string> endpointUrls;
  for (const auto& endpoint : dataEndpoints_) {
    if (endpoint.second.logsEnabled) {
      endpointUrls.push_back(
          endpoint.second.host + endpoint.second.logsWriterSuffix);
    }
  }
  pushCurlPostRequest(endpointUrls, postString);
}

void
StatsApp::pushQueuedEvents(const StatsApp::EventsQueues& eventsQueues) {
  folly::dynamic dataPointsQueue = folly::dynamic::array;

  LOG(INFO) << "Processing queued events from " << eventsQueues.size()
            << " agents...";

  for (const auto& kv : eventsQueues) {
    std::string agent = kv.first;
    auto& eventsQueue = kv.second;

    LOG(INFO) << "Processing " << eventsQueue.size() << " events from "
              << agent;

    folly::dynamic eventMsgs = folly::dynamic::array;
    for (const auto& event : eventsQueue) {
      // Convert event from thrift::Event => folly::dynamic::object
      std::string eventJson = JsonUtils::serializeToJson(event);
      try {
        folly::dynamic eventDynamic = folly::parseJson(eventJson);
        eventMsgs.push_back(eventDynamic);
      } catch (const std::exception& ex) {/* shouldn't happen */}
    }
    if (eventMsgs.empty()) {
      continue;
    }

    // Add events to queues
    auto nodeNameIt = nodeMacToName_.find(agent);
    auto siteIt = nodeMacToSite_.find(agent);
    if (nodeNameIt != nodeMacToName_.end() &&
        siteIt != nodeMacToSite_.end()) {
      // Push events from a known node
      dataPointsQueue.push_back(folly::dynamic::object(
          "mac", agent)("name", nodeNameIt->second)(
          "site", siteIt->second)("events", eventMsgs));
    } else {
      // Push events from another source (e.g. controller)
      dataPointsQueue.push_back(folly::dynamic::object(
          "mac", agent)("name", "")("site", "")("events", eventMsgs));
    }
  }

  folly::dynamic eventsWriterRequest = folly::dynamic::object(
      "topology", folly::dynamic::object("name", topology_.name))(
      "agents", dataPointsQueue);

  string postString = folly::toJson(eventsWriterRequest);
  std::vector<std::string> endpointUrls;
  for (const auto& endpoint : dataEndpoints_) {
    if (endpoint.second.eventsEnabled) {
      endpointUrls.push_back(
          endpoint.second.host + endpoint.second.eventsWriterSuffix);
    }
  }
  pushCurlPostRequest(endpointUrls, postString);
}

void
StatsApp::periodicDataPublish() {
  // Check if previous publisher thread exists, and wait for it to finish
  stopPublisherThread(dataPublisherThread_, dataPublisherStop_);

  // Move queues
  auto statsQueues = std::move(statsQueues_);
  auto sysLogsQueue = std::move(sysLogsQueue_);
  auto eventsQueues = std::move(eventsQueues_);
  statsQueues_ = {};
  sysLogsQueue_ = folly::dynamic::array;
  eventsQueues_ = {};

  // Spawn a new thread
  dataPublisherStop_ = false;
  dataPublisherThread_ =
      std::thread([this, statsQueues, sysLogsQueue, eventsQueues]() {
    // Process and push queues
    if (!statsQueues.empty()) {
      auto startTime = (int64_t)duration_cast<microseconds>(
          system_clock::now().time_since_epoch()).count();
      std::vector<std::string> endpointUrls;
      for (const auto& endpoint : dataEndpoints_) {
        if (endpoint.second.statsEnabled) {
          endpointUrls.push_back(
              endpoint.second.host + endpoint.second.statsWriterSuffix);
        }
      }
      pushQueuedStats(
          statsQueues,
          prevStatsQueues_,
          FLAGS_data_publish_interval_s,
          dataPublisherStop_,
          endpointUrls);

      auto endTime = (int64_t)duration_cast<microseconds>(
          system_clock::now().time_since_epoch()).count();
      LOG(INFO) << "Time to process queued stats: " << (endTime - startTime)
                << " us";
    }
    if (!sysLogsQueue.empty()) {
      pushQueuedSysLogs(sysLogsQueue);
    }
    if (!eventsQueues.empty()) {
      pushQueuedEvents(eventsQueues);
    }
  });
}

void
StatsApp::periodicHighFrequencyDataPublish() {
  if (highFrequencyStatsQueues_.empty()) {
    VLOG(5) << "High-frequency stats queue is empty, nothing to publish";
    return;
  }

  // Don't publish high frequency stats until the previous thread is done
  stopPublisherThread(hfDataPublisherThread_, hfDataPublisherStop_);

  // Move stats queue
  auto hfStatsQueues = std::move(highFrequencyStatsQueues_);
  highFrequencyStatsQueues_ = {};

  // Spawn new thread to process stats
  hfDataPublisherStop_ = false;
  hfDataPublisherThread_ = std::thread([this, hfStatsQueues]() {
    auto startTime = (int64_t)duration_cast<microseconds>(
                         system_clock::now().time_since_epoch())
                         .count();

    // Process and push high-frequency stats queue
    std::vector<std::string> endpointUrls;
    for (const auto& endpoint : dataEndpoints_) {
      if (endpoint.second.highFreqStatsEnabled) {
        endpointUrls.push_back(
            endpoint.second.host + endpoint.second.highFreqStatsWriterSuffix);
      }
    }
    pushQueuedStats(
        hfStatsQueues,
        prevHighFrequencyStatsQueues_,
        FLAGS_high_frequency_data_publish_interval_s,
        hfDataPublisherStop_,
        endpointUrls);

    auto endTime = (int64_t)duration_cast<microseconds>(
                       system_clock::now().time_since_epoch())
                       .count();
    VLOG(3) << "Time to process queued high-frequency stats: "
            << (endTime - startTime) << " us";
  });
}

void
StatsApp::stopPublisherThread(
    std::thread& publisher, std::atomic_bool& publisherStop) {
  if (publisher.joinable()) {
    publisherStop = true;
    publisher.join();
  }
}

} // namesapce stats
} // namespace terragraph
} // namespace facebook
