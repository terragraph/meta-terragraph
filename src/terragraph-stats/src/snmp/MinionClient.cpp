/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "MinionClient.h"
#include "StatCache.h"

#include <chrono>
#include <glog/logging.h>

#include "e2e/common/Consts.h"

using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {

// must prefix with :FWD: for minion to return to appsSock
DEFINE_string(
    minion_socket_id, ":FWD:minion_snmp_agent", "ZMQ Id for minion connection");
DEFINE_string(minion_url, "tcp://[::1]:17177", "ZMQ Url for minion connection");
DEFINE_int32(link_status_interval_ms, 5000, "Link status request interval");

MinionClient::MinionClient(
    fbzmq::Context& context, const std::vector<StatFormat>& statsFormat)
    : context_(context),
      minionSock_{context, fbzmq::IdentityString{FLAGS_minion_socket_id}},
      statsFormat_(statsFormat) {

  // connect the dealer socket to the router socket on the Broker
  LOG(INFO) << "[" << FLAGS_minion_socket_id << "] Connecting to '"
            << FLAGS_minion_url << "'";
  if (minionSock_.connect(fbzmq::SocketUrl{FLAGS_minion_url}).hasError()) {
    LOG(FATAL) << "Error connecting to '" << FLAGS_minion_url << "'";
  }

  addSocket(
      fbzmq::RawZmqSocketPtr{*minionSock_}, ZMQ_POLLIN, [this](int) noexcept {
        VLOG(4) << "[" << FLAGS_minion_socket_id
                << "] Received a message on dealer socket from broker";
        fbzmq::Message senderAppMsg, thriftMsg;

        const auto recvRet = minionSock_.recvMultiple(senderAppMsg, thriftMsg);
        if (recvRet.hasError()) {
          LOG(ERROR) << "[" << FLAGS_minion_socket_id << "["
                     << FLAGS_minion_socket_id
                     << "] Error reading message: " << recvRet.error();
          return;
        }

        const auto senderApp = senderAppMsg.read<std::string>().value();
        auto message = thriftMsg.readThriftObj<thrift::Message>(serializer_);
        if (message.hasError()) {
          LOG(ERROR) << "[" << FLAGS_minion_socket_id
                     << "] Error reading message: " << message.error();
          return;
        }
        processMessage(senderApp, message.value());
      });

  linkStatusTimer_ = fbzmq::ZmqTimeout::make(
      this, [this]() noexcept { requestLinkStatusDump(); });
  linkStatusTimer_->scheduleTimeout(
      std::chrono::milliseconds(FLAGS_link_status_interval_ms),
      true /* periodic */);
}

void
MinionClient::processMessage(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::LINK_STATUS_DUMP: {
      auto linkStatusDump = maybeReadThrift<thrift::LinkStatusDump>(message);
      if (!linkStatusDump) {
        LOG(ERROR) << "Failed to read LinkStatusDump";
        return;
      }
      auto linkKeys = StatCache::generateLinkKeys(
          statsFormat_, linkStatusDump->linkStatusDump);
      // swap to newly generated link keys
      StatCache::getKeyNameCacheInstance()->swap(linkKeys);
      break;
    }
    default: {
      LOG(ERROR)
          << "Unhandled message type: "
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                 message.mType, "UNKNOWN")
          << ", sender: " << senderApp;
    }
  }
}

void
MinionClient::requestLinkStatusDump() {
  thrift::GetLinkStatusDump getLinkStatusDumpReq;
  thrift::Message msg;
  msg.mType = thrift::MessageType::GET_LINK_STATUS_DUMP;
  msg.value = fbzmq::util::writeThriftObjStr(getLinkStatusDumpReq, serializer_);

  const auto ret = minionSock_.sendMultiple(
      fbzmq::Message::from("dummy").value(),
      fbzmq::Message::from(E2EConsts::kIgnitionAppMinionId).value(),
      fbzmq::Message::from(FLAGS_minion_socket_id).value(),
      fbzmq::Message::fromThriftObj(msg, serializer_).value());
  VLOG(2) << "Requesting GET_LINK_STATUS_DUMP from minion sock";

  if (ret.hasError()) {
    LOG(ERROR) << "Error requesting GET_LINK_STATUS_DUMP from minion sock";
    return;
  }
}

} // namespace terragraph
} // namespace facebook
