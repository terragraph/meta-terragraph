/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <folly/MacAddress.h>
#include <folly/system/ThreadName.h>
#include <folly/init/Init.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "../SharedObjects.h"
#include "../StatusApp.h"
#include "../TopologyApp.h"
#include <e2e/common/Consts.h>

#include "CtrlFixture.h"
#include <e2e/common/TestUtils.h>

using namespace std;
using namespace facebook::terragraph;

namespace { // anonymous namespace

class TopologyFixture : public CtrlFixture {
 public:
  TopologyFixture() : CtrlFixture() {}

  void
  startTopologyApp(
      const thrift::Topology& topology, bool syncWithStatusReports = true) {
    // start with blank status reports map
    SharedObjects::getStatusReports()->wlock()->clear();

    TopologyWrapper topologyW(topology);
    char tmpFileName[] = "/tmp/terraXXXXXX";
    int fd = mkstemp(tmpFileName);
    close(fd);
    string topoFileName = tmpFileName;
    topologyW.writeToFile(topoFileName);
    topologyApp_ = std::make_unique<TopologyApp>(
        context_,
        ctrlAppSockUrl_,
        monitorSockUrl_,
        // FLAGS_status_reports_interval_s
        std::chrono::seconds(syncWithStatusReports ? 1 : 9999),
        std::chrono::seconds(20), // FLAGS_topology_report_interval_s
        std::chrono::seconds(30), // FLAGS_routing_adjacencies_dump_interval_s
        nodeAliveTimeout_,
        std::chrono::seconds(60), // FLAGS_airtime_alloc_update_interval_s
        std::chrono::seconds(30), // FLAGS_centralized_prefix_update_interval_s
        topoFileName);
    topologyAppThread_ = std::make_unique<std::thread>([this]() {
      VLOG(1) << "TopologyApp thread starting";
      topologyApp_->run();
      VLOG(1) << "TopologyApp thread terminating";
    });
    topologyApp_->waitUntilRunning();
    querySock_ = createAppSock(querySockId_);
  }

  ~TopologyFixture() {
    VLOG(1) << "Stopping the TopologyApp thread";
    topologyApp_->stop();
    topologyAppThread_->join();
  }

  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> querySock_;
  const std::string querySockId_ = "QUERY_SOCK_ID";
  const std::chrono::milliseconds statusReportSyncSleepTime_{1500};
  std::chrono::seconds nodeAliveTimeout_{60};
  std::unique_ptr<std::thread> topologyAppThread_;
  std::unique_ptr<TopologyApp> topologyApp_;

  thrift::Topology
  getTopology(bool sleepBeforeQuery = false) {

    // This is to ensure msgs sent from other sockets (eg statusAppSock)
    // get to TopologyApp, before we query Topology from TopologyApp
    if (sleepBeforeQuery) {
      /* sleep override */
      std::this_thread::sleep_for(statusReportSyncSleepTime_);
    }

    // send a GetTopology
    thrift::Message msg;
    msg.mType = thrift::MessageType::GET_TOPOLOGY;
    sendInCtrlApp(
        querySock_,
        "", // minionId
        E2EConsts::kTopologyAppCtrlId,
        querySockId_,
        msg,
        serializer_);

    // read the response
    string minionName, senderApp;
    std::tie(minionName, senderApp, msg) =
        recvInCtrlApp(querySock_, serializer_);
    EXPECT_EQ("", minionName);
    EXPECT_EQ(E2EConsts::kTopologyAppCtrlId, senderApp);
    return fbzmq::util::readThriftObjStr<thrift::Topology>(
        msg.value, serializer_);
  }

  void
  bumpLinkupAttempts(const string& linkName) {

    // send a BumpLinkupAttempts
    thrift::Message msg;
    msg.mType = thrift::MessageType::BUMP_LINKUP_ATTEMPTS;
    thrift::BumpLinkUpAttempts bumpLinkUpAttempts;
    bumpLinkUpAttempts.linkName = linkName;
    msg.value = fbzmq::util::writeThriftObjStr(bumpLinkUpAttempts, serializer_);

    sendInCtrlApp(
        querySock_,
        "", // minionId
        E2EConsts::kTopologyAppCtrlId,
        querySockId_,
        msg,
        serializer_);
  }

