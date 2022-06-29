/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#pragma once

#include <fbzmq/zmq/Zmq.h>
#include <functional>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <unordered_map>

#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {
namespace api {

/**
 * Client for all Streaming API methods.
 *
 * This class translates Thrift Messages into JSON-serialized responses.
 */
class StreamApiClient {
 public:
   // Event type signature
   using Event = thrift::MessageType;

   // EventFunction type signature
   // EventFunctions should output a string that will be streamed back to the
   // client as part of the data field in an Event stream message:
   // https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events#Event_stream_format.
   using EventFunction = std::function<std::optional<std::string>(
      const thrift::Message& msg)>;

  // Check if a given stream exists
  static bool streamExists(const std::string& streamName);

  // Check if event belongs to this stream
  static bool streamContainsEvent(
      const std::string& streamName, const Event& event);

  // Return the event function that processes a given event
  static std::optional<EventFunction> get(const Event& event);

 private:
   // Map type mapping streams to their events
   using StreamEventMap = std::unordered_map<
       std::string /* streamName */,
       std::unordered_set<Event> /* events */>;

   // Map type mapping events to their processing functions
   using EventFunctionMap = std::unordered_map<Event, EventFunction>;

   // Map of stream names to their corresponding events
   static StreamEventMap streamEvents_;

   // Map of events to their corresponding EventFunction
   static EventFunctionMap eventFunctions_;

   // The serializer for all the messages
   static apache::thrift::CompactSerializer serializer_;

   // Try reading a Thrift struct out of a Message.
   // On success, return it (wrapped in an std::optional<T>).
   // On failure, return std::nullopt.
   template <class T>
   static std::optional<T> maybeReadThrift(const thrift::Message& message) {
     try {
       return fbzmq::util::readThriftObjStr<T>(message.value, serializer_);
     } catch (const std::exception& e) {
       return std::nullopt;
     }
   }

   // JSON-serialize a thrift object of type ThriftType
   template <class ThriftType>
   static std::optional<std::string> serializeThriftObject(
      const thrift::Message& msg);
};

} // namesapce api
} // namespace terragraph
} // namespace facebook
