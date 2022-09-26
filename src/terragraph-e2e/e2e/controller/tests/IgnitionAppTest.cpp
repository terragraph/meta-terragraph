/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <folly/MacAddress.h>
#include <folly/init/Init.h>

#include "../ConfigHelper.h"
#include "../IgnitionApp.h"
#include "../SharedObjects.h"
#include <e2e/common/Consts.h>

#include "CtrlFixture.h"
#include <e2e/common/TestUtils.h>

using namespace std;
using namespace facebook::terragraph;

namespace { // anonymous namespace

class CtrlIgnitionFixture : public CtrlFixture {
 public:
  CtrlIgnitionFixture()
      : CtrlFixture(),
        ignitionApp_(
            context_,
            ctrlAppSockUrl_,
            monitorSockUrl_,
            std::chrono::seconds(300) /* extendedDampenInterval */,
            std::chrono::seconds(1800) /* extendedDampenFailureInterval */,
            std::chrono::seconds(300) /* backupCnLinkInterval */,
            std::chrono::seconds(0) /* p2mpAssocDelay */,
            false /* ignoreDampenIntervalAfterResp */) {
    ignitionAppThread_ = std::make_unique<std::thread>([this]() {
      VLOG(1) << "IgnitionApp thread starting";
      ignitionApp_.run();
      VLOG(1) << "IgnitionApp thread terminating";
    });
    ignitionApp_.waitUntilRunning();
    topoAppSock_ = createAppSock(E2EConsts::kTopologyAppCtrlId);
    querySock_ = createAppSock(querySockId_);
  }

  ~CtrlIgnitionFixture() {
    VLOG(1) << "Stopping the IgnitionApp thread";
    ignitionApp_.stop();
    ignitionAppThread_->join();
  }

  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> topoAppSock_;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> querySock_;
  std::unique_ptr<std::thread> ignitionAppThread_;
  const std::string querySockId_ = "QUERY_SOCK_ID";
  IgnitionApp ignitionApp_;

  void
  updateTopology(const thrift::Topology& topology) {
    SharedObjects::getTopologyWrapper()->wlock()->setTopology(topology);
  }

  // verify a SetLinkStatus message is received at the minionSock for
  // each of the nbrs
  void
  verifyLinkupMsgRecv(
      std::string& myNodeName,
      fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>& minionSock,
      std::set<thrift::Node> nbrs,
      TopologyWrapper& topologyW) {
    auto numNbrs = nbrs.size();
    for (size_t i = 0; i < numNbrs; i++) {
      fbzmq::Message receiverApp, senderApp, setLinkStatusReqMsg;
      CHECK(
          minionSock.recvMultiple(receiverApp, senderApp, setLinkStatusReqMsg));
      auto setLinkStatusReq =
          *setLinkStatusReqMsg.readThriftObj<thrift::Message>(serializer_);
      if (setLinkStatusReq.mType == thrift::MessageType::BF_RESP_SCAN ||
          setLinkStatusReq.mType == thrift::MessageType::FW_BF_RESP_SCAN ||
          setLinkStatusReq.mType ==
              thrift::MessageType::BF_SLOT_EXCLUSION_REQ) {
        // ignore BF Slot Exclusion Request,
        //        BF responder mode messages
        i--;
        continue;
      }
      EXPECT_EQ(
          E2EConsts::kIgnitionAppMinionId, *receiverApp.read<std::string>());
      EXPECT_EQ(E2EConsts::kIgnitionAppCtrlId, *senderApp.read<std::string>());
      auto setLinkStatus = fbzmq::util::readThriftObjStr<thrift::SetLinkStatus>(
          setLinkStatusReq.value, serializer_);
      EXPECT_EQ(setLinkStatus.linkStatusType, thrift::LinkStatusType::LINK_UP);

      // Verify the msg is for one of the nbrs and remove it
      auto itr = std::find_if(
          nbrs.begin(), nbrs.end(), [&setLinkStatus](const thrift::Node& nbr) {
            return setLinkStatus.responderMac == nbr.mac_addr &&
                   setLinkStatus.responderNodeType_ref().value()
                       == nbr.node_type;
          });
      EXPECT_NE(nbrs.end(), itr);
      nbrs.erase(itr);

      // Update topology
      auto nbrNode = topologyW.getNodeByMac(setLinkStatus.responderMac);
      topologyW.setNodeStatus(
          nbrNode->name, thrift::NodeStatusType::ONLINE_INITIATOR);
      auto linkName = topologyW.getLinkName(myNodeName, nbrNode->name);
      topologyW.setLinkStatus(*linkName, true);
      updateTopology(topologyW.getTopology());
    }
    EXPECT_EQ(0, nbrs.size());
  }

