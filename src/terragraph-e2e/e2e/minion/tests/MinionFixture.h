/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/service/monitor/ZmqMonitor.h>
#include <fbzmq/zmq/Zmq.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "../Broker.h"

// A common fixture which can be used for minion app unittests.
// The fixture takes care of creating/destroying the Broker and TimeoutServer.
// It also provides some helpful methods for the apps.
class MinionFixture : public ::testing::Test {

 public:
  MinionFixture();

  ~MinionFixture();

  // Create a dealer socket which will be used by minion apps
  // to connect to the minion broker (specifically its AppsSock).
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> createAppSock(
      const std::string& id);

  // Create a router socket to emulate the controller.
  // The dealer socket in minion-broker will talk to this.
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> createCtrlSock();

  // Create a pair sock which minion driver app sock talks to
  fbzmq::Socket<ZMQ_PAIR, fbzmq::ZMQ_CLIENT> createPairSock(
      const std::string& sockUrl);

  const std::string minionCtrlSockUrl_{"ipc://minion-ctrl-dealer"};
  const std::string minionAppSockUrl_{"ipc://minion-app-router"};
  const std::string minionBroadcastPubSockUrl_{"ipc://minion-broadcast-pub"};
  const std::string monitorSockUrl_{"ipc://minion-monitor-router"};
  const std::string monitorPubSockUrl_{"ipc://minion-monitor-pub"};
  const std::string driverPairSockUrl_{"ipc://driver-if-pair"};
  const std::string macAddr_{"00:00:00:00:00:00"};
  const std::chrono::seconds ctrlSockTimeout_{30};
  const std::string myNetworkInfoFile_{"/tmp/mynetworkinfo"};

  fbzmq::Context zmqContext_;

  facebook::terragraph::minion::Broker broker_;
  std::unique_ptr<std::thread> brokerThread_;

  fbzmq::ZmqMonitor monitorServer_;
  std::unique_ptr<std::thread> monitorServerThread_;

  apache::thrift::CompactSerializer serializer_;
};
