/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <functional>

#include <fbzmq/service/monitor/ZmqMonitorClient.h>
#include <folly/dynamic.h>
#include <folly/json.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/Event_types.h"

namespace facebook {
namespace terragraph {

/**
 * Event client for publishing events to a ZmqMonitor instance.
 */
class EventClient {
 public:
  /** Constructor. */
  EventClient(
      const std::string& sourceId,
      std::shared_ptr<fbzmq::ZmqMonitorClient> zmqMonitorClient);

  /**
   * Set the callback function to retrieve the topology name to attach to all
   * events.
   */
  void setTopologyNameFunc(std::function<std::string(void)> callback);

  /**
   * Send events to ZmqMonitor for publishing.
   *
   * Returns true upon success, and false otherwise.
   *
   * @param category event category (refer to Event.thrift)
   * @param eventId event ID (refer to Event.thrift)
   * @param level event level (refer to Event.thrift)
   * @param reason event description, in plain English
   * @param details supplemental information as a JSON string (optional)
   * @param entity entity this event is associated with (optional)
   * @param nodeId associated node ID (MAC address)
   * @param nodeName associated node name
   */
  bool logEvent(
      const thrift::EventCategory category,
      const thrift::EventId eventId,
      const thrift::EventLevel level,
      const std::string& reason,
      const std::string& details = "",
      const std::optional<std::string> entity = std::nullopt,
      const std::optional<std::string> nodeId = std::nullopt,
      const std::optional<std::string> nodeName = std::nullopt) const;

  /** Send events with a supplemental Thrift structure. */
  template <class T>
  bool logEventThrift(
      const thrift::EventCategory category,
      const thrift::EventId eventId,
      const thrift::EventLevel level,
      const std::string& reason,
      const T& details,
      const std::optional<std::string> entity = std::nullopt,
      const std::optional<std::string> nodeId = std::nullopt,
      const std::optional<std::string> nodeName = std::nullopt) const {
    std::string detailsStr =
        apache::thrift::SimpleJSONSerializer::serialize<std::string>(details);
    return logEvent(
        category, eventId, level, reason, detailsStr, entity, nodeId, nodeName);
  }

  /** Send events with a supplemental folly::dynamic object. */
  bool logEventDynamic(
      const thrift::EventCategory category,
      const thrift::EventId eventId,
      const thrift::EventLevel level,
      const std::string& reason,
      const folly::dynamic& details,
      const std::optional<std::string> entity = std::nullopt,
      const std::optional<std::string> nodeId = std::nullopt,
      const std::optional<std::string> nodeName = std::nullopt) {
    std::string detailsStr = folly::toJson(details);
    return logEvent(
        category, eventId, level, reason, detailsStr, entity, nodeId, nodeName);
  }

  /** Send events to ZmqMonitor for publishing. */
  bool sendEvent(const thrift::Event& event) const;

  /** Send arbitrary data to ZmqMonitor for publishing. */
  bool sendData(
      const std::string& data, const std::string& eventLogCategory) const;

  /** Send scan data to ZmqMonitor for publishing. */
  bool sendScanData(thrift::ScanResult&& scanResult) const;

 private:
  /** Event source ID. */
  const std::string sourceId_{};

  /** Client to interact with ZmqMonitor. */
  std::shared_ptr<fbzmq::ZmqMonitorClient> zmqMonitorClient_;

  /** The callback function to retrieve the topology name. */
  std::function<std::string(void)> getTopologyName_{nullptr};
};

} // namespace terragraph
} // namespace facebook
