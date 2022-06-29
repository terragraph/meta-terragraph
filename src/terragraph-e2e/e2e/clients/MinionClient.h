/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <vector>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/async/ZmqTimeout.h>
#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/io/async/EventBase.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {

/**
 * Interface to E2E Minion API.
 *
 * Allows making requests to the minion socket and receiving/parsing thrift
 * messages.
 */
class MinionClient final : public fbzmq::ZmqEventLoop {
 public:
  MinionClient(fbzmq::Context& context);
  ~MinionClient(){};

  /* Methods for sending API requests to E2E minion */

  /**
   * Send SetLinkStatus request to minion to assoc or disassoc a link
   * Returns true if request was sent
   */
  bool sendSetLinkStatus(
      const thrift::LinkStatusType& linkStatusType,
      const std::string& initiatorMac,
      const std::string& responderMac);

  /** Request LinkStatusDump */
  std::optional<thrift::LinkStatusDump> getLinkStatusDump();
  /** Request StatusReport */
  std::optional<thrift::StatusReport> getStatusReport();
  /** Request NodeConfig */
  std::optional<thrift::GetMinionConfigResp> getNodeConfig();
  /** Set NodeConfig */
  bool setNodeConfig(const std::string& nodeConfig);
  /** Request ScanResp */
  std::optional<thrift::ScanResp> getTopoScan(const std::string& radioMac);
  /** Send reboot command */
  bool sendRebootCmd(bool force = false, int secondsToReboot = 5);

 private:
  int64_t nowInMs();
  std::string generateZmqId();
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

  /* E2E minion API request methods */

  /**
   * Send message to minion socket using specified app receiver id.
   * Returns true when message is sent.
   */
  bool apiCallRequest(
      const std::string& receiverId, const thrift::Message& msg);

  /**
   * Receive thrift Message type from minion socket
   */
  std::optional<thrift::Message> recvThriftMsg();

  /**
   * Helper function to send an API request with an empty ThriftRequestType
   * object and receive a ThriftResponseType object.
   */
  template <class ThriftRequestType, class ThriftResponseType>
  std::optional<ThriftResponseType> apiCall(
      const std::string& receiverId, const thrift::MessageType& mType);

  /**
   * Helper function to send an API request with a constructed Message and
   * receive a ThriftResponseType object
   */
  template <class ThriftResponseType>
  std::optional<ThriftResponseType> apiCall(
      const std::string& receiverId, const thrift::Message& msg);

  /**
   * Wait for a response from the minion pub socket for a specific message type
   * or until the given timeout has been reached.
   */
  template <class ThriftResponseType>
  std::optional<ThriftResponseType> waitForMinionPublisherResponse(
      const thrift::MessageType& msgType, const int timeoutMs);

  /** ZMQ context for minion sockets */
  fbzmq::Context& context_;
  /** ZMQ identity for minion sockets */
  std::string zmqId_;
  /** ZMQ minion socket */
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> minionSock_;
  /** Thrfft serializer */
  apache::thrift::CompactSerializer serializer_{};
};

} // namespace terragraph
} // namespace facebook