  void
  sendSetNodeStatus(
      const thrift::Node& node,
      const thrift::NodeStatusType nodeStatus) {
    thrift::Message msg;
    msg.mType = thrift::MessageType::SET_NODE_STATUS;
    thrift::SetNodeStatus setNodeStatus;
    setNodeStatus.nodeMac = node.mac_addr;
    setNodeStatus.nodeStatus = nodeStatus;
    msg.value = fbzmq::util::writeThriftObjStr(setNodeStatus, serializer_);
    sendInCtrlApp(
        querySock_,
        "",
        E2EConsts::kTopologyAppCtrlId,
        querySockId_,
        msg,
        serializer_);
  }
};

void
verifyTopology(
    const thrift::Topology& expectedTopo, const thrift::Topology& givenTopo) {
  EXPECT_EQ(expectedTopo.nodes.size(), givenTopo.nodes.size());
  EXPECT_EQ(expectedTopo.links.size(), givenTopo.links.size());

  apache::thrift::SimpleJSONSerializer serializer;
  EXPECT_EQ(
      serializer.serialize<std::string>(std::set<thrift::Node>(
          expectedTopo.nodes.begin(), expectedTopo.nodes.end())),
      serializer.serialize<std::string>(std::set<thrift::Node>(
          givenTopo.nodes.begin(), givenTopo.nodes.end())));
  EXPECT_EQ(
      serializer.serialize<std::string>(std::set<thrift::Link>(
          expectedTopo.links.begin(), expectedTopo.links.end())),
      serializer.serialize<std::string>(std::set<thrift::Link>(
          givenTopo.links.begin(), givenTopo.links.end())));
}

void
sendLinkStatus(
    fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>& minionSock,
    const thrift::LinkStatusType& linkStatusType,
    const string& nbrMacAddr,
    apache::thrift::CompactSerializer& serializer) {
  thrift::Message msg;
  msg.mType = thrift::MessageType::LINK_STATUS;
  thrift::LinkStatus linkStatus;
  linkStatus.responderMac = nbrMacAddr;
  linkStatus.linkStatusType = linkStatusType;
  linkStatus.isEvent = true;
  msg.value = fbzmq::util::writeThriftObjStr(linkStatus, serializer);
  sendInMinionBroker(
      minionSock,
      E2EConsts::kTopologyAppCtrlId,
      E2EConsts::kIgnitionAppMinionId,
      msg,
      serializer);
}

} // anonymous namespace

// node0 (pop) ----> node1
//
TEST_F(TopologyFixture, GetTopology) {

  // setup topology
  std::string errorMsg;
  auto topology = createTopology(2, {0}, {{0, 1}});
  TopologyWrapper expectedTopoW(topology);

  startTopologyApp(topology, false /* syncWithStatusReports */);
  topology = getTopology();
  expectedTopoW.sanitizeState();
  verifyTopology(expectedTopoW.getTopology(), topology);
}

// node0 (pop) ----> node1 ----> node2
//        |                       ^
//        |                       |
//        |--------> node3 -------|
//
TEST_F(TopologyFixture, LinkStatus) {

  std::string errorMsg;
  // setup topology
  auto topology = createTopology(
      4, // nodes
      {0}, // pop node ids
      {{0, 1}, {1, 2}, {0, 3}, {2, 3}}); // link ids
  TopologyWrapper expectedTopoW(topology);
  for (const auto& node : topology.nodes) {
    minionSocks_.push_back(std::move(createMinionSock(node.mac_addr)));
  }

  startTopologyApp(topology, false /* syncWithStatusReports */);

  topology = getTopology();
  expectedTopoW.sanitizeState();
  verifyTopology(expectedTopoW.getTopology(), topology);

  sendLinkStatus(
      minionSocks_[0],
      thrift::LinkStatusType::LINK_UP,
      topology.nodes[1].mac_addr,
      serializer_);
  expectedTopoW.setLinkStatus(topology.links[0].name, true);
  topology = getTopology(true /* sleepBeforeQuerying */);
  verifyTopology(expectedTopoW.getTopology(), topology);

  sendLinkStatus(
      minionSocks_[2],
      thrift::LinkStatusType::LINK_UP,
      topology.nodes[3].mac_addr,
      serializer_);
  expectedTopoW.setLinkStatus(topology.links[3].name, true);
  topology = getTopology(true /* sleepBeforeQuerying */);
  verifyTopology(expectedTopoW.getTopology(), topology);

  sendLinkStatus(
      minionSocks_[3],
      thrift::LinkStatusType::LINK_DOWN,
      topology.nodes[2].mac_addr,
      serializer_);
  expectedTopoW.setLinkStatus(topology.links[3].name, false);
  topology = getTopology(true /* sleepBeforeQuerying */);
  verifyTopology(expectedTopoW.getTopology(), topology);

  sendLinkStatus(
      minionSocks_[2],
      thrift::LinkStatusType::LINK_UP,
      topology.nodes[3].mac_addr,
      serializer_);
  expectedTopoW.setLinkStatus(topology.links[3].name, true);
  topology = getTopology(true /* sleepBeforeQuerying */);
  verifyTopology(expectedTopoW.getTopology(), topology);
}

