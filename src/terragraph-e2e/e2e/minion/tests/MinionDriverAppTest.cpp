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

#include "../DriverApp.h"
#include <e2e/common/Consts.h>

#include "MinionFixture.h"
#include <e2e/common/TestUtils.h>

using namespace std;
using namespace facebook::terragraph;

namespace {

class MinionDriverFixture : public MinionFixture {
 public:
  MinionDriverFixture()
      : MinionFixture(),
        driverApp_(
            zmqContext_,
            minionAppSockUrl_,
            monitorSockUrl_,
            driverPairSockUrl_,
            macAddr_) {
    driverAppThread_ = std::make_unique<std::thread>([this]() {
      VLOG(1) << "DriverApp thread starting";
      driverApp_.run();
      VLOG(1) << "DriverApp thread terminating";
    });
    driverApp_.waitUntilRunning();
    minionStatusAppSock_ = createAppSock(E2EConsts::kStatusAppMinionId);
  }

  ~MinionDriverFixture() {
    LOG(INFO) << "Minion DriverApp test operations is done";
    driverApp_.stop();
    driverAppThread_->join();
  }

  minion::DriverApp driverApp_;
  std::unique_ptr<std::thread> driverAppThread_;
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> minionStatusAppSock_;

  void
  verifyHelloRecv() {
    thrift::Message msg;
    msg.mType = thrift::MessageType::HELLO;
    msg.value = fbzmq::util::writeThriftObjStr(thrift::Hello(), serializer_);

    sendInMinionApp(
        minionStatusAppSock_,
        macAddr_,
        E2EConsts::kDriverAppMinionId,
        E2EConsts::kStatusAppMinionId,
        msg,
        serializer_);

    msg = recvInMinionApp(minionStatusAppSock_, serializer_).second;
    EXPECT_EQ(thrift::MessageType::HELLO, msg.mType);
  }
};
} // anonymous namespace

// --- Ignition Minion Work Flow tests ---
TEST_F(MinionDriverFixture, verifyHello) { verifyHelloRecv(); }

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
