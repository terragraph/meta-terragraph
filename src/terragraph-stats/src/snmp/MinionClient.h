/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "StatCache.h"

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
 * Maintain a connection to minion to fetch link status mappings.
 *
 * Periodically fetch link status from minion to build mappings from key names
 * to LinkMetric meta-data.
 */
class MinionClient final : public fbzmq::ZmqEventLoop {
 public:
  MinionClient(
      fbzmq::Context& context, const std::vector<StatFormat>& statsFormat);
  ~MinionClient(){};

 private:
  /**
   * Process message from e2e minion socket.
   */
  void processMessage(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /**
   * Request GET_LINK_STATUS_DUMP from e2e minion socket.
   */
  void requestLinkStatusDump();

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

  fbzmq::Context& context_;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> minionSock_;
  std::unique_ptr<fbzmq::ZmqTimeout> linkStatusTimer_{nullptr};
  std::vector<StatFormat> statsFormat_;
  apache::thrift::CompactSerializer serializer_{};
};

} // namespace terragraph
} // namespace facebook
