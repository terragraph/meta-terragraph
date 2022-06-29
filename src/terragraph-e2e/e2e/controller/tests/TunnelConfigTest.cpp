/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/portability/GFlags.h>

#include <folly/String.h>
#include <folly/dynamic.h>
#include <folly/init/Init.h>

#include "../ConfigApp.h"
#include "../SharedObjects.h"
#include "e2e/common/ConfigUtil.h"
#include <e2e/common/Consts.h>

#include "CtrlFixture.h"
#include <e2e/common/TestUtils.h>

using namespace std;
using namespace facebook::terragraph;

namespace {

const std::string kVersionFile = "/etc/tgversion";

class TunnelConfigFixture : public CtrlFixture {
 public:
  TunnelConfigFixture()
      : CtrlFixture(),
        configApp_(
            context_,
            ctrlAppSockUrl_,
            monitorSockUrl_,
            std::chrono::seconds(5),
            std::chrono::minutes(5),
            (pid_t)0),
        statusApp_(
            context_,
            ctrlAppSockUrl_,
            monitorSockUrl_,
            std::chrono::seconds(5),
            std::chrono::seconds(3600),
            kVersionFile) {

    configAppThread_ = std::make_unique<std::thread>([this]() {
      VLOG(1) << "ConfigApp thread starting";
      configApp_.run();
      VLOG(1) << "ConfigApp thread terminating";
    });
    configApp_.waitUntilRunning();

    statusAppThread_ = std::make_unique<std::thread>([this]() {
      VLOG(1) << "StatusApp thread starting";
      statusApp_.run();
      VLOG(1) << "StatusApp thread terminating";
    });
    statusApp_.waitUntilRunning();
    querySock_ = createAppSock(querySockId_);
  }

  ~TunnelConfigFixture() {
    VLOG(1) << "Stopping the StatusApp thread";
    statusApp_.stop();
    statusAppThread_->join();

    VLOG(1) << "Stopping the ConfigApp thread";
    configApp_.stop();
    configAppThread_->join();
  }

  std::unique_ptr<std::thread> configAppThread_;
  ConfigApp configApp_;

  std::unique_ptr<std::thread> statusAppThread_;
  StatusApp statusApp_;

  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> querySock_;
  const std::string querySockId_ = "QUERY_SOCK_ID";

  const std::string nodeName1_ = "tunnel_test_node_1";
  const std::string nodeMac1_ = "01:01:01:01:01:01";

  const std::string nodeName2_ = "tunnel_test_node_2";
  const std::string nodeMac2_ = "02:02:02:02:02:02";
  std::string nodeIp2_ = "fd00::10";

  std::string tunnelName1_ = "tunnel_test_1";
};

} // namespace

