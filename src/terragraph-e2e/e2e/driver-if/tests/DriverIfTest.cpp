/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fbzmq/zmq/Zmq.h>
#include <gtest/gtest.h>

#include <folly/init/Init.h>

#include "../ArmDriverIf.h"
#include "../DriverIfUtil.h"
#include "../X86NetlinkSocket.h"

namespace {
const auto kPairSockUrl = "ipc://pair-sock";
const auto kMonitorUrl = "ipc://monitor-sock";
const auto kSleepTime = std::chrono::milliseconds{200};
const auto kMacAddr = "1:1:1:1:1:1";
} // namespace

using namespace facebook::terragraph;

class DriverIfFixture : public ::testing::Test {
 public:
  void
  SetUp() override {
    std::unordered_map<std::string, std::string> radioMacToBusId;
    driverIf = std::make_unique<ArmDriverIf>(
        zmqContext,
        kPairSockUrl,
        std::string{kMonitorUrl},
        std::make_unique<X86NetlinkSocket>(),
        false /* isIf2if */,
        radioMacToBusId,
        false /* daemonMode */);
    driverIfThread = std::thread([this]() noexcept {
      LOG(INFO) << "Starting DriverIf thread...";
      driverIf->run();
      LOG(INFO) << "DriverIf thread got stopped";
    });
    driverIf->waitUntilRunning();
    EXPECT_EQ(0, driverIf->getUpNotifCount());
    EXPECT_EQ(0, driverIf->getDrRespCount());
  }

  void
  TearDown() override {
    driverIf->stop();
    driverIfThread.join();
  }

  fbzmq::Context zmqContext;
  std::unique_ptr<ArmDriverIf> driverIf;
  std::thread driverIfThread;
};

TEST_F(DriverIfFixture, NodeInit) {
  driverIf->sendNodeInit(kMacAddr, thrift::DriverNodeInitReq());

  // wait for driverIf to process the response
  /* sleep override */
  std::this_thread::sleep_for(kSleepTime);
  EXPECT_EQ(1, driverIf->getUpNotifCount());
}

TEST_F(DriverIfFixture, AssocLink) {
  thrift::DriverSetLinkStatus linkStatusReq;
  linkStatusReq.isAssoc = true;
  linkStatusReq.responderMac = kMacAddr;

  driverIf->sendLinkStatus(kMacAddr, linkStatusReq);

  // wait for driverIf to process the response
  /* sleep override */
  std::this_thread::sleep_for(kSleepTime);
  EXPECT_EQ(1, driverIf->getDrRespCount());
}

TEST_F(DriverIfFixture, LocationGps) {
  driverIf->sendLocationToGps(thrift::Location());

  // wait for driverIf to process the response
  /* sleep override */
  std::this_thread::sleep_for(kSleepTime);
  EXPECT_EQ(1, driverIf->getUpNotifCount());
}

TEST_F(DriverIfFixture, ECEFConversion) {
  // geodetic coords sourced from google maps
  // ecef coords sourced from https://www.ngs.noaa.gov/NCAT/
  {
    double lat, lon, h;
    // 1 Hacker Way, Menlo Park, CA
    facebook::terragraph::convertEcefToGeodetic(
        -2696275.866, -4290606.800, 3860182.483, lat, lon, h);

    EXPECT_NEAR(37.4843038, lat, 0.000001);
    EXPECT_NEAR(-122.1458147, lon, 0.000001);
    EXPECT_NEAR(1.0, h, 0.001);
  }
  {
    double lat, lon, h;
    // Mount Everest
    facebook::terragraph::convertEcefToGeodetic(
        302771.148, 5636003.102, 2979483.843, lat, lon, h);

    EXPECT_NEAR(27.9881206, lat, 0.000001);
    EXPECT_NEAR(86.9249751, lon, 0.000001);
    EXPECT_NEAR(8824.0, h, 0.001);
  }
  {
    double lat, lon, h;
    facebook::terragraph::convertEcefToGeodetic(
        6378137.0, 0.0, 0.0, lat, lon, h);

    EXPECT_NEAR(0, lat, 0.000001);
    EXPECT_NEAR(0, lon, 0.000001);
    EXPECT_NEAR(0, h, 0.001);
  }
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
