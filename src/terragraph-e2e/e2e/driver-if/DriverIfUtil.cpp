/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "DriverIfUtil.h"

#include <fbzmq/zmq/Zmq.h>
#include <folly/Format.h>
#include <folly/MacAddress.h>
#include <folly/MapUtil.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "PassThru.h"
#include "nl-driver-if/fb_tg_drvr_app_if.h"

#include <cmath>

extern "C" {
#include <nl-driver-if/fb_tgd_nlsdn_common.h>
TGENUM_DEF_NAMES(tgd_nlsdn_commands, TGENUM_CMD)
}

using namespace fbzmq;
using namespace facebook::terragraph;
using apache::thrift::detail::TEnumMapFactory;

namespace {

// 30 dBHz
const int kSnrThreshold = 30;
// Quality = 5, 6, 7: code and carrier locked and time synchronized
const int kQualityThreshold = 5;

// Build a thrift::Message that wraps a thrift::DriverMessage.
template <class T>
thrift::Message
buildMessage(
    thrift::MessageType mType,
    const T& obj,
    const DriverNlMessage& drNlMsg,
    const apache::thrift::CompactSerializer& serializer) {
  thrift::DriverMessage driverMsg;
  driverMsg.value = fbzmq::util::writeThriftObjStr(obj, serializer);
  driverMsg.radioMac = drNlMsg.radioMac.toString();
  thrift::Message msg;
  msg.mType = mType;
  msg.value = fbzmq::util::writeThriftObjStr(driverMsg, serializer);
  return msg;
}

// this works for init & linkup commands
// Note: may not be applicable for other commands
bool
validateDriverResp(const DriverNlMessage& drNlMsg) {
  if (drNlMsg.u8Attrs.size() != 1) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " has " << drNlMsg.u8Attrs.size()
               << " attributes, expected 1";
    return false;
  }
  auto val = drNlMsg.u8Attrs.front();
  if (val.first != TGD_NLSDN_ATTR_SUCCESS) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " is " << static_cast<int>(val.first) << " ("
               << static_cast<int>(val.second)
               << "), expected TGD_NLSDN_ATTR_SUCCESS";
    return false;
  }
  return true;
}

thrift::DriverNodeInitNotif
getNodeInitNotif(const DriverNlMessage& drNlMsg) {
  thrift::DriverNodeInitNotif res;
  res.success = false;
  if (drNlMsg.u8Attrs.size() != 1) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " has : " << drNlMsg.u8Attrs.size() << " 8 attributes";
    return res;
  }
  auto val8 = drNlMsg.u8Attrs.front();
  if (val8.first != TGD_NLSDN_ATTR_SUCCESS || val8.second != TG_IOCTL_SUCCESS) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " is : " << static_cast<int>(val8.first) << " , "
               << static_cast<int>(val8.second);
    return res;
  }
  if (drNlMsg.u64Attrs.size() != 1) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " has : " << drNlMsg.u64Attrs.size() << " 64 attributes";
    return res;
  }
  auto val64 = drNlMsg.u64Attrs.front();
  if (val64.first != TGD_NLSDN_ATTR_MACADDR) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " is : " << static_cast<int>(val64.first) << " , "
               << static_cast<int>(val64.second);
    return res;
  }
  const std::vector<uint8_t>& vendorStrArray = drNlMsg.u8vlaAttrs.second;

  res.success = true;
  res.macAddr = folly::MacAddress::fromHBO(val64.second).toString();
  res.vendor.assign(vendorStrArray.begin(), vendorStrArray.end());
  size_t nullpos = res.vendor.find('\0');
  if (nullpos != std::string::npos) {
    // The vendor string might be padded with zeros. Remove them if so.
    res.vendor.resize(nullpos);
  }
  return res;
}

// works for DR_LINK_STATUS notification message
bool
validateDriverLinkStatus(const DriverNlMessage& drNlMsg) {
  if (drNlMsg.u8Attrs.size() != 3) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " has " << drNlMsg.u8Attrs.size()
               << " u8 attributes instead of 3!";
    return false;
  }
  bool hasLinkStatus = false;
  bool hasSelfNodeType = false;
  bool hasPeerNodeType = false;
  for (const auto& attr : drNlMsg.u8Attrs) {
    switch (attr.first) {
      case TGD_NLSDN_ATTR_LINK_STATUS:
        hasLinkStatus = true;
        break;
      case TGD_NLSDN_ATTR_SELF_NODE_TYPE:
        hasSelfNodeType = true;
        break;
      case TGD_NLSDN_ATTR_PEER_NODE_TYPE:
        hasPeerNodeType = true;
        break;
      default:
        break;
    }
  }
  if (!hasLinkStatus || !hasSelfNodeType || !hasPeerNodeType) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " is missing required u8 attributes!";
    return false;
  }
  auto val32 = drNlMsg.u32Attrs.front();
  if (val32.first != TGD_NLSDN_ATTR_LINK_DOWN_CAUSE) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " is : " << static_cast<int>(val32.first) << " , "
               << static_cast<int>(val32.second);
    return false;
  }
  if (drNlMsg.u64Attrs.size() != 1) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " has : " << drNlMsg.u64Attrs.size() << " 64 attributes";
    return false;
  }
  auto val64 = drNlMsg.u64Attrs.front();
  if (val64.first != TGD_NLSDN_ATTR_MACADDR) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " is : " << static_cast<int>(val64.first) << " , "
               << static_cast<int>(val64.second);
    return false;
  }
  return true;
}