TEST_F(TunnelConfigFixture, TunnelConfigAutoOverrides) {
  SCOPE_EXIT {
    LOG(INFO)
        << "ConfigApp test get/set tunnel config node config overrides is done";
  };
  // Add a node to global topology
  auto node = createNode(
      nodeName1_,
      nodeMac1_,
      "test_site",
      true,
      thrift::NodeStatusType::ONLINE,
      thrift::NodeType::DN);
  auto dstNode = createNode(
      nodeName2_,
      nodeMac2_,
      "test_site",
      true,
      thrift::NodeStatusType::ONLINE,
      thrift::NodeType::DN);
  auto site = createSite("test_site", 1, 1, 1, 1);
  SharedObjects::getTopologyWrapper()->wlock()->setTopology(
      createTopology({node, dstNode}, {}, {site}));

  // Update status report for destination node
  thrift::StatusReport dstNodeStatusReport;
  dstNodeStatusReport.ipv6Address = nodeIp2_;
  StatusApp::StatusReport statusAppReport;
  statusAppReport.report = dstNodeStatusReport;
  auto statusReportLock = SharedObjects::getStatusReports()->wlock();
  (*statusReportLock)[nodeMac2_] = statusAppReport;
  statusReportLock.unlock();

  string minion;
  string senderApp;
  thrift::SetCtrlConfigNodeOverridesReq setCtrlConfigNodeOverridesReq{};
  thrift::GetCtrlConfigAutoNodeOverridesResp
      getCtrlConfigAutoNodeOverridesResp{};
  thrift::Message getCtrlConfigNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigNodeOverridesRespMsg{};
  thrift::Message setCtrlConfigNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigAutoNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigAutoNodeOverridesRespMsg{};

  // Construct TunnelConfig
  thrift::TunnelConfig tunnelConfig{};
  tunnelConfig.enabled_ref() = true;
  tunnelConfig.dstNodeName_ref() = nodeName2_;
  tunnelConfig.tunnelType_ref() = "GRE_L2";
  auto tunnelConfigJson =
      JsonUtils::serializeToJson<thrift::TunnelConfig>(tunnelConfig);
  folly::dynamic nodeOverridesObject = folly::dynamic::object(
      nodeName1_,
      folly::dynamic::object(
          "tunnelConfig",
          folly::dynamic::object(
              tunnelName1_, folly::parseJson(tunnelConfigJson))));

  setCtrlConfigNodeOverridesReqMsg.mType =
      thrift::MessageType::SET_CTRL_CONFIG_NODE_OVERRIDES_REQ;
  setCtrlConfigNodeOverridesReq.overrides = folly::toJson(nodeOverridesObject);
  setCtrlConfigNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      setCtrlConfigNodeOverridesReq,
      serializer_);

  // Send node config overrides to ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      setCtrlConfigNodeOverridesReqMsg,
      serializer_);

  // Wait for Ack back
  recvE2EAck(querySock_, E2EConsts::kConfigAppCtrlId, true, serializer_);

  getCtrlConfigNodeOverridesReqMsg.mType =
      thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_REQ;
  thrift::GetCtrlConfigNodeOverridesReq getCtrlConfigNodeOverridesReq;
  getCtrlConfigNodeOverridesReq.nodes = {nodeName1_};
  getCtrlConfigNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      getCtrlConfigNodeOverridesReq,
      serializer_);

  // Request node config overrides from ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getCtrlConfigNodeOverridesReqMsg,
      serializer_);

  // Receive node config overrides
  std::tie(minion, senderApp, getCtrlConfigNodeOverridesRespMsg) =
      recvInCtrlApp(querySock_, serializer_);

  EXPECT_EQ(E2EConsts::kConfigAppCtrlId, senderApp);
  EXPECT_EQ(
      thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_RESP,
      getCtrlConfigNodeOverridesRespMsg.mType);
  EXPECT_NO_THROW(
      fbzmq::util::readThriftObjStr<thrift::GetCtrlConfigNodeOverridesResp>(
          getCtrlConfigNodeOverridesRespMsg.value, serializer_));

  // Receive auto node config overrides
  getCtrlConfigAutoNodeOverridesReqMsg.mType =
      thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_REQ;
  thrift::GetCtrlConfigAutoNodeOverridesReq getCtrlConfigAutoNodeOverridesReq;
  getCtrlConfigAutoNodeOverridesReq.nodes = {nodeName1_};
  getCtrlConfigAutoNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      getCtrlConfigAutoNodeOverridesReq,
      serializer_);

  // Request node config overrides from ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getCtrlConfigAutoNodeOverridesReqMsg,
      serializer_);

  // Receive node config overrides
  std::tie(minion, senderApp, getCtrlConfigAutoNodeOverridesRespMsg) =
      recvInCtrlApp(querySock_, serializer_);

  EXPECT_EQ(E2EConsts::kConfigAppCtrlId, senderApp);
  EXPECT_EQ(
      thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_RESP,
      getCtrlConfigAutoNodeOverridesRespMsg.mType);
  EXPECT_NO_THROW(
      getCtrlConfigAutoNodeOverridesResp = fbzmq::util::readThriftObjStr<
          thrift::GetCtrlConfigAutoNodeOverridesResp>(
          getCtrlConfigAutoNodeOverridesRespMsg.value, serializer_));

  // Ensure dstIp is set in tunnel config
  folly::dynamic autoNodeOverridesObject;
  EXPECT_NO_THROW(
      autoNodeOverridesObject =
          folly::parseJson(getCtrlConfigAutoNodeOverridesResp.overrides));
  EXPECT_TRUE(autoNodeOverridesObject.count(nodeName1_));
  EXPECT_TRUE(autoNodeOverridesObject[nodeName1_].count("tunnelConfig"));
  EXPECT_TRUE(
      autoNodeOverridesObject[nodeName1_]["tunnelConfig"].count(tunnelName1_));
  EXPECT_TRUE(
      autoNodeOverridesObject[nodeName1_]["tunnelConfig"][tunnelName1_].count(
          "dstIp"));
  EXPECT_EQ(
      autoNodeOverridesObject[nodeName1_]["tunnelConfig"][tunnelName1_]
                             ["dstIp"],
      nodeIp2_);
}

