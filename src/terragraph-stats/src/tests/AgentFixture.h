/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/zmq/Zmq.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

namespace facebook {
namespace terragraph {
namespace stats {

// A common fixture which can be used for agent unit tests.
class AgentFixture : public ::testing::Test {
 public:
  // Create a router socket to emulate the aggregator.
  // The dealer socket in agent-broker will talk to this.
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> createAggrSock();

  const std::string agentAggrSockUrl_{"ipc://agent-aggr-dealer"};
  const std::string macAddr_{"00:00:00:00:00:00"};

  fbzmq::Context context_;

  apache::thrift::CompactSerializer serializer_;
};

} // namesapce stats
} // namespace terragraph
} // namespace facebook