// Process driver response on an South Bound PASSTHRU message.
thrift::Message
processRespForSBPassThru(
    const DriverNlMessage& drNlMsg,
    const apache::thrift::CompactSerializer& serializer) {
  // Check there is subtype and success code.
  if (drNlMsg.u8Attrs.size() != 2) {
    LOG(ERROR) << "Response for SB Pass-thru has size "
               << drNlMsg.u8Attrs.size()
               << " for vec u8Attrs instead of 2! Proceed but be warned...";
  }

  // Get success code: 1 iff successful.
  // Get subtype.
  int err = -1;
  uint8_t type = static_cast<uint8_t>(thrift::PtMsgTypes::SB_INVALID);
  for (const auto& attr : drNlMsg.u8Attrs) {
    switch (attr.first) {
      case TGD_NLSDN_ATTR_SUCCESS:
        err = attr.second == 1 ? 0 : 1;
        VLOG(4) << "processRespForSBPassThru from driver: error = " << err;
        break;

      case TGD_NLSDN_ATTR_PASSTHRU_TYPE:
        type = attr.second;
        VLOG(4) << "processRespForSBPassThru from driver: subtype = "
                << folly::get_default(
                   TEnumMapFactory<thrift::PtMsgTypes>::makeValuesToNamesMap(),
                   static_cast<thrift::PtMsgTypes>(type),
                   "UNKNOWN");
        break;

      default:
        break;
    }
  }

  // The driver response to SB pass-thru message should have a subtype(reqType);
  // These are consumed by DriverIf.
  thrift::DriverResp driverResp;
  driverResp.success = !err;
  switch (type) {
    case (uint8_t)(thrift::PtMsgTypes::SB_DISSOC):
    case (uint8_t)(thrift::PtMsgTypes::SB_ASSOC): {
      VLOG(4) << "processRespForSBPassThru: relay dissoc error = " << err;
      driverResp.reqType = thrift::MessageType::DR_SET_LINK_STATUS;
      return buildMessage(
          thrift::MessageType::DR_RESP,
            driverResp,
          drNlMsg,
          serializer);
    }
    case (uint8_t)(thrift::PtMsgTypes::SB_AIRTIMEALLOC):
    case (uint8_t)(thrift::PtMsgTypes::SB_POLARITY):
    case (uint8_t)(thrift::PtMsgTypes::SB_CHANNEL_CONFIG):
    case (uint8_t)(thrift::PtMsgTypes::SB_GOLAY_INDX): {
      VLOG(4) << "processRespForSBPassThru: relay NodeParams error = " << err;
      driverResp.reqType = thrift::MessageType::FW_SET_NODE_PARAMS;
      return buildMessage(
          thrift::MessageType::DR_RESP,
          driverResp,
          drNlMsg,
          serializer);
    }
    case (uint8_t)(thrift::PtMsgTypes::SB_STATS_CONFIGURE): {
      VLOG(4) << "processRespForSBPassThru: relay fw stats conf error = "
              << err;
      driverResp.reqType = thrift::MessageType::FW_STATS_CONFIGURE_REQ;
      return buildMessage(
          thrift::MessageType::DR_RESP,
          driverResp,
          drNlMsg,
          serializer);
    }
    case (uint8_t)(thrift::PtMsgTypes::SB_PHY_LA_CONFIG): {
      VLOG(4) << "processRespForSBPassThru: relay phy LA configuration error = "
              << err;
      driverResp.reqType = thrift::MessageType::PHY_LA_CONFIG_REQ;
      return buildMessage(
          thrift::MessageType::DR_RESP,
          driverResp,
          drNlMsg,
          serializer);
    }
    case (uint8_t)(thrift::PtMsgTypes::SB_PHY_AGC_CONFIG): {
      VLOG(4)
          << "processRespForSBPassThru: relay phy AGC configuration error = "
          << err;
      driverResp.reqType = thrift::MessageType::PHY_AGC_CONFIG_REQ;
      return buildMessage(
          thrift::MessageType::DR_RESP,
          driverResp,
          drNlMsg,
          serializer);
    }
    case (uint8_t)(thrift::PtMsgTypes::SB_PHY_TPC_CONFIG): {
      VLOG(4) << "processRespForSBPassThru: relay TPC configuration error = "
              << err;
      driverResp.reqType = thrift::MessageType::PHY_TPC_CONFIG_REQ;
      return buildMessage(
          thrift::MessageType::DR_RESP,
          driverResp,
          drNlMsg,
          serializer);
    }
    case (uint8_t)(thrift::PtMsgTypes::SB_PHY_TPC_ADJ_TBL_CFG): {
      VLOG(4) << "processRespForSBPassThru: relay TPC Adj Tbl config error = "
              << err;
      driverResp.reqType = thrift::MessageType::PHY_TPC_ADJ_TBL_CFG_REQ;
      return buildMessage(
          thrift::MessageType::DR_RESP,
          driverResp,
          drNlMsg,
          serializer);
    }
    case (uint8_t)(thrift::PtMsgTypes::SB_GPS_ENABLE): {
      VLOG(4) << "processRespForSBPassThru: relay gps enable error = " << err;
      driverResp.reqType = thrift::MessageType::GPS_ENABLE_REQ;
      return buildMessage(
          thrift::MessageType::DR_RESP,
          driverResp,
          drNlMsg,
          serializer);
    }
    case (uint8_t)(thrift::PtMsgTypes::SB_SET_LOG_CONFIG): {
      VLOG(4) << "processRespForSBPassThru: relay fw set log config error = "
              << err;
      driverResp.reqType = thrift::MessageType::FW_SET_LOG_CONFIG;
      return buildMessage(
          thrift::MessageType::DR_RESP,
          driverResp,
          drNlMsg,
          serializer);
    }
    default:
      return buildMessage(
          thrift::MessageType::NONE, thrift::DriverResp(), drNlMsg, serializer);
  }
}

