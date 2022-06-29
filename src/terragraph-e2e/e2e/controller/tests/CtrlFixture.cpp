/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "CtrlFixture.h"

#include <e2e/common/TestUtils.h>

#include "../CtrlApp.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "../SharedObjects.h"

using namespace facebook::terragraph;

CtrlFixture::CtrlFixture()
    : broker_(
          context_,
          ctrlMinionSockUrl_,
          ctrlAppSockUrl_,
          ctrlPubSockUrl_,
          false,
          false,
          false),
      monitorServer_(
          std::string{monitorSockUrl_},
          std::string{monitorPubSockUrl_},
          context_) {

  SharedObjects::getConfigHelper()->wlock()->setConfigFiles(
      "/etc/e2e_config/base_versions/",       // base_config_dir
      "/etc/e2e_config/base_versions/fw_versions/",  // fw_base_config_dir
      "/etc/e2e_config/base_versions/hw_versions/",  // hw_base_config_dir
      // hw_config_types_file
      "/etc/e2e_config/base_versions/hw_versions/hw_types.json",
      "/tmp/node_config_overrides.json",      // node_config_overrides_file
      // auto_node_config_overrides_file
      "/tmp/auto_node_config_overrides.json",
      "/tmp/network_config_overrides.json",   // network_config_overrides_file
      "/etc/e2e_config/config_metadata.json", // node_config_metadata_file
      "/tmp/cfg_backup/",                     // config_backup_dir
      {});

  brokerThread_ = std::make_unique<std::thread>([this]() {
    VLOG(1) << "broker thread starting";
    broker_.run();
    VLOG(1) << "broker thread terminating";
  });
  broker_.waitUntilRunning();

  monitorServerThread_ = std::make_unique<std::thread>([this]() {
    VLOG(1) << "MonitorServer thread starting";
    monitorServer_.run();
    VLOG(1) << "MonitorServer thread terminating";
  });
  monitorServer_.waitUntilRunning();
}

CtrlFixture::~CtrlFixture() {
  VLOG(1) << "Stopping the ctrl broker thread";
  broker_.stop();
  brokerThread_->join();
  VLOG(1) << "Stopping the ctrl monitorStoreServer thread";
  monitorServer_.stop();
  monitorServerThread_->join();
  VLOG(1) << "Cleaned up ctrl";

  VLOG(1) << "Deleting configs created by tests";
  // Delete any configs created by tests
  remove("/tmp/node_config_overrides.json");
  remove("/tmp/auto_node_config_overrides.json");
  remove("/tmp/network_config_overrides.json");
}

// Create an app sock which connects with AppSock on the Broker
fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>
CtrlFixture::createAppSock(const std::string& id) {
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> sock{context_,
                                                    fbzmq::IdentityString{id}};
  CHECK(sock.connect(fbzmq::SocketUrl{ctrlAppSockUrl_}));
  return sock;
}

// Create a minion sock which connects with MinionSock on the Broker
fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>
CtrlFixture::createMinionSock(const std::string& id) {
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> sock{context_,
                                                    fbzmq::IdentityString{id}};
  CHECK(sock.connect(fbzmq::SocketUrl{ctrlMinionSockUrl_}));
  return sock;
}

void
CtrlFixture::recvE2EAck(
    fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>& querySock,
    std::string expectedSenderApp,
    bool success,
    apache::thrift::CompactSerializer& serializer) {
  std::string minionName, senderApp;
  thrift::Message msg;
  std::tie(minionName, senderApp, msg) = recvInCtrlApp(querySock, serializer);
  EXPECT_EQ("", minionName);
  EXPECT_EQ(expectedSenderApp, senderApp);
  auto e2eAck =
      fbzmq::util::readThriftObjStr<thrift::E2EAck>(msg.value, serializer);
  EXPECT_EQ(success, e2eAck.success);
}