  void
  verifyBumpLinkupRecv(std::set<std::string> linkNames) {
    auto numLinks = linkNames.size();
    for (size_t i = 0; i < numLinks; ++i) {
      string minionName, senderApp;
      thrift::Message msg;
      do {
        std::tie(minionName, senderApp, msg) =
            recvInCtrlApp(topoAppSock_, serializer_);
      } while (msg.mType != thrift::MessageType::BUMP_LINKUP_ATTEMPTS);

      EXPECT_EQ("", minionName);
      EXPECT_EQ(E2EConsts::kIgnitionAppCtrlId, senderApp);
      EXPECT_EQ(msg.mType, thrift::MessageType::BUMP_LINKUP_ATTEMPTS);
      auto bumpLinkUpAck =
          fbzmq::util::readThriftObjStr<thrift::BumpLinkUpAttempts>(
              msg.value, serializer_);

      // Verify it is for one of the links in linkNames and remove it
      EXPECT_EQ(1, linkNames.count(bumpLinkUpAck.linkName));
      linkNames.erase(bumpLinkUpAck.linkName);
    }
    EXPECT_EQ(0, linkNames.size());
  }

  void
  disableAutoIgnition() {
    thrift::Message msg;
    thrift::IgnitionParams ignitionParams;
    ignitionParams.enable_ref() = false;
    msg.mType = thrift::MessageType::SET_IGNITION_PARAMS;
    msg.value = fbzmq::util::writeThriftObjStr(ignitionParams, serializer_);
    sendInCtrlApp(
        querySock_,
        "" /* minionName */,
        E2EConsts::kIgnitionAppCtrlId, /* receiver */
        querySockId_,
        msg,
        serializer_);
  }

  void
  sendSetLinkStatusReq(
      const thrift::LinkActionType& action, const thrift::Link& link) {
    thrift::Message msg;
    msg.mType = thrift::MessageType::SET_LINK_STATUS_REQ;
    thrift::SetLinkStatusReq setLinkStatusReq;
    setLinkStatusReq.action = action;
    setLinkStatusReq.initiatorNodeName = link.a_node_name;
    setLinkStatusReq.responderNodeName = link.z_node_name;
    msg.value = fbzmq::util::writeThriftObjStr(setLinkStatusReq, serializer_);
    sendInCtrlApp(
        querySock_,
        "", /* minion */
        E2EConsts::kIgnitionAppCtrlId, /* receiver */
        querySockId_,
        msg,
        serializer_);
  }

 protected:
  void
  SetUp() override {
    std::string emptyJson = "{}";
    folly::writeFile(emptyJson, controllerCfgFileName_.c_str());
    SharedObjects::getE2EConfigWrapper()->wlock()->setE2EConfigFile(
        controllerCfgFileName_);
  }

  void
  TearDown() override {
    remove(controllerCfgFileName_.c_str());
  }

  std::string controllerCfgFileName_ = "/tmp/controller_config.json";
};

// verify a SetLinkStatus message(LINK_DOWN) is received at the minionSock for
// a particular neighbor
void
verifyLinkdownMsgRecv(
    std::string& myNodeName,
    fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>& minionSock,
    const std::string& nbrName,
    TopologyWrapper& topologyW,
    apache::thrift::CompactSerializer& serializer) {
  fbzmq::Message receiverApp, senderApp, setLinkStatusReqMsg;
  CHECK(minionSock.recvMultiple(receiverApp, senderApp, setLinkStatusReqMsg));
  EXPECT_EQ(E2EConsts::kIgnitionAppMinionId, *receiverApp.read<std::string>());
  EXPECT_EQ(E2EConsts::kIgnitionAppCtrlId, *senderApp.read<std::string>());
  auto setLinkStatusReq =
      *setLinkStatusReqMsg.readThriftObj<thrift::Message>(serializer);
  auto setLinkStatus = fbzmq::util::readThriftObjStr<thrift::SetLinkStatus>(
      setLinkStatusReq.value, serializer);
  EXPECT_EQ(setLinkStatus.linkStatusType, thrift::LinkStatusType::LINK_DOWN);

  // Update topology
  auto linkName = topologyW.getLinkName(myNodeName, nbrName);
  topologyW.setLinkStatus(*linkName, false);
}

} // anonymous namespace

// --- Ignition Work Flow tests ---