// Process North Bound PASSTHRU message.
thrift::Message
processNorthBoundPassThru(
    const DriverNlMessage& drNlMsg,
    const apache::thrift::CompactSerializer& serializer) {
  const std::vector<uint8_t>& passThruMsg = drNlMsg.u8vlaAttrs.second;
  auto passThruMsgThrift = getPtThrift(
      passThruMsg.data(), passThruMsg.size(), drNlMsg.radioMac.toString());

  switch (passThruMsgThrift.msgType) {
    // fw stats
    case thrift::PtMsgTypes::NB_STATS: {
      thrift::DriverFwStatsReport driverFwStatsReport;
      driverFwStatsReport.stats = passThruMsgThrift.stats;
        return buildMessage(
            thrift::MessageType::FW_STATS,
            driverFwStatsReport,
            drNlMsg,
            serializer);
    }
    // fw ack for southbound passthru
    case thrift::PtMsgTypes::NB_ACK: {
      bool success = passThruMsgThrift.ack.success;
      thrift::FwAck fwAck;
      fwAck.reqType = thrift::MessageType::NONE;
      fwAck.success = success;

      // fill out fwAck.reqType
      switch (passThruMsgThrift.ack.msgType) {
        // dissoc, bandwidth map, etc.
        case thrift::PtMsgTypes::SB_DISSOC:
        case thrift::PtMsgTypes::SB_ASSOC:
          // TODO: make sure fw also returns failure indication on dissoc.
          fwAck.reqType = thrift::MessageType::DR_SET_LINK_STATUS;
          break;
        case thrift::PtMsgTypes::SB_GET_FW_PARAMS:
        case thrift::PtMsgTypes::SB_SET_FW_PARAMS:
          fwAck.reqType = thrift::MessageType::FW_CONFIG_REQ;
          break;
        case thrift::PtMsgTypes::SB_AIRTIMEALLOC:
        case thrift::PtMsgTypes::SB_GPS_TIME:
        case thrift::PtMsgTypes::SB_POLARITY:
        case thrift::PtMsgTypes::SB_GOLAY_INDX:
        case thrift::PtMsgTypes::SB_BF_SLOT_EXCLUSION_REQ:
        case thrift::PtMsgTypes::SB_CHANNEL_CONFIG:
          // TODO: make sure fw also returns failure indication
          fwAck.reqType = thrift::MessageType::FW_SET_NODE_PARAMS;
          break;
        case thrift::PtMsgTypes::SB_STATS_CONFIGURE:
          fwAck.reqType = thrift::MessageType::FW_STATS_CONFIGURE_REQ;
          break;
        case thrift::PtMsgTypes::SB_PHY_LA_CONFIG:
          fwAck.reqType = thrift::MessageType::PHY_LA_CONFIG_REQ;
          break;
        case thrift::PtMsgTypes::SB_PHY_AGC_CONFIG:
          fwAck.reqType = thrift::MessageType::PHY_AGC_CONFIG_REQ;
          break;
        case thrift::PtMsgTypes::SB_PHY_TPC_CONFIG:
          fwAck.reqType = thrift::MessageType::PHY_TPC_CONFIG_REQ;
          break;
        case thrift::PtMsgTypes::SB_PHY_TPC_ADJ_TBL_CFG:
          fwAck.reqType = thrift::MessageType::PHY_TPC_ADJ_TBL_CFG_REQ;
          break;
        case thrift::PtMsgTypes::SB_GPS_ENABLE:
          fwAck.reqType = thrift::MessageType::GPS_ENABLE_REQ;
          break;
        case thrift::PtMsgTypes::SB_PHY_ANT_WGT_TBL_CONFIG:
          fwAck.reqType = thrift::MessageType::FW_SET_CODEBOOK;
          break;
        case thrift::PtMsgTypes::SB_PHY_GOLAY_SEQUENCE_CONFIG:
          fwAck.reqType = thrift::MessageType::PHY_GOLAY_SEQUENCE_CONFIG_REQ;
          break;
        case thrift::PtMsgTypes::SB_DEBUG:
          fwAck.reqType = thrift::MessageType::FW_DEBUG_REQ;
          break;
        case thrift::PtMsgTypes::SB_BF_RESP_SCAN_CONFIG:
          fwAck.reqType = thrift::MessageType::FW_BF_RESP_SCAN;
          break;
        case thrift::PtMsgTypes::SB_SCAN_REQ:
          fwAck.reqType = thrift::MessageType::SCAN_REQ;
          break;
        case thrift::PtMsgTypes::SB_SET_LOG_CONFIG:
          fwAck.reqType = thrift::MessageType::FW_SET_LOG_CONFIG;
          break;
        case thrift::PtMsgTypes::SB_SET_ADJ:
          fwAck.reqType = thrift::MessageType::FW_ADJ_RESP;
          break;
        default:
          LOG(ERROR) << "Unexpected nouthbound ack message sub-type: ("
                     << folly::get_default(
                        TEnumMapFactory<thrift::PtMsgTypes>::
                            makeValuesToNamesMap(),
                        passThruMsgThrift.ack.msgType,
                        "UNKNOWN")
                     << ") from driver";
          return buildMessage(
              thrift::MessageType::NONE, fwAck, drNlMsg, serializer);
      }
      return buildMessage(
          thrift::MessageType::FW_ACK, fwAck, drNlMsg, serializer);
    }
    case thrift::PtMsgTypes::NB_HEALTHY:
      return buildMessage(
          thrift::MessageType::FW_HEALTHY,
          passThruMsgThrift.fwHealthReport,
          drNlMsg,
          serializer);
    case thrift::PtMsgTypes::NB_SCAN_RESP:
      return buildMessage(
          thrift::MessageType::SCAN_RESP,
          passThruMsgThrift.scanResp,
          drNlMsg,
          serializer);
    case thrift::PtMsgTypes::NB_CODEBOOK_FETCH:
      return buildMessage(
          thrift::MessageType::FW_GET_CODEBOOK,
          passThruMsgThrift.codebookFetchReq,
          drNlMsg,
          serializer);
    case thrift::PtMsgTypes::NB_GET_FW_PARAMS_RESP:
      return buildMessage(
          thrift::MessageType::FW_CONFIG_RESP,
          passThruMsgThrift.getFwParamsResp,
          drNlMsg,
          serializer);
    case thrift::PtMsgTypes::NB_SET_FW_PARAMS_RESP:
      return buildMessage(
          thrift::MessageType::FW_CONFIG_RESP,
          passThruMsgThrift.setFwParamsResp,
          drNlMsg,
          serializer);
    case thrift::PtMsgTypes::NB_ROUTING_INFO:
      return buildMessage(
          thrift::MessageType::FW_ROUTING_INFO,
          passThruMsgThrift.fwRoutingInfo,
          drNlMsg,
          serializer);
    case thrift::PtMsgTypes::NB_OPENR_ADJ_REQ:
      return buildMessage(
          thrift::MessageType::FW_ADJ_REQ,
          thrift::Empty(),
          drNlMsg,
          serializer);
    case thrift::PtMsgTypes::NB_PPS_HTSF_INFO:
      return buildMessage(
          thrift::MessageType::FW_PPS_HTSF_INFO,
          passThruMsgThrift.htsfInfo,
          drNlMsg,
          serializer);
    case thrift::PtMsgTypes::NB_PHY_TPC_ADJ_TBL_FETCH:
      return buildMessage(
          thrift::MessageType::FW_GET_PHY_TPC_ADJ_TBL,
          passThruMsgThrift.tpcAdjTblFetchReq,
          drNlMsg,
          serializer);
    default:
      LOG(ERROR) << "Unexpected northbound message type: ("
                 << folly::get_default(
                 TEnumMapFactory<thrift::PtMsgTypes>::makeValuesToNamesMap(),
                 passThruMsgThrift.msgType,
                 "UNKNOWN")
                 << ") from driver";
      return buildMessage(
          thrift::MessageType::NONE, thrift::Empty(), drNlMsg, serializer);
  }
}