TEST_F(TunnelConfigFixture, TunnelConfigIpChanged) {
  SCOPE_EXIT {
    LOG(INFO) << "ConfigApp test updating dstIp for existing tunnel is done";
  };

  // Construct status report with new IP
  nodeIp2_ = "fd00::a";
  thrift::Message getStatusDumpMsg{}, statusDumpMsg{}, statusReportMsg{};
  thrift::StatusReport statusReport;
  statusReport.ipv6Address = nodeIp2_;
  // Must specify a version for our report to be processed
  statusReport.version = "dfdsf";
  statusReportMsg.mType = thrift::MessageType::STATUS_REPORT;
  statusReportMsg.value =
      fbzmq::util::writeThriftObjStr(statusReport, serializer_);

  // Mock minion node-2
  auto minionSock2 = createMinionSock(nodeMac2_);
  SCOPE_EXIT { minionSock2.close(); };

  // Send updated status report for node-2
  sendInMinionBroker(
      minionSock2,
      E2EConsts::kStatusAppCtrlId,
      E2EConsts::kStatusAppMinionId,
      statusReportMsg,
      serializer_);
  sleep(1);

  thrift::Message getCtrlConfigAutoNodeOverridesReqMsg{};

  // Check node config for new dst ip
  getCtrlConfigAutoNodeOverridesReqMsg.mType =
      thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_REQ;
  thrift::GetCtrlConfigAutoNodeOverridesReq getCtrlConfigAutoNodeOverridesReq;
  getCtrlConfigAutoNodeOverridesReq.nodes = {nodeName1_};
  getCtrlConfigAutoNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      getCtrlConfigAutoNodeOverridesReq,
      serializer_);

  // Request node config overrides from ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getCtrlConfigAutoNodeOverridesReqMsg,
      serializer_);

  string minion;
  string senderApp;
  thrift::GetCtrlConfigAutoNodeOverridesResp
      getCtrlConfigAutoNodeOverridesResp{};
  thrift::Message getCtrlConfigAutoNodeOverridesRespMsg{};

  // Receive node config overrides
  std::tie(minion, senderApp, getCtrlConfigAutoNodeOverridesRespMsg) =
      recvInCtrlApp(querySock_, serializer_);

  EXPECT_EQ(E2EConsts::kConfigAppCtrlId, senderApp);
  EXPECT_EQ(
      thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_RESP,
      getCtrlConfigAutoNodeOverridesRespMsg.mType);
  EXPECT_NO_THROW(
      getCtrlConfigAutoNodeOverridesResp = fbzmq::util::readThriftObjStr<
          thrift::GetCtrlConfigAutoNodeOverridesResp>(
          getCtrlConfigAutoNodeOverridesRespMsg.value, serializer_));

  folly::dynamic autoNodeOverridesObject;
  EXPECT_NO_THROW(
      autoNodeOverridesObject =
          folly::parseJson(getCtrlConfigAutoNodeOverridesResp.overrides));
  // Ensure new IP is set in tunnel config
  EXPECT_EQ(
      autoNodeOverridesObject[nodeName1_]["tunnelConfig"][tunnelName1_]
                             ["dstIp"],
      nodeIp2_);
}

