/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/gen/Base.h>
#include <folly/init/Init.h>

#include <e2e/common/Consts.h>
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/DriverMessage_types.h"
#include <e2e/common/TestUtils.h>

#include "../StatusApp.h"
#include "MinionFixture.h"
#include "e2e/common/NodeConfigWrapper.h"

using namespace std;
using namespace facebook::terragraph;

namespace {
const std::string kVersionFile = "/etc/tgversion";

thrift::UpgradeStatus upgradeStatus;

class MinionStatusFixture : public MinionFixture {
 public:
  MinionStatusFixture()
      : MinionFixture(),
        statusApp_(
            zmqContext_,
            minionAppSockUrl_,
            monitorSockUrl_,
            macAddr_,
            std::chrono::seconds(1), // statusReportInterval
            std::chrono::seconds(30), // bgpStatusInterval
            "lo", // ipv6GlobalAddressableIfname
            upgradeStatus,
            kVersionFile) {

    if (!folly::readFile(kVersionFile.c_str(), version_)) {
      LOG(INFO) << "No version file available. "
                << "StatusReport should have empty version.";
    }
    version_ = folly::trimWhitespace(version_).str();
    LOG(INFO) << "Current version: " << version_;

    statusAppThread_ = std::make_unique<std::thread>([this]() {
      VLOG(1) << "StatusApp thread starting";
      statusApp_.run();
      VLOG(1) << "StatusApp thread terminating";
    });
    statusApp_.waitUntilRunning();
    driverAppSock_ = createAppSock(E2EConsts::kDriverAppMinionId);
    ctrlSock_ = createCtrlSock();
  }

  ~MinionStatusFixture() {
    LOG(INFO) << "Stopping the StatusApp thread";
    statusApp_.stop();
    statusAppThread_->join();
  }

  std::string version_;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> driverAppSock_;
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> ctrlSock_;
  std::unique_ptr<std::thread> statusAppThread_;
  minion::StatusApp statusApp_;

  template <class T>
  T
  readDriverMessage(const thrift::Message& message) {
    thrift::DriverMessage driverMsg =
        fbzmq::util::readThriftObjStr<thrift::DriverMessage>(
            message.value, serializer_);
    return fbzmq::util::readThriftObjStr<T>(driverMsg.value, serializer_);
  }

  // TODO don't copy/paste this function (won't compile inside MinionFixture)
  template <class T>
  thrift::Message
  createDriverMessage(
      const std::string& radioMac,
      thrift::MessageType mType,
      const T& obj) const {
    thrift::DriverMessage driverMsg;
    driverMsg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);
    driverMsg.radioMac = radioMac;
    thrift::Message msg;
    msg.mType = mType;
    msg.value = fbzmq::util::writeThriftObjStr(driverMsg, serializer_);
    return msg;
  }

  void
  verifyDriverAppMsgRecv() {
    thrift::Message req, resp;
    bool recvHello = false;
    bool recvInit = false;

    while (true) {
      // receive request from statusApp
      auto senderAppMsg = driverAppSock_.recvOne();
      auto senderApp = senderAppMsg->read<std::string>().value();
      EXPECT_EQ(E2EConsts::kStatusAppMinionId, senderApp);
      req = driverAppSock_.recvThriftObj<thrift::Message>(serializer_).value();

      if (req.mType == thrift::MessageType::HELLO) {
        LOG(INFO) << "Received hello from " << senderApp;
        resp.mType = thrift::MessageType::HELLO;
        resp.value =
            fbzmq::util::writeThriftObjStr(thrift::Hello(), serializer_);
        recvHello = true;
      } else if (req.mType == thrift::MessageType::NODE_INIT) {
        LOG(INFO) << "Received node init request from " << senderApp;
        // init should come after hello
        EXPECT_TRUE(recvHello);
        // check node init request has correct information
        auto nodeInitReq = readDriverMessage<thrift::DriverNodeInitReq>(req);
        NodeConfigWrapper nodeConfigWrapper("");
        auto radioParams = nodeConfigWrapper.getRadioParams();
        EXPECT_EQ(nodeInitReq.optParams, radioParams.fwParams);
        recvInit = true;

        LOG(INFO) << "Send node init notify to " << senderApp;
        thrift::DriverNodeInitNotif driverNodeInitNotif;
        driverNodeInitNotif.success = true;
        driverNodeInitNotif.macAddr = "";
        driverNodeInitNotif.vendor = "";
        resp = createDriverMessage<thrift::DriverNodeInitNotif>(
            "",
            thrift::MessageType::NODE_INIT_NOTIFY,
            driverNodeInitNotif);
      } else {
        LOG(INFO) << "Received unexpected message";
        continue;
      }

      // send resp
      sendInMinionApp(
          driverAppSock_,
          macAddr_,
          E2EConsts::kStatusAppMinionId,
          E2EConsts::kDriverAppMinionId,
          resp,
          serializer_);

      if (recvHello && recvInit) {
        break;
      }
    }
  }

  void
  sendStatusReportAck() {
    thrift::Message statusReportAckMsg;
    statusReportAckMsg.mType = thrift::MessageType::STATUS_REPORT_ACK;
    statusReportAckMsg.value =
        fbzmq::util::writeThriftObjStr(thrift::StatusReportAck(), serializer_);
    sendInCtrlBroker(
        ctrlSock_,
        macAddr_,
        E2EConsts::kStatusAppMinionId,
        E2EConsts::kStatusAppCtrlId,
        statusReportAckMsg,
        serializer_);
    LOG(INFO) << "Successfully sent StatusReportAck";
  }

