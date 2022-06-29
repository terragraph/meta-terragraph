/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "AgentFixture.h"

#include "../common/Consts.h"

using namespace facebook::terragraph;

namespace facebook {
namespace terragraph {
namespace stats {

// Create an aggregator sock which agent dealer sock talks to
fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER>
AgentFixture::createAggrSock() {
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> sock(
      context_,
      fbzmq::IdentityString{NMSConsts::kBrokerAggrId});
  sock.bind(fbzmq::SocketUrl{agentAggrSockUrl_}).value();
  return sock;
}

} // namesapce stats
} // namespace terragraph
} // namespace facebook