TEST_F(TunnelConfigFixture, TunnelConfigNodeConfigRemoved) {
  SCOPE_EXIT {
    LOG(INFO) << "ConfigApp test removing tunnel node override config is done";
  };

  // Empty node overrides
  folly::dynamic nodeOverridesObject =
      folly::dynamic::object(nodeName1_, folly::dynamic::object());

  thrift::GetCtrlConfigNodeOverridesResp getCtrlConfigNodeOverridesResp{};
  thrift::GetCtrlConfigAutoNodeOverridesResp
      getCtrlConfigAutoNodeOverridesResp{};
  thrift::Message getCtrlConfigNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigNodeOverridesRespMsg{};
  thrift::Message setCtrlConfigNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigAutoNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigAutoNodeOverridesRespMsg{};
  setCtrlConfigNodeOverridesReqMsg.mType =
      thrift::MessageType::SET_CTRL_CONFIG_NODE_OVERRIDES_REQ;
  thrift::SetCtrlConfigNodeOverridesReq setCtrlConfigNodeOverridesReq;
  setCtrlConfigNodeOverridesReq.overrides = folly::toJson(nodeOverridesObject);
  setCtrlConfigNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      setCtrlConfigNodeOverridesReq,
      serializer_);

  // Send (empty) node config overrides to ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      setCtrlConfigNodeOverridesReqMsg,
      serializer_);

  // Wait for Ack back
  recvE2EAck(querySock_, E2EConsts::kConfigAppCtrlId, true, serializer_);

  getCtrlConfigNodeOverridesReqMsg.mType =
      thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_REQ;
  thrift::GetCtrlConfigNodeOverridesReq getCtrlConfigNodeOverridesReq;
  getCtrlConfigNodeOverridesReq.nodes = {nodeName1_};
  getCtrlConfigNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      getCtrlConfigNodeOverridesReq,
      serializer_);

  // Request node config overrides from ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getCtrlConfigNodeOverridesReqMsg,
      serializer_);

  string minion;
  string senderApp;

  // Receive node config overrides
  std::tie(minion, senderApp, getCtrlConfigNodeOverridesRespMsg) =
      recvInCtrlApp(querySock_, serializer_);

  EXPECT_EQ(E2EConsts::kConfigAppCtrlId, senderApp);
  EXPECT_EQ(
      thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_RESP,
      getCtrlConfigNodeOverridesRespMsg.mType);
  EXPECT_NO_THROW(
      getCtrlConfigNodeOverridesResp =
          fbzmq::util::readThriftObjStr<thrift::GetCtrlConfigNodeOverridesResp>(
              getCtrlConfigNodeOverridesRespMsg.value, serializer_));

  // Check auto layer for tunnelConfig
  getCtrlConfigAutoNodeOverridesReqMsg.mType =
      thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_REQ;
  thrift::GetCtrlConfigAutoNodeOverridesReq getCtrlConfigAutoNodeOverridesReq;
  getCtrlConfigAutoNodeOverridesReq.nodes = {nodeName1_};
  getCtrlConfigAutoNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      getCtrlConfigAutoNodeOverridesReq,
      serializer_);

  // Request auto node config overrides from ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getCtrlConfigAutoNodeOverridesReqMsg,
      serializer_);

  // Receive node config overrides
  std::tie(minion, senderApp, getCtrlConfigAutoNodeOverridesRespMsg) =
      recvInCtrlApp(querySock_, serializer_);

  EXPECT_EQ(E2EConsts::kConfigAppCtrlId, senderApp);
  EXPECT_EQ(
      thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_RESP,
      getCtrlConfigAutoNodeOverridesRespMsg.mType);
  EXPECT_NO_THROW(
      getCtrlConfigAutoNodeOverridesResp = fbzmq::util::readThriftObjStr<
          thrift::GetCtrlConfigAutoNodeOverridesResp>(
          getCtrlConfigNodeOverridesRespMsg.value, serializer_));
  LOG(INFO) << folly::toJson(getCtrlConfigAutoNodeOverridesResp.overrides);
  EXPECT_EQ(
      getCtrlConfigAutoNodeOverridesResp.overrides,
      folly::toJson(nodeOverridesObject));
}

