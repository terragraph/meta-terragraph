/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/zmq/Zmq.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "stats/if/gen-cpp2/Aggregator_types.h"

// Send the object from an aggregator app
// Can send objects to agents or other aggr apps
// Requires aggr Broker to be running
template <typename T, typename Serializer>
bool
sendInAggrApp(
    fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>& sock,
    const std::string& agentName,
    const std::string& receiverId,
    const std::string& senderId,
    T obj,
    Serializer* serializer) {

  auto res = sock.sendMultiple(
      fbzmq::Message::from(agentName).value(),
      fbzmq::Message::from(receiverId).value(),
      fbzmq::Message::from(senderId).value(),
      fbzmq::Message::fromThriftObj(obj, *serializer).value());
  if (res.hasError()) {
    LOG(ERROR) << "Error sending msg to :" << receiverId << " from " << senderId
               << ". " << res.error();
    return false;
  }

  return true;
}

// Receive an object in an aggregator app.
// Can receive objects both from agent and other aggr apps.
// Requires Aggr Broker to be running
template <typename Socket, typename Serializer>
std::tuple<std::string, std::string, facebook::terragraph::thrift::AggrMessage>
recvInAggrApp(
    Socket& sock,
    Serializer* serializer) {

  fbzmq::Message msg1, msg2, msg3;
  sock.recvMultiple(msg1, msg2, msg3).value();

  return std::make_tuple(
      msg1.read<std::string>().value(),
      msg2.read<std::string>().value(),
      msg3.readThriftObj<facebook::terragraph::thrift::AggrMessage>(
          *serializer).value());
}

// Send the object from agent
// Imitates sending from agent
template <typename T, typename Serializer>
bool
sendFromAgent(
    fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>& sock,
    const std::string& receiverId,
    const std::string& senderId,
    T obj,
    Serializer* serializer) {

  auto res = sock.sendMultiple(
      fbzmq::Message::from(receiverId).value(),
      fbzmq::Message::from(senderId).value(),
      fbzmq::Message::fromThriftObj(obj, *serializer).value());
  if (res.hasError()) {
    LOG(ERROR) << "Error sending msg to :" << receiverId << " from " << senderId
               << ". " << res.error();
    return false;
  }

  return true;
}