// Prepare the GPS status message
thrift::DrvrStatsList
processGpsStatus(struct t_gps_stat* pGpsStat, size_t len) {
  thrift::DrvrStatsList drvrStats;
  std::vector<thrift::DrvrStatsSample>& samps = drvrStats.samples;

  size_t sampDataLen = sizeof(struct t_gps_stat);
  if (len < sampDataLen) {
    LOG(ERROR) << "NB_DRVR_STAT_GPS (Len) " << len << " < (sampDataLen) "
               << sampDataLen;

    samps.push_back(createDrvrStatsSample("tgd.gpsStat.dataLenError", len));
    return drvrStats;
  }

  size_t svDatTotalLen = (pGpsStat->sat_in_view.num_space_veh *
                          sizeof(struct t_gps_space_veh_info)) +
                         sampDataLen;
  if (len < svDatTotalLen) {
    LOG(ERROR) << "Expected svDatTotalLen: " << svDatTotalLen
               << "RxLen: " << len;
    samps.push_back(createDrvrStatsSample("tgd.gpsStat.svDataLenError", len));
    return drvrStats;
  }

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.ecefX", pGpsStat->gps_pos_fix.ecef_x));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.ecefY", pGpsStat->gps_pos_fix.ecef_y));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.ecefZ", pGpsStat->gps_pos_fix.ecef_z));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.numSamples", pGpsStat->gps_pos_fix.num_pos_observed));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.variance3d", pGpsStat->gps_pos_fix.variance_3d));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.accuracy", sqrt(pGpsStat->gps_pos_fix.variance_3d) / k1e3));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.latitude", pGpsStat->gps_pos_fix.latitude));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.longitude", pGpsStat->gps_pos_fix.longitude));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.heightMsl", pGpsStat->gps_pos_fix.hght_msl));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.heightEllipsoid", pGpsStat->gps_pos_fix.hght_elipsd));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.fixNumSat", pGpsStat->gps_pos_fix.num_sat_used));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.fixType", pGpsStat->gps_pos_fix.fix_type));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.gnssTmOfset", pGpsStat->tim_pulse_freq.gnss_tim_ofset_ns));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.gnssTmUncert", pGpsStat->tim_pulse_freq.gnss_tim_uncert_ns));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.intOscOfset", pGpsStat->tim_pulse_freq.int_osc_ofset_ppb));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.intOscUncert", pGpsStat->tim_pulse_freq.int_osc_uncert_ppb));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.discipSrc", pGpsStat->tim_pulse_freq.discp_src));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.timTosFlag", pGpsStat->tim_pulse_freq.tim_tos_flag));

  int numSatGoodSnr = 0;
  int numSatGoodQlty = 0;
  struct t_gps_space_veh_info* svInfoDptr =
      &pGpsStat->sat_in_view.space_veh_info[0];
  for (unsigned int satNum = 0; satNum < pGpsStat->sat_in_view.num_space_veh;
       satNum++) {

    // GNSS Type = GPS, UBX svId = 1-32
    // format key with svId, to keep it unique for a satellite
    uint8_t svId = (uint8_t)svInfoDptr->sat_id;

    samps.push_back(createDrvrStatsSample(
      folly::sformat("tgd.gpsStat.{}.snr", svId), svInfoDptr->snr));

    samps.push_back(createDrvrStatsSample(
      folly::sformat("tgd.gpsStat.{}.flag", svId), svInfoDptr->flags));

    samps.push_back(createDrvrStatsSample(
      folly::sformat("tgd.gpsStat.{}.quality", svId), svInfoDptr->qlty));

    samps.push_back(createDrvrStatsSample(
      folly::sformat("tgd.gpsStat.{}.elevation", svId), svInfoDptr->elev));

    if (svInfoDptr->snr >= kSnrThreshold) {
      numSatGoodSnr++;
    }
    if (svInfoDptr->qlty >= kQualityThreshold) {
      numSatGoodQlty++;
    }

    svInfoDptr++;
  }
  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.numGoodSnr", numSatGoodSnr));

  samps.push_back(createDrvrStatsSample(
      "tgd.gpsStat.numGoodQlty", numSatGoodQlty));

  return drvrStats;
}

