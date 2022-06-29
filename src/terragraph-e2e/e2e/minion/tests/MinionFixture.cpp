/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "MinionFixture.h"

#include "e2e/common/Consts.h"

using namespace facebook::terragraph;
using namespace fbzmq;

MinionFixture::MinionFixture()
    : broker_(
          zmqContext_,
          macAddr_,
          minionCtrlSockUrl_,
          minionAppSockUrl_,
          minionBroadcastPubSockUrl_,
          ctrlSockTimeout_,
          myNetworkInfoFile_),
      monitorServer_(
          std::string{monitorSockUrl_},
          std::string{monitorPubSockUrl_},
          zmqContext_) {

  brokerThread_ = std::make_unique<std::thread>([this]() {
    LOG(INFO) << "broker thread starting";
    broker_.run();
    LOG(INFO) << "broker thread terminating";
  });
  broker_.waitUntilRunning();

  monitorServerThread_ = std::make_unique<std::thread>([this]() {
    LOG(INFO) << "MonitorServer thread starting";
    monitorServer_.run();
    LOG(INFO) << "MonitorServer thread terminating";
  });
  monitorServer_.waitUntilRunning();
}

MinionFixture::~MinionFixture() {
  LOG(INFO) << "Stopping the minion broker thread";
  broker_.stop();
  brokerThread_->join();
  LOG(INFO) << "Stopping the minion monitorStoreServer thread";
  monitorServer_.stop();
  monitorServerThread_->join();
  LOG(INFO) << "Cleaned up minion";
}

// Create an app sock which connects with AppSock on the Minion Broker
Socket<ZMQ_DEALER, ZMQ_CLIENT>
MinionFixture::createAppSock(const std::string& id) {
  Socket<ZMQ_DEALER, ZMQ_CLIENT> sock{zmqContext_, IdentityString{id}};
  CHECK(sock.connect(SocketUrl{minionAppSockUrl_}));
  return sock;
}

// Create a controller sock which minion dealer sock in Broker talks to
Socket<ZMQ_ROUTER, ZMQ_SERVER>
MinionFixture::createCtrlSock() {
  Socket<ZMQ_ROUTER, ZMQ_SERVER> sock{
      zmqContext_,
      IdentityString{facebook::terragraph::E2EConsts::kBrokerCtrlId}};
  CHECK(sock.bind(SocketUrl{minionCtrlSockUrl_}));
  return sock;
}

// Create a pair sock which minion driver app sock talks to
Socket<ZMQ_PAIR, ZMQ_CLIENT>
MinionFixture::createPairSock(const std::string& sockUrl) {
  Socket<ZMQ_PAIR, ZMQ_CLIENT> sock{zmqContext_};
  CHECK(sock.connect(SocketUrl{sockUrl}));
  return sock;
}