TEST_F(TunnelConfigFixture, TunnelConfigReAdd) {
  SCOPE_EXIT {
    LOG(INFO) << "ConfigApp test re-adding tunnel node config override is done";
  };

  // Update status report for destination node
  thrift::StatusReport dstNodeStatusReport;
  dstNodeStatusReport.ipv6Address = nodeIp2_;
  StatusApp::StatusReport statusAppReport;
  statusAppReport.report = dstNodeStatusReport;
  auto statusReportLock = SharedObjects::getStatusReports()->wlock();
  (*statusReportLock)[nodeMac2_] = statusAppReport;
  statusReportLock.unlock();

  string minion;
  string senderApp;
  thrift::SetCtrlConfigNodeOverridesReq setCtrlConfigNodeOverridesReq{};
  thrift::GetCtrlConfigAutoNodeOverridesResp
      getCtrlConfigAutoNodeOverridesResp{};
  thrift::Message getCtrlConfigNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigNodeOverridesRespMsg{};
  thrift::Message setCtrlConfigNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigAutoNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigAutoNodeOverridesRespMsg{};

  // Construct TunnelConfig
  thrift::TunnelConfig tunnelConfig{};
  tunnelConfig.enabled_ref() = true;
  tunnelConfig.dstNodeName_ref() = nodeName2_;
  tunnelConfig.tunnelType_ref() = "GRE_L2";
  auto tunnelConfigJson =
      JsonUtils::serializeToJson<thrift::TunnelConfig>(tunnelConfig);
  folly::dynamic nodeOverridesObject = folly::dynamic::object(
      nodeName1_,
      folly::dynamic::object(
          "tunnelConfig",
          folly::dynamic::object(
              tunnelName1_, folly::parseJson(tunnelConfigJson))));

  setCtrlConfigNodeOverridesReqMsg.mType =
      thrift::MessageType::SET_CTRL_CONFIG_NODE_OVERRIDES_REQ;
  setCtrlConfigNodeOverridesReq.overrides = folly::toJson(nodeOverridesObject);
  setCtrlConfigNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      setCtrlConfigNodeOverridesReq,
      serializer_);

  // Send node config overrides to ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      setCtrlConfigNodeOverridesReqMsg,
      serializer_);

  // Wait for Ack back
  recvE2EAck(querySock_, E2EConsts::kConfigAppCtrlId, true, serializer_);

  getCtrlConfigNodeOverridesReqMsg.mType =
      thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_REQ;
  thrift::GetCtrlConfigNodeOverridesReq getCtrlConfigNodeOverridesReq;
  getCtrlConfigNodeOverridesReq.nodes = {nodeName1_};
  getCtrlConfigNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      getCtrlConfigNodeOverridesReq,
      serializer_);

  // Request node config overrides from ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getCtrlConfigNodeOverridesReqMsg,
      serializer_);

  // Receive node config overrides
  std::tie(minion, senderApp, getCtrlConfigNodeOverridesRespMsg) =
      recvInCtrlApp(querySock_, serializer_);

  EXPECT_EQ(E2EConsts::kConfigAppCtrlId, senderApp);
  EXPECT_EQ(
      thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_RESP,
      getCtrlConfigNodeOverridesRespMsg.mType);
  EXPECT_NO_THROW(
      fbzmq::util::readThriftObjStr<thrift::GetCtrlConfigNodeOverridesResp>(
          getCtrlConfigNodeOverridesRespMsg.value, serializer_));

  // Receive auto node config overrides
  getCtrlConfigAutoNodeOverridesReqMsg.mType =
      thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_REQ;
  thrift::GetCtrlConfigAutoNodeOverridesReq getCtrlConfigAutoNodeOverridesReq;
  getCtrlConfigAutoNodeOverridesReq.nodes = {nodeName1_};
  getCtrlConfigAutoNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      getCtrlConfigAutoNodeOverridesReq,
      serializer_);

  // Request node config overrides from ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getCtrlConfigAutoNodeOverridesReqMsg,
      serializer_);

  // Receive node config overrides
  std::tie(minion, senderApp, getCtrlConfigAutoNodeOverridesRespMsg) =
      recvInCtrlApp(querySock_, serializer_);

  EXPECT_EQ(E2EConsts::kConfigAppCtrlId, senderApp);
  EXPECT_EQ(
      thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_RESP,
      getCtrlConfigAutoNodeOverridesRespMsg.mType);
  EXPECT_NO_THROW(
      getCtrlConfigAutoNodeOverridesResp = fbzmq::util::readThriftObjStr<
          thrift::GetCtrlConfigAutoNodeOverridesResp>(
          getCtrlConfigAutoNodeOverridesRespMsg.value, serializer_));

  // Ensure dstIp is set in tunnel config
  folly::dynamic autoNodeOverridesObject;
  EXPECT_NO_THROW(
      autoNodeOverridesObject =
          folly::parseJson(getCtrlConfigAutoNodeOverridesResp.overrides));
  EXPECT_TRUE(autoNodeOverridesObject.count(nodeName1_));
  EXPECT_TRUE(
      autoNodeOverridesObject[nodeName1_].count("tunnelConfig"));
  EXPECT_TRUE(
      autoNodeOverridesObject[nodeName1_]["tunnelConfig"].count(tunnelName1_));
  EXPECT_TRUE(
      autoNodeOverridesObject[nodeName1_]["tunnelConfig"][tunnelName1_].count(
          "dstIp"));
  EXPECT_EQ(
      autoNodeOverridesObject[nodeName1_]["tunnelConfig"][tunnelName1_]
                             ["dstIp"],
      nodeIp2_);
}

