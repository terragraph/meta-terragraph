/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "MinionApp.h"

namespace facebook {
namespace terragraph {
namespace minion {

/**
 * App that handles communications with the driver interface (driver-if).
 */
class DriverApp final : public MinionApp {
 public:
  /**
   * Constructor.
   *
   * This will set up and connect sockets to the driver interface (driver-if).
   *
   * @param zmqContext the ZMQ context
   * @param brokerRouterUrl the broker address for the E2E minion
   * @param monitorSockUrl the ZmqMonitor address for the E2E minion
   * @param driverPairSockUrl the driver interface address, i.e. the ZMQ `PAIR`
   *                          socket URL to which pairSock_ connects
   * @param macAddr our MAC address
   */
  DriverApp(
      fbzmq::Context& zmqContext,
      const std::string& brokerRouterUrl,
      const std::string& monitorSockUrl,
      const std::string& driverPairSockUrl,
      const std::string& macAddr);

  void processMessage(
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

 private:
  /** \{ */
  DriverApp(const DriverApp&) = delete;
  DriverApp& operator=(const DriverApp&) = delete;
  /** \} */

  /** Receive a HELLO message from StatusApp and echo it back. */
  void processHello(const std::string& senderApp);

  /** Forward the given message to driver-if via pairSock_. */
  void sendToDriverIf(const thrift::Message& message);

  /** Wrap the given message in a thrift::Message and thrift::DriverMessage. */
  template <class T>
  thrift::Message
  createDriverMessage(
      const std::string& radioMac,
      thrift::MessageType mType,
      const T& obj) const {
    thrift::DriverMessage driverMsg;
    driverMsg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);
    driverMsg.radioMac = radioMac;
    thrift::Message msg;
    msg.mType = mType;
    msg.value = fbzmq::util::writeThriftObjStr(driverMsg, serializer_);
    return msg;
  }

  /** The ZMQ `PAIR` socket to talk to the driver interface. */
  fbzmq::Socket<ZMQ_PAIR, fbzmq::ZMQ_CLIENT> pairSock_;
};

} // namespace minion
} // namespace terragraph
} // namespace facebook
