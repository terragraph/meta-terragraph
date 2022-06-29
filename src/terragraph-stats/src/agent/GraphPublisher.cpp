/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "GraphPublisher.h"

#include <curl/curl.h>
#include <folly/compression/Zlib.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/MapUtil.h>
#include <folly/json.h>
#include <future>

#include "stats/common/StatInfo.h"

using namespace fbzmq;
using std::string;

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

static size_t
curlReadCb(char *buffer, size_t size, size_t nitems, void *instream) {
  size_t writeSize = 0;
  if (instream) {
    struct HTTPDataStruct *httpData = (struct HTTPDataStruct *) instream;
    writeSize = httpData->size < size * nitems ? httpData->size : size * nitems;
    memcpy(buffer, httpData->data, writeSize);
    httpData->data += writeSize;
    httpData->size -= writeSize;
  }
  return writeSize;
}
}

namespace {
// maximum number of records supported by pelican api call
const int kMaxPelicanRecordsPerCall{10000};
} // namespace

namespace facebook {
namespace terragraph {
namespace stats {

GraphPublisher::GraphPublisher(
    fbzmq::Context& context,
    const std::string& macAddr,
    std::chrono::seconds statsReportInterval,
    std::chrono::seconds curlTimeout,
    const thrift::StatsAgentParams& statsAgentParams)
    : BasePublisher(context, macAddr, "GraphPublisher", statsAgentParams),
      statsReportInterval_(statsReportInterval),
      curlTimeout_(curlTimeout) {
  // Check configs
  const auto& endpointParams = statsAgentParams.endpointParams;
  odsParams_ = endpointParams.odsParams_ref().value_or(thrift::OdsParams());
  pelicanParams_ =
      endpointParams.pelicanParams_ref().value_or(thrift::PelicanParams());
  scribeParams_ =
      endpointParams.scribeParams_ref().value_or(thrift::ScribeParams());
  if (!odsParams_.enabled) {
    LOG(INFO) << "No ODS params set in config";
  }
  if (!pelicanParams_.enabled) {
    LOG(INFO) << "No Pelican params set in config";
  }
  if (!scribeParams_.enabled) {
    LOG(INFO) << "No Scribe params set in config";
  }

  // Create stats-report timer
  const bool makePeriodic = true;
  statsReportTimer_ =
      ZmqTimeout::make(this, [&]() noexcept { pushQueuedGraphRequests(); });
  statsReportTimer_->scheduleTimeout(statsReportInterval_, makePeriodic);
}

void
GraphPublisher::pushQueuedGraphRequests() {
  if (!curValues_.empty()) {
    pushQueuedCounters();
  }
  if (!eventLogs_.empty()) {
    pushQueuedEventLogs();
  }
}

void
GraphPublisher::pushQueuedEventLogs() {
  folly::dynamic logMessages = folly::dynamic::array;
  for (const auto& eventLog : eventLogs_) {
    for (const auto& sample : eventLog.samples_ref().value()) {
      // scribe logging format. message must be a string, convert to JSON first
      logMessages.push_back(folly::dynamic::object(
          "category", eventLog.category_ref().value())("message", sample));
    }
  }
  pushGraphApiLogsRequest(logMessages);
  eventLogs_.clear();
}

void
GraphPublisher::pushQueuedCounters() {
  folly::dynamic dataPointsQueue = folly::dynamic::array;
  for (const auto& counterKv : curValues_) {
    const StatInfo info(counterKv.first, macAddr_);
    double curVal = counterKv.second.value_ref().value();
    int64_t ts = counterKv.second.timestamp_ref().value();
    bool isCounter =
        counterKv.second.valueType_ref() ==
            fbzmq::thrift::CounterValueType::COUNTER;

    if (isCounter && convertToRate_) {
      // For counters, calculate rate from prev value
      std::optional<double> maybeRate =
          getCounterRate(counterKv.first, counterKv.second, prevValues_);
      std::string rateKeySuffix;
      if (publishValueWithRate_) {
        // Publish raw value and rate as separate keys
        dataPointsQueue.push_back(
            createSingleOdsDataPoint(info.key, ts, curVal, info.entity));
        rateKeySuffix = ".rate";
      }
      if (maybeRate) {
        // Publish rate (if valid)
        dataPointsQueue.push_back(
            createSingleOdsDataPoint(
                info.key + rateKeySuffix, ts, *maybeRate, info.entity));
      }
    } else {
      // Push raw value
      dataPointsQueue.push_back(
          createSingleOdsDataPoint(info.key, ts, curVal, info.entity));
    }
  }

  // flip current values to previous values
  prevValues_ = curValues_;
  // wipe current values
  curValues_.clear();

  // push all ods counters in queue
  pushAsyncOdsRequest(dataPointsQueue);

  // push scribe messages in 1-second buckets
  if (!statsQueueByTimestamp_.empty()) {
    folly::dynamic pelicanLogMessages = folly::dynamic::array;
    for (const auto& tsAndCounters : statsQueueByTimestamp_) {
      for (const auto& nameAndThriftCounter : tsAndCounters.second) {
        const StatInfo info(nameAndThriftCounter.first, macAddr_);
        fbzmq::thrift::Counter thriftCounter = nameAndThriftCounter.second;
        int64_t ts = tsAndCounters.first;
        pelicanLogMessages.push_back(createSinglePelicanLogMessage(
            info.key, thriftCounter, ts, info.entity));
        if (pelicanLogMessages.size() >= kMaxPelicanRecordsPerCall) {
          pushGraphApiPelicanRequest(pelicanLogMessages);
          pelicanLogMessages.erase(
              pelicanLogMessages.begin(), pelicanLogMessages.end());
        }
      }
    }
    pushGraphApiPelicanRequest(pelicanLogMessages);
    statsQueueByTimestamp_.clear();
  }
}

folly::dynamic
GraphPublisher::createSingleOdsDataPoint(
    const string& key, const int64_t ts, double value, const string& entity) {
  string odsEntity(folly::sformat("{}{}", odsParams_.entityPrefix, entity));
  stripWhitespace(odsEntity);

  string odsKey(folly::sformat("{}{}", odsParams_.keyPrefix, key));
  stripWhitespace(odsKey);

  folly::dynamic dataPoint = folly::dynamic::object("entity", odsEntity)(
      "key", odsKey)("value", value)("time", ts);
  VLOG(4) << "Counter stat entity: " << odsEntity << ", key: " << odsKey
          << ", ts: " << ts << ", value: " << value;
  return dataPoint;
}

folly::dynamic
GraphPublisher::createSinglePelicanLogMessage(
    const std::string& counterName,
    const fbzmq::thrift::Counter& thriftCounter,
    int64_t ts,
    const std::string& entity) {
  int64_t unixUs =
      getUnixUsTimestamp(thriftCounter.timestamp_ref().value(), ts);
  folly::dynamic record = folly::dynamic::object
          ("timestamp_us", unixUs)
          ("activity_name", "terragraph." + entity)
          ("stream_name", counterName)
          ("value", thriftCounter.value_ref().value());
  return record;
}

/*
 * Push curl request to graph api endpoint and return the parsed JSON
 */
folly::dynamic
GraphPublisher::graphApiRequest(
    const string& endpointUrl,
    const std::unordered_map<string, string>& reqParams) {
  CURL* curl;
  CURLcode res;
  struct curl_httppost* formBegin = nullptr;
  struct curl_httppost* formEnd = nullptr;
  curl = curl_easy_init();
  if (!curl) {
    throw std::runtime_error("Unable to initialize CURL");
  }
  // add access token for all api requests
  curl_formadd(
      &formBegin,
      &formEnd,
      CURLFORM_COPYNAME,
      "access_token",
      CURLFORM_COPYCONTENTS,
      odsParams_.accessToken.c_str(),
      CURLFORM_END);
  // add all request specific parameters
  for (const auto& formOption : reqParams) {
    curl_formadd(
        &formBegin,
        &formEnd,
        CURLFORM_COPYNAME,
        formOption.first.c_str(),
        CURLFORM_COPYCONTENTS,
        formOption.second.c_str(),
        CURLFORM_END);
  }
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  // we can't verify the peer with our current image/lack of certs
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
  curl_easy_setopt(curl, CURLOPT_URL, endpointUrl.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPPOST, formBegin);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
  // timeout the overall request (dns, sending, receiving)
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, curlTimeout_);
  // read data from request
  struct HTTPDataStruct dataChunk;
  dataChunk.data = (char*)malloc(1);
  // Make sure Json parse does not try to parse based on random text
  dataChunk.data[0] = 0;
  dataChunk.size = 0;
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curlWriteCb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&dataChunk);
  res = curl_easy_perform(curl);
  // endpoint should return JSON result
  folly::dynamic curlJsonRes = folly::dynamic::object;
  try {
    curlJsonRes = folly::parseJson(dataChunk.data);
  } catch (const std::runtime_error& ex) {
    LOG(ERROR) << "JSON parse error: " << ex.what();
  }
  // cleanup
  curl_formfree(formBegin);
  curl_easy_cleanup(curl);
  free(dataChunk.data);
  if (res != CURLE_OK) {
    throw std::runtime_error(folly::sformat(
        "CURL error for endpoint {}: {}",
        endpointUrl,
        curl_easy_strerror(res)));
  }
  return curlJsonRes;
}

bool
GraphPublisher::pushGraphApiStatsRequest(const folly::dynamic& datapoints) {
  if (!odsParams_.enabled) {
    return false;
  }

  // create request parameters
  std::unordered_map<string, string> reqParams = {
      {"category_id", folly::to<std::string>(odsParams_.categoryId)},
      {"datapoints", folly::toJson(datapoints)}};
  folly::dynamic graphApiRes = folly::dynamic::array;
  try {
    graphApiRes = graphApiRequest(odsParams_.remoteEndpoint, reqParams);
  } catch (const std::runtime_error& ex) {
    LOG(ERROR) << ex.what();
    return false;
  }
  // verify data points count
  int datapointsSuccessCount{0};
  if (graphApiRes.isObject()) {
    if (graphApiRes.count("count")) {
      datapointsSuccessCount = graphApiRes.at("count").getInt();
    } else if (graphApiRes.count("error")) {
      LOG(ERROR) << "Graph API error: " << graphApiRes.at("error");
      return false;
    }
  }
  int submittedPoints = (int)datapoints.size();
  if (datapointsSuccessCount > 0) {
    // Report success if ODS accepts any data points, log the count
    if (submittedPoints == datapointsSuccessCount) {
      LOG(INFO) << "Successfully submitted all " << datapointsSuccessCount
                << " data points to ODS";
    } else {
      LOG(INFO) << "Successfully submitted " << datapointsSuccessCount
                << "/" << submittedPoints << " data points to ODS";
    }
    return true;
  } else {
    LOG(ERROR) << "Failed pushing " << datapoints.size()
               << " data points to ODS.";
  }
  return false;
}

bool
GraphPublisher::pushGraphApiLogsRequest(const folly::dynamic& logMessages) {
  if (!scribeParams_.enabled) {
    return false;
  }

  std::unordered_map<string, string> reqParams = {
      {"logs", folly::toJson(logMessages)}};
  folly::dynamic graphApiRes = folly::dynamic::array;
  try {
    graphApiRes = graphApiRequest(scribeParams_.remoteEndpoint, reqParams);
  } catch (const std::runtime_error& ex) {
    LOG(ERROR) << ex.what();
    return false;
  }
  // verify data points count
  int logMessagesSuccessCount{0};
  if (graphApiRes.isObject()) {
    if (graphApiRes.count("count")) {
      logMessagesSuccessCount = graphApiRes.at("count").getInt();
    }
  }
  if ((int)logMessages.size() == logMessagesSuccessCount) {
    VLOG(1) << "Successfully submitted all " << logMessagesSuccessCount
            << " messages to Scribe";
    return true;
  } else {
    LOG(ERROR) << "Failed pushing " << logMessages.size()
               << " messages to Scribe.";
  }
  return false;
}

bool
GraphPublisher::pushGraphApiPelicanRequest(const folly::dynamic& logMessages) {
  if (!pelicanParams_.enabled) {
    return false;
  }

  auto rawData = folly::toJson(folly::dynamic::object
          ("access_token", pelicanParams_.accessToken)
          ("data", folly::toJson(logMessages)));
  auto compressedData = getCodec(folly::io::CodecType::GZIP)->compress(rawData);

  CURL* curl;
  CURLcode res;
  curl = curl_easy_init();
  if (!curl) {
    throw std::runtime_error("Unable to initialize CURL");
  }
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  // we can't verify the peer with our current image/lack of certs
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
  curl_easy_setopt(curl, CURLOPT_URL, pelicanParams_.remoteEndpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
  // timeout the overall request (dns, sending, receiving)
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, curlTimeout_);
  // read data from request
  struct HTTPDataStruct dataChunk;
  dataChunk.data = (char*)malloc(1);
  // Make sure Json parse does not try to parse based on random text
  dataChunk.data[0] = 0;
  dataChunk.size = 0;
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curlWriteCb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&dataChunk);
  // post
  curl_easy_setopt(curl, CURLOPT_POST, 1);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, compressedData.length());
  struct curl_slist *list = nullptr;
  list = curl_slist_append(list, "Content-Encoding: gzip");
  list = curl_slist_append(list, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
  struct HTTPDataStruct cData;
  cData.size = compressedData.length();
  cData.data = (char*)compressedData.c_str();
  curl_easy_setopt(curl, CURLOPT_READFUNCTION, &curlReadCb);
  curl_easy_setopt(curl, CURLOPT_READDATA, (void*)&cData);
  res = curl_easy_perform(curl);
  // endpoint should return JSON result
  folly::dynamic curlJsonRes = folly::dynamic::object;
  try {
    curlJsonRes = folly::parseJson(dataChunk.data);
  } catch (const std::runtime_error& ex) {
    LOG(ERROR) << "JSON parse error: " << ex.what();
  }
  // cleanup
  curl_slist_free_all(list);
  curl_easy_cleanup(curl);
  free(dataChunk.data);
  if (res != CURLE_OK) {
    LOG(ERROR) <<  "Pelican endpoint CURL error: " <<  curl_easy_strerror(res);
    return false;
  }

  // verify data points count
  int logMessagesSuccessCount{0};
  if (curlJsonRes.isObject()) {
    if (curlJsonRes.count("count")) {
      logMessagesSuccessCount = curlJsonRes.at("count").getInt();
    }
  }
  if ((int)logMessages.size() == logMessagesSuccessCount) {
    LOG(INFO) << "Successfully submitted all " << logMessagesSuccessCount
            << " messages to Pelican."
            << " With " << rawData.length() << " bytes"
            << " compressed to " << compressedData.length() << " bytes";
    return true;
  } else {
    LOG(ERROR) << "Error. Pushed only "
               << logMessagesSuccessCount << "/" << logMessages.size()
               << " messages to Pelican";
  }
  return false;
}

