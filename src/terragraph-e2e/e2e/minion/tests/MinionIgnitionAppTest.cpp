/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <folly/FileUtil.h>
#include <folly/MacAddress.h>
#include <folly/gen/Base.h>
#include <folly/init/Init.h>

#include "../IgnitionApp.h"
#include <e2e/common/Consts.h>
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/DriverMessage_types.h"

#include "MinionFixture.h"
#include <e2e/common/TestUtils.h>

using namespace std;
using namespace facebook::terragraph;

namespace {

class MinionIgnitionFixture : public MinionFixture {
 public:
  MinionIgnitionFixture()
      : MinionFixture(),
        ignitionApp_(
            zmqContext_,
            minionAppSockUrl_,
            monitorSockUrl_,
            macAddr_,
            std::chrono::seconds(30) /* linkupRespWaitTimeout */,
            0L /* wsecEnable - disable wsec for testing */) {
    ignitionAppThread_ = std::make_unique<std::thread>([this]() {
      VLOG(1) << "MinionIgnitionApp thread starting";
      ignitionApp_.run();
      VLOG(1) << "MinionIgnitionApp thread terminating";
    });
    ignitionApp_.waitUntilRunning();

    driverAppSock_ = createAppSock(E2EConsts::kDriverAppMinionId);
    ctrlSock_ = createCtrlSock();

    // wait for minion app sock to get connected
    /* sleep override */
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  ~MinionIgnitionFixture() {
    LOG(INFO) << "Minion IgnitionApp test operations is done";
    ignitionApp_.stop();
    ignitionAppThread_->join();
  }

  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> driverAppSock_;
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> ctrlSock_;
  std::unique_ptr<std::thread> ignitionAppThread_;
  minion::IgnitionApp ignitionApp_;

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
  sendSetLinkStatusMsg(
      const std::string& responderMac,
      const thrift::LinkStatusType linkStatusType) {
    thrift::SetLinkStatus setLinkStatus;
    setLinkStatus.linkStatusType = linkStatusType;
    setLinkStatus.responderMac = responderMac;
    thrift::Message msg;
    msg.mType = thrift::MessageType::SET_LINK_STATUS;
    msg.value = fbzmq::util::writeThriftObjStr(setLinkStatus, serializer_);

    sendInCtrlBroker(
        ctrlSock_,
        macAddr_,
        E2EConsts::kIgnitionAppMinionId,
        E2EConsts::kIgnitionAppCtrlId,
        msg,
        serializer_);
  }

  void
  verifyDrSetLinkStatusRecv() {
    thrift::Message msg = recvInMinionApp(driverAppSock_, serializer_).second;
    EXPECT_EQ(thrift::MessageType::DR_SET_LINK_STATUS, msg.mType);
  }

  // send link status from driver to ignition minion app
  void
  sendDrLinkStatusMsg(
      const bool valid,
      const std::string& macAddr,
      const thrift::DriverLinkStatusType linkStatusType) {
    thrift::DriverLinkStatus drLinkStatus;
    drLinkStatus.drLinkStatusType = linkStatusType;
    drLinkStatus.macAddr = macAddr;
    drLinkStatus.valid = valid;
    thrift::Message msg = createDriverMessage(
        "", thrift::MessageType::DR_LINK_STATUS, drLinkStatus);

    sendInMinionApp(
        driverAppSock_,
        macAddr_,
        E2EConsts::kIgnitionAppMinionId,
        E2EConsts::kDriverAppMinionId,
        msg,
        serializer_);
  }

  // verify controller topology app receives link status update
  void
  verifyLinkStatusRecv(const thrift::LinkStatusType linkStatusType) {
    std::string minion, receiverApp, senderApp;
    thrift::Message msg;
    std::tie(minion, receiverApp, senderApp, msg) =
        recvInCtrlBroker(ctrlSock_, serializer_);

    EXPECT_EQ(minion, macAddr_);
    EXPECT_EQ(receiverApp, E2EConsts::kTopologyAppCtrlId);
    EXPECT_EQ(senderApp, E2EConsts::kIgnitionAppMinionId);
    EXPECT_EQ(msg.mType, thrift::MessageType::LINK_STATUS);
    auto linkStatus = fbzmq::util::readThriftObjStr<thrift::LinkStatus>(
        msg.value, serializer_);
    EXPECT_EQ(linkStatus.linkStatusType, linkStatusType);
    LOG(INFO) << "Received LINK_STATUS from " << minion << " : " << senderApp;
  }
};
} // anonymous namespace

// --- Ignition Minion Work Flow tests ---
TEST_F(MinionIgnitionFixture, LinkIgnitionCtrl2Minion) {
  std::string macAddr = "1:1:1:1:1:1";

  // verify SET_LINK_STATUS is sent
  sendSetLinkStatusMsg(
      macAddr, /* mac addr */
      thrift::LinkStatusType::LINK_UP);
  // verify DR_SET_LINK_STATUS is received by driver app
  verifyDrSetLinkStatusRecv();
}

TEST_F(MinionIgnitionFixture, LinkIgnitionMinion2Ctrl) {
  std::string macAddr = "1:1:1:1:1:1";

  // invalid DR_LINK_STATUS msg will not be forwarded all the way to controller
  sendDrLinkStatusMsg(false, macAddr, thrift::DriverLinkStatusType::LINK_UP);

  // send DR_LINK_STATUS from driver to ignition minion app
  sendDrLinkStatusMsg(true, macAddr, thrift::DriverLinkStatusType::LINK_UP);
  // make sure ignition minion app sends LINK_UP to topology app
  verifyLinkStatusRecv(thrift::LinkStatusType::LINK_UP);

  // In this case, the neighbor is already ignited, if ignition minion app
  // receives another SET_LINK_STATUS, it just sends out LINK_UP LinkStatus
  sendSetLinkStatusMsg(macAddr, thrift::LinkStatusType::LINK_UP);
  verifyLinkStatusRecv(thrift::LinkStatusType::LINK_UP);

  // Bring down the neighbor that has been ignited previously
  sendDrLinkStatusMsg(true, macAddr, thrift::DriverLinkStatusType::LINK_DOWN);
  verifyLinkStatusRecv(thrift::LinkStatusType::LINK_DOWN);

  // If a neighbor is down, or the neighbor could be up,
  // but the minion does not know about it.
  // In which case send a assoc to driver app.
  sendSetLinkStatusMsg(macAddr, thrift::LinkStatusType::LINK_UP);
  // verify DR_SET_LINK_STATUS is received by driver app
  verifyDrSetLinkStatusRecv();
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