TEST_F(TunnelConfigFixture, TunnelConfigRename) {
  SCOPE_EXIT {
    LOG(INFO)
        << "ConfigApp test renaming tunnel config in node overrides is done";
  };

  // Update status report for destination node
  thrift::StatusReport dstNodeStatusReport;
  dstNodeStatusReport.ipv6Address = nodeIp2_;
  StatusApp::StatusReport statusAppReport;
  statusAppReport.report = dstNodeStatusReport;
  auto statusReportLock = SharedObjects::getStatusReports()->wlock();
  (*statusReportLock)[nodeMac2_] = statusAppReport;
  statusReportLock.unlock();

  string minion;
  string senderApp;
  thrift::SetCtrlConfigNodeOverridesReq setCtrlConfigNodeOverridesReq{};
  thrift::GetCtrlConfigAutoNodeOverridesResp
      getCtrlConfigAutoNodeOverridesResp{};
  thrift::Message getCtrlConfigNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigNodeOverridesRespMsg{};
  thrift::Message setCtrlConfigNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigAutoNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigAutoNodeOverridesRespMsg{};

  // Construct TunnelConfig with updated tunnel name
  thrift::TunnelConfig tunnelConfig{};
  tunnelConfig.enabled_ref() = true;
  tunnelConfig.dstNodeName_ref() = nodeName2_;
  tunnelConfig.tunnelType_ref() = "GRE_L2";
  tunnelName1_ = "renamed_tunnel_test_1";
  auto tunnelConfigJson =
      JsonUtils::serializeToJson<thrift::TunnelConfig>(tunnelConfig);
  folly::dynamic nodeOverridesObject = folly::dynamic::object(
      nodeName1_,
      folly::dynamic::object(
          "tunnelConfig",
          folly::dynamic::object(
              tunnelName1_, folly::parseJson(tunnelConfigJson))));

  setCtrlConfigNodeOverridesReqMsg.mType =
      thrift::MessageType::SET_CTRL_CONFIG_NODE_OVERRIDES_REQ;
  setCtrlConfigNodeOverridesReq.overrides = folly::toJson(nodeOverridesObject);
  setCtrlConfigNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      setCtrlConfigNodeOverridesReq,
      serializer_);

  // Send node config overrides to ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      setCtrlConfigNodeOverridesReqMsg,
      serializer_);

  // Wait for Ack back
  recvE2EAck(querySock_, E2EConsts::kConfigAppCtrlId, true, serializer_);

  getCtrlConfigNodeOverridesReqMsg.mType =
      thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_REQ;
  thrift::GetCtrlConfigNodeOverridesReq getCtrlConfigNodeOverridesReq;
  getCtrlConfigNodeOverridesReq.nodes = {nodeName1_};
  getCtrlConfigNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      getCtrlConfigNodeOverridesReq,
      serializer_);

  // Request node config overrides from ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getCtrlConfigNodeOverridesReqMsg,
      serializer_);

  // Receive node config overrides
  std::tie(minion, senderApp, getCtrlConfigNodeOverridesRespMsg) =
      recvInCtrlApp(querySock_, serializer_);

  EXPECT_EQ(E2EConsts::kConfigAppCtrlId, senderApp);
  EXPECT_EQ(
      thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_RESP,
      getCtrlConfigNodeOverridesRespMsg.mType);
  EXPECT_NO_THROW(
      fbzmq::util::readThriftObjStr<thrift::GetCtrlConfigNodeOverridesResp>(
          getCtrlConfigNodeOverridesRespMsg.value, serializer_));

  // Receive auto node config overrides
  getCtrlConfigAutoNodeOverridesReqMsg.mType =
      thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_REQ;
  thrift::GetCtrlConfigAutoNodeOverridesReq getCtrlConfigAutoNodeOverridesReq;
  getCtrlConfigAutoNodeOverridesReq.nodes = {nodeName1_};
  getCtrlConfigAutoNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      getCtrlConfigAutoNodeOverridesReq,
      serializer_);

  // Request node config overrides from ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getCtrlConfigAutoNodeOverridesReqMsg,
      serializer_);

  // Receive node config overrides
  std::tie(minion, senderApp, getCtrlConfigAutoNodeOverridesRespMsg) =
      recvInCtrlApp(querySock_, serializer_);

  EXPECT_EQ(E2EConsts::kConfigAppCtrlId, senderApp);
  EXPECT_EQ(
      thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_RESP,
      getCtrlConfigAutoNodeOverridesRespMsg.mType);
  EXPECT_NO_THROW(
      getCtrlConfigAutoNodeOverridesResp = fbzmq::util::readThriftObjStr<
          thrift::GetCtrlConfigAutoNodeOverridesResp>(
          getCtrlConfigAutoNodeOverridesRespMsg.value, serializer_));

  // Ensure dstIp is set in tunnel config
  folly::dynamic autoNodeOverridesObject;
  EXPECT_NO_THROW(
      autoNodeOverridesObject =
          folly::parseJson(getCtrlConfigAutoNodeOverridesResp.overrides));
  EXPECT_TRUE(autoNodeOverridesObject.count(nodeName1_));
  EXPECT_TRUE(
      autoNodeOverridesObject[nodeName1_].count("tunnelConfig"));
  EXPECT_TRUE(
      autoNodeOverridesObject[nodeName1_]["tunnelConfig"].count(tunnelName1_));
  EXPECT_TRUE(
      autoNodeOverridesObject[nodeName1_]["tunnelConfig"][tunnelName1_].count(
          "dstIp"));
  EXPECT_EQ(
      autoNodeOverridesObject[nodeName1_]["tunnelConfig"][tunnelName1_]
                             ["dstIp"],
      nodeIp2_);
}

