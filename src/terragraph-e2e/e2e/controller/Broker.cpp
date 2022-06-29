/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Broker.h"

#include <folly/MapUtil.h>

#include "BinaryStarFsm.h"
#include "e2e/common/CompressionUtil.h"
#include "e2e/common/Consts.h"

using namespace fbzmq;

using apache::thrift::detail::TEnumMapFactory;
using std::string;

namespace {
// Default keepAlive values
// We intend to garbage collect connections after 30 seconds of inactivity
const int kKeepAliveEnable{1};
// Idle Time before sending keep alives
const std::chrono::seconds kKeepAliveTime{15};
// max keep alives before resetting connection
const int kKeepAliveCnt{3};
// interval between keep alives
const std::chrono::seconds kKeepAliveIntvl{5};
}

namespace facebook {
namespace terragraph {

Broker::Broker(
    fbzmq::Context& zmqContext,
    const std::string& minionsSockBindUrl,
    const std::string& appsSockBindUrl,
    const std::string& pubSockBindUrl,
    bool isAppsSockZapEnabled,
    bool isMinionsSockZapEnabled,
    bool isBstarEnabled,
    bool isBstarPrimary)
    : minionsSock_{zmqContext,
                   IdentityString{E2EConsts::kBrokerCtrlId}},
      appsSock_{zmqContext, IdentityString{E2EConsts::kBrokerCtrlId}},
      eventPubSock_{zmqContext, IdentityString{E2EConsts::kBrokerCtrlId},
                    folly::none, NonblockingFlag{true}},
      isAppsSockZapEnabled_{isAppsSockZapEnabled},
      isMinionsSockZapEnabled_{isMinionsSockZapEnabled},
      isBstarEnabled_{isBstarEnabled} {

  // Prepare minionsSock_ to talk to the e2e minions
  // Overwrite default TCP_KEEPALIVE options to handle minion crash and
  // drop dead socket after 30 secs
  if (minionsSock_
          .setKeepAlive(
              kKeepAliveEnable,
              kKeepAliveTime.count(),
              kKeepAliveCnt,
              kKeepAliveIntvl.count())
          .hasError()) {
    LOG(FATAL) << "Could not set zmq keepAlive options.";
  }

  // enable ZMQ_ROUTER_HANDOVER
  // Ideally the TCP keepalives should be able to handle all scenarios. But
  // if an existing connection's tcp keepalive period hasnt expired to close
  // the connection, and if minion tries to connect from the node on a new
  // connection, then zmq does consume the packets from the new TCP connection.
  // (even after the old connection is closed after keepalive timeout)
  const int handover = 1;
  if (minionsSock_.setSockOpt(ZMQ_ROUTER_HANDOVER, &handover, sizeof(int))
          .hasError()) {
    LOG(FATAL) << "Could not set ZMQ_ROUTER_HANDOVER.";
  }

  // tag all management messages as DSCP to differenciate with data traffic
  if (minionsSock_.setSockOpt(ZMQ_TOS, &E2EConsts::kIpTos, sizeof(int))
          .hasError()) {
    LOG(FATAL) << "Could not set ZMQ_TOS.";
  }

  // set ZAP domain (must be non-empty to use ZAP handler for NULL security)
  if (isMinionsSockZapEnabled_) {
    if (minionsSock_.setSockOpt(
            ZMQ_ZAP_DOMAIN,
            E2EConsts::kZmqZapDomain.data(),
            E2EConsts::kZmqZapDomain.size()).hasError()) {
      LOG(FATAL) << "Could not set ZMQ_ZAP_DOMAIN on minionSock_.";
    }
  }
  if (isAppsSockZapEnabled_) {
    if (appsSock_.setSockOpt(
            ZMQ_ZAP_DOMAIN,
            E2EConsts::kZmqZapDomain.data(),
            E2EConsts::kZmqZapDomain.size()).hasError()) {
      LOG(FATAL) << "Could not set ZMQ_ZAP_DOMAIN on appsSock_.";
    }
  }

  // bind the socket to the listenAddr:routerPort
  VLOG(1) << "Binding to '" << minionsSockBindUrl << "'";
  if (minionsSock_.bind(SocketUrl{minionsSockBindUrl}).hasError()) {
    LOG(FATAL) << "Error binding to '" << minionsSockBindUrl << "'";
  }

  // bind the socket to the listenAddr:routerPort
  VLOG(1) << "Binding to '" << appsSockBindUrl << "'";
  if (appsSock_.bind(SocketUrl{appsSockBindUrl}).hasError()) {
    LOG(FATAL) << "Error binding to '" << appsSockBindUrl << "'";
  }

  VLOG(1) << "Binding pubUrl '" << pubSockBindUrl << "'";
  const auto pubBind =
      eventPubSock_.bind(fbzmq::SocketUrl{pubSockBindUrl});
  if (pubBind.hasError()) {
    LOG(FATAL) << "Error binding to URL '" << pubSockBindUrl << "' "
               << pubBind.error();
  }

  if (isBstarEnabled_) {
    // set initial primary/backup state
    bstarFsm_.state = isBstarPrimary ?
        thrift::BinaryStarFsmState::STATE_PRIMARY :
        thrift::BinaryStarFsmState::STATE_BACKUP;
  }

  // read status events from the minions socket
  addSocket(RawZmqSocketPtr{*minionsSock_}, ZMQ_POLLIN, [this](int) noexcept {
    Message minionMsg, receiverAppMsg, senderAppMsg, thriftMsg;

    const auto recvRet = minionsSock_.recvMultiple(
        minionMsg, receiverAppMsg, senderAppMsg, thriftMsg);
    if (recvRet.hasError()) {
      LOG(ERROR) << "Error reading message: " << recvRet.error();
      return;
    }

    const auto minion = minionMsg.read<std::string>().value();
    const auto receiverApp = receiverAppMsg.read<std::string>().value();
    const auto senderApp = senderAppMsg.read<std::string>().value();

    VLOG(4) << "Processing a message from " << minion << ":" << senderApp
            << " to " << receiverApp << " on minionsSock";

    if (isMinionsSockZapEnabled_) {
      // log additional details about this request
      // NOTE: properties can't be retrieved from the first message part (?)
      const auto ipAddr = receiverAppMsg.getMetadataProperty(
          E2EConsts::kZmqIpAddressMetaProperty);
      const auto identity = receiverAppMsg.getMetadataProperty(
          E2EConsts::kZmqIdentityMetaProperty);

      VLOG(3) << "Received message on minionsSock from " << minion << ":"
              << senderApp << " to " << receiverApp << " with IP = ["
              << (ipAddr.hasError() ? "ERROR" : ipAddr.value())
              << "], ZMQ ID = "
              << (identity.hasError() ? "ERROR" : identity.value());
    }

    // if running in primary-backup mode, check if we should drop this request
    if (isBstarEnabled_) {
      auto maybeFsm = BinaryStarFsm::processEvent(
          bstarFsm_, thrift::BinaryStarFsmEvent::CLIENT_REQUEST);
      if (maybeFsm.hasError()) {
        // currently backup or passive, and peer is still alive
        VLOG(4) << "Dropping minion message: " << maybeFsm.error();
        return;
      }
      if (maybeFsm.value() != bstarFsm_) {
        // FSM state changed, notify BinaryStarApp
        thrift::Message msg;
        msg.mType = thrift::MessageType::BSTAR_FSM;
        msg.value =
            fbzmq::util::writeThriftObjStr(maybeFsm.value(), serializer_);
        const auto sendRet = appsSock_.sendMultiple(
            Message::from(E2EConsts::kBinaryStarAppCtrlId).value(),
            Message(),
            Message::from(E2EConsts::kBrokerCtrlId).value(),
            Message::fromThriftObj(msg, serializer_).value());
        if (sendRet.hasError()) {
          LOG(ERROR) << "Error sending FSM change msg to "
                     << E2EConsts::kBinaryStarAppCtrlId << " "
                     << sendRet.error();
        }
        bstarFsm_ = maybeFsm.value();
      }
    }

    const auto sendRet = appsSock_.sendMultiple(
        receiverAppMsg, minionMsg, senderAppMsg, thriftMsg);
    if (sendRet.hasError()) {
      LOG(ERROR) << "Error routing msg from " << minion << ":" << senderApp
                 << " to " << receiverApp << " " << sendRet.error();
      return;
    }
  });

  // read status events from the apps socket
  addSocket(RawZmqSocketPtr{*appsSock_}, ZMQ_POLLIN, [this](int) noexcept {
    Message firstFrameMsg, minionMsg, receiverAppMsg, senderAppMsg,
        thriftMsg;

    const auto recvRet = appsSock_.recvMultiple(
        firstFrameMsg, minionMsg, receiverAppMsg, senderAppMsg, thriftMsg);
    if (recvRet.hasError()) {
      LOG(ERROR) << "Error reading message: " << recvRet.error();
      return;
    }

    const auto minion = minionMsg.read<std::string>().value();
    const auto receiverApp = receiverAppMsg.read<std::string>().value();
    const auto senderApp = senderAppMsg.read<std::string>().value();

    VLOG(4) << "Processing a message from " << senderApp << " to " << minion
            << ":" << receiverApp << " on appsSock";

    if (isAppsSockZapEnabled_) {
      // log additional details about this request
      // NOTE: properties can't be retrieved from the first message part (?)
      const auto ipAddr = receiverAppMsg.getMetadataProperty(
          E2EConsts::kZmqIpAddressMetaProperty);
      const auto identity = receiverAppMsg.getMetadataProperty(
          E2EConsts::kZmqIdentityMetaProperty);

      // ignore messages from controller apps
      if (ipAddr.hasError() || ipAddr.value() != "::1") {
        VLOG(3) << "Received message on appsSock from " << senderApp << " to "
                << receiverApp << " with IP = ["
                << (ipAddr.hasError() ? "ERROR" : ipAddr.value())
                << "], ZMQ ID = "
                << (identity.hasError() ? "ERROR" : identity.value());
      }
    }

    // Message for broker
    if (receiverApp == E2EConsts::kBrokerCtrlId) {
      auto maybeMsg = thriftMsg.readThriftObj<thrift::Message>(serializer_);
      if (maybeMsg.hasError()) {
        LOG(ERROR) << "Error deserializing thrift Message from " << senderApp
                   << ": " << maybeMsg.error();
        return;
      }
      // Decompress the message (if needed)
      std::string error;
      if (!CompressionUtil::decompress(maybeMsg.value(), error)) {
        LOG(ERROR) << error;
        return;
      }
      processMessage(minion, senderApp, maybeMsg.value());
      return;
    }

    if (!minion.empty()) {
      // Send it to minion through minionSock_
      const auto sendRet = minionsSock_.sendMultiple(
          minionMsg, receiverAppMsg, senderAppMsg, thriftMsg);
      if (sendRet.hasError()) {
        LOG(ERROR) << "Error routing msg from " << senderApp << " to " << minion
                   << ":" << receiverApp << " " << sendRet.error();
        return;
      }
    } else if (receiverApp == E2EConsts::kApiEventSubId) {
      // Send it to api service through eventPubSock_
      const auto sendRet = eventPubSock_.sendMultiple(
          receiverAppMsg, senderAppMsg, thriftMsg);
      if (sendRet.hasError()) {
        LOG(ERROR) << "Error routing msg from " << senderApp << " to "
                   << receiverApp << " " << sendRet.error();
        return;
      }
    } else {
      // Else route it to the corresponding receiverApp in Ctrl
      const auto sendRet = appsSock_.sendMultiple(
          receiverAppMsg, minionMsg, senderAppMsg, thriftMsg);
      if (sendRet.hasError()) {
        LOG(ERROR) << "Error routing msg from " << senderApp << " to "
                   << receiverApp << " " << sendRet.error();
        return;
      }
    }
  });
}

void
Broker::processMessage(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::BSTAR_FSM:
      processBstarFsm(senderApp, message);
      break;
    default:
      LOG(ERROR)
          << "Wrong type of message ("
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                 message.mType, "UNKNOWN")
          << ") received from " << minion << ":" << senderApp;
  }
}

void
Broker::processBstarFsm(
    const std::string& senderApp, const thrift::Message& message) {
  if (!isBstarEnabled_) {
    return;
  }

  VLOG(4) << "Received BinaryStar message from " << senderApp;
  thrift::BinaryStar fsm;
  try {
    fsm = fbzmq::util::readThriftObjStr<thrift::BinaryStar>(
        message.value, serializer_);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Invalid BinaryStar message from " << senderApp;
    return;
  }

  // received new FSM, store it
  bstarFsm_ = fsm;
}

} // namespace terragraph
} // namespace facebook
