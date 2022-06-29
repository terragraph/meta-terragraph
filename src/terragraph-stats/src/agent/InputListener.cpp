/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "InputListener.h"

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <folly/MapUtil.h>

#include "../common/Consts.h"
#include "e2e/common/CompressionUtil.h"
#include "e2e/if/gen-cpp2/Event_types.h"

using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {
namespace stats {

InputListener::InputListener(
    fbzmq::Context& context,
    const std::string& sockRouterUrl,
    const std::string& monitorSubmitUrl,
    pid_t agentPid)
    : inputSock_(context), agentPid_(agentPid) {
  // Initialize ZMQ socket
  const int handover = 1;
  inputSock_.setSockOpt(ZMQ_ROUTER_HANDOVER, &handover, sizeof(int));
  LOG(INFO) << "Binding to '" << sockRouterUrl << "'";
  inputSock_.bind(fbzmq::SocketUrl{sockRouterUrl}).value();
  addSocket(
      fbzmq::RawZmqSocketPtr{*inputSock_}, ZMQ_POLLIN, [this](int) noexcept {
    fbzmq::Message firstFrameMsg, data;
    const auto res = inputSock_.recvMultiple(firstFrameMsg, data);
    if (res.hasError()) {
      LOG(ERROR) << "Error reading message: " << res.error();
      return;
    }

    auto message = data.readThriftObj<thrift::Message>(serializer_);
    if (message.hasError()) {
      LOG(ERROR) << "Error parsing message: " << res.error();
      return;
    }

    // decompress the message (if needed)
    std::string error;
    if (!CompressionUtil::decompress(message.value(), error)) {
      LOG(ERROR) << error;
      return;
    }

    processMessage(message.value());
  });

  // Initialize ZMQ monitor client and event client
  zmqMonitorClient_ = std::make_shared<fbzmq::ZmqMonitorClient>(
      context, monitorSubmitUrl, NMSConsts::kInputListenerId);
  eventClient_ = std::make_unique<EventClient>(
      NMSConsts::kInputListenerId, zmqMonitorClient_);
}

void
InputListener::processMessage(const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::EVENT:
      processEvent(message);
      break;
    case thrift::MessageType::RESTART:
      processRestart();
      break;
    default:
      LOG(ERROR)
          << "Wrong type of message ("
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                 message.mType, "UNKNOWN")
          << ") received";
      break;
  }
}

void
InputListener::processEvent(const thrift::Message& message) {
  auto event = maybeReadThrift<thrift::Event>(message);
  if (!event) {
    LOG(ERROR) << "Could not parse event message";
    return;
  }

  VLOG(2) << "Received event on input socket ["
          << folly::get_default(
                 TEnumMapFactory<thrift::EventId>::
                     makeValuesToNamesMap(), event->eventId, "UNKNOWN")
          << "]";

  eventClient_->sendEvent(event.value());
}

void
InputListener::processRestart() {
  LOG(INFO) << "Stats agent process restarting...";
  kill(agentPid_, SIGTERM);
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
