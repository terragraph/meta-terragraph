/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Broker.h"

#include <fbzmq/zmq/Zmq.h>
#include <folly/Format.h>
#include <folly/MapUtil.h>

#include "../common/Consts.h"
#include "stats/if/gen-cpp2/Aggregator_types.h"

using namespace fbzmq;

using std::string;

namespace facebook {
namespace terragraph {
namespace stats {

Broker::Broker(
    fbzmq::Context& context,
    const std::string& agentsSockBindUrl,
    const std::string& appsSockBindUrl)
    : agentsSockBindUrl_(agentsSockBindUrl),
      appsSockBindUrl_(appsSockBindUrl),
      agentsSock_(context, fbzmq::IdentityString{NMSConsts::kBrokerAggrId}),
      appsSock_(context, fbzmq::IdentityString{NMSConsts::kBrokerAggrId}) {
  prepare();
}

void
Broker::prepare() noexcept {
  // Overwrite default TCP_KEEPALIVE options to handle agent crash and
  // drop dead socket after 30 secs

  const int keepAlive = 1;
  const int keepAliveIdle = 15;
  const int keepAliveCnt = 3;
  const int keepAliveIntvl = 5;
  agentsSock_.setKeepAlive(
      keepAlive,
      keepAliveIdle,
      keepAliveCnt,
      keepAliveIntvl).value();

  // enable ZMQ_ROUTER_HANDOVER
  // Ideally the TCP keepalives should be able to handle all scenarios. But
  // if an existing connection's tcp keepalive period hasnt expired to close
  // the connection, and if agent tries to connect from the node on a new
  // connection, then zmq does consume the packets from the new TCP connection.
  // (even after the old connection is closed after keepalive timeout)
  const int enableHandOver = 1;
  agentsSock_.setSockOpt(
      ZMQ_ROUTER_HANDOVER, &enableHandOver, sizeof(int)).value();

  // tag all management messages as DSCP to differenciate with data traffic
  agentsSock_.setSockOpt(ZMQ_TOS, &NMSConsts::kIpTos, sizeof(int)).value();

  // Bind the socket to the listenAddr:routerPort
  VLOG(1) << "Binding to '" << agentsSockBindUrl_ << "'";
  agentsSock_.bind(fbzmq::SocketUrl{agentsSockBindUrl_}).value();

  // Bind appsSock_ socket to the listenAddr:routerPort
  VLOG(1) << "Binding to '" << appsSockBindUrl_ << "'";
  appsSock_.bind(fbzmq::SocketUrl{appsSockBindUrl_}).value();

  // read status events from the agents socket
  addSocket(
      fbzmq::RawZmqSocketPtr{*agentsSock_},
      ZMQ_POLLIN,
      [this](int) noexcept {

    fbzmq::Message agent, receiverApp, senderApp, data;
    auto res1 = agentsSock_.recvMultipleTimeout(
        NMSConsts::kReadTimeout, agent, receiverApp, senderApp, data);
    if (res1.hasError()) {
      LOG(ERROR) << "Error reading message. " << res1.error();
      return;
    }

    auto res2 = appsSock_.sendMultiple(receiverApp, agent, senderApp, data);
    if (res2.hasError()) {
      LOG(ERROR) << "Error routing msg from "
                 << agent.read<std::string>().value()
                 << ":" << senderApp.read<std::string>().value() << " to "
                 << receiverApp.read<std::string>().value()
                 << ". " << res2.error();
      return;
    }
  });

  // read status events from the apps socket
  addSocket(
      fbzmq::RawZmqSocketPtr{*appsSock_},
      ZMQ_POLLIN,
      [this](int) noexcept {

    fbzmq::Message frame1, agent, receiverApp, senderApp, data;
    auto res1 = appsSock_.recvMultipleTimeout(
        NMSConsts::kReadTimeout, frame1, agent, receiverApp, senderApp, data);
    if (res1.hasError()) {
      LOG(ERROR) << "Error reading message: " << res1.error();
      return;
    }

    if (!agent.empty()) {
      // Send it to agent through agentSock_
      auto res = agentsSock_.sendMultiple(agent, receiverApp, senderApp, data);
      if (res.hasError()) {
        LOG(ERROR) << "Error routing msg from "
                   << senderApp.read<std::string>().value() << " to "
                   << agent.read<std::string>().value() << ":"
                   << receiverApp.read<std::string>().value() << ". "
                   << res.error();
        return;
      }
    } else {
      // Else route it to the corresponding receiverApp in Aggr
      auto res = appsSock_.sendMultiple(receiverApp, agent, senderApp, data);
      if (res.hasError()) {
        LOG(ERROR) << "Error routing msg from "
                   << senderApp.read<std::string>().value() << " to "
                   << receiverApp.read<std::string>().value() << ". "
                   << res.error();
        return;
      }
    }

  });
}

} // namesapce stats
} // namespace terragraph
} // namespace facebook
