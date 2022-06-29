/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/service/monitor/ZmqMonitorClient.h>
#include <fbzmq/zmq/Zmq.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "../Broker.h"
#include "../ConfigHelper.h"
#include "e2e/if/gen-cpp2/Controller_types.h"

// A common fixture which can be used for controller app unittests.
// The fixture takes care of creating/destroying the Broker.
// It also provides some helpful methods for the apps.
class CtrlFixture : public ::testing::Test {

 public:
  CtrlFixture();

  ~CtrlFixture();

  // Create an app sock which connects with AppSock on the Broker
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> createAppSock(
      const std::string& id);

  // Create an minion sock which connects with MinionSock on the Broker
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> createMinionSock(
      const std::string& id);

  const std::string ctrlMinionSockUrl_ = "ipc://ctrl-minion-router";
  const std::string ctrlPubSockUrl_ = "ipc://ctrl-pub";
  const std::string ctrlAppSockUrl_ = "ipc://ctrl-app-router";
  const std::string monitorSockUrl_ = "ipc://ctrl-monitor-rep";
  const std::string monitorPubSockUrl_ = "ipc://ctrl-monitor-pub";

  fbzmq::Context context_;

  facebook::terragraph::Broker broker_;
  std::unique_ptr<std::thread> brokerThread_;

  fbzmq::ZmqMonitor monitorServer_;
  std::unique_ptr<std::thread> monitorServerThread_;

  std::vector<fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>> minionSocks_;

  apache::thrift::CompactSerializer serializer_;

  // Asserts on received minionName, senderApp and success
  void recvE2EAck(
      fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>& querySock,
      std::string expectedSenderApp,
      bool success,
      apache::thrift::CompactSerializer& serializer);
};
