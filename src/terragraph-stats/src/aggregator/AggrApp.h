/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/zmq/Zmq.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "../common/CompressionUtil.h"
#include "stats/if/gen-cpp2/Aggregator_types.h"

using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {
namespace stats {

/* This class abstracts all zmq / timer details for the apps.
 * An Aggregator app has to just inherit from this class and implement
 * processMessage().
 * The app classes themselves need not have to maintain any zmq sockets
 * or to poll zmq to schedule timeouts.  They can instead just focus on
 * the application logic.
 * processMessage() callback will be called when there is a message
 * available.
 */
class AggrApp : public fbzmq::ZmqEventLoop {

 public:
  AggrApp(
      fbzmq::Context& context,
      std::string routerSockUrl,
      std::string myId);

 protected:

  // send an ack (typically as response to asynchronous SET_ operations)
  void sendAggrAck(
      const std::string& senderApp, bool success, const std::string& message);

  // log an invalid thrift message and optionally send an ack
  void handleInvalidMessage(
      const std::string& aggrMessageType,
      const std::string& senderApp,
      const std::string& agent = "",
      bool sendAck = true);

  // Send the object to another aggregator app.
  template <class ThriftType>
  void
  sendToAggrApp(
      std::string receiverId,
      thrift::AggrMessageType mType,
      ThriftType obj,
      bool compress = false) {
    thrift::AggrMessage msg;
    msg.mType = mType;
    msg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);
    if (compress) {
      CompressionUtil::compress(msg);
    }

    auto res = dealerSock_.sendMultiple(
        fbzmq::Message::from(std::string("")).value(),
        fbzmq::Message::from(receiverId).value(),
        fbzmq::Message::from(myId_).value(),
        fbzmq::Message::fromThriftObj(msg, serializer_).value());
    if (res.hasError()) {
      LOG(FATAL) << "Error sending "
                 << TEnumMapFactory<thrift::AggrMessageType>::
                     makeValuesToNamesMap().at(mType)
                 << " to :" << receiverId << " from " << myId_ << ". "
                 << res.error();
    }
  }

  // Send the object to an agent
  template <class ThriftType>
  void
  sendToAgentApp(
      std::string agentZmqId,
      std::string receiverId,
      thrift::AggrMessageType mType,
      ThriftType obj,
      bool compress = false) {
    thrift::AggrMessage msg;
    msg.mType = mType;
    msg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);
    if (compress) {
      CompressionUtil::compress(msg);
    }

    auto res = dealerSock_.sendMultiple(
        fbzmq::Message::from(agentZmqId).value(),
        fbzmq::Message::from(receiverId).value(),
        fbzmq::Message::from(myId_).value(),
        fbzmq::Message::fromThriftObj(msg, serializer_).value());
    if (res.hasError()) {
      LOG(FATAL) << "Error sending "
                 << TEnumMapFactory<thrift::AggrMessageType>::
                     makeValuesToNamesMap().at(mType) << " to "
                 << agentZmqId << ":" << receiverId << " from " << myId_ << ". "
                 << res.error();
    }
  }

  // Try reading a Thrift struct out of an AggrMessage.
  // On success, return it (wrapped in a std::optional<T>).
  // On failure, return std::nullopt
  template <class T>
  std::optional<T>
  maybeReadThrift(const thrift::AggrMessage& message) {
    try {
      return fbzmq::util::readThriftObjStr<T>(message.value, serializer_);
    } catch (const std::exception& e) {
      return std::nullopt;
    }
  }

  // The serializer for all the messages
  apache::thrift::CompactSerializer serializer_;

 private:
  // This function will be called when any message is available for the app.
  // agent uses mac as its zmq id, here agent == node mac
  virtual void processMessage(
      const std::string& agent, /* node mac */
      const std::string& senderApp,
      const thrift::AggrMessage& message) noexcept = 0;

  // The zmq socket to talk to the broker
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> dealerSock_;

  // The zmq id of the app.
  const std::string myId_;
};

} // namesapce stats
} // namespace terragraph
} // namespace facebook
