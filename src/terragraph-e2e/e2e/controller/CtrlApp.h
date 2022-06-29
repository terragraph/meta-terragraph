/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>
#include <fbzmq/service/monitor/ZmqMonitorClient.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/Format.h>
#include <folly/MapUtil.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "e2e/common/CompressionUtil.h"
#include "e2e/common/Consts.h"
#include "e2e/common/EventClient.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/DriverMessage_types.h"
#include "e2e/if/gen-cpp2/Event_types.h"

namespace facebook {
namespace terragraph {

/**
 * Base class for E2E controller apps.
 *
 * An "app" is an event loop which serves one logical function. Events include
 * messages from other apps and timer-based actions.
 *
 * This base class abstracts all ZMQ details, and the apps themselves can focus
 * on application logic by implementing the processMessage() callback.
 */
class CtrlApp : public fbzmq::ZmqEventLoop {
 public:
  /**
   * Constructor.
   *
   * This will set up and connect sockets to the broker and ZmqMonitor instance.
   *
   * @param zmqContext the ZMQ context
   * @param routerSockUrl the broker address for the E2E controller, i.e. the
   *                      ZMQ `DEALER` socket URL to which dealerSock_ connects
   * @param monitorSubmitUrl the ZmqMonitor address for the E2E controller, i.e.
   *                         the ZMQ socket URL to which zmqMonitorClient_
   *                         connects
   * @param myId the app name (ZMQ ID)
   */
  CtrlApp(
      fbzmq::Context& zmqContext,
      const std::string& routerSockUrl,
      const std::string& monitorSubmitUrl,
      const std::string& myId);

 protected:
  /** Returns the ZMQ identity string. */
  std::string getId() const {
    return myId_;
  }

  /** Set a counter value (in ZmqMonitor). */
  bool setCounter(
      const std::string& key,
      int64_t value,
      const fbzmq::thrift::CounterValueType valueType) const;

  /** Increment a counter (in ZmqMonitor). */
  bool bumpCounter(const std::string& key) const;

  /**
   * Send an acknowledgement to the given app.
   *
   * This is typically called as a response to asynchronous SET operations.
   */
  void sendE2EAck(
      const std::string& senderApp,
      bool success,
      const std::string& message);

  /**
   * Log some details about an invalid Thrift message received, and optionally
   * send an acknowledgement using sendE2EAck().
   */
  void handleInvalidMessage(
      const std::string& messageType,
      const std::string& senderApp,
      const std::string& minion = "",
      bool sendAck = true);

  /** Send the given Thrift object to an E2E controller app over dealerSock_. */
  template <class T>
  void
  sendToCtrlApp(
      std::string receiverId,
      thrift::MessageType mType,
      const T& obj,
      bool compress = false) {
    thrift::Message msg;
    msg.mType = mType;
    msg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);
    if (compress) {
      CompressionUtil::compress(msg);
    }

    auto ret = dealerSock_.sendMultiple(
        fbzmq::Message(),
        fbzmq::Message::from(receiverId).value(),
        fbzmq::Message::from(myId_).value(),
        fbzmq::Message::fromThriftObj(msg, serializer_).value());

    if (ret.hasError()) {
      LOG(FATAL) << "Error sending "
                 << apache::thrift::detail::TEnumMapFactory<thrift::MessageType>
                        ::makeValuesToNamesMap().at(mType)
                 << " to :" << receiverId << " from " << myId_ << ret.error();
    }
  }

  /** Send the given Thrift object to an E2E minion app over dealerSock_. */
  template <class T>
  void
  sendToMinionApp(
      std::string minionZmqId,
      std::string receiverId,
      thrift::MessageType mType,
      const T& obj,
      bool compress = false) {
    thrift::Message msg;
    msg.mType = mType;
    msg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);
    if (compress) {
      CompressionUtil::compress(msg);
    }

    auto ret = dealerSock_.sendMultiple(
        fbzmq::Message::from(minionZmqId).value(),
        fbzmq::Message::from(receiverId).value(),
        fbzmq::Message::from(myId_).value(),
        fbzmq::Message::fromThriftObj(msg, serializer_).value());

    if (ret.hasError()) {
      LOG(FATAL) << "Error sending "
                 << apache::thrift::detail::TEnumMapFactory<thrift::MessageType>
                    ::makeValuesToNamesMap().at(mType)
                 << " to "
                 << minionZmqId << ":" << receiverId << " from " << myId_
                 << ". " << ret.error();
    }
  }

  /**
   * Send the given Thrift object to the event streaming address over
   * dealerSock_.
   */
  template <class T>
  void
  sendToApiStream(
      thrift::MessageType mType,
      const T& obj,
      bool compress = false) {
    sendToCtrlApp(
        E2EConsts::kApiEventSubId,
        mType,
        obj,
        compress);
  }

  /**
   * Decode the binary Thrift object contained within the given thrift::Message.
   *
   * Upon failure, this returns std::nullopt.
   */
  template <class T>
  std::optional<T>
  maybeReadThrift(const thrift::Message& message) {
    try {
      return fbzmq::util::readThriftObjStr<T>(message.value, serializer_);
    } catch (const std::exception& e) {
      return std::nullopt;
    }
  }

  /** The event client. */
  std::unique_ptr<EventClient> eventClient_;

  /** All messages are serialized using this serializer. */
  apache::thrift::CompactSerializer serializer_;

 private:
  /**
   * Function invoked when any message is available for the app.
   * @param minion if the sender was an E2E minion instance, this contains the
   *               minion's node ID (MAC address), otherwise empty
   * @param senderApp the sender's ZMQ ID
   * @param message the Thrift message object
   */
  virtual void processMessage(
      const std::string& minion /* node mac */,
      const std::string& senderApp,
      const thrift::Message& message) noexcept = 0;

  /** The app name (ZMQ ID). */
  std::string myId_{};

  /** The ZMQ `DEALER` socket to talk to the broker. */
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> dealerSock_;

  /** Client to interact with the E2E controller's ZmqMonitor instance. */
  std::shared_ptr<fbzmq::ZmqMonitorClient> zmqMonitorClient_;

  /** Socket health timer. */
  std::unique_ptr<fbzmq::ZmqTimeout> socketHealthTimeout_{nullptr};
};

} // namespace terragraph
} // namespace facebook