TEST_F(TunnelConfigFixture, TunnelConfigInvalidDstNodeName) {
  SCOPE_EXIT {
    LOG(INFO) << "ConfigApp test setting invalid dstNodeName in node overrides "
                 "is done";
  };

  // Update status report for destination node
  thrift::StatusReport dstNodeStatusReport;
  dstNodeStatusReport.ipv6Address = nodeIp2_;
  StatusApp::StatusReport statusAppReport;
  statusAppReport.report = dstNodeStatusReport;
  auto statusReportLock = SharedObjects::getStatusReports()->wlock();
  (*statusReportLock)[nodeMac2_] = statusAppReport;
  statusReportLock.unlock();

  string minion;
  string senderApp;
  thrift::SetCtrlConfigNodeOverridesReq setCtrlConfigNodeOverridesReq{};
  thrift::GetCtrlConfigAutoNodeOverridesResp
      getCtrlConfigAutoNodeOverridesResp{};
  thrift::Message getCtrlConfigNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigNodeOverridesRespMsg{};
  thrift::Message setCtrlConfigNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigAutoNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigAutoNodeOverridesRespMsg{};

  // Construct TunnelConfig with updated tunnel name
  thrift::TunnelConfig tunnelConfig{};
  tunnelConfig.enabled_ref() = true;
  tunnelConfig.dstNodeName_ref() = "invalidNodeName1";
  tunnelConfig.tunnelType_ref() = "GRE_L2";
  auto tunnelConfigJson =
      JsonUtils::serializeToJson<thrift::TunnelConfig>(tunnelConfig);
  folly::dynamic nodeOverridesObject = folly::dynamic::object(
      nodeName1_,
      folly::dynamic::object(
          "tunnelConfig",
          folly::dynamic::object(
              tunnelName1_, folly::parseJson(tunnelConfigJson))));

  setCtrlConfigNodeOverridesReqMsg.mType =
      thrift::MessageType::SET_CTRL_CONFIG_NODE_OVERRIDES_REQ;
  setCtrlConfigNodeOverridesReq.overrides = folly::toJson(nodeOverridesObject);
  setCtrlConfigNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      setCtrlConfigNodeOverridesReq,
      serializer_);

  // Send node config overrides to ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      setCtrlConfigNodeOverridesReqMsg,
      serializer_);

  // Wait for Ack back
  recvE2EAck(querySock_, E2EConsts::kConfigAppCtrlId, true, serializer_);

  getCtrlConfigNodeOverridesReqMsg.mType =
      thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_REQ;
  thrift::GetCtrlConfigNodeOverridesReq getCtrlConfigNodeOverridesReq;
  getCtrlConfigNodeOverridesReq.nodes = {nodeName1_};
  getCtrlConfigNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      getCtrlConfigNodeOverridesReq,
      serializer_);

  // Request node config overrides from ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getCtrlConfigNodeOverridesReqMsg,
      serializer_);

  // Receive node config overrides
  std::tie(minion, senderApp, getCtrlConfigNodeOverridesRespMsg) =
      recvInCtrlApp(querySock_, serializer_);

  EXPECT_EQ(E2EConsts::kConfigAppCtrlId, senderApp);
  EXPECT_EQ(
      thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_RESP,
      getCtrlConfigNodeOverridesRespMsg.mType);
  EXPECT_NO_THROW(
      fbzmq::util::readThriftObjStr<thrift::GetCtrlConfigNodeOverridesResp>(
          getCtrlConfigNodeOverridesRespMsg.value, serializer_));

  // Receive auto node config overrides
  getCtrlConfigAutoNodeOverridesReqMsg.mType =
      thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_REQ;
  thrift::GetCtrlConfigAutoNodeOverridesReq getCtrlConfigAutoNodeOverridesReq;
  getCtrlConfigAutoNodeOverridesReq.nodes = {nodeName1_};
  getCtrlConfigAutoNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      getCtrlConfigAutoNodeOverridesReq,
      serializer_);

  // Request node config overrides from ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getCtrlConfigAutoNodeOverridesReqMsg,
      serializer_);

  // Receive node config overrides
  std::tie(minion, senderApp, getCtrlConfigAutoNodeOverridesRespMsg) =
      recvInCtrlApp(querySock_, serializer_);

  EXPECT_EQ(E2EConsts::kConfigAppCtrlId, senderApp);
  EXPECT_EQ(
      thrift::MessageType::GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_RESP,
      getCtrlConfigAutoNodeOverridesRespMsg.mType);
  EXPECT_NO_THROW(
      getCtrlConfigAutoNodeOverridesResp = fbzmq::util::readThriftObjStr<
          thrift::GetCtrlConfigAutoNodeOverridesResp>(
          getCtrlConfigAutoNodeOverridesRespMsg.value, serializer_));

  // Ensure no tunnel config is set
  folly::dynamic autoNodeOverridesObject;
  EXPECT_NO_THROW(
      autoNodeOverridesObject =
          folly::parseJson(getCtrlConfigAutoNodeOverridesResp.overrides));
  EXPECT_TRUE(autoNodeOverridesObject.count(nodeName1_));
  EXPECT_TRUE(autoNodeOverridesObject[nodeName1_].count("tunnelConfig"));
  EXPECT_TRUE(
      autoNodeOverridesObject[nodeName1_]["tunnelConfig"].count(tunnelName1_));
  EXPECT_FALSE(
      autoNodeOverridesObject[nodeName1_]["tunnelConfig"][tunnelName1_].count(
          "dstIp"));
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