// Process North Bound Driver stats message.
thrift::Message
processNorthBoundDrvrStat(
    const DriverNlMessage& drNlMsg,
    const apache::thrift::CompactSerializer& serializer) {
  auto drvrRspMsg = drNlMsg.u8vlaAttrs.second;
  thrift::DrvrStatsList drvrStats;

  size_t len = drvrRspMsg.size();
  size_t statHdrLen = offsetof(struct TgdDrvrStat, data);

  if (len <= statHdrLen) {
    LOG(ERROR) << "parseError (Len) " << len << " < (statHdrLen) "
               << statHdrLen;
    return buildMessage(
        thrift::MessageType::NONE, drvrStats, drNlMsg, serializer);
  } else {
    len -= statHdrLen;
  }

  uint8_t* drvrStatPtr = reinterpret_cast<uint8_t*>(drvrRspMsg.data());
  auto msgType = ((struct TgdDrvrStat*)drvrStatPtr)->msgType;
  unsigned int gpsTimeInSec =
      ((struct TgdDrvrStat*)drvrStatPtr)->gps_time_in_sec;
  drvrStatPtr += statHdrLen;

  switch (msgType) {
    case NB_DRVR_STAT_GPS: {
      drvrStats = processGpsStatus((struct t_gps_stat*)drvrStatPtr, len);
      drvrStats.gpsTimeUs = int64_t(gpsTimeInSec) * 1000000;
      return buildMessage(
          thrift::MessageType::DR_STAT_PUSH, drvrStats, drNlMsg, serializer);
    }
    default: {
      LOG(ERROR) << "DRVR_STAT_NB: Unknown message type: " << msgType;
      return buildMessage(
          thrift::MessageType::NONE, drvrStats, drNlMsg, serializer);
    }
  }
}

// Process device alloc response from the driver
thrift::Message
processDevAllocRsp(
    const DriverNlMessage& drNlMsg,
    const apache::thrift::CompactSerializer& serializer) {
  bool success = false;
  std::string macAddr;
  std::string ifName;
  uint32_t ifIndex = (uint32_t)-1;

  // Validate the incoming message
  do {
    if (drNlMsg.u64Attrs.size() != 1) {
      LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
                 << " has : " << drNlMsg.u64Attrs.size() << " 64 attributes";
      break;
    }
    auto val64 = drNlMsg.u64Attrs.front();
    if (val64.first != TGD_NLSDN_ATTR_MACADDR) {
      LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
                 << " is : " << static_cast<int>(val64.first) << " , "
                 << static_cast<int>(val64.second);
      break;
    }
    macAddr =
        folly::MacAddress::fromHBO(drNlMsg.u64Attrs.front().second).toString();

    if (drNlMsg.u8Attrs.size() != 1) {
      LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
                 << " has : " << drNlMsg.u8Attrs.size() << " 8 attributes";
      break;
    }
    auto val8 = drNlMsg.u8Attrs.front();
    if (val8.first != TGD_NLSDN_ATTR_SUCCESS) {
      LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
                 << " is : " << static_cast<int>(val8.first) << " , "
                 << static_cast<int>(val8.second);
      break;
    }
    // Do not bother decoding the rest if request did not succeed
    if (!val8.second)
      break;

    if (drNlMsg.u32Attrs.size() != 1) {
      LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
                 << " has : " << drNlMsg.u64Attrs.size() << " 32 attributes";
      break;
    }
    auto val32 = drNlMsg.u32Attrs.front();
    if (val32.first != TGD_NLSDN_ATTR_IFINDEX) {
      LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
                 << " is : " << static_cast<int>(val32.first) << " , "
                 << static_cast<int>(val32.second);
      break;
    }
    ifIndex = val32.second;

    auto& vla = drNlMsg.u8vlaAttrs;
    if (vla.first != TGD_NLSDN_ATTR_VARDATA) {
      LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
                 << " is : " << static_cast<int>(vla.first) << " , "
                 << vla.second.size() << "bytes long";
      break;
    }
    ifName = reinterpret_cast<const char*>(vla.second.data());
    // We have it all
    success = true;
  } while (0);


  thrift::DriverDevAllocRes driverDevAllocRes;
  driverDevAllocRes.success = success;
  driverDevAllocRes.macAddr = macAddr;
  driverDevAllocRes.ifindex = ifIndex;
  driverDevAllocRes.ifname = ifName;
  return buildMessage(
      thrift::MessageType::DR_DEV_ALLOC_RES,
      driverDevAllocRes,
      drNlMsg,
      serializer);
}

