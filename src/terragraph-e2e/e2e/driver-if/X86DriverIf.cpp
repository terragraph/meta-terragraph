/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "X86DriverIf.h"

#include <fbzmq/zmq/Zmq.h>
#include <folly/Conv.h>
#include <folly/Format.h>
#include <folly/MapUtil.h>

#include "e2e/common/SysUtils.h"

using namespace fbzmq;

namespace {
const uint32_t kDriverIfRouterPort = 20000;

// TODO - need to support multiple links on different interfaces
const std::string kTerraIfName{"terra0"};

// bring link/tunnel up
void
bringLinkUp(const std::string& linkName, const std::string& llIp) {
  // bring link/tunnel up
  facebook::terragraph::SysUtils::system(
      folly::sformat("ip link set dev {} up", linkName));
  facebook::terragraph::SysUtils::system(
      folly::sformat("ip -6 addr add {} dev {}", llIp, linkName));
}

// bring link/tunnel down
void
bringLinkDown(const std::string& linkName) {
  facebook::terragraph::SysUtils::system(
      folly::sformat("ip link set dev {} down", linkName));
}

//
// emulation specific code
// we can reverse engineer all parameters since we configure them, such as MAC
// addr and OOB IP

// namespace ip prefix
const std::string kNsPrefix = "2401:db00:11:d07f:feed::{}";
// mac prefix
const std::string kMacPrefix = "fa:ce:b0:0c:{:02d}:{:02d}";
// link local ip prefix
const std::string kLLIpPrefix = "fe80::{}/64";

// map emulation id to IP
std::string
id2IP(const uint16_t id) {
  return folly::sformat(kNsPrefix, id + 1);
}

// id to MAC
std::string
id2Mac(const uint16_t id) {
  const auto hiId = id / 100;
  CHECK_LT(hiId, 100);
  const auto loId = id % 100;
  return folly::sformat(kMacPrefix, hiId, loId);
}

// map MAC to id
uint16_t
mac2Id(const std::string& mac) {
  std::vector<std::string> macBytes;
  folly::split(":", mac, macBytes);
  if (macBytes.size() != 6) {
    LOG(FATAL) << "X86DriverIf requires node ID to be a valid MAC address";
  }
  const auto hiId = folly::to<uint16_t>(macBytes[4]);
  const auto loId = folly::to<uint16_t>(macBytes[5]);
  return (hiId * 100 + loId);
}

// form link/tunnel name
std::string
formLinkName(uint16_t myId, uint16_t otherId) {
  return folly::sformat("tg_{}_{}_0", myId + 1, otherId + 1);
}

// form link local ip
std::string
formLLIp(const uint16_t id) {
  return folly::sformat(kLLIpPrefix, id + 1);
}
} // namespace