void
GraphPublisher::pushAsyncOdsRequest(const folly::dynamic& dataPoints) {
  if (!odsParams_.enabled) {
    return;
  }

  // drop empty requests
  if (dataPoints.empty()) {
    return;
  }

  // start new thread so we don't block
  std::thread asyncGraphApiThread([=]() {
    return pushGraphApiStatsRequest(dataPoints);
  });
  asyncGraphApiThread.detach();
}

void
GraphPublisher::processCountersMessage(
    const fbzmq::thrift::CounterValuesResponse& counters) noexcept {
  int64_t ts = std::time(nullptr); // seconds
  // create datapoints from counters, add to queue
  for (const auto& kv : counters.counters_ref().value()) {
    const StatInfo info(kv.first);

    // check if blacklisted
    if (isBlacklisted(info.key)) {
      VLOG(4) << "Dropped blacklisted counter '" << info.key << "'";
      continue;
    }

    // modify the timestamp of the counter we're sending to ods
    // without changing the data going to scribe
    auto copiedStat = kv.second;
    copiedStat.timestamp_ref() = ts;
    if (kv.second.valueType_ref().value() ==
        fbzmq::thrift::CounterValueType::COUNTER) {
      // counters require computing the difference for the time period
      // keep the oldest value in prevValue_, then update curValue_ everytime.
      auto pIt = prevValues_.find(kv.first);
      if (pIt == prevValues_.end()) {
        // no previous value, set for the first time
        prevValues_[kv.first] = copiedStat;
      }
    }
    // always update current value, even if same as prevValue, since we'll
    // flip prevValues = curValues on iterations
    curValues_[kv.first] = copiedStat;

    // add to scuba stats queue to aggregate by the second
    statsQueueByTimestamp_[ts].push_back({kv.first, kv.second});
  }
}

void
GraphPublisher::processEventLogMessage(
    const fbzmq::thrift::EventLog& eventLog) noexcept {
  eventLogs_.push_back(eventLog);
}

void
GraphPublisher::stripWhitespace(string& inStr) {
  std::transform(inStr.begin(), inStr.end(), inStr.begin(), [](char c) {
    return (c == ' ' ? '_' : c);
  });
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