// Process wsec status event from the driver
thrift::Message
processWsecStatus(
    const DriverNlMessage& drNlMsg,
    const apache::thrift::CompactSerializer& serializer) {
  // Validate the incoming message
  if (drNlMsg.u8Attrs.size() != 1) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " has : " << drNlMsg.u64Attrs.size() << " 8 attributes";
    return buildMessage(
        thrift::MessageType::DR_WSEC_STATUS,
        thrift::DriverWsecStatus(),
        drNlMsg,
        serializer);
  }
  if (drNlMsg.u8vlaAttrs.first != TGD_NLSDN_ATTR_VARDATA) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " is : " << static_cast<int>(drNlMsg.u8vlaAttrs.first)
               << " , " << drNlMsg.u8vlaAttrs.second.size() << "bytes long";
    return buildMessage(
        thrift::MessageType::DR_WSEC_STATUS,
        thrift::DriverWsecStatus(),
        drNlMsg,
        serializer);
  }

  uint8_t status = drNlMsg.u8Attrs.front().second;
  std::string ifName =
      reinterpret_cast<const char*>(drNlMsg.u8vlaAttrs.second.data());

  thrift::DriverWsecStatus driverWsecStatus;
  driverWsecStatus.ifname = ifName;
  driverWsecStatus.status = (bool) status;
  return buildMessage(
      thrift::MessageType::DR_WSEC_STATUS,
      driverWsecStatus,
      drNlMsg,
      serializer);
}

// Process wsec linkup status event from the driver
thrift::Message
processWsecLinkupStatus(
    const DriverNlMessage& drNlMsg,
    const apache::thrift::CompactSerializer& serializer) {
  // Validate the incoming message
  if (drNlMsg.u8vlaAttrs.first != TGD_NLSDN_ATTR_VARDATA) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " is : " << static_cast<int>(drNlMsg.u8vlaAttrs.first)
               << " , " << drNlMsg.u8vlaAttrs.second.size() << "bytes long";
    return buildMessage(
        thrift::MessageType::DR_WSEC_LINKUP_STATUS,
        thrift::DriverWsecLinkupStatus(),
        drNlMsg,
        serializer);
  }

  std::string ifName =
      reinterpret_cast<const char*>(drNlMsg.u8vlaAttrs.second.data());

  thrift::DriverWsecLinkupStatus driverWsecLinkupStatus;
  driverWsecLinkupStatus.ifname = ifName;
  return buildMessage(
      thrift::MessageType::DR_WSEC_LINKUP_STATUS,
      driverWsecLinkupStatus,
      drNlMsg,
      serializer);
}

// Process backhaul baseband device up/down notification
thrift::Message
processDevUpDownStatus(
    const DriverNlMessage& drNlMsg,
    const apache::thrift::CompactSerializer& serializer) {
  // Validate the incoming message
  if (drNlMsg.u8Attrs.size() != 1) {
    LOG(ERROR) << "Response of type " << static_cast<int>(drNlMsg.mType)
               << " has : " << drNlMsg.u64Attrs.size() << " 8 attributes";
    return buildMessage(
        thrift::MessageType::DR_DEV_UPDOWN_STATUS,
        thrift::DriverDevUpDownStatus(), // assume down
        drNlMsg,
        serializer);
  }

  uint8_t status = drNlMsg.u8Attrs.front().second;
  thrift::DriverDevUpDownStatus driverDevUpDownStatus;
  driverDevUpDownStatus.status = (bool) status;
  return buildMessage(
      thrift::MessageType::DR_DEV_UPDOWN_STATUS,
      driverDevUpDownStatus,
      drNlMsg,
      serializer);
}

// Process North Bound Driver response message.
thrift::Message
processDrvrRsp(
    const DriverNlMessage& drNlMsg,
    const apache::thrift::CompactSerializer& serializer) {
  bool isValid = validateDriverResp(drNlMsg);
  if (!isValid || drNlMsg.u8Attrs.front().second != 0) {
    LOG(ERROR) << "Driver command was unsuccessful (see driver logs)";
    return buildMessage(
        thrift::MessageType::NONE, thrift::Empty(), drNlMsg, serializer);
  }

  auto drvrRspMsg = drNlMsg.u8vlaAttrs.second;
  size_t len = drvrRspMsg.size();
  if (len < sizeof(struct t_gps_self_pos)) {
    LOG(ERROR) << "Bad length of driver response, len: " << len
               << ", expected: " << sizeof(struct t_gps_self_pos);
    return buildMessage(
        thrift::MessageType::NONE, thrift::Empty(), drNlMsg, serializer);
  }

  struct t_gps_self_pos* pos =
      reinterpret_cast<struct t_gps_self_pos*>(drvrRspMsg.data());
  if (pos->cmd == DRVR_CFG_CMD_GPS && pos->sub_cmd == GPS_SET_CMD_SING_SAT) {
    thrift::DriverAck driverAck;
    driverAck.reqType = thrift::MessageType::FW_SET_NODE_PARAMS;
    driverAck.success = isValid;
    return buildMessage(
        thrift::MessageType::DR_ACK,
        driverAck,
        drNlMsg,
        serializer);
  } else if (pos->cmd == DRVR_CFG_CMD_GPS && pos->sub_cmd == GPS_GET_CMD_POS) {
    thrift::Location location;
    if (pos->accuracy >= 0) {
      convertEcefToGeodetic(
          // cm to m conversion for ecef values
          pos->ecef_x / k1e2,
          pos->ecef_y / k1e2,
          pos->ecef_z / k1e2,
          location.latitude,
          location.longitude,
          location.altitude);
      // mm^2 --> m
      location.accuracy = sqrt(pos->accuracy) / k1e3;
    } else {
      // else location takes thrift default value
      VLOG(3) << "received invalid location from driver";
    }
    return buildMessage(
        thrift::MessageType::GPS_GET_POS_RESP, location, drNlMsg, serializer);
  } else {
    LOG(ERROR) << "Unknown driver response to cmd: " << pos->cmd
               << ", sub_cmd: " << pos->sub_cmd;
    return buildMessage(
        thrift::MessageType::NONE, thrift::Empty(), drNlMsg, serializer);
  }
}

} // anonymous namespace