namespace facebook {
namespace terragraph {

// Build a thrift::Message that wraps a thrift::DriverMessage.
template <class T>
thrift::Message
buildMessage(
    thrift::MessageType mType,
    const T& obj,
    const std::string& radioMac,
    const apache::thrift::CompactSerializer& serializer) {
  thrift::DriverMessage driverMsg;
  driverMsg.value = fbzmq::util::writeThriftObjStr(obj, serializer);
  driverMsg.radioMac = radioMac;
  thrift::Message msg;
  msg.mType = mType;
  msg.value = fbzmq::util::writeThriftObjStr(driverMsg, serializer);
  return msg;
}

X86DriverIf::X86DriverIf(
    fbzmq::Context& zmqContext,
    const std::string& pairSockUrl,
    const std::string& monitorSubmitUrl,
    const std::string& macAddr,
    const std::unordered_map<std::string, std::string>& radioMacToBusId,
    bool daemonMode)
    : BaseDriverIf(
          zmqContext,
          pairSockUrl,
          monitorSubmitUrl,
          radioMacToBusId,
          daemonMode),
      myId_{mac2Id(macAddr)},
      myLLIp_{formLLIp(myId_)},
      cmdServerSock_{zmqContext},
      cmdClientSock_{zmqContext,
                     IdentityString{folly::to<std::string>(myId_)}} {
  // bind to command socket
  std::string cmdServerSockUrl =
      folly::sformat("tcp://*:{}", kDriverIfRouterPort);
  if (cmdServerSock_.bind(SocketUrl{cmdServerSockUrl}).hasError()) {
    LOG(FATAL) << "Error binding to '" << cmdServerSockUrl << "'";
  }

  // message on command socket
  addSocket(RawZmqSocketPtr{*cmdServerSock_}, ZMQ_POLLIN, [this](int) noexcept {
    VLOG(4) << "DriverIf: Received a message from other driver";
    // initiator id
    auto idMsg = cmdServerSock_.recvOne().value();
    const auto initiatorId =
        folly::to<uint16_t>(idMsg.read<std::string>().value());
    const auto thriftMsg =
        cmdServerSock_.recvThriftObj<thrift::X86PeerDriverMsg>(serializer_);
    if (thriftMsg.hasError()) {
      LOG(ERROR) << "Error receiving message: " << thriftMsg.error().errString;
      return;
    }
    processPeerDriverMessage(initiatorId, *thriftMsg);
  });
}

void
X86DriverIf::processPeerDriverMessage(
    const uint16_t initiatorId, const thrift::X86PeerDriverMsg& req) {
  // responder also sends up notifications
  thrift::Message notif1, notif2;

  const auto initiatorMac = id2Mac(initiatorId);
  const auto linkName = formLinkName(myId_, initiatorId);
  VLOG(4) << (req.isAssoc ? "LINK_UP" : "LINK_DOWN")
          << " received at responder from " << initiatorMac << " for link "
          << linkName;

  if (req.isAssoc) {
    bringLinkUp(linkName, myLLIp_);

    // send notification up
    thrift::DriverLinkStatus driverLinkStatus;
    driverLinkStatus.valid = true;
    driverLinkStatus.macAddr = initiatorMac;
    driverLinkStatus.drLinkStatusType = thrift::DriverLinkStatusType::LINK_UP;
    driverLinkStatus.linkDownCause = thrift::LinkDownCause::NOT_APPLICABLE;
    driverLinkStatus.ifname = kTerraIfName;
    driverLinkStatus.selfNodeType = static_cast<thrift::NodeType>(0);
    driverLinkStatus.peerNodeType = static_cast<thrift::NodeType>(0);
    notif1 = buildMessage(
        thrift::MessageType::DR_LINK_STATUS,
        driverLinkStatus,
        initiatorMac,
        serializer_);
    sendToDriverApp(notif1);
  } else {
    bringLinkDown(linkName);

    // for dissoc
    thrift::DriverLinkStatus driverLinkStatus1;
    driverLinkStatus1.valid            = true;
    driverLinkStatus1.macAddr          = initiatorMac;
    driverLinkStatus1.drLinkStatusType =
        thrift::DriverLinkStatusType::LINK_PAUSE;
    driverLinkStatus1.linkDownCause    = thrift::LinkDownCause::NOT_APPLICABLE;
    driverLinkStatus1.ifname           = kTerraIfName;
    driverLinkStatus1.selfNodeType     = static_cast<thrift::NodeType>(0);
    driverLinkStatus1.peerNodeType     = static_cast<thrift::NodeType>(0);
    notif1 = buildMessage(
        thrift::MessageType::DR_LINK_STATUS,
        driverLinkStatus1,
        initiatorMac,
        serializer_);
    thrift::DriverLinkStatus driverLinkStatus2;
    driverLinkStatus2.valid            = true;
    driverLinkStatus2.macAddr          = initiatorMac;
    driverLinkStatus2.drLinkStatusType
        = thrift::DriverLinkStatusType::LINK_DOWN;
    driverLinkStatus2.linkDownCause    = thrift::LinkDownCause::NOT_APPLICABLE;
    driverLinkStatus2.ifname           = kTerraIfName;
    driverLinkStatus2.selfNodeType     = static_cast<thrift::NodeType>(0);
    driverLinkStatus2.peerNodeType     = static_cast<thrift::NodeType>(0);
    notif2 = buildMessage(
        thrift::MessageType::DR_LINK_STATUS,
        driverLinkStatus2,
        initiatorMac,
        serializer_);
    sendToDriverApp(notif1);
    sendToDriverApp(notif2);
  }
}

void
X86DriverIf::sendLinkCmdToResponder(
    const std::string& responderMac, bool isAssoc) {
  const auto responderIp = id2IP(mac2Id(responderMac));
  const auto responderUrl =
      folly::sformat("tcp://{}:{}", responderIp, kDriverIfRouterPort);

  if (not lastResponderUrl_) {
    CHECK(cmdClientSock_.connect(SocketUrl{responderUrl}));
    lastResponderUrl_ = responderUrl;
  } else if (*lastResponderUrl_ != responderUrl) {
    // disconnect from previous responder
    CHECK(cmdClientSock_.disconnect(SocketUrl{*lastResponderUrl_}));
    // connect to new responder
    CHECK(cmdClientSock_.connect(SocketUrl{responderUrl}));
    lastResponderUrl_ = responderUrl;
  }

  thrift::X86PeerDriverMsg x86PeerDriverMsg;
  x86PeerDriverMsg.isAssoc = isAssoc;
  const auto ret = cmdClientSock_.sendThriftObj(
      x86PeerDriverMsg, serializer_);
  if (ret.hasError()) {
    LOG(ERROR) << "Error sending message " << ret.error();
  }
}

void
X86DriverIf::sendFwAck(
    const std::string& radioMac, thrift::MessageType reqType, bool success) {
  thrift::FwAck fwAck;
  fwAck.reqType = reqType;
  fwAck.success = success;

  thrift::Message notif = buildMessage(
     thrift::MessageType::FW_ACK,
     fwAck,
     radioMac,
     serializer_);
  sendToDriverApp(notif);
}

void
X86DriverIf::sendNodeInit(
    const std::string& radioMac,
    const thrift::DriverNodeInitReq& /*msg*/) {
  thrift::DriverNodeInitNotif driverNodeInitNotif;
  driverNodeInitNotif.success = true;
  driverNodeInitNotif.macAddr = radioMac;
  driverNodeInitNotif.vendor = "fake";
  thrift::Message notif = buildMessage(
      thrift::MessageType::NODE_INIT_NOTIFY,
      driverNodeInitNotif,
      radioMac,
      serializer_);
  sendToDriverApp(notif);
}

void
X86DriverIf::sendLinkStatus(
    const std::string& radioMac, const thrift::DriverSetLinkStatus& msg) {
  thrift::Message notif1, notif2;

  const auto linkName = formLinkName(myId_, mac2Id(msg.responderMac));
  if (msg.isAssoc) {
    bringLinkUp(linkName, myLLIp_);

    // tell responder to bring the other end of tunnel up
    sendLinkCmdToResponder(msg.responderMac, msg.isAssoc);

    // send notification up
    thrift::DriverLinkStatus driverLinkStatus;
    driverLinkStatus.valid            = true;
    driverLinkStatus.macAddr          = msg.responderMac;
    driverLinkStatus.drLinkStatusType = thrift::DriverLinkStatusType::LINK_UP;
    driverLinkStatus.linkDownCause    = thrift::LinkDownCause::NOT_APPLICABLE;
    driverLinkStatus.ifname           = kTerraIfName;
    driverLinkStatus.selfNodeType     = static_cast<thrift::NodeType>(0);
    driverLinkStatus.peerNodeType     = static_cast<thrift::NodeType>(0);
    notif1 = buildMessage(
        thrift::MessageType::DR_LINK_STATUS,
            driverLinkStatus,
        radioMac,
        serializer_);
    sendToDriverApp(notif1);
  } else {
    bringLinkDown(linkName);

    // tell responder to bring the other end of tunnel down
    sendLinkCmdToResponder(msg.responderMac, msg.isAssoc);

    // for dissoc
    thrift::DriverLinkStatus driverLinkStatus1;
    driverLinkStatus1.valid            = true;
    driverLinkStatus1.macAddr          = msg.responderMac;
    driverLinkStatus1.drLinkStatusType =
        thrift::DriverLinkStatusType::LINK_PAUSE;
    driverLinkStatus1.linkDownCause    = thrift::LinkDownCause::NOT_APPLICABLE;
    driverLinkStatus1.ifname           = kTerraIfName;
    driverLinkStatus1.selfNodeType     = static_cast<thrift::NodeType>(0);
    driverLinkStatus1.peerNodeType     = static_cast<thrift::NodeType>(0);
    notif1 = buildMessage(
        thrift::MessageType::DR_LINK_STATUS,
        driverLinkStatus1,
        radioMac,
        serializer_);
    thrift::DriverLinkStatus driverLinkStatus2;
     driverLinkStatus2.valid            = true;
     driverLinkStatus2.macAddr          = msg.responderMac;
     driverLinkStatus2.drLinkStatusType =
         thrift::DriverLinkStatusType::LINK_DOWN;
     driverLinkStatus2.linkDownCause    = thrift::LinkDownCause::NOT_APPLICABLE;
     driverLinkStatus2.ifname           = kTerraIfName;
     driverLinkStatus2.selfNodeType     = static_cast<thrift::NodeType>(0);
     driverLinkStatus2.peerNodeType     = static_cast<thrift::NodeType>(0);
    notif2 = buildMessage(
        thrift::MessageType::DR_LINK_STATUS,
        driverLinkStatus2,
        radioMac,
        serializer_);
    sendToDriverApp(notif1);
    sendToDriverApp(notif2);
  }
}

void
X86DriverIf::sendNodeParams(
    const std::string& radioMac, const thrift::FwSetNodeParams& msg) {
  // for all the sb passthru we should get a fw ack
  for (size_t i = 0; i < msg.passThruMsgs.size(); i++) {
    sendFwAck(radioMac, thrift::MessageType::FW_SET_NODE_PARAMS);
  }
}

void
X86DriverIf::sendGpsGetPosReq() {
  thrift::Location location;
  location.latitude = 37.4847215;
  location.longitude = -122.1472362;
  location.altitude = 17.92;
  location.accuracy = 50;
  thrift::Message notif = buildMessage(
     thrift::MessageType::GPS_GET_POS_RESP,
     location,
     "" /* terminated at driver */,
     serializer_);
  sendToDriverApp(notif);
}

void
X86DriverIf::sendDevAllocReq(
    const std::string& radioMac, const thrift::DriverDevAllocReq& msg) {
  thrift::DriverDevAllocRes driverDevAllocRec;
  driverDevAllocRec.success = true;
  driverDevAllocRec.macAddr = msg.macAddr;
  driverDevAllocRec.ifindex = 5;
  driverDevAllocRec.ifname = kTerraIfName;
  thrift::Message notif = buildMessage(
     thrift::MessageType::DR_DEV_ALLOC_RES,
     driverDevAllocRec,
     radioMac,
     serializer_);
  sendToDriverApp(notif);
}

// ---- unimplemented, but send FW_ACK ---- //

void
X86DriverIf::sendGpsEnableReq(const std::string& radioMac) {
  sendFwAck(radioMac, thrift::MessageType::GPS_ENABLE_REQ);
}
void
X86DriverIf::sendFwStatsConfigureReq(
    const std::string& radioMac, const thrift::StatsConfigure& /*msg*/) {
  sendFwAck(radioMac, thrift::MessageType::FW_STATS_CONFIGURE_REQ);
}
void
X86DriverIf::sendPhyLAConfigReq(
    const std::string& radioMac, const thrift::PhyLAConfig& /*msg*/) {
  sendFwAck(radioMac, thrift::MessageType::PHY_LA_CONFIG_REQ);
}
void
X86DriverIf::sendPhyAgcConfigReq(
    const std::string& radioMac, const thrift::PhyAgcConfig& /*msg*/) {
  sendFwAck(radioMac, thrift::MessageType::PHY_AGC_CONFIG_REQ);
}
void
X86DriverIf::sendPhyTpcConfigReq(
    const std::string& radioMac, const thrift::PhyTpcConfig& /*msg*/) {
  sendFwAck(radioMac, thrift::MessageType::PHY_TPC_CONFIG_REQ);
}
void
X86DriverIf::sendPhyTpcAdjTblCfgReq(
    const std::string& radioMac, const thrift::PhyTpcAdjTblCfg& /*msg*/) {
  sendFwAck(radioMac, thrift::MessageType::PHY_TPC_ADJ_TBL_CFG_REQ);
}
void
X86DriverIf::sendFwConfigParamsReq(
    const std::string& radioMac, const thrift::FwConfigParams& /*msg*/) {
  sendFwAck(radioMac, thrift::MessageType::FW_CONFIG_REQ);
}
void
X86DriverIf::processFwSetCodebook(
    const std::string& radioMac,
    const thrift::PhyAntWgtCodeBookConfig& /*msg*/) {
  sendFwAck(radioMac, thrift::MessageType::FW_SET_CODEBOOK);
}
void
X86DriverIf::sendPhyGolaySequenceConfigReq(
    const std::string& radioMac,
    const thrift::PhyGolaySequenceConfigReq& /*msg*/) {
  sendFwAck(radioMac, thrift::MessageType::PHY_GOLAY_SEQUENCE_CONFIG_REQ);
}
void
X86DriverIf::sendFwDebugReq(
    const std::string& radioMac, const thrift::Debug& /*msg*/) {
  sendFwAck(radioMac, thrift::MessageType::FW_DEBUG_REQ);
}
void
X86DriverIf::sendFwBfRespScan(
    const std::string& radioMac, const thrift::BfRespScanConfig& /*msg*/) {
  sendFwAck(radioMac, thrift::MessageType::FW_BF_RESP_SCAN);
}
void
X86DriverIf::sendScanReq(
    const std::string& radioMac, const thrift::ScanReq& /*msg*/) {
  sendFwAck(radioMac, thrift::MessageType::SCAN_REQ);
}
void
X86DriverIf::sendFwSetLogConfig(
    const std::string& radioMac, const thrift::SetLogConfig& /*msg*/) {
  sendFwAck(radioMac, thrift::MessageType::FW_SET_LOG_CONFIG);
}
void
X86DriverIf::sendFwAdjResp(
    const std::string& radioMac, const thrift::FwAdjResp& /*msg*/) {
  sendFwAck(radioMac, thrift::MessageType::FW_ADJ_RESP);
}
void
X86DriverIf::sendBfSlotExclusionReq(
    const std::string& radioMac, const thrift::BfSlotExclusionReq& /*msg*/) {
  sendFwAck(radioMac, thrift::MessageType::FW_SET_NODE_PARAMS);
}

// ---- unimplemented methods ---- //

void
X86DriverIf::sendGpsTime(
    const std::string& /*radioMac*/, const thrift::GpsTimeValue& /*msg*/) {
}
void
X86DriverIf::sendGpsSetPosReq(
    const std::string& /*radioMac*/, const thrift::Location& /*msg*/) {
}

} // namespace terragraph
} // namespace facebook
