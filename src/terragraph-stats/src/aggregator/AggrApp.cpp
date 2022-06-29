/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "AggrApp.h"

#include <fbzmq/zmq/Zmq.h>
#include <folly/Format.h>
#include <folly/Random.h>

#include "../common/Consts.h"

using namespace fbzmq;
using std::string;

namespace facebook {
namespace terragraph {
namespace stats {

AggrApp::AggrApp(
    fbzmq::Context& context, string routerSockUrl, string myId)
    : dealerSock_(context, fbzmq::IdentityString{myId}), myId_(myId) {

  // connect the dealer socket to the router socket on the Broker
  LOG(INFO) << "[" << myId_ << "] Connecting to '" << routerSockUrl << "'";
  dealerSock_.connect(fbzmq::SocketUrl{routerSockUrl}).value();

  // message on dealer socket (from broker)
  addSocket(
      fbzmq::RawZmqSocketPtr{*dealerSock_},
      ZMQ_POLLIN,
      [this](int) noexcept {
    VLOG(6) << "[" << myId_
            << "] Received a message on dealer socket from broker";

    fbzmq::Message agent, senderApp, data;
    auto res = dealerSock_.recvMultipleTimeout(
        NMSConsts::kReadTimeout, agent, senderApp, data);
    if (res.hasError()) {
      LOG(ERROR) << "[" << myId_ << "] Error reading message. " << res.error();
      return;
    }

    auto message = data.readThriftObj<thrift::AggrMessage>(serializer_);
    if (message.hasError()) {
      LOG(ERROR) << "[" << myId_ << "] Error parsing message. " << res.error();
      return;
    }

    // decompress the message (if needed)
    std::string error;
    if (!CompressionUtil::decompress(message.value(), error)) {
      LOG(ERROR) << "[" << myId_ << "] " << error;
      return;
    }

    processMessage(
        agent.read<std::string>().value(),
        senderApp.read<std::string>().value(),
        message.value());
  });
}

void
AggrApp::sendAggrAck(
    const std::string& senderApp, bool success, const std::string& message) {
  thrift::AggrAck ack{};
  ack.success = success;
  ack.message = message;
  sendToAggrApp(senderApp, thrift::AggrMessageType::AGGR_ACK, ack);
}

void
AggrApp::handleInvalidMessage(
    const std::string& aggrMessageType,
    const std::string& senderApp,
    const std::string& agent,
    bool sendAck) {
  LOG(ERROR) << "[" << myId_
             << "] Invalid " << aggrMessageType << " message from " << agent
             << ":" << senderApp;
  if (sendAck) {
    sendAggrAck(senderApp, false, "Could not read " + aggrMessageType);
  }
}

} // namesapce stats
} // namespace terragraph
} // namespace facebook
