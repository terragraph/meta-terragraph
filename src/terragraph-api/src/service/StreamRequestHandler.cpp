/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "StreamRequestHandler.h"

#include <chrono>

#include <folly/Format.h>
#include <folly/gen/Base.h>
#include <proxygen/httpserver/ResponseBuilder.h>

#include "StreamApiClient.h"

using apache::thrift::detail::TEnumMapFactory;
using namespace proxygen;

namespace {
// Interval between heartbeats to ensure client is alive
const std::chrono::seconds kHeartbeatInterval{20};
// Heartbeat string to send to clients
// The only restriction is it must start with ":" to be treated as a comment.
// https://html.spec.whatwg.org/multipage/server-sent-events.html#parsing-an-event-stream
const std::string kHeartbeatMessage{": heartbeat\n\n"};
}  // namespace

namespace facebook {
namespace terragraph {
namespace api {

StreamRequestHandler::StreamRequestHandler(
    const std::string& urlPrefix,
    folly::EventBase* evb,
    StreamClients& streamClients)
    : folly::AsyncTimeout(evb),
      urlPrefix_(urlPrefix),
      streamClients_(streamClients),
      evb_(evb) {
}

StreamRequestHandler::~StreamRequestHandler() {
  // Remove this request from streamClients_ since the request is over
  streamClients_.wlock()->erase(this);
}

void StreamRequestHandler::timeoutExpired() noexcept {
  if (connectionClosed_) {
    return;
  }

  // Send heartbeat to detect down clients
  ResponseBuilder(downstream_)
      .body(folly::to<std::string>(kHeartbeatMessage))
      .send();
  scheduleTimeout(kHeartbeatInterval);
}

void StreamRequestHandler::streamCallback(
    const thrift::MessageType& event,
    const std::string& data) {
  if (connectionClosed_) {
    return;
  }

  std::string eventStr;
  try {
     eventStr = TEnumMapFactory<thrift::MessageType>
         ::makeValuesToNamesMap().at(event);
  } catch (const std::exception& ex) {  // Shouldn't happen...
    LOG(ERROR) << "Invalid Stream event: " << eventStr;
    return;
  }

  // Sends an event type and the event data to the client
  // https://html.spec.whatwg.org/multipage/server-sent-events.html#server-sent-events-intro
  ResponseBuilder(downstream_)
      .body(folly::to<std::string>("event: ", std::move(eventStr), "\n"))
      .body(folly::to<std::string>("data: ", std::move(data), "\n\n"))
      .send();

  // Reset heartbeat timeout since we just sent data to the client
  scheduleTimeout(kHeartbeatInterval);
}

void
StreamRequestHandler::onRequest(std::unique_ptr<HTTPMessage> headers)
    noexcept {
  LOG(INFO) << "[" << headers->getClientIP() << "] Request path: "
            << headers->getPath();
  headers_ = std::move(headers);
}

void
StreamRequestHandler::onBody(std::unique_ptr<folly::IOBuf> /*body*/) noexcept {
  // handler doesn't support requests with bodies
}

void
StreamRequestHandler::onEOM() noexcept {
  // Find the Stream API method
  if (!headers_ || headers_->getPath().find(urlPrefix_) != 0) {
    return sendErrorResponse();
  }

  std::string streamName = headers_->getPath().substr(urlPrefix_.length());
  if (!StreamApiClient::streamExists(streamName)) {
    return sendErrorResponse();
  }
  streamName_ = std::move(streamName);

  // Add this request to shared structure so streamer can send events
  streamClients_.wlock()->insert(this);

  // Everything is ok
  ResponseBuilder(downstream_)
      .status(200, "OK")
      .header("Content-Type", "text/event-stream")
      .send();

  // Schedule heartbeat timeout to ensure client is alive
  scheduleTimeout(kHeartbeatInterval);
}

void
StreamRequestHandler::onUpgrade(UpgradeProtocol /*protocol*/) noexcept {
  // handler doesn't support upgrades
}

void
StreamRequestHandler::requestComplete() noexcept {
  connectionClosed_ = true;
  delete this;
}

void
StreamRequestHandler::onError(ProxygenError err) noexcept {
  LOG(ERROR) << "onError: " << getErrorString(err);
  // We should stop processing request
  connectionClosed_ = true;
  delete this;
}

void
StreamRequestHandler::sendErrorResponse() {
  ResponseBuilder(downstream_).status(400, "Bad Request").sendWithEOM();
}

std::string
StreamRequestHandler::getStreamName() {
  return streamName_;
}

folly::EventBase*
StreamRequestHandler::getEventBase() {
  return evb_;
}

} // namesapce api
} // namespace terragraph
} // namespace facebook