// simple 2 node topology
//
// node0 (pop) ----> node1
//
TEST_F(CtrlIgnitionFixture, 2nodeIgnition) {

  // setup topology
  auto topology = createTopology(2, {0}, {{0, 1}});
  std::string errorMsg;
  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  for (const auto& node : topology.nodes) {
    minionSocks_.push_back(std::move(createMinionSock(node.mac_addr)));
  }
  TopologyWrapper topologyW(topology);
  lockedConfigHelper.unlock();

  // update initial topology
  updateTopology(topology);

  verifyLinkupMsgRecv(
      topology.nodes[0].name, minionSocks_[0], {topology.nodes[1]}, topologyW);

  verifyBumpLinkupRecv({topology.links[0].name});
}

// A 4 node topology
//
// node0 (pop) ----> node1 ----> node2
//        |                       ^
//        |                       |
//        |--------> node3 -------|
//
TEST_F(CtrlIgnitionFixture, 4nodeIgnition) {

  // setup topology
  auto topology = createTopology(
      4, // nodes
      {0}, // pop node ids
      {{0, 1}, {1, 2}, {0, 3}, {2, 3}}); // link ids
  std::string errorMsg;
  for (const auto& node : topology.nodes) {
    minionSocks_.push_back(std::move(createMinionSock(node.mac_addr)));
  }
  TopologyWrapper topologyW(topology);

  // update initial topology
  updateTopology(topology);

  // node0 will receive a linkUpdateRequest for node3 and node1
  std::thread node0Thread([this, &topology, &topologyW]() {
    verifyLinkupMsgRecv(
        topology.nodes[0].name,
        minionSocks_[0],
        {topology.nodes[3], topology.nodes[1]},
        topologyW);
  });
  node0Thread.join();

  // node1 will receive a linkUpdateRequest for node2
  std::thread node1Thread([this, &topology, &topologyW]() {
    verifyLinkupMsgRecv(
        topology.nodes[1].name,
        minionSocks_[1],
        {topology.nodes[2]},
        topologyW);
  });
  node1Thread.join();

  // node3 will receive a linkUpdateRequest for node2
  std::thread node2Thread([this, &topology, &topologyW]() {
    verifyLinkupMsgRecv(
        topology.nodes[3].name,
        minionSocks_[3],
        {topology.nodes[2]},
        topologyW);
  });
  node2Thread.join();

  verifyBumpLinkupRecv({topology.links[0].name,
                        topology.links[1].name,
                        topology.links[2].name,
                        topology.links[3].name});
}

// This test has same logic as next one,
// except it brings node1 down in TopologyW and does not verify linkup attempts
// It simulates the scenario when the node reboots and comes back
//
// A 4 node topology with failure link
//
// node0 (pop) ----> node1 ----> node2
//        |                       ^
//        |                       |
//        |--------> node3 -------|
//
// after all links are up, bring node1 down,
// make sure ignition rebuilds all links to node1(node0->node1, node1->node2)
TEST_F(CtrlIgnitionFixture, 4nodeIgnitionWithNodeFailure) {

  // setup topology
  auto topology = createTopology(
      4, // nodes
      {0}, // pop node ids
      {{0, 1}, {1, 2}, {0, 3}, {2, 3}}); // link ids
  std::string errorMsg;
  for (const auto& node : topology.nodes) {
    minionSocks_.push_back(std::move(createMinionSock(node.mac_addr)));
  }
  TopologyWrapper topologyW(topology);

  // update initial topology
  updateTopology(topology);

  // node0 will receive a linkUpdateRequest for node3 and node1
  std::thread node0Thread([this, &topology, &topologyW]() {
    verifyLinkupMsgRecv(
        topology.nodes[0].name,
        minionSocks_[0],
        {topology.nodes[3], topology.nodes[1]},
        topologyW);
  });
  node0Thread.join();

  // node1 will receive a linkUpdateRequest for node2
  std::thread node1Thread([this, &topology, &topologyW]() {
    verifyLinkupMsgRecv(
        topology.nodes[1].name,
        minionSocks_[1],
        {topology.nodes[2]},
        topologyW);
  });
  node1Thread.join();

  // node3 will receive a linkUpdateRequest for node2
  std::thread node2Thread([this, &topology, &topologyW]() {
    verifyLinkupMsgRecv(
        topology.nodes[3].name,
        minionSocks_[3],
        {topology.nodes[2]},
        topologyW);
  });
  node2Thread.join();

  // Since node1->node2 and node0->node1 have been ignited only recently,
  // we wait for a few seconds (with added jitter) before trying to ignite
  // them again. So they can be ignited in any order
  verifyBumpLinkupRecv({topology.links[0].name,
                        topology.links[1].name,
                        topology.links[2].name,
                        topology.links[3].name});

  // bring node1 down
  LOG(INFO) << "Bringing node-1 down";
  topologyW.setNodeStatus(
      topology.nodes[1].name, thrift::NodeStatusType::OFFLINE);
  topologyW.setLinkStatus(topology.links[0].name, false); // {0, 1}
  topologyW.setLinkStatus(topology.links[1].name, false); // {1, 2}
  updateTopology(topologyW.getTopology());

  // node0 will receive a linkUpdateRequest for node1
  std::thread node0ThreadAfterFailure([this, &topology, &topologyW]() {
    verifyLinkupMsgRecv(
        topology.nodes[0].name,
        minionSocks_[0],
        {topology.nodes[1]},
        topologyW);
  });
  node0ThreadAfterFailure.join();

  // node1 will receive a linkUpdateRequest for node2
  std::thread node1ThreadAfterFailure([this, &topology, &topologyW]() {
    verifyLinkupMsgRecv(
        topology.nodes[1].name,
        minionSocks_[1],
        {topology.nodes[2]},
        topologyW);
  });
  node1ThreadAfterFailure.join();
}

