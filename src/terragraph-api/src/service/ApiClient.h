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
#include <vector>

#include "api/if/gen-cpp2/Permissions_types.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "stats/if/gen-cpp2/Aggregator_types.h"

namespace facebook {
namespace terragraph {
namespace api {

/**
 * Client for all API methods.
 *
 * This class translates JSON-encoded Thrift requests into ZMQ/Thrift calls
 * to the controller or aggregator, then returns a JSON-serialized response.
 *
 * NOTE: This class is not thread-safe (aside from static map access).
 */
class ApiClient {
 public:
  class RequestFunction {
   public:
    // List of HTTP methods the endpoints may use
    enum HTTPMethod {
        GET,
        POST
    };

    RequestFunction(
        std::function<std::optional<std::string>(
            ApiClient* apiClient, const std::string& json)> function,
        thrift::ApiCategory category,
        thrift::ApiLevel level,
        HTTPMethod method);

    RequestFunction(
        std::function<std::optional<std::string>(
            ApiClient* apiClient, const std::string& json)> function,
        HTTPMethod method);

    // Check if any of the roles are valid
    bool hasPermission(const std::vector<std::string>& roles);

    // Run the lambda function
    std::optional<std::string> applyFunction(
        ApiClient* apiClient, const std::string& body);

    // Returns true if function performs a write operation to the network
    bool isWriteOperation() const;

    // Returns true if no auth is needed for this endpoint
    bool isAuthless() const;

    // Get the HTTP method that should be used for this function
    HTTPMethod method() const;

   private:
    // Underlying lambda function
    std::function<std::optional<std::string>(
        ApiClient* apiClient, const std::string& json)>
        function_;

    // The API category that this function belongs to
    std::optional<thrift::ApiCategory> category_;

    // The base level needed in order to authorize
    std::optional<thrift::ApiLevel> level_;

    // The HTTP method that must be used for this function
    HTTPMethod method_;
  };

  ApiClient(
      fbzmq::Context& context,
      const std::string& ctrlRouterUrl,
      const std::string& aggrRouterUrl,
      const std::string& zmqId);

  // Check if a given method exists
  static bool contains(const std::string& methodName);

  // Return the request function for a given method
  static std::optional<RequestFunction> get(const std::string& methodName);

 private:
  // Map type for API methods
  using MethodMap =
      std::unordered_map<std::string /* methodName */, RequestFunction>;

  // Static map of all API methods
  static MethodMap methods_;

  // Make a request to the controller, returning the JSON response if successful
  template <class ThriftRequestType, class ThriftResponseType>
  std::optional<std::string> makeCtrlRequest(
      const std::string& json,
      const std::string& receiverId,
      const thrift::MessageType& mType);

  // Make a request to the aggregator, returning the JSON response if successful
  template <class ThriftRequestType, class ThriftResponseType>
  std::optional<std::string> makeAggrRequest(
      const std::string& json,
      const std::string& receiverId,
      const thrift::AggrMessageType& mType);

  // Send the object to the controller
  template <class T>
  void sendToCtrl(
      const std::string& receiverId,
      const thrift::MessageType& mType,
      const T& obj);

  // Send the object to the aggregator
  template <class T>
  void sendToAggr(
      const std::string& receiverId,
      const thrift::AggrMessageType& mType,
      const T& obj);

  // Receive an object from the controller
  std::optional<thrift::Message> receiveFromCtrl();

  // Receive an object from the aggregator
  std::optional<thrift::AggrMessage> receiveFromAggr();

  // Serialize an object to JSON
  template <class T>
  std::string serializeToJson(const T& obj);

  // Deserialize an object from JSON
  template <class T>
  std::optional<T> deserializeFromJson(const std::string& s);

  // Deserialize an object from Thrift
  template <class T>
  std::optional<T> deserializeFromThrift(const std::string& buf);

  // Tries to connect to the controller
  void connectToCtrl();

  // Tries to connect to the aggregator
  void connectToAggr();

  // The zmq socket to talk to the controller
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> ctrlSock_;

  // The zmq socket to talk to the aggregator
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> aggrSock_;

  // The ZMQ socket URL of the controller
  const std::string ctrlRouterUrl_;

  // The ZMQ socket URL of the aggregator
  const std::string aggrRouterUrl_;

  // The zmq id
  const std::string zmqId_;

  // Whether the controller is connected
  bool ctrlConnected_{false};

  // Whether the aggregator is connected
  bool aggrConnected_{false};

  // The serializer for all the messages
  apache::thrift::CompactSerializer serializer_;
};

} // namespace api
} // namespace terragraph
} // namespace facebook
