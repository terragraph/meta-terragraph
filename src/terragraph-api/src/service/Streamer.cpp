/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Streamer.h"

#include <chrono>
#include <cstdlib>

#include <fbzmq/zmq/Zmq.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/MapUtil.h>
#include <folly/dynamic.h>
#include <folly/json.h>

#include "e2e/common/CompressionUtil.h"
#include "e2e/if/gen-cpp2/Controller_types.h"

#include "StreamApiClient.h"

using apache::thrift::detail::TEnumMapFactory;
using namespace fbzmq;

using std::string;

namespace {
// Default keepAlive values
// We intend to garbage collect connections after 30 seconds of inactivity
const int kKeepAliveEnable{1};
// Idle Time before sending keep alives
const std::chrono::seconds kKeepAliveTime{15};
// max keep alives before resetting connection
const int kKeepAliveCnt{3};
// interval between keep alives
const std::chrono::seconds kKeepAliveIntvl{5};
// Timeout for an unacknowledged TCP retransmit in ms
const std::chrono::milliseconds kMaxRetransmitTimeInMs{30000};
} // namespace

namespace facebook {
namespace terragraph {
namespace api {

Streamer::Streamer(
    Context& zmqContext,
    const string& zmqId,
    const string& ctrlPubUrl,
    const std::chrono::seconds& ctrlSockTimeout,
    StreamRequestHandler::StreamClients& streamClients)
    : subSock_{zmqContext, IdentityString{zmqId}},
      ctrlSockTimeout_{ctrlSockTimeout},
      ctrlPubUrl_{ctrlPubUrl},
      streamClients_{streamClients} {

  // -- Prepare the subscriber socket to talk to the controller --

  // Overwrite default TCP_KEEPALIVE options to handle controller crash and
  // drop dead socket after 30 secs
  if (subSock_
          .setKeepAlive(
              kKeepAliveEnable,
              kKeepAliveTime.count(),
              kKeepAliveCnt,
              kKeepAliveIntvl.count())
          .hasError()) {
    LOG(FATAL) << "Could not set zmq keepAlive options.";
  }

  // Set TCP maximum retransmit timeout
  // This allows a session to be re-established in a short time
  int maxRT = kMaxRetransmitTimeInMs.count();
  if (subSock_.setSockOpt(ZMQ_TCP_MAXRT, &maxRT, sizeof(int))
          .hasError()) {
    LOG(FATAL) << "Could not set ZMQ_TCP_MAXRT.";
  }

  // Subscribe to all messages
  auto const subOpt = subSock_.setSockOpt(ZMQ_SUBSCRIBE, "", 0);
  if (subOpt.hasError()) {
    LOG(FATAL) << "Error setting ZMQ_SUBSCRIBE to "
               << ""
               << " " << subOpt.error();
  }

  // time out subSock_ if we haven't heard from it in too long
  // (we should receive periodic STATUS_REPORT_ACK for minion's status report)
  ctrlSockTimeoutTimer_ = ZmqTimeout::make(this, [this]() noexcept {
    VLOG(5) << "Controller socket timed out!";
    connectToCtrl();
  });

  connectToCtrl();

  LOG(INFO) << "API Streamer attaching socket/event callbacks...";

  // message on sub socket
  addSocket(
      RawZmqSocketPtr{*subSock_}, ZMQ_POLLIN, [this](int) noexcept {
        VLOG(1) << "Received a message on controller sub socket";

        Message receiverAppMsg, senderAppMsg, thriftMsg;

        const auto recvRet = subSock_.recvMultiple(
            receiverAppMsg, senderAppMsg, thriftMsg);
        if (recvRet.hasError()) {
          LOG(ERROR) << "Error reading message: " << recvRet.error();
          return;
        }

        // Reset timer since we received a message on this socket
        ctrlSockTimeoutTimer_->scheduleTimeout(ctrlSockTimeout_);

        const auto receiverApp = receiverAppMsg.read<std::string>().value();
        const auto senderApp = senderAppMsg.read<std::string>().value();

        VLOG(4) << "Processing a message: " << senderApp
                << " to " << receiverApp << " on pubSock";

        if (receiverApp == E2EConsts::kApiEventSubId) {
          auto maybeMsg = thriftMsg.readThriftObj<thrift::Message>(serializer_);
          if (maybeMsg.hasError()) {
            LOG(ERROR) << "Error deserializing thrift Message from "
                       << senderApp << ": " << maybeMsg.error();
            return;
          }
          // Decompress the message (if needed)
          std::string error;
          if (!CompressionUtil::decompress(maybeMsg.value(), error)) {
            LOG(ERROR) << error;
            return;
          }
          processMessage(senderApp, maybeMsg.value());
        }
      });
}

void
Streamer::processMessage(
    const std::string& senderApp,
    const thrift::Message& message) noexcept {
  VLOG(3) << "Message received from: " << senderApp;
  auto eventFunc = StreamApiClient::get(message.mType);
  if (!eventFunc) {
    LOG(ERROR)
        << "Wrong type of message ("
        << folly::get_default(
               TEnumMapFactory<thrift::MessageType>
               ::makeValuesToNamesMap(), message.mType, "UNKNOWN")
        << ") received from:" << senderApp;
    return;
  }

  auto data = eventFunc.value()(message);
  if (data) {
    sendToClients(message.mType, data.value());
  }
}

void
Streamer::sendToClients(
    const thrift::MessageType& event,
    const std::string& data) {
  auto eventStr = TEnumMapFactory<thrift::MessageType>
      ::makeValuesToNamesMap().at(event);
  VLOG(3) << "Sending event to clients: " << eventStr;
  VLOG(3) << "Sending data to clients: " << data;

  auto lockedClients = streamClients_.rlock();
  for (auto const& req : *lockedClients) {
    // StreamRequestHandler* req = kv.second;
    folly::EventBase* evb = req->getEventBase();

    if (StreamApiClient::streamContainsEvent(req->getStreamName(), event)) {
      evb->runInEventBaseThread([req, event, data]() {
        req->streamCallback(event, data);
      });
    }
  }
}

void
Streamer::connectToCtrl() {
  if (ctrlPubUrl_.empty()) {
    return;
  }

  // (re)start socket timeout timer since we're going to (re)connect
  ctrlSockTimeoutTimer_->scheduleTimeout(ctrlSockTimeout_);

  // disconnect previous connection
  VLOG(5) << "Disconnecting from controller on url '" << ctrlPubUrl_ << "'";
  auto disconnect = subSock_.disconnect(SocketUrl{ctrlPubUrl_});
  if (disconnect.hasError()) {
    LOG(ERROR) << "Error disconnecting from controller URL '" << ctrlPubUrl_
               << "': " << disconnect.error();
  }

  // reconnect to ctrlPubUrl_
  VLOG(5) << "Connecting to controller on url '" << ctrlPubUrl_ << "'";
  auto connect = subSock_.connect(fbzmq::SocketUrl{ctrlPubUrl_});
  if (connect.hasError()) {
    LOG(ERROR) << "Error connecting to controller URL '" << ctrlPubUrl_
               << "': " << connect.error();
  }
}

} // namespace api
} // namespace terragraph
} // namespace facebook
