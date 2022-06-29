/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <folly/init/Init.h>

#include "../UpgradeApp.h"
#include <e2e/common/Consts.h>

#include "CtrlFixture.h"
#include <e2e/common/TestUtils.h>

using namespace std;
using namespace facebook::terragraph;

namespace {

class CtrlUpgradeFixture : public CtrlFixture {
 public:
  CtrlUpgradeFixture()
      : CtrlFixture(),
        upgradeApp_(
            context_,
            ctrlAppSockUrl_,
            monitorSockUrl_,
            std::chrono::seconds(5)) { /* FLAGS_status_dump_interval_s */
    upgradeAppThread_ = std::make_unique<std::thread>([this]() {
      VLOG(1) << "UpgradeApp thread starting";
      upgradeApp_.run();
      VLOG(1) << "UpgradeApp thread terminating";
    });
    upgradeApp_.waitUntilRunning();
    nmsSock_ = createAppSock(nmsSockId_);
  }

  ~CtrlUpgradeFixture() {
    VLOG(1) << "Stopping the UpgradeApp thread";
    upgradeApp_.stop();
    upgradeAppThread_->join();
  }

  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> nmsSock_;
  const std::string nmsSockId_ = "NMS_SOCK_ID";
  std::unique_ptr<std::thread> upgradeAppThread_;
  UpgradeApp upgradeApp_;
};

} //namespace

/* test for NodesUpgradeReq routing */
TEST_F(CtrlUpgradeFixture, upgradeNodesRoute) {
  // UpgradeGroupReq message
  thrift::Message upgradeMsg {};
  thrift::UpgradeGroupReq upgradeReq {};
  upgradeReq.urReq.urType = thrift::UpgradeReqType::RESET_STATUS;
  upgradeMsg.mType = thrift::MessageType::UPGRADE_GROUP_REQ;
  upgradeMsg.value =
    fbzmq::util::writeThriftObjStr(upgradeReq, serializer_);

  // send UpgradeGroupReq message
  sendInCtrlApp(
      nmsSock_,
      "",
      E2EConsts::kUpgradeAppCtrlId,
      nmsSockId_,
      upgradeMsg,
      serializer_);
  recvE2EAck(nmsSock_, E2EConsts::kUpgradeAppCtrlId, true, serializer_);
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
