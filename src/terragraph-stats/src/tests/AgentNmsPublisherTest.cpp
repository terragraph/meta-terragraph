/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <folly/FileUtil.h>
#include <folly/gen/Base.h>
#include <folly/init/Init.h>

#include "../agent/NmsPublisher.h"
#include "../common/Consts.h"
#include "e2e/common/Consts.h"

#include "AgentFixture.h"
#include "Utils.h"

using namespace std;
using namespace fbzmq;
using namespace facebook::terragraph;

namespace {
const std::string kMyNetworkInfoFile = "/tmp/mynetworkinfo";
} // namespace

namespace facebook {
namespace terragraph {
namespace stats {

// Receive a stats report from the aggregator socket
void
recvStatsReport(
    fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER>& aggrSock,
    const std::string& macAddr,
    apache::thrift::CompactSerializer* serializer) {
  std::string agent, receiverApp, senderApp;
  thrift::AggrMessage msg;

  agent = aggrSock.recvOne().value().read<std::string>().value();
  std::tie(receiverApp, senderApp, msg) = recvInAggrApp(aggrSock, serializer);

  EXPECT_EQ(agent, macAddr);
  EXPECT_EQ(receiverApp, NMSConsts::kStatsAppAggrId);
  EXPECT_EQ(senderApp, NMSConsts::kNmsPublisherId);
  EXPECT_EQ(msg.mType, thrift::AggrMessageType::STATS_REPORT);
  LOG(INFO) << "Received STATS_REPORT from " << agent << " : " << senderApp;
}

// Verify agent nms publisher sends first status report to aggregator
void
verifyFirstTimeReportsRecv(
    fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER>& aggrSock,
    const std::string& macAddr,
    apache::thrift::CompactSerializer* serializer) {
  recvStatsReport(aggrSock, macAddr, serializer);
}

// Verify agent nms publisher has established stable connection with aggregator
void
verifyReportsRecv(
    fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER>& aggrSock,
    const std::string& macAddr,
    apache::thrift::CompactSerializer* serializer) {
  // receive 3 heartbeats
  for (int i = 0; i < 3; i++) {
    recvStatsReport(aggrSock, macAddr, serializer);
  }
}

TEST_F(AgentFixture, NmsPublisher) {
  SCOPE_EXIT { LOG(INFO) << "Agent NmsPublisher test operations is done"; };

  // construct config
  thrift::StatsAgentParams statsAgentParams;
  statsAgentParams.publisherParams.defaultStatsInterval = 1;
  statsAgentParams.publisherParams.highFrequencyStatsInterval = 1;
  statsAgentParams.publisherParams.statsBufferSize = 10000;
  statsAgentParams.publisherParams.eventLogsBufferSize = 2000;
  thrift::NmsPublisherParams nmsPublisherParams;
  nmsPublisherParams.enabled = true;
  nmsPublisherParams.zmqSndHwm = 100;
  statsAgentParams.endpointParams.nmsPublisherParams_ref() = nmsPublisherParams;

  // setup NmsPublisher
  NmsPublisher nmsPublisher(
      context_,
      macAddr_,
      agentAggrSockUrl_,
      kMyNetworkInfoFile,
      statsAgentParams);

  auto nmsPublisherThread = std::make_unique<std::thread>([&nmsPublisher]() {
    LOG(INFO) << "NmsPublisher thread starting";
    nmsPublisher.run();
    LOG(INFO) << "NmsPublisher thread terminating";
  });
  SCOPE_EXIT {
    LOG(INFO) << "Stopping the NmsPublisher thread";
    nmsPublisher.stop();
    nmsPublisherThread->join();
  };
  nmsPublisher.waitUntilRunning();

  // create aggregator socket
  auto aggrSock = createAggrSock();
  SCOPE_EXIT { aggrSock.close(); };

  // verify messages were received by aggregator
  verifyFirstTimeReportsRecv(aggrSock, macAddr_, &serializer_);
  verifyReportsRecv(aggrSock, macAddr_, &serializer_);
}

} // namesapce stats
} // namespace terragraph
} // namespace facebook

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