// node0 (pop) ----> node1 ----> node2
//        |                       ^
//        |                       |
//        |--------> node3 -------|
//
TEST_F(TopologyFixture, BumpLinkUpAttempts) {

  std::string errorMsg;
  // setup topology
  auto topology = createTopology(
      4, // nodes
      {0}, // pop node ids
      {{0, 1}, {1, 2}, {0, 3}, {2, 3}}); // link ids
  TopologyWrapper expectedTopoW(topology);

  startTopologyApp(topology, false /* syncWithStatusReports */);

  topology = getTopology();
  expectedTopoW.sanitizeState();
  verifyTopology(expectedTopoW.getTopology(), topology);

  expectedTopoW.bumpLinkupAttempts(topology.links[0].name);
  bumpLinkupAttempts(topology.links[0].name);
  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  expectedTopoW.bumpLinkupAttempts(topology.links[3].name);
  bumpLinkupAttempts(topology.links[3].name);
  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  expectedTopoW.bumpLinkupAttempts(topology.links[3].name);
  bumpLinkupAttempts(topology.links[3].name);
  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);
}

// node0 (pop) ----> node1 ----> node2
//        |                       ^
//        |                       |
//        |--------> node3 -------|
//
TEST_F(TopologyFixture, ProcessStatusDump) {

  std::string errorMsg;
  // setup topology
  auto topology = createTopology(
      4, // nodes
      {0}, // pop node ids
      {{0, 1}, {1, 2}, {0, 3}, {2, 3}}); // link ids
  TopologyWrapper expectedTopoW(topology);

  expectedTopoW.sanitizeState();

  startTopologyApp(topology);

  // Update status reports to mark all nodes as alive
  auto statusAppSock = createAppSock(E2EConsts::kStatusAppCtrlId);
  SCOPE_EXIT { statusAppSock.close(); };
  std::unordered_map<string, StatusApp::StatusReport> statusReports;
  for (const auto& node : topology.nodes) {
    thrift::StatusReport statusReport;
    statusReport.timeStamp = std::time(nullptr);
    statusReport.status = node.status;
    statusReports[node.mac_addr] =
        StatusApp::StatusReport(std::chrono::steady_clock::time_point(
              std::chrono::seconds(statusReport.timeStamp)
            ), statusReport);
  }
  (*SharedObjects::getStatusReports()->wlock()) = statusReports;

  // mark all nodes as alive in expectedTopoW
  for (const auto& node : expectedTopoW.getAllNodes()) {
    expectedTopoW.setNodeStatus(
        node.name,
        node.pop_node ? thrift::NodeStatusType::ONLINE_INITIATOR
                      : thrift::NodeStatusType::ONLINE);
  }

  topology = getTopology(true /* sleepBeforeQuerying */);
  verifyTopology(expectedTopoW.getTopology(), topology);
}

