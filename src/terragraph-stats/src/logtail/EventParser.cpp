/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "EventParser.h"

#include <stdexcept>

#include <fbzmq/service/monitor/ZmqMonitorClient.h>
#include <folly/ExceptionString.h>
#include <folly/Format.h>
#include <folly/dynamic.h>

DEFINE_string(nms_publisher_host, "localhost", "NMS publisher Hostname or IP");
DEFINE_int32(nms_publisher_port, 4231, "NMS publisher port");
DEFINE_string(
    event_marker_suffix, ".marker.2", "EventParser marker file suffix");

using std::string;

namespace facebook {
namespace terragraph {
namespace stats {

EventParser::EventParser(
    fbzmq::Context& context,
    const string& macAddr,
    const string& configFileName,
    int32_t submissionInterval,
    uint32_t bufferSize)
    : LogTailer(
          macAddr,
          configFileName,
          FLAGS_event_marker_suffix,
          submissionInterval,
          bufferSize),
      myId_(folly::sformat("{}-eventparser", macAddr)) {

  eventClient_ = std::make_unique<EventClient>(
      myId_,
      std::make_shared<fbzmq::ZmqMonitorClient>(
          context,
          folly::sformat(
              "tcp://{}:{}",
              FLAGS_nms_publisher_host,
              FLAGS_nms_publisher_port),
          myId_));

  for (const auto& sourcesIt : logTailParams_->sources) {
    auto eventFilters = sourcesIt.second.eventFilters;
    for (const auto& eventIt : eventFilters) {
      auto& entry = processedEventFilters_[sourcesIt.first];
      try {
        entry.emplace(
            eventIt.first,
            ProcessedEventFilter(eventIt.second.regex, eventIt.second.level));
      } catch (const std::regex_error& ex) {
        LOG(ERROR) << folly::format(
            "Ignoring malformed custom event regular expression, {}: {}",
            eventIt.second.regex,
            folly::exceptionStr(ex));
      } catch (const std::out_of_range& ex) {
        LOG(ERROR) << folly::format(
            "Ignoring invalid custom event level, {}: {}",
            eventIt.second.level,
            folly::exceptionStr(ex));
      }
    }
  }
}

void
EventParser::monitor() noexcept {
  for (const auto& processedEventIt : processedEventFilters_) {
    string source = processedEventIt.first;
    auto& agent = tailAgents_.at(source);
    agent.fetchLogLines(logsBuffer_, bufferSize_);

    for (const auto& eventIt : processedEventIt.second) {
      string eventName = eventIt.first;
      ProcessedEventFilter eventFilter = eventIt.second;

      for (const auto& line : logsBuffer_) {
        // check if the log line matches the event regex
        if (std::regex_search(line, eventFilter.regex)) {
          VLOG(2) << "Matched event: " << eventName;

          // log the event
          eventClient_->logEventDynamic(
              thrift::EventCategory::LOGTAIL,
              thrift::EventId::LOG_BASED_EVENT,
              eventFilter.level,
              eventName,
              folly::dynamic::object("source", source)("log", line));
        }
      }
    }

    logsBuffer_.clear();
  }
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
