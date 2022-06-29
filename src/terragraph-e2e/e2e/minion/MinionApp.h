/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>

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

using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {
namespace minion {

/**
 * Base class for E2E minion apps.
 *
 * An "app" is an event loop which serves one logical function. Events include
 * messages from other apps and timer-based actions.
 *
 * This base class abstracts all ZMQ details, and the apps themselves can focus
 * on application logic by implementing the processMessage() callback.
 */
class MinionApp : public fbzmq::ZmqEventLoop {
 public:
  /**
   * Constructor.
   *
   * This will set up and connect sockets to the broker and ZmqMonitor instance.
   *
   * @param zmqContext the ZMQ context
   * @param brokerRouterUrl the broker address for the E2E minion, i.e. the ZMQ
   *                        `DEALER` socket URL to which dealerSock_ connects
   * @param monitorSubmitUrl the ZmqMonitor address for the E2E minion, i.e. the
   *                         ZMQ socket URL to which zmqMonitorClient_ connects
   * @param macAddr our MAC address
   * @param myId the app name (ZMQ ID)
   */
  MinionApp(
      fbzmq::Context& zmqContext,
      const std::string& brokerRouterUrl,
      const std::string& monitorSubmitUrl,
      const std::string& macAddr,
      const std::string& myId);

  /** Returns the ZMQ identity string. */
  std::string
  getId() const {
    return myId_;
  }

  virtual ~MinionApp() = default;

 protected:
  /** Wrapper for thrift::DriverMessage with the object value deserialized. */
  template <class T>
  struct DriverMessageWrapper {
    /** The radio MAC address. */
    std::string radioMac;
    /** The deserialized Thrift object. */
    T value;

    /** Returns a prefix string "<radioMac> " if set, else an empty string. */
    std::string macPrefix() {
      return radioMac.empty() ? "" : "<" + radioMac + "> ";
    }
  };

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
    } catch (const std::exception& ex) {
      LOG(ERROR)
          << "Could not read "
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                 message.mType, "UNKNOWN");
      return std::nullopt;
    }
  }

  /**
   * Decode the binary thrift::DriverMessage object contained within the given
   * thrift::Message.
   *
   * Upon failure, this returns std::nullopt.
   */
  template <class T>
  std::optional<DriverMessageWrapper<T>>
  maybeReadDriverMessage(const thrift::Message& message) {
    if (auto driverMsg = maybeReadThrift<thrift::DriverMessage>(message)) {
      try {
        DriverMessageWrapper<T> wrapper;
        wrapper.radioMac = driverMsg->radioMac;
        wrapper.value =
            fbzmq::util::readThriftObjStr<T>(driverMsg->value, serializer_);
        return wrapper;
      } catch (const std::exception& ex) {
        LOG(ERROR)
            << "Could not read "
            << folly::get_default(
                   TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                   message.mType, "UNKNOWN");
      }
    }
    return std::nullopt;
  }

  /** Send the given Thrift object to an E2E controller app over dealerSock_. */
  template <class T>
  void
  sendToCtrlApp(
      const std::string& receiverId,
      thrift::MessageType mType,
      const T& obj,
      bool compress = false) {
    thrift::Message msg;
    msg.mType = mType;
    msg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);
    if (compress) {
      CompressionUtil::compress(msg);
    }

    const auto ret = dealerSock_.sendMultiple(
        fbzmq::Message(),
        fbzmq::Message::from(receiverId).value(),
        fbzmq::Message::from(myId_).value(),
        fbzmq::Message::fromThriftObj(msg, serializer_).value());

    if (ret.hasError()) {
      LOG(FATAL) << "Error sending "
                 << TEnumMapFactory<thrift::MessageType>::
                     makeValuesToNamesMap().at(mType)
                 << " to :" << receiverId << " from " << myId_ << ret.error();
    }
  }

  /** Log some details about an invalid Thrift message received. */
  void handleInvalidMessage(
      const std::string& messageType, const std::string& senderApp);

  /** Send the given Thrift object to an E2E minion app over dealerSock_. */
  template <class T>
  void
  sendToMinionApp(
      const std::string& receiverId,
      thrift::MessageType mType,
      const T& obj) {
    thrift::Message msg;
    msg.mType = mType;
    msg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);
    sendToMinionApp(receiverId, msg);
  }

  /** Send the given thrift::Message to an E2E minion app over dealerSock_. */
  void sendToMinionApp(
      const std::string& receiverId, thrift::Message msg);

  /** Send the given Thrift object to DriverApp over dealerSock_. */
  template <class T>
  void
  sendToDriverApp(
      const std::string& radioMac,
      thrift::MessageType mType,
      const T& obj) {
    thrift::DriverMessage driverMsg;
    driverMsg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);
    driverMsg.radioMac = radioMac;
    sendToMinionApp(E2EConsts::kDriverAppMinionId, mType, driverMsg);
  }

  /**
   * Send the given Thrift object to the broadcast address over dealerSock_.
   */
  template <class T>
  void
  sendToBroadcastSock(thrift::MessageType mType, const T& obj) {
    sendToMinionApp(E2EConsts::kBroadcastSockMinionId, mType, obj);
  }

  /**
   * Send the given thrift::Message to the broadcast address over dealerSock_.
   */
  void
  sendToBroadcastSock(thrift::Message msg) {
    sendToMinionApp(E2EConsts::kBroadcastSockMinionId, msg);
  }

  /**
   * Set a counter value (in ZmqMonitor) using the current system time as the
   * timestamp.
   */
  bool setCounter(
      const std::string& key,
      int64_t value,
      const fbzmq::thrift::CounterValueType valueType) const;

  /** Set a counter value (in ZmqMonitor) using a given timestamp. */
  bool setCounter(
      const std::string& key,
      int64_t value,
      const fbzmq::thrift::CounterValueType valueType,
      int64_t timestamp) const;

  /** Increment a counter (in ZmqMonitor). */
  bool bumpCounter(const std::string& key) const;

  /** Our MAC address (node ID). */
  const std::string macAddr_{};

  /** The event client. */
  std::unique_ptr<EventClient> eventClient_;

  /** All messages are serialized using this serializer. */
  apache::thrift::CompactSerializer serializer_{};

 private:
  /**
   * Function invoked when any message is available for the app.
   * @param senderApp the sender's ZMQ ID
   * @param message the Thrift message object
   */
  virtual void processMessage(
      const std::string& senderApp,
      const thrift::Message& message) noexcept = 0;

  /** The app name (ZMQ ID). */
  const std::string myId_{};

  /** The ZMQ `DEALER` socket to talk to the broker. */
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> dealerSock_;

  /** Client to interact with the E2E minion's ZmqMonitor instance. */
  std::shared_ptr<fbzmq::ZmqMonitorClient> zmqMonitorClient_;

  /** Socket health timer. */
  std::unique_ptr<fbzmq::ZmqTimeout> socketHealthTimeout_{nullptr};
};

} // namespace minion
} // namespace terragraph
} // namespace facebook