// node0 (pop) ----> node1 ----> node2 (x)
//        |                       ^
//        |                       |
//        |--------> node3 -------|
//                     |
//                     |
//                     v
//               node4(NEVER UP!!!)
TEST_F(TopologyFixture, ProcessDirtyStatusDump) {
  std::string errorMsg;
  // setup topology
  auto topology = createTopology(
      5, // nodes
      {0}, // pop node ids
      {{0, 1}, {1, 2}, {0, 3}, {2, 3}, {3, 4}}, // link ids
      2, // sites
      {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 1}}, // node ids by site
      {4}); // CN node ids
  // force to mark node[0] ~ node[3] ONLINE
  topology.nodes[0].status = thrift::NodeStatusType::ONLINE_INITIATOR;
  topology.nodes[1].status = thrift::NodeStatusType::ONLINE;
  topology.nodes[2].status = thrift::NodeStatusType::ONLINE;
  topology.nodes[3].status = thrift::NodeStatusType::ONLINE;
  // force to mark up all links
  for (auto& link : topology.links) {
    link.is_alive = true;
  }
  startTopologyApp(topology);

  /* sleep override */
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // Update status reports to mark all nodes as alive
  auto statusAppSock = createAppSock(E2EConsts::kStatusAppCtrlId);
  SCOPE_EXIT { statusAppSock.close(); };
  std::unordered_map<string, StatusApp::StatusReport> statusReports;
  for (const auto& node : topology.nodes) {
    // Never report status of node4
    if (node.name != topology.nodes[4].name) {
      thrift::StatusReport statusReport;
      statusReport.timeStamp = std::time(nullptr);
      statusReport.status = node.status;
      statusReports[node.mac_addr] =
          StatusApp::StatusReport(std::chrono::steady_clock::time_point(
                std::chrono::seconds(statusReport.timeStamp)
              ), statusReport);
    }
  }
  (*SharedObjects::getStatusReports()->wlock()) = statusReports;

  // Inform links are up
  for (const auto& node : topology.nodes) {
    minionSocks_.push_back(std::move(createMinionSock(node.mac_addr)));
  }
  sendLinkStatus(
      minionSocks_[0],
      thrift::LinkStatusType::LINK_UP,
      topology.nodes[1].mac_addr,
      serializer_);
  sendLinkStatus(
      minionSocks_[1],
      thrift::LinkStatusType::LINK_UP,
      topology.nodes[2].mac_addr,
      serializer_);
  sendLinkStatus(
      minionSocks_[0],
      thrift::LinkStatusType::LINK_UP,
      topology.nodes[3].mac_addr,
      serializer_);
  sendLinkStatus(
      minionSocks_[3],
      thrift::LinkStatusType::LINK_UP,
      topology.nodes[2].mac_addr,
      serializer_);
  sendLinkStatus(
      minionSocks_[3],
      thrift::LinkStatusType::LINK_UP,
      topology.nodes[4].mac_addr,
      serializer_);

  // In this case, don't sanitize topology wrapper,
  // we start topology with links up on purpose
  TopologyWrapper expectedTopoW(topology);

  topology = getTopology(true /* sleepBeforeQuerying */);
  verifyTopology(expectedTopoW.getTopology(), topology);

  // Mark nodes[2] as dead by making the status report stale
  StatusApp::StatusReport statusReport;
  statusReport.report.timeStamp -= 2 * nodeAliveTimeout_.count();
  statusReport.steadyTs =
      std::chrono::steady_clock::time_point(
          std::chrono::seconds(statusReport.report.timeStamp));
  (*SharedObjects::getStatusReports()->wlock())[topology.nodes[2].mac_addr] =
      statusReport;

  // mark nodes[2] as dead in expectedTopoW
  expectedTopoW.setNodeStatus(
      topology.nodes[2].name, thrift::NodeStatusType::OFFLINE);
  topology = getTopology(true /* sleepBeforeQuerying */);
  verifyTopology(expectedTopoW.getTopology(), topology);

  // This simulates case where controller restarted with
  // a snapshot where nodes[3] is present (and marked alive),
  // but it has actually disappeared and we dont get any heartbeats from it
  SharedObjects::getStatusReports()->wlock()->erase(topology.nodes[3].mac_addr);

  // mark nodes[3] as dead in expectedTopoW
  expectedTopoW.setNodeStatus(
      topology.nodes[3].name, thrift::NodeStatusType::OFFLINE);

  // at this point link node-2->node-3 and node-3->node-4 should be dead
  expectedTopoW.setLinkStatus(
      getLinkName(topology.nodes[2], topology.nodes[3]), false);
  expectedTopoW.setLinkStatus(
      getLinkName(topology.nodes[3], topology.nodes[4]), false);

  topology = getTopology(true /* sleepBeforeQuerying */);
  verifyTopology(expectedTopoW.getTopology(), topology);
}