namespace facebook {
namespace terragraph {

thrift::Message
driverNl2IfMessage(const DriverNlMessage& drNlMsg) {
  VLOG(5)
      << "Received nl message type: "
      << tgd_nlsdn_commands_values_to_names[static_cast<int>(drNlMsg.mType)];

  apache::thrift::CompactSerializer serializer;
  switch (drNlMsg.mType) {
    // --- driver responses for south bound messages to fw ---
    case DriverNlMessageType::NODE_INIT: {
      thrift::DriverResp driverResp;
      driverResp.reqType = thrift::MessageType::NODE_INIT;
      driverResp.success = validateDriverResp(drNlMsg);
      return buildMessage(
          thrift::MessageType::DR_RESP,
          driverResp,
          drNlMsg,
          serializer);
    }

    case DriverNlMessageType::PASSTHRU_SB:
      return processRespForSBPassThru(drNlMsg, serializer);

    // --- north bound messages from firmware ---
    case DriverNlMessageType::NODE_INIT_NOTIFY: {
      thrift::DriverNodeInitNotif notif = getNodeInitNotif(drNlMsg);
      return buildMessage(
          thrift::MessageType::NODE_INIT_NOTIFY,
          std::move(notif),
          drNlMsg,
          serializer);
    }

    case DriverNlMessageType::DR_LINK_STATUS: {
      // Parse u8 attrs
      thrift::DriverLinkStatusType drLinkStatus =
          thrift::DriverLinkStatusType::NONE;
      uint8_t selfNodeType = 0;
      uint8_t peerNodeType = 0;
      for (const auto& attr : drNlMsg.u8Attrs) {
        switch (attr.first) {
          case TGD_NLSDN_ATTR_LINK_STATUS:
            // Parse the link status type
            switch (attr.second) {
              case TG_LINKINIT:
                drLinkStatus = thrift::DriverLinkStatusType::LINK_INIT;
                break;
              case TG_LINKDOWN:
                drLinkStatus = thrift::DriverLinkStatusType::LINK_DOWN;
                break;
              case TG_LINKUP:
                drLinkStatus = thrift::DriverLinkStatusType::LINK_UP;
                break;
              case TG_LINKPAUSE:
                drLinkStatus = thrift::DriverLinkStatusType::LINK_PAUSE;
                break;
              default:
                drLinkStatus = thrift::DriverLinkStatusType::NONE;
            }
            break;
          case TGD_NLSDN_ATTR_SELF_NODE_TYPE:
            selfNodeType = attr.second;
            break;
          case TGD_NLSDN_ATTR_PEER_NODE_TYPE:
            peerNodeType = attr.second;
            break;
          default:
            break;
        }
      }

      // Parse the link down cause
      thrift::LinkDownCause linkDownCause;
      switch (drNlMsg.u32Attrs.front().second) {
        case TG_UNEXPECTED_COMMAND:
          linkDownCause = thrift::LinkDownCause::UNEXPECTED_COMMAND;
          break;
        case TG_SYSTEM_GPS_SYNC_IN_PROG:
          linkDownCause = thrift::LinkDownCause::SYSTEM_GPS_SYNC_IN_PROG;
          break;
        case TG_SYSTEM_NOT_CONFIGURED:
          linkDownCause = thrift::LinkDownCause::SYSTEM_NOT_CONFIGURED;
          break;
        case TG_BF_TRAINING_FAILED:
          linkDownCause = thrift::LinkDownCause::BF_TRAINING_FAILED;
          break;
        case TG_BF_TRAINING_SHUTDOWN:
          linkDownCause = thrift::LinkDownCause::BF_TRAINING_SHUTDOWN;
          break;
        case TG_HB_KA_LOSS_DETECTED:
          linkDownCause = thrift::LinkDownCause::HB_KA_LOSS_DETECTED;
          break;
        case TG_LINK_SHUTDOWN_RECVD:
          linkDownCause = thrift::LinkDownCause::LINK_SHUTDOWN_RECVD;
          break;
        case TG_CHANNEL_NOT_CONFIGURED:
          linkDownCause = thrift::LinkDownCause::CHANNEL_NOT_CONFIGURED;
          break;
        case TG_CHANNEL_NOT_SET_WAITING_LINK_DOWN:
          linkDownCause =
              thrift::LinkDownCause::CHANNEL_NOT_SET_WAITING_LINK_DOWN;
          break;
        case TG_CHANNEL_IS_BEING_SET:
          linkDownCause = thrift::LinkDownCause::CHANNEL_IS_BEING_SET;
          break;
        case TG_UNSUPPORTED_CAPABILITY:
          linkDownCause = thrift::LinkDownCause::UNSUPPORTED_CAPABILITY;
          break;
        case TG_WSEC_MISMATCH:
          linkDownCause = thrift::LinkDownCause::WSEC_MISMATCH;
          break;
        case TG_DISASSOC_RCVD_FROM_PEER:
          linkDownCause = thrift::LinkDownCause::DISASSOC_RCVD_FROM_PEER;
          break;
        default:
          linkDownCause = thrift::LinkDownCause::NOT_APPLICABLE;
          break;
      }

      // Parse the link's MAC address
      auto macAddr = folly::MacAddress::fromHBO(drNlMsg.u64Attrs.front().second)
                         .toString();

      // Parse the link's interface name
      std::string ifName;
      if (!drNlMsg.u8vlaAttrs.second.empty() &&
          drNlMsg.u8vlaAttrs.first == TGD_NLSDN_ATTR_VARDATA) {
        ifName =
            reinterpret_cast<const char*>(drNlMsg.u8vlaAttrs.second.data());
      }

      thrift::DriverLinkStatus driverLinkStatus;
      driverLinkStatus.valid = validateDriverLinkStatus(drNlMsg);
      driverLinkStatus.macAddr = macAddr;
      driverLinkStatus.drLinkStatusType = drLinkStatus;
      driverLinkStatus.linkDownCause = linkDownCause;
      driverLinkStatus.ifname = ifName;
      driverLinkStatus.selfNodeType =
          static_cast<thrift::NodeType>(selfNodeType);
      driverLinkStatus.peerNodeType =
          static_cast<thrift::NodeType>(peerNodeType);

      return buildMessage(
          thrift::MessageType::DR_LINK_STATUS,
          driverLinkStatus,
          drNlMsg,
          serializer);
    }

    case DriverNlMessageType::PASSTHRU_NB:
      return processNorthBoundPassThru(drNlMsg, serializer);

    case DriverNlMessageType::DRVR_STAT_NB:
      return processNorthBoundDrvrStat(drNlMsg, serializer);

    case DriverNlMessageType::DRVR_RSP:
      return processDrvrRsp(drNlMsg, serializer);

    case DriverNlMessageType::DEV_ALLOC_RSP:
      return processDevAllocRsp(drNlMsg, serializer);

    case DriverNlMessageType::WSEC_STATUS:
      return processWsecStatus(drNlMsg, serializer);

    case DriverNlMessageType::WSEC_LINKUP_STATUS:
      return processWsecLinkupStatus(drNlMsg, serializer);

    case DriverNlMessageType::DEV_UPDOWN_STATUS:
      return processDevUpDownStatus(drNlMsg, serializer);

    case DriverNlMessageType::NONE:
      // Ignore superfluous messages
      VLOG(4) << "ignoring nl message type: "
              << tgd_nlsdn_commands_values_to_names[static_cast<int>(
                     drNlMsg.mType)];
      return buildMessage(
          thrift::MessageType::NONE, thrift::Empty(), drNlMsg, serializer);

    // --- Unexpected netlink message ---
    default:
      VLOG(2) << "Unexpected nl message type: "
              << tgd_nlsdn_commands_values_to_names[static_cast<int>(
                     drNlMsg.mType)];
      return buildMessage(
          thrift::MessageType::NONE, thrift::Empty(), drNlMsg, serializer);
  }
}

void
convertEcefToGeodetic(
    double ecefX,
    double ecefY,
    double ecefZ,
    double& latitude,
    double& longitude,
    double& height) {
  // Zhu, J. (1994). "Conversion of Earth-centered Earth-fixed coordinates to
  // geodetic coordinates". IEEE Trans. Aerospace and Electronic Systems.
  // 30: 957-961. doi:10.1109/7.303772.

  // This is using the Ferrari solution, see also
  // https://en.wikipedia.org/wiki/Geographic_coordinate_conversion#From_ECEF_to_geodetic_coordinates
  // as of Nov 19 2018

  const double wgs84a = 6378137.0;
  const double wgs84b = 6356752.314245;
  const double eccSq = 1.0 - (wgs84b * wgs84b / (wgs84a * wgs84a));
  const double ecc2Sq = wgs84a * wgs84a / (wgs84b * wgs84b) - 1.0;
  const double rad2deg = 45.0 / atan(1.0);

  double ecefZSq = ecefZ * ecefZ;
  double rSq = ecefX * ecefX + ecefY * ecefY;
  double r = sqrt(rSq);
  double ESq = wgs84a * wgs84a - wgs84b * wgs84b;
  double F = 54.0 * wgs84b * wgs84b * ecefZSq;
  double G = rSq + (1.0 - eccSq) * ecefZSq - eccSq * ESq;
  double C = (eccSq * eccSq * F * rSq) / pow(G, 3.0);
  double S = cbrt(1.0 + C + sqrt(C * C + 2.0 * C));
  double P = F / (3.0 * pow((S + 1.0 / S + 1.0), 2.0) * G * G);
  double Q = sqrt(1.0 + 2.0 * eccSq * eccSq * P);
  double r0 =
      -(P * eccSq * r) / (1.0 + Q) +
      sqrt(
          0.5 * wgs84a * wgs84a * (1.0 + 1.0 / Q) -
          P * (1.0 - eccSq) * ecefZSq / (Q * (1.0 + Q)) - 0.5 * P * rSq);
  double U = sqrt(pow((r - eccSq * r0), 2.0) + ecefZSq);
  double V = sqrt(pow((r - eccSq * r0), 2.0) + (1.0 - eccSq) * ecefZSq);
  double Z0 = wgs84b * wgs84b * ecefZ / (wgs84a * V);

  latitude = atan((ecefZ + ecc2Sq * Z0) / r) * rad2deg;
  longitude = atan2(ecefY, ecefX) * rad2deg;
  // ellipsoid height
  height = U * (1.0 - wgs84b * wgs84b / (wgs84a * V));
}

} // namespace terragraph
} // namespace facebook