// Test SetLinkStatus(UP) and SetLinkStatus(DOWN) commands
// node0 (pop) ----> node1
//
TEST_F(CtrlIgnitionFixture, ManualIgnition) {

  // setup topology
  auto topology = createTopology(2, {0}, {{0, 1}});
  std::string errorMsg;
  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  for (const auto& node : topology.nodes) {
    minionSocks_.push_back(std::move(createMinionSock(node.mac_addr)));
  }
  TopologyWrapper topologyW(topology);
  lockedConfigHelper.unlock();

  disableAutoIgnition();
  recvE2EAck(querySock_, E2EConsts::kIgnitionAppCtrlId, true, serializer_);

  // update initial topology
  updateTopology(topologyW.getTopology());

  // send LINK_UP (SetLinkStatusReq)
  sendSetLinkStatusReq(thrift::LinkActionType::LINK_UP, topology.links[0]);
  recvE2EAck(querySock_, E2EConsts::kIgnitionAppCtrlId, true, serializer_);
  verifyLinkupMsgRecv(
      topology.nodes[0].name, minionSocks_[0], {topology.nodes[1]}, topologyW);

  // send invalid LINK_UPs
  sendSetLinkStatusReq(thrift::LinkActionType::LINK_UP, topology.links[0]);
  recvE2EAck(querySock_, E2EConsts::kIgnitionAppCtrlId, false, serializer_);
  sendSetLinkStatusReq(thrift::LinkActionType::LINK_UP, thrift::Link());
  recvE2EAck(querySock_, E2EConsts::kIgnitionAppCtrlId, false, serializer_);

  // send LINK_DOWN (SetLinkStatusReq)
  sendSetLinkStatusReq(thrift::LinkActionType::LINK_DOWN, topology.links[0]);
  recvE2EAck(querySock_, E2EConsts::kIgnitionAppCtrlId, true, serializer_);

  verifyLinkdownMsgRecv(
      topology.nodes[0].name,
      minionSocks_[0],
      topology.nodes[1].name,
      topologyW,
      serializer_);

  // send invalid LINK_DOWNs
  sendSetLinkStatusReq(thrift::LinkActionType::LINK_UP, topology.links[0]);
  recvE2EAck(querySock_, E2EConsts::kIgnitionAppCtrlId, false, serializer_);
  sendSetLinkStatusReq(thrift::LinkActionType::LINK_UP, thrift::Link());
  recvE2EAck(querySock_, E2EConsts::kIgnitionAppCtrlId, false, serializer_);
}

// Test SetLinkStatus(UP) commands with
// node0 (pop) ----> node1
// where node type of pop node is CN
// SetLinkStatus is expected to fail
TEST_F(CtrlIgnitionFixture, ManualInvalidIgnition) {

  // setup topology
  auto topology = createTopology(2, {0}, {{0, 1}});
  // hardcode node type of node0 as CN
  topology.nodes[0].node_type = thrift::NodeType::CN;
  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  std::string errorMsg;
  for (const auto& node : topology.nodes) {
    minionSocks_.push_back(std::move(createMinionSock(node.mac_addr)));
  }
  TopologyWrapper topologyW(topology);
  lockedConfigHelper.unlock();

  disableAutoIgnition();
  recvE2EAck(querySock_, E2EConsts::kIgnitionAppCtrlId, true, serializer_);

  // update initial topology
  updateTopology(topologyW.getTopology());

  // send invalid LINK_UP request (from CN -> DN)
  sendSetLinkStatusReq(thrift::LinkActionType::LINK_UP, topology.links[0]);
  recvE2EAck(querySock_, E2EConsts::kIgnitionAppCtrlId, false, serializer_);
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
