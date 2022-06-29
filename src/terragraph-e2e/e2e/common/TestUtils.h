/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/zmq/Zmq.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/Topology_types.h"

// Send the object from a controller app
// Used in minion ut to imitates sending message in controller broker
template <typename T, typename Serializer>
void
sendInCtrlBroker(
    fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER>& sock,
    const std::string& minionName,
    const std::string& receiverId,
    const std::string& senderId,
    T obj,
    Serializer& serializer) {
  const auto ret = sock.sendMultiple(
      fbzmq::Message::from(minionName).value(),
      fbzmq::Message::from(receiverId).value(),
      fbzmq::Message::from(senderId).value(),
      fbzmq::Message::fromThriftObj(obj, serializer).value());
  CHECK(ret) << "Error sending msg to :" << receiverId << " from " << senderId
             << ret.error();
}

// Receive an object in controller broker.
// Used in minion ut to imitates receiving message in controller broker
template <typename Serializer>
std::tuple<
    std::string,
    std::string,
    std::string,
    facebook::terragraph::thrift::Message>
recvInCtrlBroker(
    fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER>& sock,
    Serializer& serializer) {
  fbzmq::Message msg1, msg2, msg3, msg4;
  const auto ret = sock.recvMultiple(msg1, msg2, msg3, msg4);
  CHECK(ret) << "Error receiving msg " << ret.error();
  auto f1 = msg1.read<std::string>();
  auto f2 = msg2.read<std::string>();
  auto f3 = msg3.read<std::string>();
  auto f4 =
      msg4.readThriftObj<facebook::terragraph::thrift::Message>(serializer);
  return std::make_tuple(
      std::move(*f1), std::move(*f2), std::move(*f3), std::move(*f4));
}

// Send the object from a controller app
// Cand send objects to both minion and other ctrl apps
// Requires Ctrl Broker to be running (or CtrlFixture)
template <typename T, typename Serializer>
void
sendInCtrlApp(
    fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>& sock,
    const std::string& minionName,
    const std::string& receiverId,
    const std::string& senderId,
    T obj,
    Serializer& serializer) {
  const auto ret = sock.sendMultiple(
      fbzmq::Message::from(minionName).value(),
      fbzmq::Message::from(receiverId).value(),
      fbzmq::Message::from(senderId).value(),
      fbzmq::Message::fromThriftObj(obj, serializer).value());
  CHECK(ret) << "Error sending msg to :" << receiverId << " from " << senderId
             << ret.error();
}

// Receive an object in a controller app.
// Can receive objects both from minion and other ctrl apps.
// Requires Ctrl Broker to be running (or CtrlFixture)
template <typename Serializer>
std::tuple<std::string, std::string, facebook::terragraph::thrift::Message>
recvInCtrlApp(
    fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>& sock,
    Serializer& serializer) {
  fbzmq::Message msg1, msg2, msg3;
  const auto ret = sock.recvMultiple(msg1, msg2, msg3);
  CHECK(ret) << "Error receiving msg " << ret.error();
  auto f1 = msg1.read<std::string>();
  auto f2 = msg2.read<std::string>();
  auto f3 =
      msg3.readThriftObj<facebook::terragraph::thrift::Message>(serializer);
  return std::make_tuple(std::move(*f1), std::move(*f2), std::move(*f3));
}

// Send the object from minion broker
// Used in controller ut to imitates sending message in minion broker
template <typename T, typename Serializer>
void
sendInMinionBroker(
    fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>& sock,
    const std::string& receiverId,
    const std::string& senderId,
    T obj,
    Serializer& serializer) {
  const auto ret = sock.sendMultiple(
      fbzmq::Message::from(receiverId).value(),
      fbzmq::Message::from(senderId).value(),
      fbzmq::Message::fromThriftObj(obj, serializer).value());
  CHECK(ret) << "Error sending msg to :" << receiverId << " from " << senderId
             << ret.error();
}

