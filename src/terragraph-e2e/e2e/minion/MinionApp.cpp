/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "MinionApp.h"

#include "e2e/common/Consts.h"

using namespace fbzmq;

using apache::thrift::detail::TEnumMapFactory;
using std::string;

namespace facebook {
namespace terragraph {
namespace minion {

DEFINE_int32(
    socket_health_check_s,
    60,
    "The periodic socket health check interval for each ZMQ thread "
    "(in seconds, 0 to disable)");

namespace {
// Counter prefix for socket health check (suffixed with thread's ZMQ ID)
const std::string kSocketHealthCheckStatPrefix{"socketMonitor.e2e_minion."};
}

MinionApp::MinionApp(
    fbzmq::Context& zmqContext,
    const string& brokerRouterUrl,
    const std::string& monitorSubmitUrl,
    const string& macAddr,
    const string& myId)
    : macAddr_(macAddr),
      myId_(myId),
      dealerSock_{zmqContext, IdentityString{myId_}} {

  // connect the dealer socket to the router socket on the Broker
  LOG(INFO) << "[" << myId_ << "] Connecting to '" << brokerRouterUrl << "'";
  if (dealerSock_.connect(SocketUrl{brokerRouterUrl}).hasError()) {
    LOG(FATAL) << "[" << myId_ << "] Error connecting to '" << brokerRouterUrl
               << "'";
  }

  zmqMonitorClient_ = std::make_shared<fbzmq::ZmqMonitorClient>(
      zmqContext, monitorSubmitUrl, myId_);

  eventClient_ = std::make_unique<EventClient>(myId_, zmqMonitorClient_);

  // check ZMQ socket health periodically
  if (FLAGS_socket_health_check_s > 0) {
    socketHealthTimeout_ = ZmqTimeout::make(this, [this]() noexcept {
      sendToMinionApp(
          myId_, thrift::MessageType::SOCKET_HEALTH_CHECK, thrift::Empty());
    });
    socketHealthTimeout_->scheduleTimeout(
      std::chrono::seconds(FLAGS_socket_health_check_s), true);
  }

  VLOG(2) << "[" << myId_ << "] Starting the main loop";

  // message on dealer socket
  addSocket(RawZmqSocketPtr{*dealerSock_}, ZMQ_POLLIN, [this](int) noexcept {
    VLOG(4) << "[" << myId_
            << "] Received a message on dealer socket from broker";
    Message senderAppMsg, thriftMsg;

    const auto recvRet = dealerSock_.recvMultiple(senderAppMsg, thriftMsg);
    if (recvRet.hasError()) {
      LOG(ERROR) << "[" << myId_
                 << "] Error reading message: " << recvRet.error();
      return;
    }

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

    VLOG(4) << "[" << myId_ << "] Received a message from " << senderApp;
    processMessage(senderApp, message.value());
  });
}

void
MinionApp::sendToMinionApp(
    const std::string& receiverId, thrift::Message msg) {
  const auto ret = dealerSock_.sendMultiple(
      Message::from(macAddr_).value(),
      Message::from(receiverId).value(),
      Message::from(myId_).value(),
      Message::fromThriftObj(msg, serializer_).value());

  if (ret.hasError()) {
    LOG(FATAL) << "Error sending "
               << TEnumMapFactory<thrift::MessageType>::
                   makeValuesToNamesMap().at(msg.mType)
               << " to :" << receiverId << " from " << myId_ << ". "
               << ret.error();
  }
}

void
MinionApp::handleInvalidMessage(
    const std::string& messageType, const std::string& senderApp) {
  LOG(ERROR) << "[" << myId_
             << "] Invalid " << messageType << " message from " << senderApp;
}

bool
MinionApp::setCounter(
    const std::string& key,
    int64_t value,
    const fbzmq::thrift::CounterValueType valueType) const {
  auto microSecTime = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::system_clock::now().time_since_epoch());
  return setCounter(key, value, valueType, microSecTime.count());
}

bool
MinionApp::setCounter(
    const std::string& key,
    int64_t value,
    const fbzmq::thrift::CounterValueType valueType,
    int64_t timestamp) const {
  // create counter object
  fbzmq::thrift::Counter counter;
  counter.value_ref() = value;
  counter.valueType_ref() = valueType;
  counter.timestamp_ref() = timestamp;
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
MinionApp::bumpCounter(const std::string& key) const {
  try {
    zmqMonitorClient_->bumpCounter(key);
  } catch (std::exception const& e) {
    LOG(ERROR) << "[" << myId_
               << "] Error reading message: " << folly::exceptionStr(e);
    return false;
  }
  return true;
}

} // namespace minion
} // namespace terragraph
} // namespace facebook