// node0 (pop) ----> node1 ----> node2
//        |                       ^
//        |                       |
//        |--------> node3 -------|
//
TEST_F(TopologyFixture, SetNodeStatus) {

  std::string errorMsg;
  // setup topology
  auto topology = createTopology(
      4, // nodes
      {0}, // pop node ids
      {{0, 1}, {1, 2}, {0, 3}, {2, 3}}); // link ids
  TopologyWrapper expectedTopoW(topology);
  expectedTopoW.sanitizeState();
  for (const auto& node : topology.nodes) {
    minionSocks_.push_back(std::move(createMinionSock(node.mac_addr)));
  }

  startTopologyApp(topology);

  // make a query initially.  To ensure other messages are received.
  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // Send status dump to topologyApp and inform all nodes are alive
  auto statusAppSock = createAppSock(E2EConsts::kStatusAppCtrlId);
  SCOPE_EXIT { statusAppSock.close(); };
  std::unordered_map<string, StatusApp::StatusReport> statusReports;
  for (const auto& node : topology.nodes) {
    thrift::StatusReport statusReport;
    statusReport.timeStamp = std::time(nullptr);
    statusReport.status = node.status;
    statusReports[node.mac_addr] =
      StatusApp::StatusReport(std::chrono::steady_clock::time_point(
            std::chrono::seconds(statusReport.timeStamp)
          ), statusReport);
  }
  (*SharedObjects::getStatusReports()->wlock()) = statusReports;

  // Inform links are up
  sendLinkStatus(
      minionSocks_[0],
      thrift::LinkStatusType::LINK_UP,
      topology.nodes[1].mac_addr,
      serializer_);
  sendLinkStatus(
      minionSocks_[1],
      thrift::LinkStatusType::LINK_UP,
      topology.nodes[2].mac_addr,
      serializer_);
  sendLinkStatus(
      minionSocks_[0],
      thrift::LinkStatusType::LINK_UP,
      topology.nodes[3].mac_addr,
      serializer_);
  sendLinkStatus(
      minionSocks_[3],
      thrift::LinkStatusType::LINK_UP,
      topology.nodes[2].mac_addr,
      serializer_);

  // mark all nodes/links as alive in expectedTopology
  for (const auto& node : expectedTopoW.getAllNodes()) {
    expectedTopoW.setNodeStatus(node.name, thrift::NodeStatusType::ONLINE);
  }
  for (const auto& link : expectedTopoW.getAllLinks()) {
    expectedTopoW.setLinkStatus(link.name, true);
  }

  topology = getTopology(true /* sleepBeforeQuerying */);
  verifyTopology(expectedTopoW.getTopology(), topology);

  // Mark nodes[2] as dead by calling SetNodeStatus
  sendSetNodeStatus(topology.nodes[2], thrift::NodeStatusType::OFFLINE);

  // mark nodes[2] as dead in expectedTopo
  expectedTopoW.setNodeStatus(
      topology.nodes[2].name, thrift::NodeStatusType::OFFLINE);

  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);
}

TEST_F(TopologyFixture, SetNodeParamsReq) {

  // setup topology
  std::string errorMsg;
  auto topology = createTopology(1, {0}, {});
  TopologyWrapper expectedTopoW(topology);

  expectedTopoW.sanitizeState();
  for (const auto& node : topology.nodes) {
    minionSocks_.push_back(std::move(createMinionSock(node.mac_addr)));
  }

  startTopologyApp(topology);

  // send request
  thrift::Message setNodeParamsReqMsg;
  setNodeParamsReqMsg.mType = thrift::MessageType::SET_NODE_PARAMS_REQ;
  thrift::SetNodeParamsReq setNodeParamsReq;
  setNodeParamsReq.nodeMac = topology.nodes[0].mac_addr;
  setNodeParamsReqMsg.value =
      fbzmq::util::writeThriftObjStr(setNodeParamsReq, serializer_);
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      setNodeParamsReqMsg,
      serializer_);

  // minion should receive the msg
  string receiverApp, senderApp;
  thrift::Message setNodeParamsMsg;
  std::tie(receiverApp, senderApp, setNodeParamsMsg) =
      recvInMinionBroker(minionSocks_[0], serializer_);
  EXPECT_EQ(E2EConsts::kStatusAppMinionId, receiverApp);
  EXPECT_EQ(E2EConsts::kTopologyAppCtrlId, senderApp);
  EXPECT_NO_THROW(fbzmq::util::readThriftObjStr<thrift::NodeParams>(
      setNodeParamsMsg.value, serializer_));
}

