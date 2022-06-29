/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "MinionClient.h"

#include <chrono>
#include <fbzmq/zmq/Zmq.h>
#include <folly/Random.h>
#include <glog/logging.h>

#include "e2e/common/Consts.h"
#include "e2e/if/gen-cpp2/DriverMessage_types.h"

using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {

// must prefix with :FWD: for minion to return to appsSock
DEFINE_string(
    minion_socket_id,
    ":FWD:minion_client",
    "ZMQ identity for minion connection");
DEFINE_string(minion_url, "tcp://[::1]:17177", "ZMQ url for minion connection");
DEFINE_string(
    minion_pub_url, "tcp://[::1]:17277", "ZMQ pub url for minion connection");
DEFINE_string(
    minion_pub_socket_id,
    "minion_client_pub",
    "ZMQ identity to minion pub connection");
DEFINE_int32(zmq_rcv_timeout_ms, 1000, "ZMQ recv timeout");
DEFINE_int32(
    topo_scan_timeout_ms,
    5000,
    "Timeout (ms) for minion publisher response after topology scan request");

MinionClient::MinionClient(fbzmq::Context& context)
    : context_(context),
      zmqId_{generateZmqId()},
      minionSock_{context, fbzmq::IdentityString{zmqId_}} {}

int64_t
MinionClient::nowInMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string
MinionClient::generateZmqId() {
  std::string zmqId =
      folly::sformat("{}-{}", FLAGS_minion_socket_id, folly::Random::rand32());
  return zmqId;
}

std::optional<thrift::LinkStatusDump>
MinionClient::getLinkStatusDump() {
  return apiCall<thrift::GetLinkStatusDump, thrift::LinkStatusDump>(
      E2EConsts::kIgnitionAppMinionId,
      thrift::MessageType::GET_LINK_STATUS_DUMP);
}

std::optional<thrift::StatusReport>
MinionClient::getStatusReport() {
  return apiCall<thrift::GetStatusReport, thrift::StatusReport>(
      E2EConsts::kStatusAppMinionId, thrift::MessageType::GET_STATUS_REPORT);
}

std::optional<thrift::GetMinionConfigResp>
MinionClient::getNodeConfig() {
  // construct request without a message value
  thrift::Message msg;
  msg.mType = thrift::MessageType::GET_MINION_CONFIG_REQ;
  if (!apiCallRequest(E2EConsts::kConfigAppMinionId, msg)) {
    return std::nullopt;
  }
  auto message = recvThriftMsg();
  if (message) {
    // return json response
    try {
      auto nodeConfigResp =
          fbzmq::util::readThriftObjStr<thrift::GetMinionConfigResp>(
              message.value().value, serializer_);
      return nodeConfigResp;
    } catch (const std::exception& e) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

bool
MinionClient::setNodeConfig(const std::string& nodeConfig) {
  // construct request
  thrift::SetMinionConfigReq configReq;
  configReq.config = nodeConfig;
  // apply config immediately
  // configReq.bwgdIdx = 0;
  thrift::Message msg;
  msg.mType = thrift::MessageType::SET_MINION_CONFIG_REQ;
  msg.value = fbzmq::util::writeThriftObjStr(configReq, serializer_);
  return apiCallRequest(E2EConsts::kConfigAppMinionId, msg);
}

bool
MinionClient::sendSetLinkStatus(
    const thrift::LinkStatusType& linkStatusType,
    const std::string& initiatorMac,
    const std::string& responderMac) {
  thrift::SetLinkStatus setLinkStatusReq;
  setLinkStatusReq.linkStatusType = linkStatusType;
  setLinkStatusReq.initiatorMac = initiatorMac;
  setLinkStatusReq.responderMac = responderMac;

  thrift::Message msg;
  msg.mType = thrift::MessageType::SET_LINK_STATUS;
  msg.value = fbzmq::util::writeThriftObjStr(setLinkStatusReq, serializer_);
  // do not expect a response
  return apiCallRequest(E2EConsts::kIgnitionAppMinionId, msg);
}

std::optional<thrift::ScanResp>
MinionClient::getTopoScan(const std::string& radioMac) {
  thrift::ScanReq scanReq;
  scanReq.radioMac_ref() = radioMac;
  // generate randomized token for matching request to response
  scanReq.token = folly::Random::rand32(2 ^ 31);
  scanReq.scanType_ref() = thrift::ScanType::TOPO;
  // start request immediately
  scanReq.startBwgdIdx = 0;
  // broadcast mac addr
  scanReq.rxNodeMac_ref() = "ff:ff:ff:ff:ff:ff";
  thrift::Message msg;
  msg.mType = thrift::MessageType::SCAN_REQ;
  msg.value = fbzmq::util::writeThriftObjStr(scanReq, serializer_);
  // send request without expecting a response on the RPC socket
  bool callSuccess = apiCallRequest(E2EConsts::kStatusAppMinionId, msg);
  if (!callSuccess) {
    LOG(ERROR) << "Unable to send scan request for radio mac: " << radioMac;
    return std::nullopt;
  }
  // wait for scan response as DriverMessage on minion publisher socket
  // this is a blocking call which will block all HTTP requests since
  // thread count = 1
  auto driverResp = waitForMinionPublisherResponse<thrift::DriverMessage>(
      thrift::MessageType::SCAN_RESP, FLAGS_topo_scan_timeout_ms);
  if (!driverResp) {
    LOG(ERROR) << "No response for scan request.";
    return std::nullopt;
  }
  // decode ScanResp from driver message value
  try {
    auto scanResp = fbzmq::util::readThriftObjStr<thrift::ScanResp>(
        driverResp.value().value, serializer_);
    return scanResp;
  } catch (const std::exception& e) {
    return std::nullopt;
  }
}

bool
MinionClient::sendRebootCmd(bool force, int secondsToReboot) {
  thrift::RebootNode rebootReq;
  rebootReq.force = force;
  rebootReq.secondsToReboot_ref() = secondsToReboot;

  thrift::Message msg;
  msg.mType = thrift::MessageType::REBOOT_NODE;
  msg.value = fbzmq::util::writeThriftObjStr(rebootReq, serializer_);
  // do not expect a response
  return apiCallRequest(E2EConsts::kStatusAppMinionId, msg);
}

bool
MinionClient::apiCallRequest(
    const std::string& receiverId, const thrift::Message& msg) {
  // connect the dealer socket to the router socket on the Broker
  VLOG(3) << "[" << zmqId_ << "] Connecting to '" << FLAGS_minion_url << "'";
  if (minionSock_.connect(fbzmq::SocketUrl{FLAGS_minion_url}).hasError()) {
    LOG(ERROR) << "Error connecting to '" << FLAGS_minion_url << "'";
    return false;
  }

  const auto ret = minionSock_.sendMultiple(
      fbzmq::Message::from("dummy").value(),
      fbzmq::Message::from(receiverId).value(),
      fbzmq::Message::from(zmqId_).value(),
      fbzmq::Message::fromThriftObj(msg, serializer_).value());
  VLOG(3) << "Requesting " << TEnumMapFactory<thrift::MessageType>
      ::makeValuesToNamesMap().at(msg.mType)
          << " from minion sock";
  return true;
}

std::optional<thrift::Message>
MinionClient::recvThriftMsg() {
  fbzmq::Message senderApp, thriftMsg;
  auto res = minionSock_.recvMultipleTimeout(
      std::chrono::milliseconds(FLAGS_zmq_rcv_timeout_ms),
      senderApp,
      thriftMsg);
  if (res.hasError()) {
    LOG(ERROR) << "Error reading message from minion: " << res.error();
    return std::nullopt;
  }
  auto msg = thriftMsg.readThriftObj<thrift::Message>(serializer_);
  if (msg.hasError()) {
    return std::nullopt;
  }
  return msg.value();
}

template <class ThriftResponseType>
std::optional<ThriftResponseType>
MinionClient::apiCall(
    const std::string& receiverId, const thrift::Message& msg) {
  if (!apiCallRequest(receiverId, msg)) {
    return std::nullopt;
  }
  auto message = recvThriftMsg();
  if (!message) {
    return std::nullopt;
  }
  auto response = maybeReadThrift<ThriftResponseType>(message.value());
  if (!response) {
    LOG(ERROR) << "Error parsing message from minion: "
               << static_cast<int>(message.value().mType);
    return std::nullopt;
  }

  return response;
}

template <class ThriftRequestType, class ThriftResponseType>
std::optional<ThriftResponseType>
MinionClient::apiCall(
    const std::string& receiverId, const thrift::MessageType& mType) {

  thrift::Message msg;
  msg.mType = mType;
  ThriftRequestType thriftRequest;
  msg.value = fbzmq::util::writeThriftObjStr(thriftRequest, serializer_);
  return MinionClient::apiCall<ThriftResponseType>(receiverId, msg);
}

template <class ThriftResponseType>
std::optional<ThriftResponseType>
MinionClient::waitForMinionPublisherResponse(
    const thrift::MessageType& msgType, const int timeoutMs) {
  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> zmqSock(
      context_, fbzmq::IdentityString(FLAGS_minion_pub_socket_id));
  auto res = zmqSock.connect(fbzmq::SocketUrl{FLAGS_minion_pub_url});
  VLOG(3) << "Connecting to '" << FLAGS_minion_pub_url << "'";
  if (res.hasError()) {
    VLOG(3) << "Unable to connect to minion sub socket.";
    return std::nullopt;
  }
  zmqSock.setSockOpt(ZMQ_SUBSCRIBE, "", 0).value();
  bool done = false;
  // calculate max time to wait
  auto endTimeMs = nowInMs() + timeoutMs;
  // wait for expected message
  while (!done && nowInMs() < endTimeMs) {
    fbzmq::Message senderApp, msg;
    VLOG(4) << "Waiting for minion publish socket response.";
    auto maybeMsg = zmqSock.recvMultipleTimeout(
        std::chrono::milliseconds{1000}, senderApp, msg);

    if (maybeMsg.hasError()) {
      continue;
    }
    auto maybeThriftMsg = msg.readThriftObj<thrift::Message>(serializer_);
    if (maybeThriftMsg.hasError()) {
      LOG(ERROR) << "Unable to parse thrift message.";
      continue;
    }
    auto& thriftMsg = maybeThriftMsg.value();
    // skip message we aren't waiting for
    if (thriftMsg.mType != msgType) {
      continue;
    }
    // decode driver msg
    auto driverMsg = maybeReadThrift<ThriftResponseType>(thriftMsg);
    if (!driverMsg) {
      LOG(ERROR) << "Error decoding driver message.";
      return std::nullopt;
    }
    return driverMsg;
  }
  return std::nullopt;
}

} // namespace terragraph
} // namespace facebook
