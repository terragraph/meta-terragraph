/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "StatsSubscriber.h"

#include <chrono>
#include <glog/logging.h>

using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {

DEFINE_string(
    driver_if_socket_id,
    "driver-if_led_agent",
    "ZMQ identity to use for driver-if stats publisher");
DEFINE_string(
    driver_if_pub_url,
    "tcp://[::1]:18990",
    "ZMQ URL for driver-if stats publisher");

StatsSubscriber::StatsSubscriber(
    fbzmq::Context& context,
    fbzmq::ZmqEventLoop& loop,
    std::function<void(const fbzmq::thrift::CounterMap&)> callback)
    : driverIfSock_(context, fbzmq::IdentityString(FLAGS_driver_if_socket_id)),
      callback_(std::move(callback)) {

  auto res = driverIfSock_.connect(fbzmq::SocketUrl{FLAGS_driver_if_pub_url});
  if (res.hasError()) {
    LOG(FATAL) << "Unable to connect to driver-if socket";
    return;
  }
  driverIfSock_.setSockOpt(ZMQ_SUBSCRIBE, "", 0).value();

  loop.addSocket(
      fbzmq::RawZmqSocketPtr{*driverIfSock_}, ZMQ_POLLIN, [this](int) noexcept {
        auto res = driverIfSock_.recvThriftObj<fbzmq::thrift::MonitorPub>(
            serializer_, std::chrono::milliseconds{1000});
        if (res.hasError()) {
          LOG(ERROR) << "Error reading message: " << res.error();
          return;
        }

        auto& message = res.value();
        auto pubTypeMap =
            TEnumMapFactory<fbzmq::thrift::PubType> ::makeValuesToNamesMap();
        VLOG(4) << "Received publication of type: "
                << folly::get_default(
                       pubTypeMap,
                       message.pubType_ref().value(),
                       "UNKNOWN");
        switch (message.pubType_ref().value()) {
          case fbzmq::thrift::PubType::COUNTER_PUB:
            callback_(message.counterPub_ref().value().counters_ref().value());
            break;
          default:
            VLOG(2) << "Skip unexpected publication of type: "
                    << folly::get_default(
                           pubTypeMap,
                           message.pubType_ref().value(),
                           "UNKNOWN");
        }
      });
}

} // namespace terragraph
} // namespace facebook
