/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#pragma once

#include <folly/io/async/EventBaseManager.h>
#include <folly/Memory.h>
#include <folly/Synchronized.h>
#include <functional>
#include <proxygen/httpserver/RequestHandler.h>

#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {
namespace api {

/**
 * Handler for all stream requests.
 *
 * On success, this handler will generate a response with a 'text/event-stream'
 * MIME type that will send Server Side Events to the client.
 *
 * On an event, this handler will send the following (strings) to the client
 * (newlines written out for clarity):
 * ```
 * event: ADD_NODE\n
 * data: {...}\n\n
 * ```
 *
 * This format tags the data with the event type `ADD_NODE`, allowing clients to
 * further filter the events by type. The full spec can be found at
 * https://html.spec.whatwg.org/multipage/server-sent-events.html.
 *
 * Most browsers have implemented clients for server side events with
 * EventSource. Basic usage is as follows:
 * ```
 * var s = new EventSource('/stream/topology');
 * s.addEventListener('EVENT_ADD_NODE', function(event) {
 *   // event.data contains the data sent by the controller
* }, false);
 * // Do other things...
 * s.close(); // Closes the stream
 * ```
 * More information:
 * https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events
 *
 */
class StreamRequestHandler : public proxygen::RequestHandler,
                             public folly::AsyncTimeout {
 public:
  // Set type containing pointers to handlers for stream clients
  using StreamClients = folly::Synchronized<std::unordered_set<
      StreamRequestHandler*>>;

  explicit StreamRequestHandler(
      const std::string& urlPrefix,
      folly::EventBase* evb,
      StreamClients& streamClients);

  void onRequest(std::unique_ptr<proxygen::HTTPMessage> headers)
      noexcept override;

  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

  void onEOM() noexcept override;

  void onUpgrade(proxygen::UpgradeProtocol proto) noexcept override;

  void requestComplete() noexcept override;

  void onError(proxygen::ProxygenError err) noexcept override;

  // Called by the streamer thread when it receives events from the controller
  // Sends a response with an event and corresponding data
  // https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events#Event_stream_format
  void streamCallback(
      const thrift::MessageType& event, const std::string& data);

  // Return the stream to which the client is subscribed
  std::string getStreamName();

  // Return the EventBase in which this handler is running
  folly::EventBase* getEventBase();

 private:
  // Sends a 400 response to the client
  void sendErrorResponse();

  // Performs clean up before the handler is destroyed
  // - Removes handler from the shared streamClients structure so the Streamer
  //   thread stops sending events to it
  ~StreamRequestHandler();

  // Called by folly::AsyncTimeout
  // Invoked when the timeout period has expired.
  void timeoutExpired() noexcept override;

  // The expected URL prefix
  const std::string urlPrefix_;

  // The HTTP headers
  std::unique_ptr<proxygen::HTTPMessage> headers_;

  // Whether or not the connection has been closed to avoid unnecessary work
  bool connectionClosed_{false};

  // Map stream requests used to send controller events to clients
  StreamClients& streamClients_;

  // Name of the stream this request is subscribed to
  std::string streamName_;

  // EventBase in which this request is running
  folly::EventBase* evb_;
};

} // namesapce api
} // namespace terragraph
} // namespace facebook
