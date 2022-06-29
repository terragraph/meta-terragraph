/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "EventClient.h"

#include <chrono>
#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>
#include <folly/Format.h>
#include <utility>

#include "Consts.h"
#include "JsonUtils.h"

using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {

EventClient::EventClient(
    const std::string& sourceId,
    std::shared_ptr<fbzmq::ZmqMonitorClient> zmqMonitorClient)
    : sourceId_(sourceId), zmqMonitorClient_(zmqMonitorClient) {
}

void
EventClient::setTopologyNameFunc(std::function<std::string(void)> callback) {
  getTopologyName_ = callback;
}

bool
EventClient::logEvent(
    const thrift::EventCategory category,
    const thrift::EventId eventId,
    const thrift::EventLevel level,
    const std::string& reason,
    const std::string& details,
    const std::optional<std::string> entity,
    const std::optional<std::string> nodeId,
    const std::optional<std::string> nodeName) const {
  // Validate inputs
  auto eventCategoryMap =
      TEnumMapFactory<thrift::EventCategory>::makeValuesToNamesMap();
  if (!eventCategoryMap.count(category)) {
    LOG(ERROR) << folly::sformat(
        "Invalid event category {} from source {}",
        static_cast<size_t>(category),
        sourceId_);
    return false;
  }
  auto eventIdMap = TEnumMapFactory<thrift::EventId>::makeValuesToNamesMap();
  if (!eventIdMap.count(eventId)) {
    LOG(ERROR) << folly::sformat(
        "Invalid event ID {} from source {}",
        static_cast<size_t>(eventId),
        sourceId_);
    return false;
  }
  auto eventLevelMap =
      TEnumMapFactory<thrift::EventLevel>::makeValuesToNamesMap();
  if (!eventLevelMap.count(level)) {
    LOG(ERROR) << folly::sformat(
        "Invalid event level {} from source {}",
        static_cast<size_t>(level),
        sourceId_);
    return false;
  }
  VLOG(3) << folly::sformat(
      "Event {}:{} ({}) => {}",
      eventCategoryMap.at(category),
      eventIdMap.at(eventId),
      eventLevelMap.at(level),
      reason);

  // Construct the event
  thrift::Event event;
  event.source = sourceId_;
  event.timestamp = std::time(nullptr);
  event.reason = reason;
  event.details = details;
  event.category = category;
  event.eventId = eventId;
  event.level = level;
  if (entity.has_value()) {
    event.entity_ref() = entity.value();
  }
  if (nodeId.has_value()) {
    event.nodeId_ref() = nodeId.value();
  }
  if (nodeName.has_value()) {
    event.nodeName_ref() = nodeName.value();
  }
  if (getTopologyName_ != nullptr) {
    event.topologyName_ref() = getTopologyName_();
  }

  // Send the event via ZmqMonitorClient
  return sendEvent(event);
}

bool
EventClient::sendEvent(const thrift::Event& event) const {
  return sendData(JsonUtils::serializeToJson(event), E2EConsts::kEventCategory);
}

bool
EventClient::sendData(
    const std::string& data, const std::string& eventLogCategory) const {
  try {
    fbzmq::thrift::EventLog eventLog{};
    eventLog.category_ref() = eventLogCategory;
    eventLog.samples_ref() = {data};
    zmqMonitorClient_->addEventLog(eventLog);
  } catch (const std::exception& e) {
    LOG(ERROR) << "[" << sourceId_
               << "] Error sending message: " << folly::exceptionStr(e);
    return false;
  }
  return true;
}

bool
EventClient::sendScanData(thrift::ScanResult&& scanResult) const {
  thrift::ScanResultEventWrapper scanResultEvent;
  scanResultEvent.result = std::move(scanResult);
  if (getTopologyName_ != nullptr) {
    scanResultEvent.topologyName_ref() = getTopologyName_();
  }
  return sendData(
      JsonUtils::serializeToJson(scanResultEvent),
      E2EConsts::kEventScanResultCategory);
}

} // namespace terragraph
} // namespace facebook
