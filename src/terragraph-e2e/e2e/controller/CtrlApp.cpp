/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "CtrlApp.h"

#include <chrono>

#include <fbzmq/service/monitor/ZmqMonitor.h>
#include <fbzmq/zmq/Zmq.h>

#include "SharedObjects.h"
#include "e2e/common/Consts.h"

using namespace fbzmq;
using std::string;

DEFINE_int32(
    socket_health_check_s,
    60,
    "The periodic socket health check interval for each ZMQ thread "
    "(in seconds, 0 to disable)");

namespace {
// Counter prefix for socket health check (suffixed with thread's ZMQ ID)
const std::string kSocketHealthCheckStatPrefix{"socketMonitor.e2e_controller."};
}

namespace facebook {
namespace terragraph {

CtrlApp::CtrlApp(
    fbzmq::Context& zmqContext,
    const string& routerSockUrl,
    const std::string& monitorSubmitUrl,
    const string& myId)
    : myId_(myId), dealerSock_{zmqContext, IdentityString{myId_}} {

  // connect the dealer socket to the router socket on the Broker
  LOG(INFO) << "[" << myId_ << "] Connecting to '" << routerSockUrl << "'";

  if (dealerSock_.connect(SocketUrl{routerSockUrl}).hasError()) {
    LOG(FATAL) << "[" << myId_ << "] Error connecting to '" << routerSockUrl
               << "'";
  }

  zmqMonitorClient_ = std::make_shared<fbzmq::ZmqMonitorClient>(
      zmqContext, monitorSubmitUrl, myId_);

  eventClient_ = std::make_unique<EventClient>(myId_, zmqMonitorClient_);
  eventClient_->setTopologyNameFunc([]() {
    // Dynamically return the topology name (since it could change)
    return *(SharedObjects::getTopologyName()->rlock());
  });

  // check ZMQ socket health periodically
  if (FLAGS_socket_health_check_s > 0) {
    socketHealthTimeout_ = ZmqTimeout::make(this, [this]() noexcept {
      sendToCtrlApp(
          myId_, thrift::MessageType::SOCKET_HEALTH_CHECK, thrift::Empty());
    });
    socketHealthTimeout_->scheduleTimeout(
      std::chrono::seconds(FLAGS_socket_health_check_s), true);
  }

  // message on dealer socket (from broker)
  addSocket(RawZmqSocketPtr{*dealerSock_}, ZMQ_POLLIN, [this](int) noexcept {
    VLOG(6) << "[" << myId_
            << "] Received a message on dealer socket from broker";
    Message senderAppMsg, minionMsg, thriftMsg;

    const auto recvRet =
        dealerSock_.recvMultiple(minionMsg, senderAppMsg, thriftMsg);
    if (recvRet.hasError()) {
      LOG(ERROR) << "[" << myId_
                 << "] Error receiving message: " << recvRet.error();
      return;
    }

    const auto minion = minionMsg.read<std::string>().value();
    const auto senderApp = senderAppMsg.read<std::string>().value();
    auto message = thriftMsg.readThriftObj<thrift::Message>(serializer_);
    if (message.hasError()) {
      LOG(ERROR) << "[" << myId_
                 << "] Error reading message: " << message.error();
      return;
    }

    // decompress the message (if needed)
    std::string error;
    if (!CompressionUtil::decompress(message.value(), error)) {
      LOG(ERROR) << "[" << myId_ << "] " << error;
      return;
    }

    // is this a socket health check message?
    if (message->mType == thrift::MessageType::SOCKET_HEALTH_CHECK) {
      VLOG(6) << "[" << myId_ << "] ZMQ socket is alive.";
      bumpCounter(kSocketHealthCheckStatPrefix + myId_);
      return;
    }

    VLOG(4) << "[" << myId_ << "] Received a message from " << minion << ":"
            << senderApp;
    processMessage(minion, senderApp, message.value());
  });
}

bool
CtrlApp::setCounter(
    const std::string& key,
    int64_t value,
    const fbzmq::thrift::CounterValueType valueType) const {
  // create counter object
  fbzmq::thrift::Counter counter;
  counter.value_ref() = value;
  counter.valueType_ref() = valueType;
  try {
    zmqMonitorClient_->setCounter(key, counter);
  } catch (std::exception const& e) {
    LOG(ERROR) << "[" << myId_
               << "] Error sending message: " << folly::exceptionStr(e);
    return false;
  }
  return true;
}

bool
CtrlApp::bumpCounter(const std::string& key) const {
  try {
    zmqMonitorClient_->bumpCounter(key);
  } catch (std::exception const& e) {
    LOG(ERROR) << "[" << myId_
               << "] Error sending message: " << folly::exceptionStr(e);
    return false;
  }
  return true;
}

void
CtrlApp::sendE2EAck(
    const std::string& senderApp,
    bool success,
    const std::string& message) {
  thrift::E2EAck e2EAck;
  e2EAck.success = success;
  e2EAck.message = message;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::E2E_ACK,
      e2EAck);
}

void
CtrlApp::handleInvalidMessage(
    const std::string& messageType,
    const std::string& senderApp,
    const std::string& minion,
    bool sendAck) {
  LOG(ERROR) << "[" << myId_
             << "] Invalid " << messageType << " message from " << minion
             << ":" << senderApp;
  if (sendAck) {
    sendE2EAck(senderApp, false, "Could not read " + messageType);
  }
}

} // namespace terragraph
} // namespace facebook