  // Verify minion status app sends first status report to controller with
  // status = OFFLINE
  void
  verifyFirstTimeStatusReportRecv() {
    // receive status report from minion status app in ctrl status app
    std::string minion, receiverApp, senderApp;
    thrift::Message msg;
    std::tie(minion, receiverApp, senderApp, msg) =
        recvInCtrlBroker(ctrlSock_, serializer_);

    EXPECT_EQ(minion, macAddr_);
    EXPECT_EQ(receiverApp, E2EConsts::kStatusAppCtrlId);
    EXPECT_EQ(senderApp, E2EConsts::kStatusAppMinionId);
    EXPECT_EQ(msg.mType, thrift::MessageType::STATUS_REPORT);
    auto statusReport = fbzmq::util::readThriftObjStr<thrift::StatusReport>(
        msg.value, serializer_);
    EXPECT_EQ(statusReport.version, version_);
    EXPECT_EQ(statusReport.status, thrift::NodeStatusType::OFFLINE);
    // Minion should not send a nodeReachability metric on its first report
    EXPECT_FALSE(statusReport.nodeReachability_ref().has_value());
    LOG(INFO) << "Received first statusReport from " << minion << " : "
              << senderApp;

    sendStatusReportAck();

    // status in status Report is OFFLINE, send SetNodeParams
    thrift::Message setNodeParamsMsg;
    setNodeParamsMsg.mType = thrift::MessageType::SET_NODE_PARAMS;
    setNodeParamsMsg.value =
        fbzmq::util::writeThriftObjStr(thrift::NodeParams(), serializer_);
    sendInCtrlBroker(
        ctrlSock_,
        macAddr_,
        E2EConsts::kStatusAppMinionId,
        E2EConsts::kTopologyAppCtrlId,
        setNodeParamsMsg,
        serializer_);
    LOG(INFO) << "Successfully sent SetNodeParams to " << minion << " : "
              << senderApp;
  }

  // Verify minion has established stable connection with controller
  void
  verifyStatusReportRecv() {
    // receive hearbeats
    std::string minion, receiverApp, senderApp;
    thrift::Message msg;

    for (int i = 0; i < 3; i++) {
      std::tie(minion, receiverApp, senderApp, msg) =
          recvInCtrlBroker(ctrlSock_, serializer_);

      EXPECT_EQ(minion, macAddr_);
      EXPECT_EQ(receiverApp, E2EConsts::kStatusAppCtrlId);
      EXPECT_EQ(senderApp, E2EConsts::kStatusAppMinionId);
      EXPECT_EQ(msg.mType, thrift::MessageType::STATUS_REPORT);
      auto statusReport = fbzmq::util::readThriftObjStr<thrift::StatusReport>(
          msg.value, serializer_);
      EXPECT_EQ(statusReport.status, thrift::NodeStatusType::ONLINE);
      LOG(INFO) << "Received statusReport from " << minion << " : "
                << senderApp;

      sendStatusReportAck();
    }
  }

  // Verify minion tracks ping ack rate correctly
  void
  verifyStatusReportAckMetric() {
    // receive heartbeats
    std::string minion, receiverApp, senderApp;
    thrift::Message msg;
    double prevReachability;

    // Send 10 status reports to controller
    // Verify that reachability drops when no ack is received
    // Verify that reachability goes back up when acks are received again
    for (int i = 0; i < 10; i++) {
      std::tie(minion, receiverApp, senderApp, msg) =
        recvInCtrlBroker(ctrlSock_, serializer_);

      EXPECT_EQ(minion, macAddr_);
      EXPECT_EQ(receiverApp, E2EConsts::kStatusAppCtrlId);
      EXPECT_EQ(senderApp, E2EConsts::kStatusAppMinionId);
      EXPECT_EQ(msg.mType, thrift::MessageType::STATUS_REPORT);
      auto statusReport = fbzmq::util::readThriftObjStr<thrift::StatusReport>(
        msg.value, serializer_);
      EXPECT_EQ(statusReport.status, thrift::NodeStatusType::ONLINE);

      // Verify stat is correct when an ack is not sent
      switch (i) {
        case 0:
          // Note: we expect 1 here since minion is receiving acks for all
          // status reports it sends throughout other tests
          EXPECT_TRUE(statusReport.nodeReachability_ref().has_value());
          EXPECT_EQ(statusReport.nodeReachability_ref().value(), 1.0);
          break;
        case 1:
          EXPECT_TRUE(statusReport.nodeReachability_ref().has_value());
          EXPECT_LT(statusReport.nodeReachability_ref().value(),
              prevReachability);
          break;
        case 2:
          EXPECT_TRUE(statusReport.nodeReachability_ref().has_value());
          EXPECT_LT(statusReport.nodeReachability_ref().value(),
              prevReachability);
          sendStatusReportAck();
          break;
        default:
          EXPECT_TRUE(statusReport.nodeReachability_ref().has_value());
          EXPECT_GE(statusReport.nodeReachability_ref().value(),
              prevReachability);
          sendStatusReportAck();
      }
      prevReachability = statusReport.nodeReachability_ref().value();
    }
  }
};
} // anonymous namespace

TEST_F(MinionStatusFixture, statusReportFlow) {
  // check node init request and node config request has been sent
  verifyDriverAppMsgRecv();

  verifyFirstTimeStatusReportRecv();

  verifyStatusReportRecv();

  verifyStatusReportAckMetric();
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
