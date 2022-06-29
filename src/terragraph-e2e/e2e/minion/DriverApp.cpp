/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "DriverApp.h"

#include "e2e/common/Consts.h"
#include "e2e/if/gen-cpp2/DriverMessage_types.h"

using apache::thrift::detail::TEnumMapFactory;
using namespace fbzmq;

namespace facebook {
namespace terragraph {
namespace minion {

DriverApp::DriverApp(
    fbzmq::Context& zmqContext,
    const std::string& brokerRouterUrl,
    const std::string& monitorSockUrl,
    const std::string& driverPairSockUrl,
    const std::string& macAddr)
    : MinionApp(
          zmqContext,
          brokerRouterUrl,
          monitorSockUrl,
          macAddr,
          E2EConsts::kDriverAppMinionId),
      pairSock_{zmqContext} {

  // connect the pair socket to driver
  LOG(INFO) << "Connecting to '" << driverPairSockUrl << "'";
  if (pairSock_.connect(SocketUrl{driverPairSockUrl}).hasError()) {
    LOG(FATAL) << "Error binding to '" << driverPairSockUrl << "'";
  }

  // message on pair socket from driver
  addSocket(RawZmqSocketPtr{*pairSock_}, ZMQ_POLLIN, [this](int) noexcept {
    VLOG(4) << "Received a message from driver";

    const auto thriftMsg = pairSock_.recvThriftObj<thrift::Message>(
        serializer_, E2EConsts::kReadTimeout);
    if (thriftMsg.hasError()) {
      LOG(ERROR) << "Error receiving message: " << thriftMsg.error().errString;
      return;
    }
    auto& message = *thriftMsg;

    // send it to different receiver apps accordingly
    std::string receiverApp;
    switch (message.mType) {
      // messages to status app
      case thrift::MessageType::NODE_INIT_NOTIFY: {
        receiverApp = E2EConsts::kStatusAppMinionId;
        sendToBroadcastSock(message);
        break;
      }
      case thrift::MessageType::FW_HEALTHY: {
        receiverApp = E2EConsts::kStatusAppMinionId;
        break;
      }
      case thrift::MessageType::FW_ROUTING_INFO: {
        receiverApp = E2EConsts::kStatusAppMinionId;
        break;
      }
      // messages to ignition app
      case thrift::MessageType::DR_LINK_STATUS: {
        receiverApp = E2EConsts::kIgnitionAppMinionId;
        sendToBroadcastSock(message);
        break;
      }
      case thrift::MessageType::DR_DEV_ALLOC_RES: {
        receiverApp = E2EConsts::kIgnitionAppMinionId;
        sendToBroadcastSock(message);
        break;
      }
      case thrift::MessageType::DR_WSEC_STATUS: {
        receiverApp = E2EConsts::kIgnitionAppMinionId;
        sendToBroadcastSock(message);
        break;
      }
      case thrift::MessageType::DR_WSEC_LINKUP_STATUS: {
        receiverApp = E2EConsts::kIgnitionAppMinionId;
        sendToBroadcastSock(message);
        break;
      }
      case thrift::MessageType::DR_DEV_UPDOWN_STATUS: {
        receiverApp = E2EConsts::kStatusAppMinionId;
        sendToBroadcastSock(message);
        break;
      }
      // fw ack for passthru messages
      case thrift::MessageType::FW_ACK: {
        auto drMessage = maybeReadDriverMessage<thrift::FwAck>(message);
        if (!drMessage) {
          break;
        }
        thrift::FwAck& fwAck = drMessage->value;

        switch (fwAck.reqType) {
          // messages to status app
          case thrift::MessageType::FW_SET_NODE_PARAMS:
          case thrift::MessageType::GPS_ENABLE_REQ:
          case thrift::MessageType::FW_STATS_CONFIGURE_REQ: {
            receiverApp = E2EConsts::kStatusAppMinionId;
            break;
          }
          // log known fw ack types and spare upper layer
          case thrift::MessageType::DR_SET_LINK_STATUS:
          case thrift::MessageType::PHY_LA_CONFIG_REQ:
          case thrift::MessageType::PHY_AGC_CONFIG_REQ:
          case thrift::MessageType::FW_DEBUG_REQ:
          case thrift::MessageType::FW_BF_RESP_SCAN:
          case thrift::MessageType::PHY_TPC_CONFIG_REQ:
          case thrift::MessageType::FW_CONFIG_REQ:
          case thrift::MessageType::FW_SET_LOG_CONFIG:
          case thrift::MessageType::SCAN_REQ:
          case thrift::MessageType::BF_SLOT_EXCLUSION_REQ:
          case thrift::MessageType::FW_ADJ_RESP: {
            auto reqType = folly::get_default(
                TEnumMapFactory<thrift::MessageType>::
                    makeValuesToNamesMap(), fwAck.reqType, "UNKNOWN");
            if (fwAck.success) {
              VLOG(4) << drMessage->macPrefix()
                      << "Fw ack for " << reqType << " succeeded";
            } else {
              LOG(ERROR) << drMessage->macPrefix()
                         << "Fw ack for" << reqType << " failed";
            }
            break;
          }
          // unknown fwAck.reqType
          default: {
            LOG(ERROR) << drMessage->macPrefix()
                       << "Ignore wrong type of fw ack message ("
                       << folly::get_default(
                              TEnumMapFactory<thrift::MessageType>::
                                  makeValuesToNamesMap(),
                              message.mType,
                              "UNKNOWN")
                       << ") received from driver";
          }
        }
        break;
      }
      case thrift::MessageType::DR_ACK: {
        auto drMessage = maybeReadDriverMessage<thrift::DriverAck>(message);
        if (!drMessage) {
          break;
        }
        thrift::DriverAck& driverAck = drMessage->value;

        auto reqType = folly::get_default(
            TEnumMapFactory<thrift::MessageType>::
                makeValuesToNamesMap(), driverAck.reqType, "UNKNOWN");
        if (driverAck.success) {
          VLOG(4) << drMessage->macPrefix()
                  << "Driver ack for " << reqType << " succeeded";
        } else {
          LOG(ERROR) << drMessage->macPrefix()
                     << "Driver ack for" << reqType << " failed";
        }
        break;
      }
      case thrift::MessageType::SCAN_RESP: {
        auto drMessage = maybeReadDriverMessage<thrift::ScanResp>(message);
        if (drMessage) {
          sendToCtrlApp(
              E2EConsts::kScanAppCtrlId,
              thrift::MessageType::SCAN_RESP,
              drMessage->value);
          sendToBroadcastSock(message);
          eventClient_->logEvent(
              thrift::EventCategory::SCAN,
              thrift::EventId::SCAN_RESP,
              thrift::EventLevel::INFO,
              "Received scan response");
        }
        break;
      }
      case thrift::MessageType::GPS_GET_POS_RESP: {
        auto drMessage = maybeReadDriverMessage<thrift::Location>(message);
        if (drMessage) {
          sendToCtrlApp(
              E2EConsts::kTopologyAppCtrlId,
              thrift::MessageType::GPS_GET_POS_RESP,
              drMessage->value);
          sendToBroadcastSock(message);
        }
        break;
      }
      case thrift::MessageType::FW_CONFIG_RESP: {
        receiverApp = E2EConsts::kStatusAppMinionId;
        sendToBroadcastSock(message);
        break;
      }
      case thrift::MessageType::FW_ADJ_REQ: {
        receiverApp = E2EConsts::kOpenrClientAppMinionId;
        break;
      }
      // unknown message type
      default: {
        sendToBroadcastSock(message);
        LOG(ERROR) << "Ignore wrong type of message ("
                   << folly::get_default(
                          TEnumMapFactory<thrift::MessageType>::
                              makeValuesToNamesMap(),
                          message.mType,
                          "UNKNOWN")
                   << ") received from driver";
      }
    }
    if (!receiverApp.empty()) {
      sendToMinionApp(receiverApp, message);
    }
  });
}

void
DriverApp::processMessage(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  // NOTE:
  // We should only be receiving messages locally, e.g. not from the controller.
  // This way, driver-related structures are isolated to the node, and are
  // easier to change if needed. All controller-initiated messages have been
  // deprecated since RELEASE_M46.
  switch (message.mType) {
    case thrift::MessageType::HELLO:
      processHello(senderApp);
      break;
    default:
      sendToDriverIf(message);
  }
}

void
DriverApp::processHello(const std::string& senderApp) {
  if (senderApp != E2EConsts::kStatusAppMinionId) {
    LOG(ERROR) << "Ignoring HELLO from unexpected app " << senderApp;
    return;
  }
  VLOG(3) << "Replying to HELLO from " << senderApp;
  sendToMinionApp(senderApp, thrift::MessageType::HELLO, thrift::Hello());
}

void
DriverApp::sendToDriverIf(const thrift::Message& message) {
  const auto ret = pairSock_.sendThriftObj(message, serializer_);
  if (ret.hasError()) {
    LOG(ERROR) << "Error routing message to pair socket: " << ret.error();
  }
}

} // namespace minion
} // namespace terragraph
} // namespace facebook
