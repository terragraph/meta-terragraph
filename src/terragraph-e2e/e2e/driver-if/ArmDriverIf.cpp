/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ArmDriverIf.h"

#include <boost/filesystem.hpp>
#include <fbzmq/zmq/Zmq.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/MacAddress.h>
#include <folly/MapUtil.h>

#include "DriverIfUtil.h"
#include "FbTgFwParam.h"
#include "PassThru.h"
#include "e2e/if/gen-cpp2/BWAllocation_types.h"
#include "e2e/common/JsonUtils.h"

using namespace fbzmq;
using apache::thrift::detail::TEnumMapFactory;

DEFINE_string(
    pcie_suffix,
    ":00.0",
    "String suffix to attach to bus IDs from the node info file to form the "
    "PCI ID, e.g. converting '0001:01' to '0001:01:00.0'.");

DEFINE_string(
    codebook_dir,
    "/etc/e2e_config/wil6210/",
    "Directory containing firmware codebooks which are natively calibrated "
    "for the specific RF boards. Must end with a forward slash.");

DEFINE_string(
    fallback_codebook_dir,
    "/etc/e2e_config/",
    "Fallback directory containing firmware codebooks (within vendor-specific "
    "subdirectories). Must end with a forward slash.");

DEFINE_string(
    txpower_adj_tbl_dir,
    "/etc/e2e_config/tpc_tables/",
    "Directory containing firmware phy txpower adjustment tables. "
    "Must end with a forward slash.");

#define MAX_SUPPORTED_CHANNEL_INDEX 11

namespace {
// max optional params buffer length
uint16_t kBufLen{512};

// watchdog progress
const std::string kFwHealthDir{"fw"};
const std::string kFwNoLinkDir{"nolink"};
const std::string kIgnoredMacAddress{"00:00:00:00:00:00"};
const std::string kGpsProgressFile{"gps"};

// Get the codebook path for the given parameters.
// Note that baseDir and fallbackDir should end in a forward slash ('/').
std::string
getFwCodebookPath(
    const std::string& baseDir,
    const std::string& fallbackDir,
    const std::string& vendor,
    uint64_t ibfProcedureType,
    uint64_t codebookVariant,
    const std::string& pciSlot,
    uint8_t channel) {
  std::string codebookVariantStr;
  std::string ibfProcedureTypeStr;

  // See ibfCodebookVariant in fb_tg_fw_bf_state_if.h
  switch (codebookVariant) {
    case 0 /* CODEBOOK_VARIANT_LEGACY */:
      codebookVariantStr = "";
      break;
    case 1 /* CODEBOOK_VARIANT_1D */:
      codebookVariantStr = "_1D";
      break;
    case 2 /* CODEBOOK_VARIANT_2D */:
    default:
      codebookVariantStr = "_2D";
      break;
  }

  // See ibfProcedureType in fb_tg_fw_bf_state_if.h
  if (!codebookVariantStr.empty()) {
    switch (ibfProcedureType) {
      case 1 /* IBF_DIVERSITY */:
        ibfProcedureTypeStr = "_diversity";
        break;
      case 0 /* IBF_MASSIVE */:
      default:
        ibfProcedureTypeStr = "_massive";
        break;
    }
  }

  // Try to load native codebook first, otherwise use fallback path.
  std::string codebookFilePath;
  codebookFilePath = folly::sformat(
      "{}{}/fw_cfg_ant_codebook{}{}_chn_{}.json",
      baseDir,
      pciSlot,
      ibfProcedureTypeStr,
      codebookVariantStr,
      channel);
  if (!boost::filesystem::exists(codebookFilePath)) {
    VLOG(3) << "Native codebook not found in " << codebookFilePath;
    codebookFilePath = folly::sformat(
        "{}{}/fw_cfg_ant_codebook{}{}_chn_{}.json",
        fallbackDir, vendor, ibfProcedureTypeStr, codebookVariantStr, channel);
  }
  return codebookFilePath;
}

// Get the txPower adj table path for the given parameters.
// Note that baseDir should end in a forward slash ('/').
std::string
getFwTxPowerAdjTblPath(
    const std::string& baseDir,
    uint64_t ibfProcedureType,
    uint64_t codebookVariant,
    int channel) {
  std::string codebookVariantStr;
  std::string ibfProcedureTypeStr;

  // See ibfCodebookVariant in fb_tg_fw_bf_state_if.h
  switch (codebookVariant) {
    case 0 /* CODEBOOK_VARIANT_LEGACY */:
      codebookVariantStr = "";
      break;
    case 1 /* CODEBOOK_VARIANT_1D */:
      codebookVariantStr = "_1D";
      break;
    case 2 /* CODEBOOK_VARIANT_2D */:
    default:
      codebookVariantStr = "_2D";
      break;
  }

  // See ibfProcedureType in fb_tg_fw_bf_state_if.h
  if (!codebookVariantStr.empty()) {
    switch (ibfProcedureType) {
      case 1 /* IBF_DIVERSITY */:
        ibfProcedureTypeStr = "_diversity";
        break;
      case 0 /* IBF_MASSIVE */:
      default:
        ibfProcedureTypeStr = "_massive";
        break;
    }
  }

  std::string filePath;
  filePath = folly::sformat(
      "{}fw_phy_txpower_adj_tbl{}{}_chn_{}.json",
      baseDir, ibfProcedureTypeStr, codebookVariantStr, channel);

  return filePath;
}

void
fillGpsSelfPos(
    struct t_gps_self_pos& gpsPosCmd,
    const facebook::terragraph::thrift::Location& location) {
  // driver is expecting lat/long scaled by 10^7
  gpsPosCmd.latitude = location.latitude * k1e7;
  gpsPosCmd.longitude = location.longitude * k1e7;
  // driver is expecting altitude in cm
  gpsPosCmd.height = location.altitude * k1e2;
  // driver is expecting accuracy in mm
  gpsPosCmd.accuracy = location.accuracy * k1e3;
}

} // namespace