TEST_F(TopologyFixture, SetNodeMac) {

  // setup topology
  std::string errorMsg;
  auto topology = createTopology(1, {0}, {});
  TopologyWrapper expectedTopoW(topology);
  expectedTopoW.sanitizeState();

  startTopologyApp(topology);

  // send request
  thrift::Message setNodeMacMsg;
  setNodeMacMsg.mType = thrift::MessageType::SET_NODE_MAC;
  thrift::SetNodeMac setNodeMac;
  setNodeMac.nodeName = topology.nodes[0].name;
  setNodeMac.nodeMac = "A:5:A:5:A:5";
  setNodeMac.force = false;
  setNodeMacMsg.value = fbzmq::util::writeThriftObjStr(setNodeMac, serializer_);
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      setNodeMacMsg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, true, serializer_);

  // change MAC address in expected topology
  expectedTopoW.setNodeMacByName(
      setNodeMac.nodeName,
      setNodeMac.nodeMac);
  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);
}

// node0 (pop) ----> node1 ----> node2
//        |
//        |
//        |--------> *node3
TEST_F(TopologyFixture, TopologyChange) {

  // setup topology: start with 3 nodes
  std::string errorMsg;
  auto topology = createTopology(
      3, // nodes
      {0}, // pop node ids
      {{0, 1}, {1, 2}}); // link ids
  EXPECT_NO_THROW(TopologyWrapper expectedTopoW(topology));
  TopologyWrapper expectedTopoW(topology);
  expectedTopoW.sanitizeState();
  for (const auto& node : topology.nodes) {
    minionSocks_.push_back(std::move(createMinionSock(node.mac_addr)));
  }

  startTopologyApp(topology);

  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // add node-3
  // send a AddNode
  thrift::Message msg;
  msg.mType = thrift::MessageType::ADD_NODE;
  thrift::Node node;
  node.name = "node-3";
  node.mac_addr = "3:3:3:3:3:3";
  node.site_name = "pole-0";
  thrift::AddNode addNode;
  addNode.node = node;
  msg.value = fbzmq::util::writeThriftObjStr(addNode, serializer_);
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, true, serializer_);

  // add a node in expected topology
  expectedTopoW.addNode(node);
  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // add same node again
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, false, serializer_);

  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // add node-4 & plug in site pole-0
  // should fail since 4 nodes are already associated with the site.
  msg.mType = thrift::MessageType::ADD_NODE;
  node.name = "node-4";
  node.mac_addr = "4:4:4:4:4:4";
  node.site_name = "pole-0";
  addNode = thrift::AddNode();
  addNode.node = node;
  msg.value = fbzmq::util::writeThriftObjStr(addNode, serializer_);
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, false, serializer_);

  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // add node-5 & plug in site pole-1
  // should fail since pole-1 doesn't exist
  msg.mType = thrift::MessageType::ADD_NODE;
  node.name = "node-5";
  node.mac_addr = "5:5:5:5:5:5";
  node.site_name = "pole-1";
  addNode = thrift::AddNode();
  addNode.node = node;
  msg.value = fbzmq::util::writeThriftObjStr(
      addNode, serializer_);
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, false, serializer_);

  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // add pole-1
  // send a AddSite
  msg.mType = thrift::MessageType::ADD_SITE;
  thrift::Site pole1 = createSite("pole-1", 11, -11, 0,  0);

  thrift::AddSite addSite;
  addSite.site = pole1;
  msg.value = fbzmq::util::writeThriftObjStr(addSite, serializer_);
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, true, serializer_);

  // add a site in expected topology
  expectedTopoW.addSite(pole1);
  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // add same site again
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, false, serializer_);

  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // delete pole-0
  // should fail because there are nodes associated to it
  msg.mType = thrift::MessageType::DEL_SITE;
  thrift::DelSite delSite;
  delSite.siteName = "pole-0";
  msg.value = fbzmq::util::writeThriftObjStr(delSite, serializer_);
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, false, serializer_);

  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // delete pole-1
  msg.mType = thrift::MessageType::DEL_SITE;
  delSite = thrift::DelSite();
  delSite.siteName = "pole-1";
  msg.value = fbzmq::util::writeThriftObjStr(
      delSite, serializer_);
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, true, serializer_);

  // delete pole-1 in expected topology
  expectedTopoW.delSite("pole-1");
  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // add link from node-0 -> node-3
  // send a AddLink
  msg.mType = thrift::MessageType::ADD_LINK;
  thrift::Link link;
  link.a_node_name = "node-0";
  link.z_node_name = "node-3";
  link.link_type = thrift::LinkType::WIRELESS;
  thrift::AddLink addLink;
  addLink.link = link;
  msg.value = fbzmq::util::writeThriftObjStr(addLink, serializer_);
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, true, serializer_);

  // add a link in expected topology
  expectedTopoW.addLink(link);
  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // add same link again
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, false, serializer_);

  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // delete the link from node-0 -> node-3
  // send a DelLink
  msg.mType = thrift::MessageType::DEL_LINK;
  thrift::DelLink delLink;
  delLink.aNodeName = "node-0";
  delLink.zNodeName = "node-3";
  delLink.force = true;
  msg.value = fbzmq::util::writeThriftObjStr(delLink, serializer_);
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, true, serializer_);

  // delete the link in expected topology
  expectedTopoW.delLink("node-0", "node-3", true /* force */);
  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // delete same link again
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, false, serializer_);

  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // delete node-3
  // send a DelNode
  msg.mType = thrift::MessageType::DEL_NODE;
  thrift::DelNode delNode;
  delNode.nodeName = "node-3";
  delNode.force = true;
  msg.value = fbzmq::util::writeThriftObjStr(delNode, serializer_);
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, true, serializer_);

  // delete the node in expected topology
  expectedTopoW.delNode("node-3", true /* force */);
  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // delete same node again
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kTopologyAppCtrlId, false, serializer_);

  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);

  // bulk add node-6, node-7, pole-2, link
  // send a BulkAdd
  msg.mType = thrift::MessageType::BULK_ADD;
  thrift::Site pole2 = createSite("pole-2", 11, -11, 0,  0);
  thrift::Node node6;
  node6.name = "node-6";
  node6.mac_addr = "6:6:6:6:6:6";
  node6.site_name = "pole-2";
  thrift::Node node7;
  node7.name = "node-7";
  node7.mac_addr = "7:7:7:7:7:7";
  node7.site_name = "pole-2";
  thrift::Link link67;
  link67.a_node_name = "node-6";
  link67.z_node_name = "node-7";
  link67.link_type = thrift::LinkType::ETHERNET;
  std::vector<thrift::Site> sites { pole2 };
  std::vector<thrift::Node> nodes { node6, node7 };
  std::vector<thrift::Link> links { link67 };
  thrift::BulkAdd bulkAdd;
  bulkAdd.sites = sites;
  bulkAdd.nodes = nodes;
  bulkAdd.links = links;
  msg.value = fbzmq::util::writeThriftObjStr(bulkAdd, serializer_);
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kTopologyAppCtrlId,
      querySockId_,
      msg,
      serializer_);
  // receive response
  string minionName, senderApp;
  std::tie(minionName, senderApp, msg) = recvInCtrlApp(querySock_, serializer_);
  EXPECT_EQ("", minionName);
  EXPECT_EQ(E2EConsts::kTopologyAppCtrlId, senderApp);
  thrift::BulkAddResult bulkAddResult =
      fbzmq::util::readThriftObjStr<thrift::BulkAddResult>(
          msg.value, serializer_);
  EXPECT_TRUE(bulkAddResult.success);
  EXPECT_EQ(1, bulkAddResult.addedSites.size());
  EXPECT_EQ(2, bulkAddResult.addedNodes.size());
  EXPECT_EQ(1, bulkAddResult.addedLinks.size());

  // add the site/nodes/link in expected topology
  expectedTopoW.addSite(pole2);
  expectedTopoW.addNode(node6);
  expectedTopoW.addNode(node7);
  expectedTopoW.addLink(link67);
  topology = getTopology();
  verifyTopology(expectedTopoW.getTopology(), topology);
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
