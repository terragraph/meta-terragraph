/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <folly/init/Init.h>

#include "../SharedObjects.h"
#include "../StatusApp.h"
#include <e2e/common/Consts.h>

#include "CtrlFixture.h"
#include <e2e/common/TestUtils.h>

using namespace std;
using namespace facebook::terragraph;

namespace {

const std::string kVersionFile = "/etc/tgversion";

class CtrlStatusFixture : public CtrlFixture {
 public:
  CtrlStatusFixture()
      : CtrlFixture(),
        statusApp_(
            context_,
            ctrlAppSockUrl_,
            monitorSockUrl_,
            std::chrono::seconds(5),
            std::chrono::seconds(3600),
            kVersionFile) {
    // start with blank status reports map
    SharedObjects::getStatusReports()->wlock()->clear();

    // create topology with a few test nodes
    auto testTopology = createTopology(
      3 /* numNodes */,
      {0} /* popNodeNums */,
      {} /* linkIds */,
      2 /* numSites */,
      {{0, 0}, {1, 1}, {2, 1}} /* nodeSiteMap */);
    SharedObjects::getTopologyWrapper()->wlock()->setTopology(testTopology);

    statusAppThread_ = std::make_unique<std::thread>([this]() {
      VLOG(1) << "StatusApp thread starting";
      statusApp_.run();
      VLOG(1) << "StatusApp thread terminating";
    });
    statusApp_.waitUntilRunning();
  }

  ~CtrlStatusFixture() {
    VLOG(1) << "Stopping the StatusApp thread";
    statusApp_.stop();
    statusAppThread_->join();
  }

  std::unique_ptr<std::thread> statusAppThread_;
  StatusApp statusApp_;
};
}

TEST_F(CtrlStatusFixture, StatusApp) {
  // create topology app test to receive status app messages
  auto topoAppSock = createAppSock(E2EConsts::kTopologyAppCtrlId);
  SCOPE_EXIT { topoAppSock.close(); };

  SCOPE_EXIT { LOG(INFO) << "StatusApp test/basic operations is done"; };

  std::string node1 = "1:1:1:1:1:1";
  std::string node2 = "2:2:2:2:2:2";
  string minion{}, senderApp{};
  thrift::Message getStatusDumpMsg{}, statusDumpMsg{}, statusReportMsg{};
  thrift::StatusReport statusReport;
  statusReport.version = "asdf";
  getStatusDumpMsg.mType = thrift::MessageType::GET_STATUS_DUMP;
  getStatusDumpMsg.value =
      fbzmq::util::writeThriftObjStr(thrift::GetStatusDump(), serializer_);
  statusReportMsg.mType = thrift::MessageType::STATUS_REPORT;
  statusReportMsg.value =
      fbzmq::util::writeThriftObjStr(statusReport, serializer_);
  thrift::StatusDump statusDump{};

  // setup a socket to query StatusApp
  string querySockId = "querier";
  auto querySock = createAppSock(querySockId);
  SCOPE_EXIT { querySock.close(); };

  // query the StatusApp
  sendInCtrlApp(
      querySock,
      "",
      E2EConsts::kStatusAppCtrlId,
      querySockId,
      getStatusDumpMsg,
      serializer_);
  // We should have received zero minions in the response
  std::tie(minion, senderApp, statusDumpMsg) =
      recvInCtrlApp(querySock, serializer_);
  EXPECT_EQ("", minion);
  EXPECT_EQ(E2EConsts::kStatusAppCtrlId, senderApp);
  EXPECT_EQ(thrift::MessageType::STATUS_DUMP, statusDumpMsg.mType);
  statusDump = fbzmq::util::readThriftObjStr<thrift::StatusDump>(
      statusDumpMsg.value, serializer_);
  EXPECT_EQ(0, statusDump.statusReports.size());

  // mock minion node-1
  auto minionSock1 = createMinionSock(node1);
  SCOPE_EXIT { minionSock1.close(); };
  sendInMinionBroker(
      minionSock1,
      E2EConsts::kStatusAppCtrlId,
      E2EConsts::kStatusAppMinionId,
      statusReportMsg,
      serializer_);
  sleep(1);

  // query the StatusApp
  sendInCtrlApp(
      querySock,
      "",
      E2EConsts::kStatusAppCtrlId,
      querySockId,
      getStatusDumpMsg,
      serializer_);
  // We should have received 1 minion in the response
  std::tie(minion, senderApp, statusDumpMsg) =
      recvInCtrlApp(querySock, serializer_);
  EXPECT_EQ("", minion);
  EXPECT_EQ(E2EConsts::kStatusAppCtrlId, senderApp);
  EXPECT_EQ(thrift::MessageType::STATUS_DUMP, statusDumpMsg.mType);
  statusDump = fbzmq::util::readThriftObjStr<thrift::StatusDump>(
      statusDumpMsg.value, serializer_);
  EXPECT_EQ(1, statusDump.statusReports.size());

  // mock minion node-2
  auto minionSock2 = createMinionSock(node2);
  SCOPE_EXIT { minionSock2.close(); };
  sendInMinionBroker(
      minionSock2,
      E2EConsts::kStatusAppCtrlId,
      E2EConsts::kStatusAppMinionId,
      statusReportMsg,
      serializer_);
  sleep(1);

  // query the StatusApp
  sendInCtrlApp(
      querySock,
      "",
      E2EConsts::kStatusAppCtrlId,
      querySockId,
      getStatusDumpMsg,
      serializer_);
  // We should have received 2 minions in the response
  std::tie(minion, senderApp, statusDumpMsg) =
      recvInCtrlApp(querySock, serializer_);
  EXPECT_EQ("", minion);
  EXPECT_EQ(E2EConsts::kStatusAppCtrlId, senderApp);
  EXPECT_EQ(thrift::MessageType::STATUS_DUMP, statusDumpMsg.mType);
  statusDump = fbzmq::util::readThriftObjStr<thrift::StatusDump>(
      statusDumpMsg.value, serializer_);
  EXPECT_EQ(2, statusDump.statusReports.size());
}

TEST_F(CtrlStatusFixture, StatusAppFirstStatusReport) {
  auto topoAppSock = createAppSock(E2EConsts::kTopologyAppCtrlId);
  SCOPE_EXIT { topoAppSock.close(); };

  thrift::Message statusReportMsg{};
  statusReportMsg.mType = thrift::MessageType::STATUS_REPORT;
  thrift::StatusReport statusReport;
  statusReport.status = thrift::NodeStatusType::OFFLINE;
  statusReport.version = "jkl";
  statusReportMsg.value =
      fbzmq::util::writeThriftObjStr(statusReport, serializer_);

  // mock minion node-1 sending heartbeat
  std::string node1 = "1:1:1:1:1:1";
  auto minionSock1 = createMinionSock(node1);
  SCOPE_EXIT { minionSock1.close(); };
  sendInMinionBroker(
      minionSock1,
      E2EConsts::kStatusAppCtrlId,
      E2EConsts::kStatusAppMinionId,
      statusReportMsg,
      serializer_);

  // topoAppSock should receive SET_NODE_PARAMS_REQ
  string minion, senderApp;
  thrift::Message msg;
  std::tie(minion, senderApp, msg) = recvInCtrlApp(topoAppSock, serializer_);
  EXPECT_EQ("", minion);
  EXPECT_EQ(E2EConsts::kStatusAppCtrlId, senderApp);
  auto setNodeParamsReq =
      fbzmq::util::readThriftObjStr<thrift::SetNodeParamsReq>(
            msg.value, serializer_);
  EXPECT_EQ(node1, setNodeParamsReq.nodeMac);
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