namespace facebook {
namespace terragraph {

ArmDriverIf::ArmDriverIf(
    fbzmq::Context& zmqContext,
    const std::string& pairSockUrl,
    const std::string& monitorSubmitUrl,
    std::unique_ptr<BaseNetlinkSocket> netlinkSocket,
    bool isIf2if,
    const std::unordered_map<std::string, std::string>& radioMacToBusId,
    bool daemonMode)
    : BaseDriverIf(
          zmqContext,
          pairSockUrl,
          monitorSubmitUrl,
          radioMacToBusId,
          daemonMode),
      netlinkSocket_(std::move(netlinkSocket)),
      isIf2if_{isIf2if} {
  try {
    netlinkSocket_->connect();
  } catch (std::exception const& e) {
    LOG(FATAL) << "Could not connect to the underlying driver netlink socket.  "
                  "Please check whether the driver is loaded properly or if "
                  "the pci devices are detected correctly. "
               << folly::exceptionStr(e);
  }

  if (isIf2if) {
    LOG(INFO) << "Operating in IF2IF mode";
  }

  // message from netlink socket
  addSocketFd(netlinkSocket_->getSocketFd(), ZMQ_POLLIN, [this](int) noexcept {
    VLOG(6) << "ArmDriverIf: Received a message from netlink";
    auto driverNlMessage = netlinkSocket_->getMessage();
    if (!driverNlMessage) {
      nlRecvErrCount_++;
      return;
    }
    nlRecvOkCount_++;
    auto message = driverNl2IfMessage(*driverNlMessage);

    // parse DriverMessage wrapper struct
    auto driverMsg = unwrap<thrift::DriverMessage>(message.value);
    if (!driverMsg) {
      LOG(ERROR) << "Failed to unwrap message of type: "
                 << folly::get_default( TEnumMapFactory<thrift::MessageType>::
                        makeValuesToNamesMap(), message.mType, "UNKNOWN");
      return;
    }

    switch (message.mType) {
      case thrift::MessageType::DR_RESP: {
        // terminate all driver responses here and spare upper layer apps
        ++drRespCount_;
        auto driverResp = unwrap<thrift::DriverResp>(driverMsg->value);
        if (driverResp) {
          auto reqType = folly::get_default(
              TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
              driverResp->reqType,
              "UNKNOWN");
          if (driverResp->success) {
            VLOG(4) << "Driver response for " << reqType << " succeeded";
          } else {
            LOG(ERROR)
                << folly::sformat("Driver response for {} failed", reqType);
          }
        }
        break;
      }
      case thrift::MessageType::NONE: {
        // Ignore all nonsense messages, e.g., the superfluous responses for the
        // firmware-bound pass-through messages sent by the driver.
        break;
      }
      case thrift::MessageType::FW_STATS: {
        // publish firmware stats
        auto report = unwrap<thrift::DriverFwStatsReport>(driverMsg->value);
        if (report) {
          processFwStatsMessage(driverMsg->radioMac, *report);
        }
        break;
      }
      case thrift::MessageType::FW_HEALTHY: {
        // Handle FW health report
        auto fwHealthReport = unwrap<thrift::FwHealthReport>(driverMsg->value);
        if (fwHealthReport) {
          processFwHealthyMessage(*fwHealthReport);
          goto default_case;  // forward message up the stack
        }
        break;
      }
      case thrift::MessageType::DR_STAT_PUSH: {
        auto report = unwrap<thrift::DrvrStatsList>(driverMsg->value);
        if (report) {
          processDrvrStats(*report);
        }
        break;
      }
      case thrift::MessageType::FW_GET_CODEBOOK: {
        auto req = unwrap<thrift::CodebookFetchReq>(driverMsg->value);
        if (req) {
          processFwGetCodebook(driverMsg->radioMac, *req);
        }
        break;
      }
      case thrift::MessageType::NODE_INIT_NOTIFY: {
        auto notify = unwrap<thrift::DriverNodeInitNotif>(driverMsg->value);
        if (notify) {
          processNodeInitNotify(driverMsg->radioMac, *notify);
          goto default_case;  // forward message up the stack
        }
        break;
      }
      case thrift::MessageType::FW_GET_PHY_TPC_ADJ_TBL: {
        auto req = unwrap<thrift::TpcAdjTblFetchReq>(driverMsg->value);
        if (req) {
          processFwGetPhyTpcAdjTbl(driverMsg->radioMac, *req);
        }
        break;
      }
      default: {
      default_case:
        // forward notifications up
        ++upNotifCount_;
        sendToDriverApp(message);
      }
    }
  });
}

void
ArmDriverIf::publishDriverIfStats(int64_t tsUs) {
  BaseDriverIf::publishDriverIfStats(tsUs);

  auto gaugeType = fbzmq::thrift::CounterValueType::GAUGE;
  setCounter("tgd.nl.recv_ok", nlRecvOkCount_, gaugeType, tsUs);
  setCounter("tgd.nl.recv_err", nlRecvErrCount_, gaugeType, tsUs);
}

void
ArmDriverIf::processFwHealthyMessage(
    const thrift::FwHealthReport& fwHealthReport) const {
  if (fwHealthReport.selfMacAddr.compare(kIgnoredMacAddress) == 0) {
    return;
  }

  // The f/w is running and has valid mac address.
  progress_.report(kFwHealthDir + "/" + fwHealthReport.selfMacAddr);

  // No RF link formed for a long time.
  // Note: We are stretching the 'progress' concept. Here it means
  //       'sticky fault'. The watchdog will restart e2e_minion and remove
  //       the fault file(s).
  if ((fwHealthReport.faultFlags & int(thrift::FaultFlags::FW_NOLINK_FAULT)) !=
      0) {
    progress_.report(kFwNoLinkDir + "/" + fwHealthReport.selfMacAddr);
  }

  // The GPS is good if any baseband card reports it as such.
  if ((fwHealthReport.faultFlags & int(thrift::FaultFlags::FW_GPS_FAULT)) ==
      0) {
    progress_.report(kGpsProgressFile);
  }
}

void
ArmDriverIf::processFwStatsMessage(
    const std::string& radioMac,
    const thrift::DriverFwStatsReport& report) const {
  const auto& statsSamples = report.stats.statsSamples;
  for (uint32_t i = 0; i < statsSamples.size(); i++) {
    // fbzmq's Counter has no "entity" concept (each radioMac should be an
    // entity). Add the entity as part of the key, then split it out on all
    // consumers.
    // key format: [key_name]\0[entity]
    std::string key = statsSamples[i].key + '\0' + radioMac;

    // use firmware time in tsf (us) as timestamp, may not be from epoch
    setCounter(
        key,
        statsSamples[i].value,
        fbzmq::thrift::CounterValueType::GAUGE,
        statsSamples[i].tsf);
  }
}

void
ArmDriverIf::sendNodeInit(
    const std::string& radioMac, const thrift::DriverNodeInitReq& msg) {
  // Record info about IBF procedure type and codebook variant
  ArmDriverIf::RadioProperties& radioProps = radioInitProps_[radioMac];
  if (msg.optParams.ibfProcedureType_ref().has_value()) {
    radioProps.ibfProcedureType = msg.optParams.ibfProcedureType_ref().value();
  }
  if (msg.optParams.ibfCodebookVariant_ref().has_value()) {
    radioProps.codebookVariant = msg.optParams.ibfCodebookVariant_ref().value();
  }

  // HACK:
  // ibfSet*RficBitmap must be 0 for IF2IF operation.
  // Put a workaround here while we wait for a fix in the IF2IF firmware.
  thrift::FwOptParams optParams = msg.optParams;
  if (isIf2if_) {
    if (optParams.ibfSet1RficBitmap_ref().has_value() &&
        optParams.ibfSet1RficBitmap_ref().value() != 0) {
      LOG(INFO) << "HACK: Setting ibfSet1RficBitmap to 0 (required for IF2IF)";
      optParams.ibfSet1RficBitmap_ref() = 0;
    }
    if (optParams.ibfSet2RficBitmap_ref().has_value() &&
        optParams.ibfSet2RficBitmap_ref().value() != 0) {
      LOG(INFO) << "HACK: Setting ibfSet2RficBitmap to 0 (required for IF2IF)";
      optParams.ibfSet2RficBitmap_ref() = 0;
    }
  }

  DriverNlMessage dnlMessage;
  dnlMessage.mType = DriverNlMessageType::NODE_INIT;
  dnlMessage.setRadioMac(radioMac);

  // append optional fw parameters if any
  uint8_t buf[kBufLen];
  auto len = fbFwParams2Pack(optParams, buf, kBufLen);
  if (len > 0) {
    std::vector<uint8_t> vec(buf, buf + len);
    dnlMessage.u8vlaAttrs = {TGD_NLSDN_ATTR_VARDATA, vec};
  }

  netlinkSocket_->sendMessage(dnlMessage);
}

void
ArmDriverIf::sendLinkStatus(
    const std::string& radioMac, const thrift::DriverSetLinkStatus& msg) {
  thrift::PassThruMsg passThruMsg;
  if (msg.isAssoc) {
    // bring up link
    passThruMsg.msgType = thrift::PtMsgTypes::SB_ASSOC;
    passThruMsg.dest = thrift::PtMsgDest::SB;
    passThruMsg.assoc.addr = msg.responderMac;
    passThruMsg.assoc.optionalParams = msg.optParams;
  } else {
    // bring down link
    passThruMsg.msgType = thrift::PtMsgTypes::SB_DISSOC;
    passThruMsg.dest = thrift::PtMsgDest::SB;
    passThruMsg.dissoc.addr = msg.responderMac;
  }
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
}

void
ArmDriverIf::sendGpsTime(
  const std::string& radioMac, const thrift::GpsTimeValue& msg) {
  DriverNlMessage dnlMessage;
  dnlMessage.mType = DriverNlMessageType::SET_GPS_TIME;
  dnlMessage.setRadioMac(radioMac);
  dnlMessage.u64Attrs.emplace_back(
      TGD_NLSDN_ATTR_GPS_TIME_S, (uint64_t) msg.unixTimeSecs);
  dnlMessage.u64Attrs.emplace_back(
      TGD_NLSDN_ATTR_GPS_TIME_NS, (uint64_t) msg.unixTimeNsecs);
  netlinkSocket_->sendMessage(dnlMessage);
}

void
ArmDriverIf::sendNodeParams(
    const std::string& radioMac, const thrift::FwSetNodeParams& msg) {
  for (const thrift::PassThruMsg& passThruMsg : msg.passThruMsgs) {
    // check message type
    if (passThruMsg.msgType == thrift::PtMsgTypes::SB_AIRTIMEALLOC ||
        passThruMsg.msgType == thrift::PtMsgTypes::SB_POLARITY ||
        passThruMsg.msgType == thrift::PtMsgTypes::SB_GOLAY_INDX ||
        passThruMsg.msgType == thrift::PtMsgTypes::SB_CHANNEL_CONFIG ||
        passThruMsg.msgType == thrift::PtMsgTypes::SB_BF_SLOT_EXCLUSION_REQ) {
      prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
    } else {
      LOG(ERROR) << "Request for south bound passthru has bad message type: "
                 << folly::get_default(
                    TEnumMapFactory<thrift::PtMsgTypes>::makeValuesToNamesMap(),
                    passThruMsg.msgType,
                    "UNKNOWN");
    }
  }

  if (msg.location_ref().has_value()) {
    sendLocationToGps(msg.location_ref().value());
  }
}

void
ArmDriverIf::sendFwConfigParamsReq(
    const std::string& radioMac, const thrift::FwConfigParams& msg) {
  for (const thrift::PassThruMsg& passThruMsg : msg.passThruMsgs) {
    LOG(INFO) << "sendFwConfigParamsReq type ="
              << (uint32_t)passThruMsg.msgType;
    // check message type
    if (passThruMsg.msgType != thrift::PtMsgTypes::SB_INVALID) {
      prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
    } else {
      LOG(ERROR) << "Request for south bound passthru has bad message type: "
                 << folly::get_default(
                    TEnumMapFactory<thrift::PtMsgTypes>::makeValuesToNamesMap(),
                    passThruMsg.msgType,
                    "UNKNOWN");
    }
  }
}

void
ArmDriverIf::sendFwStatsConfigureReq(
    const std::string& radioMac, const thrift::StatsConfigure& msg) {
  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_STATS_CONFIGURE;
  passThruMsg.dest = thrift::PtMsgDest::SB;
  passThruMsg.statsConfigure = msg;
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
}

void
ArmDriverIf::sendPhyLAConfigReq(
    const std::string& radioMac, const thrift::PhyLAConfig& msg) {
  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_PHY_LA_CONFIG;
  passThruMsg.dest = thrift::PtMsgDest::SB;
  passThruMsg.phyLAConfig.addr = msg.addr;
  passThruMsg.phyLAConfig.laParams = msg.laParams;
  passThruMsg.phyLAConfig.laNodeParams = msg.laNodeParams;
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
}

void
ArmDriverIf::sendPhyAgcConfigReq(
    const std::string& radioMac, const thrift::PhyAgcConfig& msg) {
  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_PHY_AGC_CONFIG;
  passThruMsg.dest = thrift::PtMsgDest::SB;
  passThruMsg.phyAgcConfig.addr = msg.addr;
  passThruMsg.phyAgcConfig.agcNodeParams = msg.agcNodeParams;
  passThruMsg.phyAgcConfig.agcLinkParams = msg.agcLinkParams;
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
}

void
ArmDriverIf::sendPhyTpcConfigReq(
    const std::string& radioMac, const thrift::PhyTpcConfig& msg) {
  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_PHY_TPC_CONFIG;
  passThruMsg.dest = thrift::PtMsgDest::SB;
  passThruMsg.phyTpcConfig.addr = msg.addr;
  passThruMsg.phyTpcConfig.tpcNodeParams = msg.tpcNodeParams;
  passThruMsg.phyTpcConfig.tpcLinkParams = msg.tpcLinkParams;
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
}

void
ArmDriverIf::sendPhyTpcAdjTblCfgReq(
    const std::string& radioMac, const thrift::PhyTpcAdjTblCfg& msg) {
  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_PHY_TPC_ADJ_TBL_CFG;
  passThruMsg.dest = thrift::PtMsgDest::SB;

  passThruMsg.phyTpcAdjTblCfg.channel = msg.channel;
  passThruMsg.phyTpcAdjTblCfg.txPowerAdjTable = msg.txPowerAdjTable;
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
}

void
ArmDriverIf::sendGpsGetPosReq() {
  DriverNlMessage dnlMessage;
  dnlMessage.mType = DriverNlMessageType::DRVR_REQ;

  struct t_gps_self_pos gpsPosCmd;
  gpsPosCmd.cmd = DRVR_CFG_CMD_GPS;
  gpsPosCmd.sub_cmd = GPS_GET_CMD_POS;
  uint8_t* buf = (uint8_t*)&gpsPosCmd;
  std::vector<uint8_t> vec(buf, buf + sizeof(gpsPosCmd));
  dnlMessage.u8vlaAttrs = {TGD_NLSDN_ATTR_VARDATA, vec};

  netlinkSocket_->sendMessage(dnlMessage);
}

void
ArmDriverIf::sendGpsSetPosReq(
    const std::string& radioMac, const thrift::Location& msg) {
  DriverNlMessage dnlMessage;
  dnlMessage.mType = DriverNlMessageType::SET_GPS_POS;
  dnlMessage.setRadioMac(radioMac);

  struct t_gps_self_pos gpsPosCmd;
  fillGpsSelfPos(gpsPosCmd, msg);
  uint8_t* buf = (uint8_t*)&gpsPosCmd;
  std::vector<uint8_t> vec(buf, buf + sizeof(gpsPosCmd));
  dnlMessage.u8vlaAttrs = {TGD_NLSDN_ATTR_VARDATA, vec};

  netlinkSocket_->sendMessage(dnlMessage);
}

void
ArmDriverIf::sendGpsEnableReq(const std::string& radioMac) {
  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_GPS_ENABLE;
  passThruMsg.dest = thrift::PtMsgDest::SB;
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
}

void
ArmDriverIf::sendDevAllocReq(
    const std::string& radioMac, const thrift::DriverDevAllocReq& msg) {
  DriverNlMessage dnlMessage;
  dnlMessage.mType = DriverNlMessageType::DEV_ALLOC;
  dnlMessage.setRadioMac(radioMac);
  dnlMessage.u64Attrs.emplace_back(
      TGD_NLSDN_ATTR_MACADDR, folly::MacAddress(msg.macAddr).u64HBO());

  netlinkSocket_->sendMessage(dnlMessage);
}

void
ArmDriverIf::sendPhyGolaySequenceConfigReq(
    const std::string& radioMac, const thrift::PhyGolaySequenceConfigReq& msg) {
  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_PHY_GOLAY_SEQUENCE_CONFIG;
  passThruMsg.dest = thrift::PtMsgDest::SB;
  passThruMsg.phyGolaySequenceCfg.transmitSequence = msg.transmitSequence;
  passThruMsg.phyGolaySequenceCfg.receiveSequence = msg.receiveSequence;
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
}

void
ArmDriverIf::sendCodebook(
    const std::string& radioMac,
    const thrift::PhyAntWgtCodeBookConfig& codebook,
    const thrift::CodebookFetchReq& req) const {
  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_PHY_ANT_WGT_TBL_CONFIG;
  passThruMsg.dest = thrift::PtMsgDest::SB;

  const size_t kMaxBeamCountInOneMsg = 8;

  // If req.beamCount > kMaxBeamCountInOneMsg, then we'll partition beams
  // into kMaxBeamCountInOneMsg-sized messages and send
  // roundup(req.beamCount/kMaxBeamCountInOneMsg) messages for each stride
  // (out of req.strideCount). This would be the case, e.g. when requesting
  // the whole codebook.
  // Example: beamStart=0, beamCount=64, strideCount=1
  // We'll send 8 messages (per module per tx/rx):
  // Msg 1: Beams 0-7
  // Msg 2: Beams 8-15
  // etc.
  //
  // If req.beamCount <= kMaxBeamCountInOneMsg, then we'll pack
  // s = rounddown(kMaxBeamCountInOneMsg/req.beamCount) strides, each of
  // req.beamCount beams, into a message, and will send req.strideCount/s
  // messages. This would be the case when subsampling the codebook
  // (e.g. requesting the first 3 beams of every 10)
  // Example: beamStart=5, beamCount=3, stride=10, strideCount=3
  // Since beamCount=3 and kMaxBeamCountInOneMsg=8, a message will contain 2
  // strides.
  // Msg 1: Beams 5-7, 15-17
  // Msg 2: Beams 25-27
  // (Msg 2 contains only one stride since it's the last one and we don't
  // have enough strides to fully fill it)

  for (const thrift::PhyAntWgtTable& codeBookTable : codebook.codeBookTables) {
    if (codeBookTable.tableParts.size() != 1) {
      LOG(FATAL) << "tablepart of size " << codeBookTable.tableParts.size();
    }

    const thrift::PhyAntWgtTblPartJson& tablePart = codeBookTable.tableParts[0];

    thrift::PhyAntWgtTblPart& output = passThruMsg.phyAntWgtTblPart;
    output.module = tablePart.module;
    output.isRx = tablePart.isRx;
    // req.beamCount might be larger than the actual number of beams if the
    // caller doesn't know the exact amount, e.g. processFwSetCodebook()
    const size_t beamCount = std::min(
        (size_t)req.beamCount, tablePart.antWgtCode.size() - req.beamStart);

    if (beamCount > kMaxBeamCountInOneMsg) {
      for (int strideIndex = 0; strideIndex < req.strideCount; strideIndex++) {
        const size_t strideStart = req.beamStart + strideIndex * req.stride;
        size_t beamStart = strideStart;
        if (beamStart >= tablePart.antWgtCode.size()) {
          break;
        }
        const size_t beamEnd = strideStart + beamCount;
        while (beamStart < beamEnd) {
          output.beamStart = beamStart;
          output.beamCount =
              std::min(kMaxBeamCountInOneMsg, beamEnd - beamStart);
          output.stride = 0; // doesn't matter since strideCount=1
          output.strideCount = 1;
          output.antWgtCode.assign(
              tablePart.antWgtCode.begin() + beamStart,
              tablePart.antWgtCode.begin() + beamStart + output.beamCount);
          prepareAndSendSBPassThruMessage(
              passThruMsg, radioMac, true /* noAck */);
          beamStart += output.beamCount;
        }
      }
    } else {
      // beamCount <= kMaxBeamCountInOneMsg
      if ((size_t)(req.beamStart + req.stride * (req.strideCount - 1)) >=
          tablePart.antWgtCode.size()) {
        LOG(ERROR) << "Bad codebook request, beam out of range: start="
                   << req.beamStart << ", stride=" << req.stride
                   << ", strideCount=" << req.strideCount;
        return;
      }
      const size_t stridesPerMessage = kMaxBeamCountInOneMsg / beamCount;
      // round messageCount up
      const size_t messageCount =
          (req.strideCount + stridesPerMessage - 1) / stridesPerMessage;
      for (size_t m = 0; m < messageCount; m++) {
        const size_t beamStart =
            req.beamStart + m * stridesPerMessage * req.stride;
        output.beamStart = beamStart;
        output.beamCount = beamCount;
        output.stride = req.stride;
        // Usually this is stridesPerMessage, except the last message, which can
        // be less
        output.strideCount = std::min(
            stridesPerMessage, req.strideCount - m * stridesPerMessage);
        output.antWgtCode.clear();
        output.antWgtCode.reserve(beamCount * output.strideCount);
        for (size_t s = 0; s < (size_t)output.strideCount; s++) {
          output.antWgtCode.insert(
              output.antWgtCode.end(),
              tablePart.antWgtCode.begin() + beamStart + s * req.stride,
              tablePart.antWgtCode.begin() + beamStart + s * req.stride +
                  beamCount);
        }
        prepareAndSendSBPassThruMessage(
            passThruMsg, radioMac, true /* noAck */);
      }
    }
  }
}

void
ArmDriverIf::processCodebook(
    thrift::PhyAntWgtCodeBookConfig& codebook,
    const std::string& codebookFile) const {
  // Concatenate all table parts of the same table (that have the same module
  // and direction). This part will be removed when we update the codebook
  // files to have this format in the first place.
  for (thrift::PhyAntWgtTable& table : codebook.codeBookTables) {
    if (table.tableParts.empty()) {
      LOG(FATAL) << "Empty tablepart in codebook file " << codebookFile;
    }
    table.tableParts[0].antWgtCode.reserve(
        table.tableParts.size() * table.tableParts[0].antWgtCode.size());
    for (size_t i = 1; i < table.tableParts.size(); i++) {
      table.tableParts[0].antWgtCode.insert(
          table.tableParts[0].antWgtCode.end(),
          table.tableParts[i].antWgtCode.begin(),
          table.tableParts[i].antWgtCode.end());
      table.tableParts[i].antWgtCode.clear();
    }
    table.tableParts.resize(1);
  }

  // Verify that all weight vectors have the same number of elements.
  // We do it in a separate loop since the previous loop will disappear
  // once we move to the new codebook format (with no table parts)
  for (const thrift::PhyAntWgtTable& table : codebook.codeBookTables) {
    const thrift::PhyAntWgtTblPartJson& part = table.tableParts[0];
    if (part.antWgtCode.empty()) {
      continue;
    }
    size_t numElements = part.antWgtCode[0].size();
    for (const auto& weightVector : part.antWgtCode) {
      if (weightVector.size() != numElements) {
        LOG(FATAL) << "Weight vector contains " << weightVector.size()
                   << " elements instead of " << numElements;
      }
    }
  }
}

void
ArmDriverIf::processFwSetCodebook(
    const std::string& radioMac, const thrift::PhyAntWgtCodeBookConfig& msg) {
  thrift::PhyAntWgtCodeBookConfig codebook = msg;
  processCodebook(codebook);
  // Make a fake request that covers all beams
  thrift::CodebookFetchReq req;
  req.beamStart = 0;
  req.beamCount = ~0;
  req.stride = 0;
  req.strideCount = 1;
  sendCodebook(radioMac, codebook, req);
}

void
ArmDriverIf::processFwGetCodebook(
    const std::string& radioMac,
    const thrift::CodebookFetchReq& codebookFetchReq) const {
  // Validity checks
  auto radioPropsIter = radioInitProps_.find(radioMac);
  if (radioPropsIter == radioInitProps_.end()) {
    LOG(FATAL) << "Received codebook request for unknown radio MAC "
               << radioMac;
  }
  const auto& radioProps = radioPropsIter->second;
  int codebookIdx = getCodebookVecIndex(radioProps);
  if (codebookIdx == -1) {
    LOG(FATAL) << "Received codebook request for radio MAC " << radioMac
               << " with uninitialized codebooks (vendor=<" << radioProps.vendor
               << ">, ibfProcedureType=" << radioProps.ibfProcedureType
               << ", codebookVariant=" << radioProps.codebookVariant
               << ", pciSlot=" << radioProps.pciSlot << ")";
  }
  const auto& codebooks = codebooks_[codebookIdx].second;

  // Validate requested channel
  uint8_t channel = (uint8_t)codebookFetchReq.channel;
  auto iter = codebooks.find(channel);
  if (iter == codebooks.end()) {
    LOG(FATAL) << "Request from firmware for codebook for unknown channel "
               << (int)channel << ", vendor=<" << radioProps.vendor << ">";
  }

  // Send the codebook for this channel
  VLOG(4) << "Sending codebook for radio MAC " << radioMac
          << ", channel=" << (int)channel
          << ", vendor=<" << radioProps.vendor
          << ">, ibfProcedureType=" << radioProps.ibfProcedureType
          << ", codebookVariant=" << radioProps.codebookVariant
          << ", pciSlot=" << radioProps.pciSlot;
  sendCodebook(radioMac, iter->second, codebookFetchReq);
}

void
ArmDriverIf::processNodeInitNotify(
    const std::string& radioMac, const thrift::DriverNodeInitNotif& notify) {
  // Validity checks
  if (!notify.success) {
    LOG(ERROR) << "Node init unsuccessful";
    return;
  }
  if (notify.vendor.empty()) {
    LOG(ERROR) << "Received empty vendor string for radio MAC " << radioMac;
    return;
  }
  if (!radioInitProps_.count(radioMac)) {
    if (radioInitProps_.count("")) {
      // Special case: For single-baseband nodes, we may not care what the radio
      // MAC is beforehand (i.e. when sending NODE_INIT), and we learn it from
      // this response. Associate "" (empty MAC) with the actual MAC here.
      VLOG(3) << "Associating empty radio MAC from NODE_INIT with MAC received "
                 "from NODE_INIT_NOTIFY: " << radioMac;
      radioInitProps_[radioMac] = radioInitProps_[""];
      radioInitProps_.erase("");
    } else {
      LOG(ERROR) << "Received DriverNodeInitNotif for unknown radio MAC "
                 << radioMac;
      // continue anyway...
    }
  }

  // Store vendor string for this radioMac
  ArmDriverIf::RadioProperties& radioProps = radioInitProps_[radioMac];
  radioProps.vendor = notify.vendor;
  auto iter = radioMacToBusId_.find(radioMac);
  if (iter != radioMacToBusId_.end()) {
    radioProps.pciSlot = iter->second + FLAGS_pcie_suffix;
  }

  // Load codebooks (if needed)
  if (getCodebookVecIndex(radioProps) == -1) {
    loadCodebook(radioMac, radioProps);
  } else {
    VLOG(2) << "Using previously-loaded codebooks for radio MAC " << radioMac;
  }

  // Load phy txpower adjustment tables (if needed)
  if (getTpcAdjTblVecIndex(radioProps) == -1) {
    loadPhyTpcAdjTables(radioMac, radioProps);
  } else {
    VLOG(2) << "Using previously-loaded txpower adjustment tables"
               " for radio MAC " << radioMac;
  }
}

void
ArmDriverIf::processFwGetPhyTpcAdjTbl(
    const std::string& radioMac,
    const thrift::TpcAdjTblFetchReq& tpcAdjTblFetchReq) const {

  // Sanity checks
  auto radioPropsIter = radioInitProps_.find(radioMac);
  if (radioPropsIter == radioInitProps_.end()) {
    LOG(ERROR) << "Received txpower adj tbl request for unknown radio MAC "
               << radioMac;
    return;
  }

  const auto& radioProps = radioPropsIter->second;
  int tblIdx = getTpcAdjTblVecIndex(radioProps);
  if (tblIdx == -1) {
    LOG(ERROR) << "Received txpower adj tbl fetch request for radio MAC "
               << radioMac << " with uninitialized txpower adjustment tables ("
               << "ibfProcedureType=" << radioProps.ibfProcedureType
               << ", codebookVariant=" << radioProps.codebookVariant << ")";
    return;
  }
  const auto& tpcAdjTables = tpcAdjTables_[tblIdx].second;

  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_PHY_TPC_ADJ_TBL_CFG;
  passThruMsg.dest = thrift::PtMsgDest::SB;

  // Validate requested channel
  uint8_t channel = (uint8_t)tpcAdjTblFetchReq.channel;

  /* Get channel index from table */
  size_t chanIdx = 0;
  for (;chanIdx < tpcAdjTables.size(); chanIdx++) {
    if ((uint8_t)tpcAdjTables[chanIdx].channel == channel) {
      break;
    }
  }

  if (chanIdx >= tpcAdjTables.size()) {
    LOG(ERROR) << "Request from firmware for txpower adj table for "
               << "invalid channel " << (int)tpcAdjTblFetchReq.channel;

    // Pass failure notification to firmware
    passThruMsg.phyTpcAdjTblCfg.channel = 0;
    passThruMsg.phyTpcAdjTblCfg.txPowerAdjTable = {0};
  } else {
    // Send the txpower adjustment table for this channel
    VLOG(4) << "Sending txpower adj table for radio MAC " << radioMac
            << ", channel=" << (int)tpcAdjTblFetchReq.channel
            << ", ibfProcedureType=" << radioProps.ibfProcedureType
            << ", codebookVariant=" << radioProps.codebookVariant;

    passThruMsg.phyTpcAdjTblCfg.channel = tpcAdjTables[chanIdx].channel;
    passThruMsg.phyTpcAdjTblCfg.txPowerAdjTable =
      tpcAdjTables[chanIdx].txPowerAdjTable;
  }

  // Send the table to firmware
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac, true /* noAck */);
}

void
ArmDriverIf::sendFwDebugReq(
    const std::string& radioMac, const thrift::Debug& msg) {
  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_DEBUG;
  passThruMsg.dest = thrift::PtMsgDest::SB;
  passThruMsg.debug = msg;
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
}

void
ArmDriverIf::sendFwBfRespScan(
    const std::string& radioMac, const thrift::BfRespScanConfig& msg) {
  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_BF_RESP_SCAN_CONFIG;
  passThruMsg.dest = thrift::PtMsgDest::SB;
  passThruMsg.bfRespScanCfg = msg;
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
}

void
ArmDriverIf::prepareAndSendSBPassThruMessage(
    const thrift::PassThruMsg& passThroughMessage,
    const std::string& radioMac,
    bool noAck) const {
  // create driver-bound pass-through message
  DriverNlMessage dnlMessage;

  // set message type
  dnlMessage.mType = DriverNlMessageType::PASSTHRU_SB;

  // set radio MAC
  dnlMessage.setRadioMac(radioMac);

  // pass along message subtype for passthru, using TGD_NLSDN_ATTR_PASSTHRU_TYPE
  // as the attribute key
  dnlMessage.u8Attrs.push_back(
      {TGD_NLSDN_ATTR_PASSTHRU_TYPE,
       static_cast<uint8_t>(passThroughMessage.msgType)});

  // request no ACK confirmation for message?
  if (noAck) {
    dnlMessage.u8Attrs.push_back({TGD_NLSDN_ATTR_PASSTHRU_NOACK, 1});
  }

  // fill message data
  uint8_t buf[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(passThroughMessage, buf);
  if (len > 0) {
    std::vector<uint8_t> vec(buf, buf + len);
    dnlMessage.u8vlaAttrs = {TGD_NLSDN_ATTR_VARDATA, vec};
  }

  // send message via netlink
  netlinkSocket_->sendMessage(dnlMessage);
}

void
ArmDriverIf::sendLocationToGps(const thrift::Location& location) const {
  // Should we avoid sending to u-blox driver?
  if (sendLocationToGpsBase(location)) {
    return;
  }

  DriverNlMessage dnlMessage;
  dnlMessage.mType = DriverNlMessageType::DRVR_REQ;

  struct t_gps_self_pos gpsPosCmd;
  gpsPosCmd.cmd = DRVR_CFG_CMD_GPS;
  gpsPosCmd.sub_cmd = GPS_SET_CMD_SING_SAT;
  fillGpsSelfPos(gpsPosCmd, location);
  uint8_t* buf = (uint8_t*)&gpsPosCmd;
  std::vector<uint8_t> vec(buf, buf + sizeof(gpsPosCmd));
  dnlMessage.u8vlaAttrs = {TGD_NLSDN_ATTR_VARDATA, vec};

  netlinkSocket_->sendMessage(dnlMessage);
}

void
ArmDriverIf::sendScanReq(
    const std::string& radioMac, const thrift::ScanReq& msg) {
  std::string scanTypeStr;
  if (msg.scanType_ref().has_value()) {
    auto map = TEnumMapFactory<thrift::ScanType>::makeValuesToNamesMap();
    auto iter = map.find(msg.scanType_ref().value());
    if (iter != map.end()) {
      scanTypeStr = iter->second;
    }
  }
  LOG(INFO) << "Sending passthrough scan request (type='" << scanTypeStr
            << "', mac='" << msg.radioMac_ref().value_or("") << "', bwgd="
            << msg.startBwgdIdx << ", token=" << msg.token << ")";
  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_SCAN_REQ;
  passThruMsg.dest = thrift::PtMsgDest::SB;
  passThruMsg.scanReq = msg;
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
}

void
ArmDriverIf::sendFwSetLogConfig(
    const std::string& radioMac, const thrift::SetLogConfig& msg) {
  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_SET_LOG_CONFIG;
  passThruMsg.dest = thrift::PtMsgDest::SB;
  passThruMsg.setLogConfig = msg;
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
}

void
ArmDriverIf::sendFwAdjResp(
    const std::string& radioMac, const thrift::FwAdjResp& msg) {
  // send local adjacency info response to fw
  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_SET_ADJ;
  passThruMsg.dest = thrift::PtMsgDest::SB;
  passThruMsg.fwAdjResp = msg;
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
}

void
ArmDriverIf::loadCodebook(
    const std::string& radioMac,
    const ArmDriverIf::RadioProperties& radioProps) {
  int codebookIdx = getCodebookVecIndex(radioProps);
  if (codebookIdx == -1) {
    codebookIdx = codebooks_.size();
    std::unordered_map<uint8_t, thrift::PhyAntWgtCodeBookConfig> codebooks;
    codebooks_.push_back(std::make_pair(radioProps, codebooks));
  }
  auto& codebooks = codebooks_[codebookIdx].second;
  codebooks.clear();
  // Load codebooks for each channel
  for (uint8_t channel = 1; channel <= MAX_SUPPORTED_CHANNEL_INDEX; channel++) {
    // Construct file path
    std::string codebookFile = getFwCodebookPath(
        FLAGS_codebook_dir,
        FLAGS_fallback_codebook_dir,
        radioProps.vendor,
        radioProps.ibfProcedureType,
        radioProps.codebookVariant,
        radioProps.pciSlot,
        channel);

    // Read file
    std::string contents;
    if (!folly::readFile(codebookFile.c_str(), contents)) {
      if (errno == ENOENT) {
        // File not found, skip
        continue;
      }
      // Any other error means the file exists but we failed to read it
      LOG(FATAL) << "Could not read firmware codebook file: " << codebookFile;
    }
    VLOG(2) << "Loaded codebook file: " << codebookFile
            << " for mac=" << radioMac
            << ", pciSlot=" << radioProps.pciSlot;

    // Parse the codebook
    auto codebook =
        JsonUtils::deserializeFromJson<thrift::PhyAntWgtCodeBookConfig>(
            contents);
    if (!codebook) {
      LOG(FATAL) << "Could not parse codebook struct in " << codebookFile;
    }
    processCodebook(codebook.value(), codebookFile);

    codebooks[channel] = std::move(codebook.value());
  }

  VLOG(2) << "Finished loading " << codebooks.size()
          << " codebook(s) for vendor=<" << radioProps.vendor
          << ">, ibfProcedureType=" << radioProps.ibfProcedureType
          << ", codebookVariant=" << radioProps.codebookVariant
          << ", mac=" << radioMac
          << ", pciSlot=" << radioProps.pciSlot;
}

int
ArmDriverIf::getCodebookVecIndex(const RadioProperties& radioProps) const {
  for (size_t i = 0; i < codebooks_.size(); i++) {
    if (codebooks_[i].first == radioProps) {
      return (int)i;
    }
  }
  return -1;
}

void
ArmDriverIf::loadPhyTpcAdjTables(
    const std::string& radioMac,
    const ArmDriverIf::RadioProperties& radioProps) {
  int tblIdx = getTpcAdjTblVecIndex(radioProps);
  if (tblIdx == -1) {
    std::vector<thrift::PhyTpcAdjTblCfg> tpcAdjTables;
    tblIdx = tpcAdjTables_.size();
    tpcAdjTables_.push_back(std::make_pair(radioProps, tpcAdjTables));
  }
  auto& tpcAdjTables = tpcAdjTables_[tblIdx].second;
  tpcAdjTables.clear();

  // Load txPower adjustment tables for each channel
  for (int channel = 1; channel <= MAX_SUPPORTED_CHANNEL_INDEX; channel++) {
    // Construct file path
    std::string adjTblFile = getFwTxPowerAdjTblPath(
          FLAGS_txpower_adj_tbl_dir,
          radioProps.ibfProcedureType,
          radioProps.codebookVariant,
          channel);

    // Read file
    std::string contents;
    if (!folly::readFile(adjTblFile.c_str(), contents)) {
      if (errno == ENOENT) {
        // File not found, meaning we're done
        LOG(ERROR) << "Firmware txpower adj tbl file not found: "
                   << adjTblFile;
      } else {
        // Any other error means the file exists but we failed to read it
        LOG(ERROR) << "Could not read firmware txpower adj tbl file: "
                   << adjTblFile;
      }
      continue;
    }

    VLOG(2) << "Loaded txPower Adj Tbl file: " << adjTblFile
            << " for Mac=" << radioMac;

    // Parse the txPower adj table
    auto adjTable =
        JsonUtils::deserializeFromJson<thrift::PhyTpcAdjTblCfg>(
            contents);
    if (!adjTable) {
      LOG(ERROR) << "Could not parse txPowerAdjTbl struct in " << adjTblFile;
      continue;
    }
    tpcAdjTables.push_back(std::move(adjTable.value()));
  }

  VLOG(2) << "Finished loading " << tpcAdjTables.size()
          << " txPower Adj Table(s) for"
          << " ibfProcedureType=" << radioProps.ibfProcedureType
          << ", codebookVariant=" << radioProps.codebookVariant
          << ", mac=" << radioMac;
}

int
ArmDriverIf::getTpcAdjTblVecIndex(const RadioProperties& radioProps) const {
  for (size_t i = 0; i < tpcAdjTables_.size(); i++) {
    if (tpcAdjTables_[i].first == radioProps) {
      return (int)i;
    }
  }
  return -1;
}

void
ArmDriverIf::sendBfSlotExclusionReq(
    const std::string& radioMac, const thrift::BfSlotExclusionReq& msg) {
  thrift::PassThruMsg passThruMsg;
  passThruMsg.msgType = thrift::PtMsgTypes::SB_BF_SLOT_EXCLUSION_REQ;
  passThruMsg.dest = thrift::PtMsgDest::SB;
  passThruMsg.bfSlotExclusionReq = msg;
  prepareAndSendSBPassThruMessage(passThruMsg, radioMac);
}

} // namespace terragraph
} // namespace facebook
