/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "StreamApiClient.h"

#include <folly/String.h>

namespace facebook {
namespace terragraph {
namespace api {

apache::thrift::CompactSerializer StreamApiClient::serializer_;

//
// This function populates the map of streams to their corresponding events.
//
// For apidoc conventions see ApiClient.cpp.
//
StreamApiClient::StreamEventMap StreamApiClient::streamEvents_ = [] {
  StreamEventMap map;

   /**
    * @api {get} /stream/topology Get Topology Events
    * @apiName TopologyStream
    * @apiGroup Streams
    *
    * @apiDescription Creates a stream of Topology events.
    *
    * @apiUse TopologyEvents
    * @apiExample {curl} Example:
    *    curl -i http://localhost:443/stream/topology
    * @apiSuccessExample {json} Success-Response:
    *  HTTP/1.1 200 OK
    *  event: EVENT_ADD_NODE
    *  data: {"name":"terra322.f5.tb.a404-if","node_type":2,"mac_addr":"00:00:00:10:0b:4c","pop_node":false,"status":3,"wlan_mac_addrs":[],"site_name":"D","ant_azimuth":0,"ant_elevation":0}
    *
    *  event: EVENT_ADD_LINK
    *  data: {"name":"link-terra314.f5.tb.a404-if-terra323.f5.tb.a404-if","a_node_name":"terra314.f5.tb.a404-if","z_node_name":"terra323.f5.tb.a404-if","link_type":1,"is_alive":true,"linkup_attempts":9,"a_node_mac":"00:00:00:10:0b:4b","z_node_mac":"00:00:00:10:0b:4d"}
    */
  map.insert({"topology", {
        thrift::MessageType::EVENT_ADD_NODE,
        thrift::MessageType::EVENT_DEL_NODE,
        thrift::MessageType::EVENT_EDIT_NODE,
        thrift::MessageType::EVENT_ADD_LINK,
        thrift::MessageType::EVENT_DEL_LINK,
        thrift::MessageType::EVENT_ADD_SITE,
        thrift::MessageType::EVENT_DEL_SITE,
        thrift::MessageType::EVENT_EDIT_SITE,
  }});

  /**
   * @api {get} /stream/statusChanges Get Status Change Events
   * @apiName StatusChangeStream
   * @apiGroup Streams
   *
   * @apiDescription Creates a stream of status change events.
   *
   * @apiUse StatusChangeEvents
   * @apiExample {curl} Example:
   *    curl -i http://localhost:443/stream/state
   * @apiSuccessExample {json} Success-Response:
   *  HTTP/1.1 200 OK
   *  event: EVENT_NODE_STATUS
   *  data: {"name":"terra322.f5.tb.a404-if","node_type":2,"mac_addr":"00:00:00:10:0b:4c","pop_node":false,"status":3,"wlan_mac_addrs":[],"site_name":"D","ant_azimuth":0,"ant_elevation":0}
   *
   *  event: EVENT_LINK_STATUS
   *  data: {"name":"link-terra314.f5.tb.a404-if-terra323.f5.tb.a404-if","a_node_name":"terra314.f5.tb.a404-if","z_node_name":"terra323.f5.tb.a404-if","link_type":1,"is_alive":true,"linkup_attempts":9,"a_node_mac":"00:00:00:10:0b:4b","z_node_mac":"00:00:00:10:0b:4d"}
   */
  map.insert({"statusChanges", {
        thrift::MessageType::EVENT_NODE_STATUS,
        thrift::MessageType::EVENT_LINK_STATUS
  }});

  return map;
}();

//
// This function populates the map of supported events in the streaming API.
// The EventFunction for each event is expected to return a string to be
// directly forwarded as data to the client.
//
// For apidoc conventions see ApiClient.cpp.
//
StreamApiClient::EventFunctionMap StreamApiClient::eventFunctions_ = [] {
  EventFunctionMap map;

  // Topology stream events
  /**
   * @apiDefine TopologyEvents
   * @apiParam (TopologyEvents) {Object(Node)} EVENT_ADD_NODE
   *                            Node added event
   * @apiParam (TopologyEvents) {Object(Node)} EVENT_DEL_NODE
   *                            Node deleted event
   * @apiParam (TopologyEvents) {Object(Node)} EVENT_EDIT_NODE
   *                            Node edited event
   * @apiParam (TopologyEvents) {Object(Link)} EVENT_ADD_LINK
   *                            Link added event
   * @apiParam (TopologyEvents) {Object(Link)} EVENT_DEL_LINK
   *                            Link deleted event
   * @apiParam (TopologyEvents) {Object(Site)} EVENT_ADD_SITE
   *                            Site added event
   * @apiParam (TopologyEvents) {Object(Site)} EVENT_DEL_SITE
   *                            Site deleted event
   * @apiParam (TopologyEvents) {Object(Site)} EVENT_EDIT_SITE
   *                            Site edited event
   */
  map.insert({thrift::MessageType::EVENT_ADD_NODE,
      [] (const thrift::Message& msg) -> std::optional<std::string> {
        return serializeThriftObject<thrift::Node>(msg);
      }
  });
  map.insert({thrift::MessageType::EVENT_DEL_NODE,
      [] (const thrift::Message& msg) -> std::optional<std::string> {
        return serializeThriftObject<thrift::Node>(msg);
      }
  });
  map.insert({thrift::MessageType::EVENT_EDIT_NODE,
      [] (const thrift::Message& msg) -> std::optional<std::string> {
        return serializeThriftObject<thrift::Node>(msg);
      }
  });
  map.insert({thrift::MessageType::EVENT_ADD_LINK,
      [] (const thrift::Message& msg) -> std::optional<std::string> {
        return serializeThriftObject<thrift::Link>(msg);
      }
  });
  map.insert({thrift::MessageType::EVENT_DEL_LINK,
      [] (const thrift::Message& msg) -> std::optional<std::string> {
        return serializeThriftObject<thrift::Link>(msg);
      }
  });
  map.insert({thrift::MessageType::EVENT_ADD_SITE,
      [] (const thrift::Message& msg) -> std::optional<std::string> {
        return serializeThriftObject<thrift::Site>(msg);
      }
  });
  map.insert({thrift::MessageType::EVENT_DEL_SITE,
      [] (const thrift::Message& msg) -> std::optional<std::string> {
        return serializeThriftObject<thrift::Site>(msg);
      }
  });
  map.insert({thrift::MessageType::EVENT_EDIT_SITE,
      [] (const thrift::Message& msg) -> std::optional<std::string> {
        return serializeThriftObject<thrift::Site>(msg);
      }
  });


  // Status change stream events
  /**
   * @apiDefine StatusChangeEvents
   * @apiParam (StatusChangeEvents) {Object(Node)} EVENT_NODE_STATUS
   *                                Node status change event
   * @apiParam (StatusChangeEvents) {Object(Link)} EVENT_LINK_STATUS
   *                                Link status change event
   */
  map.insert({thrift::MessageType::EVENT_NODE_STATUS,
      [] (const thrift::Message& msg) -> std::optional<std::string> {
        return serializeThriftObject<thrift::Node>(msg);
      }
  });
  map.insert({thrift::MessageType::EVENT_LINK_STATUS,
      [] (const thrift::Message& msg) -> std::optional<std::string> {
        return serializeThriftObject<thrift::Link>(msg);
      }
  });

  return map;
}();

bool
StreamApiClient::streamExists(const std::string& streamName) {
  return (streamEvents_.count(streamName) != 0);
}

bool
StreamApiClient::streamContainsEvent(
    const std::string& streamName, const Event& event) {
  auto it = streamEvents_.find(streamName);
  if (it == streamEvents_.end()) {
    return false;
  }

  return (it->second.count(event) != 0);
}

std::optional<StreamApiClient::EventFunction>
StreamApiClient::get(const Event& event) {
  auto it = eventFunctions_.find(event);
  if (it == eventFunctions_.end()) {
    return std::nullopt;
  }
  return it->second;
}

template <class ThriftType>
std::optional<std::string>
StreamApiClient::serializeThriftObject(const thrift::Message& msg) {
  auto resp = maybeReadThrift<ThriftType>(msg);
  if (!resp) {
    return std::nullopt;
  }

  std::string contents;
  auto jsonSerializer = apache::thrift::SimpleJSONSerializer();
  try {
    jsonSerializer.serialize(resp.value(), &contents);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not serialize object: " << folly::exceptionStr(ex);
    return std::nullopt;
  }

  return contents;
}

} // namesapce api
} // namespace terragraph
} // namespace facebook