// Receive the object in minion broker
// Used in controller ut to imitates receiving message in minion broker
template <typename Serializer>
std::tuple<std::string, std::string, facebook::terragraph::thrift::Message>
recvInMinionBroker(
    fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>& sock,
    Serializer& serializer) {
  fbzmq::Message msg1, msg2, msg3;
  const auto ret = sock.recvMultiple(msg1, msg2, msg3);
  CHECK(ret) << "Error receiving msg " << ret.error();
  auto f1 = msg1.read<std::string>();
  auto f2 = msg2.read<std::string>();
  auto f3 =
      msg3.readThriftObj<facebook::terragraph::thrift::Message>(serializer);
  return std::make_tuple(std::move(*f1), std::move(*f2), std::move(*f3));
}

// Send the object from a minion app
// Can send objects to controller and other minion apps
// Requires Minion Broker to be running (or MinionFixture)
template <typename T, typename Serializer>
void
sendInMinionApp(
    fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>& sock,
    const std::string& minionName,
    const std::string& receiverId,
    const std::string& senderId,
    T obj,
    Serializer& serializer) {
  const auto ret = sock.sendMultiple(
      fbzmq::Message::from(minionName).value(),
      fbzmq::Message::from(receiverId).value(),
      fbzmq::Message::from(senderId).value(),
      fbzmq::Message::fromThriftObj(obj, serializer).value());
  CHECK(ret) << "Error sending msg to :" << receiverId << " from " << senderId
             << ret.error();
}

// Receive the object in minion app
// Cand receive objects from controller and other minion apps
// Requires Minion Broker to be running (or MinionFixture)
template <typename Serializer>
std::pair<std::string, facebook::terragraph::thrift::Message>
recvInMinionApp(
    fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>& sock,
    Serializer& serializer) {
  fbzmq::Message msg1, msg2;
  const auto ret = sock.recvMultiple(msg1, msg2);
  CHECK(ret) << "Error receiving msg " << ret.error();
  auto f1 = msg1.read<std::string>();
  auto f2 =
      msg2.readThriftObj<facebook::terragraph::thrift::Message>(serializer);
  return {std::move(*f1), std::move(*f2)};
}

facebook::terragraph::thrift::Node createNode(
    const std::string& nodeName,
    const std::string& nodeMac,
    const std::string& siteName = "",
    const bool popNode = false,
    const facebook::terragraph::thrift::NodeStatusType status =
        facebook::terragraph::thrift::NodeStatusType::OFFLINE,
    const facebook::terragraph::thrift::NodeType nodeType =
        facebook::terragraph::thrift::NodeType::DN,
    const std::vector<std::string>& wlanMacs = std::vector<std::string>());

facebook::terragraph::thrift::Site createSite(
    const std::string& siteName,
    const float latitude,
    const float longitude,
    const float altitude,
    const float accuracy);

std::string getLinkName(
    const facebook::terragraph::thrift::Node& aNode,
    const facebook::terragraph::thrift::Node& zNode);

void bumpLinkupAttempts(
    facebook::terragraph::thrift::Topology& topology,
    const std::string linkName);

// TODO: Accept a_node_mac and z_node_mac as well for multi-MAC nodes.
facebook::terragraph::thrift::Link createLink(
    const facebook::terragraph::thrift::Node& aNode,
    const facebook::terragraph::thrift::Node& zNode);

facebook::terragraph::thrift::Topology createTopology(
    const std::vector<facebook::terragraph::thrift::Node>& nodes,
    const std::vector<facebook::terragraph::thrift::Link>& links,
    const std::vector<facebook::terragraph::thrift::Site>& sites);

facebook::terragraph::thrift::IgnitionCandidate createIgCandidate(
    const facebook::terragraph::thrift::Node& initiatorNode,
    const facebook::terragraph::thrift::Link& link);

// Create thrift::Topology and mark pop nodes appropriately
facebook::terragraph::thrift::Topology createTopology(
    const int32_t numNodes,
    const std::vector<int32_t> popNodeNums,
    const std::vector<std::pair<int32_t, int32_t>>& linkIds,
    const int32_t numSites = 0,
    const std::vector<std::pair<int32_t, int32_t>>& nodeSiteMap = {},
    const std::vector<int32_t>& cnNodeNums = {});
