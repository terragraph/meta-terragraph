/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/zmq/Zmq.h>
#include <fbzmq/service/monitor/ZmqMonitorClient.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "e2e/common/EventClient.h"
#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {
namespace stats {

/*
 * Listen for events and commands on a local ZMQ socket.
 */
class InputListener final : public fbzmq::ZmqEventLoop {
 public:
  InputListener(
      fbzmq::Context& context,
      const std::string& sockRouterUrl,
      const std::string& monitorSubmitUrl,
      pid_t agentPid);

 private:
  // Process an input message
  void processMessage(const thrift::Message& message) noexcept;

  // Process an input event
  void processEvent(const thrift::Message& message);

  // Process a restart request
  void processRestart();

  // Try reading a Thrift struct out of a Message.
  // On success, return it (wrapped in a std::optional<T>).
  // On failure, return std::nullopt.
  template <class T>
  std::optional<T>
  maybeReadThrift(const thrift::Message& message) {
    try {
      return fbzmq::util::readThriftObjStr<T>(message.value, serializer_);
    } catch (const std::exception& e) {
      return std::nullopt;
    }
  }

  // The input socket
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> inputSock_;

  // Event client
  std::unique_ptr<EventClient> eventClient_;

  // The client to interact with monitor
  std::shared_ptr<fbzmq::ZmqMonitorClient> zmqMonitorClient_;

  // The process ID of the stats agent
  pid_t agentPid_;

  // The serializer for all the messages
  apache::thrift::CompactSerializer serializer_;
};

} // namespace stats
} // namepsace terragraph
} // namespace facebook
