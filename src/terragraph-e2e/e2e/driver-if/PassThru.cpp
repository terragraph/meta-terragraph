/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "PassThru.h"
#include "DriverIfUtil.h"
#include "FbTgFwParam.h"
#include <fb-fw-if/fb_tg_fw_pt_if.h>
#include <folly/Format.h>
#include <folly/MacAddress.h>
#include <folly/MapUtil.h>
#include <nl-driver-if/fb_tg_fw_driver_if.h>
#include <optional>
#include <stddef.h>
#include <string.h>

using apache::thrift::detail::TEnumMapFactory;

namespace {
constexpr folly::StringPiece kSamplePrefix = "tgf.";

// Try to parse a MAC address, returning std::nullopt on failure
std::optional<folly::MacAddress>
parseMacAddress(const std::string& mac) {
  try {
    return folly::MacAddress(mac);
  } catch (std::invalid_argument& ex) {
    LOG(ERROR) << "Invalid MAC address '" << mac << "': " << ex.what();
    return std::nullopt;
  }
}
}

namespace facebook {
namespace terragraph {

thrift::Stats getStats(const tgfStatsMsgHdr* buffStats, size_t len);

/* round x/y */
#define TGF_ROUND(x, y) (((x) + ((y) / 2)) / (y))

#define TGF_PACK_INT64(low, high) \
  (static_cast<int64_t>(low) + (static_cast<int64_t>(high) << 32));

#define THRIFT_RANGE_CHECK(NAME, MIN, MAX, MINVALID, MAXVALID) \
  if (NAME ## _ref().has_value() && ((MAXVALID && NAME ## _ref().value() > MAX)\
      || (MINVALID && NAME ## _ref().value() < MIN))) {        \
    LOG(ERROR) << #NAME << " out of range";                    \
    len = 0;                                                   \
    break;                                                     \
  }

size_t
getPtBuff(const thrift::PassThruMsg& thriftMsg, uint8_t* buff) {
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;
  buffMsg->driverType = TG_SB_PASSTHRU;
  buffMsg->dest = TGF_PT_DEST_SB;
  buffMsg->cookie = thriftMsg.cookie;
  size_t len = 0;

  switch (thriftMsg.msgType) {

    case thrift::PtMsgTypes::SB_ASSOC: {
      /* max option len is total size of buff which is assumed to be
       * SB_PT_BUFF_LEN minus space taken up assoc msg header
       */
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtAssocMsg);
      auto maxOptSize = SB_PT_BUFF_LEN - len;
      auto lenOpt = fbFwParams2Pack(
          thriftMsg.assoc.optionalParams, buffMsg->data.assoc.data, maxOptSize);
      if ((len + lenOpt) > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      auto macAddress = parseMacAddress(thriftMsg.assoc.addr);
      if (!macAddress) {
        len = 0;
        break;
      }
      buffMsg->msgType = TGF_PT_SB_ASSOC;
      memcpy(
          buffMsg->data.assoc.addr,
          macAddress->bytes(),
          sizeof(buffMsg->data.assoc.addr));
      buffMsg->data.assoc.len = lenOpt;
      // Return the total message length
      len += lenOpt;
      break;
    }

    case thrift::PtMsgTypes::SB_DISSOC: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtDissocMsg);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      auto macAddress = parseMacAddress(thriftMsg.dissoc.addr);
      if (!macAddress) {
        len = 0;
        break;
      }
      buffMsg->msgType = TGF_PT_SB_DISSOC;
      memcpy(
          buffMsg->data.dissoc.addr,
          macAddress->bytes(),
          sizeof(buffMsg->data.dissoc.addr));
      break;
    }

    case thrift::PtMsgTypes::SB_SET_FW_PARAMS: {
      /* max option len is total size of buff which is assumed to be
       * SB_PT_BUFF_LEN minus space taken up fwParams msg header
       */

      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtSetFwParamsReq);
      auto maxOptSize = SB_PT_BUFF_LEN - len;
      auto lenOpt = fbFwParams2Pack(
          thriftMsg.setfwParamsReq.optionalParams,
          buffMsg->data.setfwParamsReq.data,
          maxOptSize);
      if ((len + lenOpt) > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      auto macAddress = parseMacAddress(thriftMsg.setfwParamsReq.addr);
      if (!macAddress) {
        len = 0;
        break;
      }
      buffMsg->msgType = TGF_PT_SB_SET_FW_PARAMS;
      if (thriftMsg.setfwParamsReq.bwgdIdx_ref().has_value()) {
        buffMsg->data.setfwParamsReq.bwgdIdxL =
            (uint32_t)(thriftMsg.setfwParamsReq.bwgdIdx_ref().value()
                & 0xffffffff);
        buffMsg->data.setfwParamsReq.bwgdIdxH =
            (uint32_t)(thriftMsg.setfwParamsReq.bwgdIdx_ref().value() >> 32);
      } else {
        buffMsg->data.setfwParamsReq.bwgdIdxL = 0;
        buffMsg->data.setfwParamsReq.bwgdIdxH = 0;
      }
      memcpy(
          buffMsg->data.setfwParamsReq.addr,
          macAddress->bytes(),
          sizeof(buffMsg->data.assoc.addr));
      buffMsg->data.setfwParamsReq.len = lenOpt;
      // Return the total message length
      len += lenOpt;
      break;
    }
    case thrift::PtMsgTypes::SB_GET_FW_PARAMS: {
      // Translate the Thrift GetFwParamsReq struct to the FW
      // tgfPtGetFwParamsReq
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtGetFwParamsReq);

      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      if (thriftMsg.getFwParamsReq.addr_ref().has_value()) {
        auto macAddress =
            parseMacAddress(thriftMsg.getFwParamsReq.addr_ref().value());
        if (!macAddress) {
          len = 0;
          break;
        }
        memcpy(
            buffMsg->data.getFwParamsReq.addr,
            macAddress->bytes(),
            sizeof(buffMsg->data.getFwParamsReq.addr));
      } else {
        // MAC address is not set , set to default value
        memset(
            buffMsg->data.getFwParamsReq.addr,
            0,
            sizeof(buffMsg->data.getFwParamsReq.addr));
      }
      buffMsg->msgType = TGF_PT_SB_GET_FW_PARAMS_REQ;

      buffMsg->data.getFwParamsReq.requestedFwParams =
          (tgfGetFwParamsTypes_t)thriftMsg.getFwParamsReq.requestedParamsType;

      break;
    }

    case thrift::PtMsgTypes::SB_AIRTIMEALLOC: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtAirtimeAllocMsg);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }

      buffMsg->msgType = TGF_PT_SB_AIRTIME_ALLOC;
      // Initialize & populate the (MAC address, Airtimes) table
      uint8_t* addr = (uint8_t*)(buffMsg->data.airtimeAlloc.addrAirtimeTable);
      memset(addr, 0, sizeof(buffMsg->data.airtimeAlloc.addrAirtimeTable));
      uint8_t validCnt = 0;
      for (const auto& entry : thriftMsg.airtimeAllocMap.linkAirtimes) {
        if (validCnt >= TGF_MAX_PEER_ENTRIES) {
          LOG(ERROR) << "Num of link entries exceeds max limit!";
          break;
        }
        auto macAddress = parseMacAddress(entry.macAddress);
        if (!macAddress) {
          continue;
        }
        tgfAddrAirtimeTable* destEntry =
            &(buffMsg->data.airtimeAlloc.addrAirtimeTable[validCnt]);
        memcpy(destEntry->macAddr, macAddress->bytes(), 6);
        destEntry->txIdeal = entry.txIdeal;
        destEntry->txMin = entry.txMin;
        destEntry->txMax = entry.txMax;
        destEntry->rxIdeal = entry.rxIdeal;
        destEntry->rxMin = entry.rxMin;
        destEntry->rxMax = entry.rxMax;
        validCnt++;
      }
      break;
    }

    case thrift::PtMsgTypes::SB_STATS_CONFIGURE: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtStatsConfigure);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      buffMsg->msgType = TGF_PT_SB_STATS_CONFIGURE;
      buffMsg->data.statsConfigure.statsCycle.onDuration =
          (uint32_t)thriftMsg.statsConfigure.onDuration;
      buffMsg->data.statsConfigure.statsCycle.period =
          (uint32_t)thriftMsg.statsConfigure.period;

      // every time the MAC address filter is configured, it overwrites
      // previous configurations
      for (uint32_t i = 0; i < TGF_HF_MACS_NUM; i++) {
        memset(
            buffMsg->data.statsConfigure.statsCycle.hfMacAddressFilter[i]
                .macAddr,
            0xff,
            sizeof(buffMsg->data.statsConfigure.statsCycle.hfMacAddressFilter[i]
                       .macAddr));
      }
      int idx = 0;
      for (const auto& macAddr : thriftMsg.statsConfigure.hfMacAddressFilter) {
        auto macAddress = parseMacAddress(macAddr);
        if (!macAddress) {
          continue;
        }
        memcpy(
            buffMsg->data.statsConfigure.statsCycle.hfMacAddressFilter[idx]
                .macAddr,
            macAddress->bytes(),
            sizeof(
                buffMsg->data.statsConfigure.statsCycle.hfMacAddressFilter[idx]
                    .macAddr));
        if (++idx == TGF_HF_MACS_NUM) {
          break;
        }
      }
      // initialize configure = 0
      for (uint32_t i = 0; i < TGF_STATS_NUM; i++) {
        buffMsg->data.statsConfigure.statsConfigs[i].configure = 0;
      }
      // set configure / enable based on thrift object
      for (const auto& config : thriftMsg.statsConfigure.configs) {
        uint32_t idx = 0;
        if (config.first == "TGF_STATS_STA_PKT") {
          idx = TGF_STATS_STA_PKT;
        } else if (config.first == "TGF_STATS_TEST_A") {
          idx = TGF_STATS_TEST_A;
        } else if (config.first == "TGF_STATS_TEST_B") {
          idx = TGF_STATS_TEST_B;
        } else if (config.first == "TGF_STATS_GPS") {
          idx = TGF_STATS_GPS;
        } else if (config.first == "TGF_STATS_PHYSTATUS") {
          idx = TGF_STATS_PHYSTATUS;
        } else if (config.first == "TGF_STATS_PHYSTATUS_MGMT_HF") {
          idx = TGF_STATS_PHYSTATUS_MGMT_HF;
        } else if (config.first == "TGF_STATS_PHYSTATUS_PRINT_HF") {
          idx = TGF_STATS_PHYSTATUS_PRINT_HF;
        } else if (config.first == "TGF_STATS_LA_TPC_PRINT_HF") {
          idx = TGF_STATS_LA_TPC_PRINT_HF;
        } else if (config.first == "TGF_STATS_MGMT_TX") {
          idx = TGF_STATS_MGMT_TX;
        } else if (config.first == "TGF_STATS_MGMT_RX") {
          idx = TGF_STATS_MGMT_RX;
        } else if (config.first == "TGF_STATS_SLOT") {
          idx = TGF_STATS_SLOT;
        } else if (config.first == "TGF_STATS_BWHAN_LINK") {
          idx = TGF_STATS_BWHAN_LINK;
        } else if (config.first == "TGF_STATS_BWHAN_SYS") {
          idx = TGF_STATS_BWHAN_SYS;
        } else if (config.first == "TGF_STATS_BF") {
          idx = TGF_STATS_BF;
        } else if (config.first == "TGF_STATS_PHY_PERIODIC") {
          idx = TGF_STATS_PHY_PERIODIC;
        } else if (config.first == "TGF_STATS_RECV_MGMT") {
          idx = TGF_STATS_RECV_MGMT;
        } else if (config.first == "TGF_STATS_MGMT_DATA") {
          idx = TGF_STATS_MGMT_DATA;
        } else if (config.first == "TGF_STATS_MEM") {
          idx = TGF_STATS_MEM;
        } else if (config.first == "TGF_STATS_MISC_SYS") {
          idx = TGF_STATS_MISC_SYS;
        } else if (config.first == "TGF_STATS_MISC_LINK") {
          idx = TGF_STATS_MISC_LINK;
        } else if (config.first == "TGF_STATS_LA_HIGH_SPEED") {
          idx = TGF_STATS_LA_HIGH_SPEED;
        } else if (config.first == "TGF_STATS_TPC_HIGH_SPEED") {
          idx = TGF_STATS_TPC_HIGH_SPEED;
        } else if (config.first == "TGF_STATS_TPC") {
          idx = TGF_STATS_TPC;
        } else if (config.first == "TGF_STATS_LA_TPC") {
          idx = TGF_STATS_LA_TPC;
        } else if (config.first == "TGF_STATS_TSF") {
          idx = TGF_STATS_TSF;
        } else if (config.first == "TGF_STATS_LIFETIME_EXPIRED") {
          idx = TGF_STATS_LIFETIME_EXPIRED;
        } else if (config.first == "TGF_STATS_LIFETIME_OK") {
          idx = TGF_STATS_LIFETIME_OK;
        } else if (config.first == "TGF_STATS_MAX_AGC") {
          idx = TGF_STATS_MAX_AGC;
        } else if (config.first == "TGF_STATS_MAX_AGC_HIST") {
          idx = TGF_STATS_MAX_AGC_HIST;
        } else if (config.first == "TGF_STATS_CHN") {
          idx = TGF_STATS_CHN;
        } else if (config.first == "TGF_STATS_LINK_DOWN") {
          idx = TGF_STATS_LINK_DOWN;
        } else if (config.first == "TGF_STATS_SECURITY") {
          idx = TGF_STATS_SECURITY;
        } else if (config.first == "TGF_STATS_RADIO") {
          idx = TGF_STATS_RADIO;
        } else if (config.first == "TGF_STATS_BF_SCAN") {
          idx = TGF_STATS_BF_SCAN;
        } else if (config.first == "TGF_STATS_MTPO") {
          idx = TGF_STATS_MTPO;
        } else {
          LOG(ERROR) << "unknown stats name for configuration, config.first="
                     << config.first;
          continue;
        }

        buffMsg->data.statsConfigure.statsConfigs[idx].configure = 1;
        buffMsg->data.statsConfigure.statsConfigs[idx].enable =
            config.second ? 1 : 0;
      }
      break;
    }

    case thrift::PtMsgTypes::SB_PHY_LA_CONFIG: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtPhyLAConfig);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      auto macAddress = parseMacAddress(thriftMsg.phyLAConfig.addr);
      if (!macAddress) {
        len = 0;
        break;
      }
      buffMsg->msgType = TGF_PT_SB_PHY_LA_CONFIG;

      memcpy(
          buffMsg->data.phyLAConfig.addr,
          macAddress->bytes(),
          sizeof(buffMsg->data.phyLAConfig.addr));

      if (thriftMsg.phyLAConfig.laNodeParams.mcsLqmQ8_ref().has_value()) {
        for (int i = 0; i < TGF_MCS_LQM_NUM; i++) {
          buffMsg->data.phyLAConfig.laNodeParams.mcsLqmQ8[i] =
              thriftMsg.phyLAConfig.laNodeParams.mcsLqmQ8_ref().value()[i];
        }
      }

      if (thriftMsg.phyLAConfig.laParams.laConvergenceFactordBperSFQ8_ref()
          .has_value()) {
        buffMsg->data.phyLAConfig.laParams.laConvergenceFactordBperSFQ8 =
            thriftMsg.phyLAConfig.laParams.laConvergenceFactordBperSFQ8_ref()
                .value();
      }
      if (thriftMsg.phyLAConfig.laParams.nackWeightFactor_ref().has_value()) {
        buffMsg->data.phyLAConfig.laParams.nackWeightFactor =
            thriftMsg.phyLAConfig.laParams.nackWeightFactor_ref().value();
      }
      if (thriftMsg.phyLAConfig.laParams.laMcs_ref().has_value()) {
        buffMsg->data.phyLAConfig.laParams.laMcs =
            (laMcsMode_t)thriftMsg.phyLAConfig.laParams.laMcs_ref().value();
      }

      if (buffMsg->data.phyLAConfig.laParams.laMcs >= LA_NUM_MODES) {
        LOG(ERROR) << "laMcs is invalid";
        len = 0;
        break;
      }

      if (thriftMsg.phyLAConfig.laParams.laMaxMcs_ref().has_value()) {
        if ((thriftMsg.phyLAConfig.laParams.laMaxMcs_ref().value() >=
             TGF_MCS_LQM_NUM) ||
            (thriftMsg.phyLAConfig.laParams.laMaxMcs_ref().value() < 1)) {
          LOG(ERROR) << "laMaxMcs is out of range";
          len = 0;
          break;
        }
        buffMsg->data.phyLAConfig.laParams.laMaxMcs =
            thriftMsg.phyLAConfig.laParams.laMaxMcs_ref().value();
      }
      if (thriftMsg.phyLAConfig.laParams.laMinMcs_ref().has_value()) {
        if ((thriftMsg.phyLAConfig.laParams.laMinMcs_ref().value() >=
             TGF_MCS_LQM_NUM) ||
            (thriftMsg.phyLAConfig.laParams.laMinMcs_ref().value() < 1)) {
          LOG(ERROR) << "laMinMcs is out of range";
          len = 0;
          break;
        }
        buffMsg->data.phyLAConfig.laParams.laMinMcs =
            thriftMsg.phyLAConfig.laParams.laMinMcs_ref().value();
      }

      if (thriftMsg.phyLAConfig.laParams.laMinMcs_ref().has_value() &&
          thriftMsg.phyLAConfig.laParams.laMaxMcs_ref().has_value()) {
        if (thriftMsg.phyLAConfig.laParams.laMinMcs_ref().value() >
            thriftMsg.phyLAConfig.laParams.laMaxMcs_ref().value()) {
          LOG(ERROR) << "Error: laMinMcs > laMaxMcs";
          len = 0;
          break;
        }
      }

      if (thriftMsg.phyLAConfig.laParams.laMargindBQ2_ref().has_value()) {
        if ((thriftMsg.phyLAConfig.laParams.laMargindBQ2_ref().value() > 120) ||
            (thriftMsg.phyLAConfig.laParams.laMargindBQ2_ref().value() < -120))
        {
          LOG(ERROR) << "laMargindB must be between -30 and +30dB";
          len = 0;
          break;
        }
        buffMsg->data.phyLAConfig.laParams.laMargindBQ2 =
            thriftMsg.phyLAConfig.laParams.laMargindBQ2_ref().value();
      }
      if (thriftMsg.phyLAConfig.laParams.debug_ref().has_value()) {
        buffMsg->data.phyLAConfig.laParams.debug =
            thriftMsg.phyLAConfig.laParams.debug_ref().value();
      }
      break;
    }

    case thrift::PtMsgTypes::SB_PHY_AGC_CONFIG: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtPhyAgcConfig);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds api limit";
        break;
      }
      auto macAddress = parseMacAddress(thriftMsg.phyAgcConfig.addr);
      if (!macAddress) {
        len = 0;
        break;
      }
      buffMsg->msgType = TGF_PT_SB_PHY_AGC_CONFIG;

      memcpy(
          buffMsg->data.phyAgcConfig.addr,
          macAddress->bytes(),
          sizeof(buffMsg->data.phyAgcConfig.addr));

      auto& agcNodeParams = thriftMsg.phyAgcConfig.agcNodeParams;
      if (agcNodeParams.rfGaindBperIndexQ8_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.rfGaindBperIndexQ8 =
            agcNodeParams.rfGaindBperIndexQ8_ref().value();
      }
      if (agcNodeParams.rfGainHiLo_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.rfGainHiLo =
            agcNodeParams.rfGainHiLo_ref().value();
      }
      if (agcNodeParams.ifGaindBperIndexQ8_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.ifGaindBperIndexQ8 =
            agcNodeParams.ifGaindBperIndexQ8_ref().value();
      }
      if (agcNodeParams.rawAdcScaleFactorQ8_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.rawAdcScaleFactorQ8 =
            agcNodeParams.rawAdcScaleFactorQ8_ref().value();
      }
      if (agcNodeParams.targetRawAdc_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.targetRawAdc =
            agcNodeParams.targetRawAdc_ref().value();
      }
      if (agcNodeParams.maxIfGainIndex_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.maxIfGainIndex =
            agcNodeParams.maxIfGainIndex_ref().value();
      }
      if (agcNodeParams.minIfGainIndex_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.minIfGainIndex =
            agcNodeParams.minIfGainIndex_ref().value();
      }
      if (agcNodeParams.maxRfGainIndex_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.maxRfGainIndex =
            agcNodeParams.maxRfGainIndex_ref().value();
      }
      if (agcNodeParams.minRfGainIndex_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.minRfGainIndex =
            agcNodeParams.minRfGainIndex_ref().value();
      }
      if (agcNodeParams.maxIfSweetGainRange_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.maxIfSweetGainRange =
            agcNodeParams.maxIfSweetGainRange_ref().value();
      }
      if (agcNodeParams.minIfSweetGainRange_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.minIfSweetGainRange =
            agcNodeParams.minIfSweetGainRange_ref().value();
      }
      if (agcNodeParams.crsScale_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.crsScale =
            agcNodeParams.crsScale_ref().value();
      }
      if (agcNodeParams.logIntervalBwgd_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.logIntervalBwgd =
            agcNodeParams.logIntervalBwgd_ref().value();
      }
      if (agcNodeParams.useMinRssi_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.useMinRssi =
            agcNodeParams.useMinRssi_ref().value();
      }
      if (agcNodeParams.useSameForAllSta_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.useSameForAllSta =
            agcNodeParams.useSameForAllSta_ref().value();
      }
      if (agcNodeParams.debugagc_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcNodeConfig.debugagc =
            agcNodeParams.debugagc_ref().value();
      }

      auto& agcLinkParams = thriftMsg.phyAgcConfig.agcLinkParams;
      if (agcLinkParams.maxAgcTrackingMargindB_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcLinkConfig.maxAgcTrackingMargindB =
            agcLinkParams.maxAgcTrackingMargindB_ref().value();
      }
      if (agcLinkParams.maxAgcTrackingEnabled_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcLinkConfig.maxAgcTrackingEnabled =
            agcLinkParams.maxAgcTrackingEnabled_ref().value();
      }
      if (agcLinkParams.linkAgc_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcLinkConfig.linkAgc =
            agcLinkParams.linkAgc_ref().value();
      }
      if (agcLinkParams.maxNumberMissingBWGDs_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcLinkConfig.maxNumberMissingBWGDs =
            agcLinkParams.maxNumberMissingBWGDs_ref().value();
      }
      if (agcLinkParams.iirAlphaUpQ8_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcLinkConfig.iirAlphaUpQ8 =
            agcLinkParams.iirAlphaUpQ8_ref().value();
      }
      if (agcLinkParams.iirAlphaDownQ8_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcLinkConfig.iirAlphaDownQ8 =
            agcLinkParams.iirAlphaDownQ8_ref().value();
      }
      if (agcLinkParams.minRssi_ref().has_value()) {
        buffMsg->data.phyAgcConfig.agcLinkConfig.minRssi =
            agcLinkParams.minRssi_ref().value();
      }

      // range checking
      THRIFT_RANGE_CHECK(
          thriftMsg.phyAgcConfig.agcLinkParams.iirAlphaUpQ8, 0, 256, 1, 1);
      THRIFT_RANGE_CHECK(
          thriftMsg.phyAgcConfig.agcLinkParams.iirAlphaDownQ8, 0, 256, 1, 1);
      THRIFT_RANGE_CHECK(
          thriftMsg.phyAgcConfig.agcLinkParams.minRssi, -127, 128, 1, 1);
      THRIFT_RANGE_CHECK(
          thriftMsg.phyAgcConfig.agcLinkParams.maxAgcTrackingEnabled,
          0,
          1,
          1,
          1);
      THRIFT_RANGE_CHECK(
          thriftMsg.phyAgcConfig.agcNodeParams.useSameForAllSta, 0, 1, 1, 1);
      THRIFT_RANGE_CHECK(
          thriftMsg.phyAgcConfig.agcNodeParams.useMinRssi, 0, 1, 1, 1);
      THRIFT_RANGE_CHECK(
          thriftMsg.phyAgcConfig.agcNodeParams.maxRfGainIndex, 0, 15, 1, 1);
      THRIFT_RANGE_CHECK(
          thriftMsg.phyAgcConfig.agcNodeParams.minRfGainIndex, 0, 15, 1, 1);
      THRIFT_RANGE_CHECK(
          thriftMsg.phyAgcConfig.agcNodeParams.maxIfGainIndex, 0, 31, 1, 1);
      THRIFT_RANGE_CHECK(
          thriftMsg.phyAgcConfig.agcNodeParams.minIfGainIndex, 0, 31, 1, 1);
      THRIFT_RANGE_CHECK(
          thriftMsg.phyAgcConfig.agcNodeParams.maxIfSweetGainRange,
          0,
          31,
          1,
          1);
      THRIFT_RANGE_CHECK(
          thriftMsg.phyAgcConfig.agcNodeParams.minIfSweetGainRange,
          0,
          31,
          1,
          1);
      break;
    } // end AGC

    case thrift::PtMsgTypes::SB_PHY_TPC_CONFIG: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtPhyTpcConfig);

      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds api limit";
        break;
      }
      auto macAddress = parseMacAddress(thriftMsg.phyTpcConfig.addr);
      if (!macAddress) {
        len = 0;
        break;
      }
      buffMsg->msgType = TGF_PT_SB_PHY_TPC_CONFIG;

      memcpy(
          buffMsg->data.phyTpcConfig.addr,
          macAddress->bytes(),
          sizeof(buffMsg->data.phyTpcConfig.addr));

      if (thriftMsg.phyTpcConfig.tpcNodeParams.powerQ2AtIndex_ref().has_value())
      {
        for (int i = 0; i < TGF_TPC_GAIN_INDEX_TBL_SIZE; i++) {
          buffMsg->data.phyTpcConfig.tpcNodeConfig.powerQ2AtIndex[i] =
              thriftMsg.phyTpcConfig.tpcNodeParams.powerQ2AtIndex_ref()
                  .value()[i];
        }
      }
      auto& tpcLinkParams = thriftMsg.phyTpcConfig.tpcLinkParams;
      if (tpcLinkParams.tpcEnable_ref().has_value()) {
        buffMsg->data.phyTpcConfig.tpcLinkConfig.tpcEnable =
            tpcLinkParams.tpcEnable_ref().value();
      }
      if (tpcLinkParams.tpcPBEnable_ref().has_value()) {
        buffMsg->data.phyTpcConfig.tpcLinkConfig.tpcPBEnable =
            tpcLinkParams.tpcPBEnable_ref().value();
      }
      if (tpcLinkParams.txPower_ref().has_value()) {
        buffMsg->data.phyTpcConfig.tpcLinkConfig.txPowerIndex =
            tpcLinkParams.txPower_ref().value();
      }
      if (tpcLinkParams.maxTxPower_ref().has_value()) {
        buffMsg->data.phyTpcConfig.tpcLinkConfig.maxTxPowerIndex =
            tpcLinkParams.maxTxPower_ref().value();
      }
      if (tpcLinkParams.minTxPower_ref().has_value()) {
        buffMsg->data.phyTpcConfig.tpcLinkConfig.minTxPowerIndex =
            tpcLinkParams.minTxPower_ref().value();
      }
      if (tpcLinkParams.tpcDelPowerStep3_ref().has_value()) {
        buffMsg->data.phyTpcConfig.tpcLinkConfig.delPowerStep3Q2 =
            tpcLinkParams.tpcDelPowerStep3_ref().value() *
            Q(2);
      }
      if (tpcLinkParams.tpcRefStfSnrStep3_ref().has_value()) {
        buffMsg->data.phyTpcConfig.tpcLinkConfig.refStfSnrStep3Q2 =
            tpcLinkParams.tpcRefStfSnrStep3_ref().value() *
            Q(2);
      }
      if (tpcLinkParams.tpcAlphaUpRssiStep3Q10_ref().has_value()) {
        buffMsg->data.phyTpcConfig.tpcLinkConfig.alphaUpRssiQ10 =
            tpcLinkParams.tpcAlphaUpRssiStep3Q10_ref().value();
      }
      if (tpcLinkParams.tpcAlphaDownRssiStep3Q10_ref().has_value()) {
        buffMsg->data.phyTpcConfig.tpcLinkConfig.alphaDownRssiQ10 =
            tpcLinkParams.tpcAlphaDownRssiStep3Q10_ref().value();
      }
      if (tpcLinkParams.tpcAlphaUpTargetRssiStep3Q10_ref().has_value()) {
        buffMsg->data.phyTpcConfig.tpcLinkConfig.alphaUpTargetRssiQ10 =
            tpcLinkParams.tpcAlphaUpTargetRssiStep3Q10_ref().value();
      }
      if (tpcLinkParams.tpcAlphaDownTargetRssiStep3Q10_ref().has_value()) {
        buffMsg->data.phyTpcConfig.tpcLinkConfig.alphaDownTargetRssiQ10 =
            tpcLinkParams.tpcAlphaDownTargetRssiStep3Q10_ref().value();
      }
      if (tpcLinkParams.statsReportingPeriodBwgd_ref().has_value()) {
        buffMsg->data.phyTpcConfig.tpcLinkConfig.statsReportingPeriodBwgd =
            tpcLinkParams.statsReportingPeriodBwgd_ref().value();
      }
      if (tpcLinkParams.tpcHysteresisdBStep3Q2_ref().has_value()) {
        buffMsg->data.phyTpcConfig.tpcLinkConfig.tpcHysteresisdBQ2 =
            tpcLinkParams.tpcHysteresisdBStep3Q2_ref().value();
      }
      if (tpcLinkParams.debugtpc_ref().has_value()) {
        buffMsg->data.phyTpcConfig.tpcLinkConfig.debugtpc =
            tpcLinkParams.debugtpc_ref().value();
      }

      // error checking
      if (buffMsg->data.phyTpcConfig.tpcLinkConfig.tpcEnable > 3) {
        LOG(ERROR) << "tpcEnable invalid";
        len = 0;
      }
      if (buffMsg->data.phyTpcConfig.tpcLinkConfig.maxTxPowerIndex >
          TGF_TPC_GAIN_INDEX_TBL_SIZE - 1) {
        LOG(ERROR) << "maxTxPowerIndex out of bounds";
        len = 0;
      }
      if (buffMsg->data.phyTpcConfig.tpcLinkConfig.txPowerIndex >
          TGF_TPC_GAIN_INDEX_TBL_SIZE - 1) {
        LOG(ERROR) << "txPowerIndex out of bounds";
        len = 0;
      }
      break;
    }

    case thrift::PtMsgTypes::SB_GPS_ENABLE: {
      len = offsetof(tgfPtMsg, data);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      buffMsg->msgType = TGF_PT_SB_GPS_ENABLE;
      break;
    }

    case thrift::PtMsgTypes::SB_POLARITY: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtPolarityConfig);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      buffMsg->msgType = TGF_PT_SB_POLARITY;
      buffMsg->data.polarityCfg.polarity =
          (uint8_t)thriftMsg.polarityCfg.polarity;
      break;
    }

    case thrift::PtMsgTypes::SB_GOLAY_INDX: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtGolayConfig);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      buffMsg->msgType = TGF_PT_SB_GOLAY_INDX;
      buffMsg->data.golayCfg.txIndx = (uint8_t)thriftMsg.golayCfg.txGolayIndx;
      buffMsg->data.golayCfg.rxIndx = (uint8_t)thriftMsg.golayCfg.rxGolayIndx;
      break;
    }

    case thrift::PtMsgTypes::SB_BF_SLOT_EXCLUSION_REQ: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtBfSlotExclusionReq);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      buffMsg->msgType = TGF_PT_SB_BF_SLOT_EXCLUSION_REQ;
      buffMsg->data.bfSlotExclusionReq.startBwgdIdxL =
          (uint32_t)(thriftMsg.bfSlotExclusionReq.startBwgdIdx & 0xffffffff);
      buffMsg->data.bfSlotExclusionReq.startBwgdIdxH =
          (uint32_t)(thriftMsg.bfSlotExclusionReq.startBwgdIdx >> 32);
      break;
    }

    case thrift::PtMsgTypes::SB_PHY_GOLAY_SEQUENCE_CONFIG: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtPhyGolaySequenceConfig);
      buffMsg->msgType = TGF_PT_SB_PHY_GOLAY_SEQUENCE_CONFIG;

      for (uint32_t i = 0; i < TGF_NUM_GOLAY_SEQ_COUNT; i++) {
        for (uint32_t j = 0; j < TGF_NUM_GOLAY_SEQ_COUNT; j++) {
          buffMsg->data.phyGolaySequenceCfg.transmitSequence[i][j] =
              thriftMsg.phyGolaySequenceCfg.transmitSequence[i][j];
        }
      }
      for (uint32_t i = 0; i < TGF_NUM_GOLAY_SEQ_COUNT; i++) {
        buffMsg->data.phyGolaySequenceCfg.receiveSequence[i] =
            thriftMsg.phyGolaySequenceCfg.receiveSequence[i];
      }
      break;
    }

    case thrift::PtMsgTypes::SB_PHY_ANT_WGT_TBL_CONFIG: {
      const thrift::PhyAntWgtTblPart& table = thriftMsg.phyAntWgtTblPart;
      uint8_t numElements = table.antWgtCode[0].size();
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtPhyAntWgtTblConfig) +
            table.strideCount * table.beamCount * numElements *
                sizeof(buffMsg->data.phyAntWgtTblConfig.antWgtCode[0]);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      buffMsg->msgType = TGF_PT_SB_PHY_ANT_WGT_TBL_CONFIG;
      tgfPtPhyAntWgtTblConfig& output = buffMsg->data.phyAntWgtTblConfig;
      output.antWgtTblModule = table.module;
      output.antWgtTblIsRx = table.isRx;
      output.beamStart = table.beamStart;
      output.beamCount = table.beamCount;
      output.stride = table.stride;
      output.strideCount = table.strideCount;
      output.numElements = numElements;

      for (uint32_t i = 0; i < output.beamCount; i++) {
        for (uint32_t j = 0; j < numElements; j++) {
          output.antWgtCode[i * numElements + j] = table.antWgtCode[i][j];
        }
      }
      break;
    }

    case thrift::PtMsgTypes::SB_DEBUG: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtDebug);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      buffMsg->msgType = TGF_PT_SB_DEBUG;
      size_t count = thriftMsg.debug.cmdStr.copy(
          (char*)buffMsg->data.debug.cmdStr,
          sizeof(buffMsg->data.debug.cmdStr) - 1);
      buffMsg->data.debug.cmdStr[count] = '\0';
      buffMsg->data.debug.valueL = (uint32_t)thriftMsg.debug.value;
      buffMsg->data.debug.valueH = (uint32_t)(thriftMsg.debug.value >> 32);
      break;
    }

    case thrift::PtMsgTypes::SB_BF_RESP_SCAN_CONFIG: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfBfResponderModeConfig);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      buffMsg->msgType = TGF_PT_SB_BF_RESPONDER_MODE_CONFIG;
      buffMsg->data.bfRespModeCfg.config = (uint8_t)(
          (thriftMsg.bfRespScanCfg.cfg == true) ? BFRESP_MODE_ENABLE
                                                : BFRESP_MODE_DISABLE);
      break;
    }

    case thrift::PtMsgTypes::SB_SCAN_REQ: {
      const thrift::ScanReq& src = thriftMsg.scanReq;
      bool isTx = src.rxNodeMac_ref().has_value();
      tgfPtScanReq& dst = buffMsg->data.scanReq;
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtScanReq);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      auto peerMacAddress = parseMacAddress(
          isTx ? src.rxNodeMac_ref().value() : src.txNodeMac_ref().value());
      if (!peerMacAddress) {
        len = 0;
        break;
      }
      buffMsg->msgType = TGF_PT_SB_SCAN_REQ;

      dst.token = src.token;
      dst.scanType = src.scanType_ref().has_value() ?
          (uint8_t)src.scanType_ref().value() : 0xff;
      dst.scanMode = (uint8_t)src.scanMode;
      dst.startBwgdIdxL = src.startBwgdIdx & 0xffffffff;
      dst.startBwgdIdxH = src.startBwgdIdx >> 32;
      dst.bfScanInvertPolarity = src.bfScanInvertPolarity;
      dst.txPwrIndex = src.txPwrIndex_ref().has_value() ?
          src.txPwrIndex_ref().value() : 0xff;
      dst.beamIdxLow = src.beams_ref().has_value() ? src.beams_ref().value().low
          : -1;
      dst.beamIdxHigh = src.beams_ref().has_value() ?
          src.beams_ref().value().high : -1;
      dst.isTx = isTx;
      dst.apply = src.apply_ref().has_value() ? src.apply_ref().value() : 0;
      dst.subType = src.subType_ref().has_value() ?
          (uint8_t)src.subType_ref().value() : (uint8_t)-1;
      dst.bwgdLen = src.bwgdLen_ref().has_value() ? src.bwgdLen_ref().value() :
          -1;

      memcpy(dst.peerMacAddr, peerMacAddress->bytes(), 6);

      dst.nullAngle  = src.nullAngle_ref().has_value() ?
          src.nullAngle_ref().value() : 0x8000;
      dst.cbfBeamIdx = src.cbfBeamIdx_ref().has_value() ?
          src.cbfBeamIdx_ref().value() : 0xff;
      break;
    }

    case thrift::PtMsgTypes::SB_CHANNEL_CONFIG: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtChannelConfig);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      buffMsg->msgType = TGF_PT_SB_CHANNEL_CONFIG;
      buffMsg->data.channelCfg.channel = (uint8_t)thriftMsg.channelCfg.channel;
      break;
    }

    case thrift::PtMsgTypes::SB_SET_LOG_CONFIG: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtSetLogConfig);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      buffMsg->msgType = TGF_PT_SB_SET_LOG_CONFIG;
      tgfPtSetLogConfig* cfg = &buffMsg->data.setLogConfig;
      memset(cfg->logLevel, TGF_LOG_NUM_LEVELS, sizeof(cfg->logLevel));
      for (const auto& config : thriftMsg.setLogConfig.configs) {
        cfg->logLevel[(int)config.first] = (int)config.second;
      }
      break;
    }

    case thrift::PtMsgTypes::SB_SET_ADJ: {
      // prepare the sb pt msg to set the local adjs in fw
      size_t offset = 0;
      len = offsetof(tgfPtMsg, data.setAdj) + sizeof(buffMsg->data.setAdj);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      buffMsg->msgType = TGF_PT_SB_TOPO_SET_ADJ;
      buffMsg->data.setAdj.numAdjs = 0;

      for (const auto& kv : thriftMsg.fwAdjResp.adjs) {
        auto macAddress = parseMacAddress(kv.first);
        if (!macAddress) {
          continue;
        }

        // Keep this entry if:
        // - The interface name starts with "nic" (wired adjacency)
        // - The interface name is empty (other local radio MACs)
        if (kv.second.empty() || kv.second.find("nic") == 0) {
          len += 6;
          if (len > SB_PT_BUFF_LEN) {
            len = 0;
            LOG(ERROR) << "message exceeds vendor api limit";
            break;
          }
          memcpy(
              &buffMsg->data.setAdj.adjMacAddrs[offset],
              macAddress->bytes(),
              6);
          offset += 6;
          buffMsg->data.setAdj.numAdjs++;
        }
      }
      break;
    }

    case thrift::PtMsgTypes::SB_GPS_TIME: {
      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtGpsTimeValue);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }
      buffMsg->msgType = TGF_PT_SB_GPS_TIME;
      buffMsg->data.gpsTimeValue.unixTimeSecsH =
          thriftMsg.gpsTimeValue.unixTimeSecs >> 32;
      buffMsg->data.gpsTimeValue.unixTimeSecsL =
          thriftMsg.gpsTimeValue.unixTimeSecs & 0xffffffff;
      break;
    }

    case thrift::PtMsgTypes::SB_PHY_TPC_ADJ_TBL_CFG: {
      const thrift::PhyTpcAdjTblCfg& table = thriftMsg.phyTpcAdjTblCfg;
      uint8_t numOfTxPowerAdjs = table.txPowerAdjTable.size();

      len = offsetof(tgfPtMsg, data) + sizeof(tgfPtTpcAdjPerBeamTblCfg) +
                numOfTxPowerAdjs *
                sizeof(buffMsg->data.tpcAdjPerBeamCfg.txPowerAdjPerBeamTbl[0]);
      if (len > SB_PT_BUFF_LEN) {
        len = 0;
        LOG(ERROR) << "message exceeds vendor api limit";
        break;
      }

      buffMsg->msgType = TGF_PT_SB_PHY_TPC_ADJ_TBL_CFG;
      buffMsg->data.tpcAdjPerBeamCfg.numOfTxPowerAdjs = numOfTxPowerAdjs;
      for (uint32_t i = 0; i < numOfTxPowerAdjs; i++) {
        buffMsg->data.tpcAdjPerBeamCfg.txPowerAdjPerBeamTbl[i] =
            table.txPowerAdjTable[i];
      }
      break;
    }

    default: {
      LOG(ERROR) << "Unexpected thriftMsg.msgType: "
                 << folly::get_default(
                 TEnumMapFactory<thrift::PtMsgTypes>::makeValuesToNamesMap(),
                 thriftMsg.msgType, "UNKNOWN");
    }
  }
  return len;
}

// Map of (radiomac, token) -> ScanResp
// Holds partial scan responses until we get an end-of-scan message
static std::unordered_map<std::pair<std::string, uint32_t>, thrift::ScanResp>
    scanPartialReplies;

thrift::PassThruMsg
getPtThrift(const uint8_t* buff, size_t len, const std::string& radioMac) {
  const tgfPtMsg* buffMsg = reinterpret_cast<const tgfPtMsg*>(buff);
  thrift::PassThruMsg thriftMsg;

  size_t ptHdrLen = offsetof(tgfPtMsg, data);
  if (len < ptHdrLen) {
    thriftMsg.msgType = thrift::PtMsgTypes::NB_INVALID;
    LOG(ERROR) << "insufficient, len: " << len;
    return thriftMsg;
  }

  thriftMsg.cookie = buffMsg->cookie;

  switch (buffMsg->dest) {

    case TGF_PT_DEST_E2E:
      thriftMsg.dest = thrift::PtMsgDest::E2E;
      break;

    case TGF_PT_DEST_DBG:
      thriftMsg.dest = thrift::PtMsgDest::DBG;
      break;

    case TGF_PT_DEST_E2E_DBG:
      thriftMsg.dest = thrift::PtMsgDest::E2E_DBG;
      break;

    default:
      thriftMsg.msgType = thrift::PtMsgTypes::NB_INVALID;
      LOG(ERROR) << "unknown, buffMsg->dest: " << buffMsg->dest;
      return thriftMsg;
  }

  switch (buffMsg->msgType) {

    case TGF_PT_NB_STATS:
      thriftMsg.msgType = thrift::PtMsgTypes::NB_STATS;
      thriftMsg.stats = getStats(&buffMsg->data.statsHdr, len - ptHdrLen);
      break;

    case TGF_PT_NB_ACK:
      if (len < ptHdrLen + sizeof(buffMsg->data.ack)) {
        thriftMsg.msgType = thrift::PtMsgTypes::NB_INVALID;
      } else {
        thriftMsg.msgType = thrift::PtMsgTypes::NB_ACK;
        thriftMsg.ack.success = buffMsg->data.ack.success != 0;
        switch (buffMsg->data.ack.msgType) {
          case TGF_PT_SB_ASSOC:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_ASSOC;
            break;
          case TGF_PT_SB_DISSOC:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_DISSOC;
            break;
          case TGF_PT_SB_SET_FW_PARAMS:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_SET_FW_PARAMS;
            break;
          case TGF_PT_SB_AIRTIME_ALLOC:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_AIRTIMEALLOC;
            break;
          case TGF_PT_SB_STATS_CONFIGURE:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_STATS_CONFIGURE;
            break;
          case TGF_PT_SB_PHY_LA_CONFIG:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_PHY_LA_CONFIG;
            break;
          case TGF_PT_SB_PHY_AGC_CONFIG:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_PHY_AGC_CONFIG;
            break;
          case TGF_PT_SB_PHY_TPC_CONFIG:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_PHY_TPC_CONFIG;
            break;
          case TGF_PT_SB_GPS_ENABLE:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_GPS_ENABLE;
            break;
          case TGF_PT_SB_PHY_ANT_WGT_TBL_CONFIG:
            thriftMsg.ack.msgType =
                thrift::PtMsgTypes::SB_PHY_ANT_WGT_TBL_CONFIG;
            break;
          case TGF_PT_SB_POLARITY:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_POLARITY;
            break;
          case TGF_PT_SB_GOLAY_INDX:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_GOLAY_INDX;
            break;
          case TGF_PT_SB_DEBUG:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_DEBUG;
            break;
          case TGF_PT_SB_BF_RESPONDER_MODE_CONFIG:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_BF_RESP_SCAN_CONFIG;
            break;
          case TGF_PT_SB_SCAN_REQ:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_SCAN_REQ;
            break;
          case TGF_PT_SB_BF_SLOT_EXCLUSION_REQ:
            thriftMsg.ack.msgType =
                thrift::PtMsgTypes::SB_BF_SLOT_EXCLUSION_REQ;
            break;
          case TGF_PT_SB_PHY_GOLAY_SEQUENCE_CONFIG:
            thriftMsg.ack.msgType =
                thrift::PtMsgTypes::SB_PHY_GOLAY_SEQUENCE_CONFIG;
            break;
          case TGF_PT_SB_CHANNEL_CONFIG:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_CHANNEL_CONFIG;
            break;
          case TGF_PT_SB_GET_FW_PARAMS_REQ:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_GET_FW_PARAMS;
            break;
          case TGF_PT_SB_SET_LOG_CONFIG:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_SET_LOG_CONFIG;
            break;
          case TGF_PT_SB_TOPO_SET_ADJ:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_SET_ADJ;
            break;
          case TGF_PT_SB_GPS_TIME:
            thriftMsg.ack.msgType = thrift::PtMsgTypes::SB_GPS_TIME;
            break;
          case TGF_PT_SB_PHY_TPC_ADJ_TBL_CFG:
            thriftMsg.ack.msgType =
                thrift::PtMsgTypes::SB_PHY_TPC_ADJ_TBL_CFG;
            break;
          default:
            LOG(ERROR) << "Unexpected buffMsg->data.ack.msgType: "
                       << buffMsg->data.ack.msgType;
            thriftMsg.msgType = thrift::PtMsgTypes::NB_INVALID;
        }
      }
      break;

    case TGF_PT_NB_HEALTHY:
      thriftMsg.msgType = thrift::PtMsgTypes::NB_HEALTHY;

      thriftMsg.fwHealthReport.faultFlags =
          buffMsg->data.fwHealthReport.faultFlags;

      thriftMsg.fwHealthReport.selfMacAddr =
          folly::MacAddress::fromBinary(
              folly::ByteRange(buffMsg->data.fwHealthReport.selfMacAddr, 6))
              .toString();

      // Firmware reports GPS time as two 32-bit values for legacy reasons.
      // Shift high order bits to correct position and combine with low
      // order bits to form correct, 64-bit value for GPS time.
      thriftMsg.fwHealthReport.tsf =
          (uint32_t)buffMsg->data.fwHealthReport.tsfL |
          ((uint64_t)buffMsg->data.fwHealthReport.tsfH << 32);
      break;

    case TGF_PT_NB_SCAN_RESP: {
      if (len < ptHdrLen + sizeof(tgfPtScanRespHdr)) {
        LOG(ERROR) << "TGF_PT_NB_SCAN_RESP: Too short (1). Need at least "
                   << ptHdrLen + sizeof(tgfPtScanRespHdr) << " bytes, have "
                   << len << " bytes";
        thriftMsg.msgType = thrift::PtMsgTypes::NB_INVALID;
        break;
      }
      int numResponses = buffMsg->data.scanResp.numResponses;
      uint8_t* end = (uint8_t*)buffMsg + len;
      // Update scanPartialReplies with received replies
      const tgfPtScanResp* resp = &buffMsg->data.scanResp.responses[0];
      for (int i = 0; i < numResponses; i++) {
        if ((uint8_t*)(resp) + sizeof(tgfPtScanResp) +
                (resp->numRoutes * sizeof(tgfPtRouteInfo))  > end) {
          LOG(ERROR) << "TGF_PT_NB_SCAN_RESP: too short (2) expected "
                     << resp->numRoutes * sizeof(tgfPtRouteInfo);
          thriftMsg.msgType = thrift::PtMsgTypes::NB_INVALID;
          break;
        }
        uint32_t token = resp->token;
        thrift::ScanResp& scanResp =
            scanPartialReplies[std::make_pair(radioMac, token)];
        scanResp.token = token;
        scanResp.radioMac_ref() = radioMac;
        if (resp->txPowerIndex != POWER_INDEX_NOT_USED) {
          scanResp.txPwrIndex_ref() = resp->txPowerIndex;
        } else {
          scanResp.txPwrIndex_ref().reset();
        }
        scanResp.curSuperframeNum = (uint64_t)resp->curSuperframeNumL |
                                    ((uint64_t)resp->curSuperframeNumH << 32);
        if (resp->numRoutes) {
          if (resp->topoRespFlg > 1) {
            LOG(ERROR) << "Invalid topology scan response flag";
            thriftMsg.msgType = thrift::PtMsgTypes::NB_INVALID;
            break;
          }
          if (resp->topoRespFlg) {
            // update the topology scan results
            const tgfPtTopoScanInfo* info = &resp->topoInfo;
            if (!scanResp.topoResps_ref().has_value()) {
              scanResp.topoResps_ref().emplace();
            }
            thrift::TopoResponderInfo& topoResp =
                (*scanResp.topoResps_ref())[info->respIdx];

            switch (info->type) {
              case TOPO_INFO_ROUTE: {
                // add the routes
                auto& rxBeamIdxLqm =
                    topoResp.itorLqmMat[info->data.routes.txBeamIdx];
                if (info->data.routes.rxBeamCnt > TOPO_SCAN_RESP_UR_MAX_NUM) {
                  LOG(ERROR)
                      << "Invalid number of Routes: "
                      << info->data.routes.rxBeamCnt << " in topology scan";
                  break;
                }
                // for a specific tx-rx beam pair, only the route lqm in
                // the last response packet containing it will be kept
                for (int j = 0; j < info->data.routes.rxBeamCnt; ++j) {
                  rxBeamIdxLqm[info->data.routes.rxBeamIdxs[j]] =
                      info->data.routes.rxBeamLqms[j];
                }
                topoResp.rtoiLqmMat[info->data.routes.rxBeamIdx]
                                   [info->data.routes.txBeamIdx] =
                    info->data.routes.rtoiLqm;
                break;
              }

              case TOPO_INFO_MAC_ADDR: {
                topoResp.addr = folly::MacAddress::fromBinary(
                                    folly::ByteRange(info->data.addr, 6))
                                    .toString();
                break;
              }

              case TOPO_INFO_GPS_POS: {
                if (info->data.pos.accuracy > 0) {
                  thrift::Location location;
                  location.latitude = info->data.pos.latitude / k1e7;
                  location.longitude = info->data.pos.longitude / k1e7;
                  location.altitude = info->data.pos.height / k1e3;
                  location.accuracy = sqrt(info->data.pos.accuracy) / k1e3;
                  topoResp.pos_ref() = location;
                } else {
                  LOG(ERROR) << "Invalid GPS position in topology scan";
                }
                break;
              }

              case TOPO_INFO_LOCAL_ADJ: {
                for (int j = 0; j < TGF_MAX_TOPO_LOCAL_ADJ; ++j) {
                  const auto& adjMac = folly::MacAddress::fromBinary(
                      folly::ByteRange(&info->data.adjAddrs[6 * j], 6));
                  if (adjMac != folly::MacAddress::ZERO) {
                    topoResp.adjs.insert(adjMac.toString());
                  }
                }
                break;
              }

              default: {
                LOG(ERROR)
                    << "TGF_PT_NB_TOPO_SCAN_RESP: Invalid TOPO RESP type";
              }
            }
          } else {
            for ( int k = 0; k < resp->numRoutes; k++) {
              const tgfPtRouteInfo& ri = resp->route[k];
            thrift::MicroRoute microRoute;
            microRoute.rx = ri.rxBeamIdx;
            microRoute.tx = ri.txBeamIdx;

            thrift::RouteInfo routeInfo;
            routeInfo.route = microRoute;
            routeInfo.rssi = ri.rssidBm;
            routeInfo.snrEst = (double)ri.snrEstdBQ8 / 256;
            routeInfo.postSnr = (double)ri.postSnrdBQ1 / 2;
            routeInfo.rxStart = ri.rxStart;
            routeInfo.packetIdx = ri.dblPktIdx;
            routeInfo.sweepIdx = ri.sweepIdx;

            scanResp.routeInfoList.push_back(routeInfo);

            }
          }
        }

        if (resp->numRoutes == 0) {
          // End of scan marker
          scanResp.status = (thrift::ScanFwStatus)resp->status;
          // This scan is complete, return it
          if (resp->status == SCAN_PROCEDURE_COMPLETE) {
            scanResp.numSweeps_ref() = resp->numSweeps;
            scanResp.startSuperframeNum_ref() = resp->startSFNum;
            scanResp.endSuperframeNum_ref() = resp->endSFNum;
            scanResp.azimuthBeam_ref() = resp->azimuthBeam;
            scanResp.oldBeam_ref() = resp->oldBeam;
            scanResp.newBeam_ref() = resp->newBeam;
            scanResp.sweepStartBeam_ref() = resp->sweepStartBeam;
            scanResp.sweepEndBeam_ref() = resp->sweepEndBeam;
          }
          // Copy beam info
          if (resp->numBeamInfo) {
            if (!scanResp.beamInfoList_ref().has_value()) {
              scanResp.beamInfoList_ref().emplace();
            }
            for (int idx = 0; idx < resp->numBeamInfo; idx++) {
              thrift::BeamInfo beamInfo;
              beamInfo.addr_ref() = folly::MacAddress::fromBinary(
                  folly::ByteRange(resp->beamInfo[idx].addr, 6)).toString();
              beamInfo.beam_ref() = resp->beamInfo[idx].beam;
              scanResp.beamInfoList_ref()->push_back(beamInfo);
            }
          }

          thriftMsg.msgType = thrift::PtMsgTypes::NB_SCAN_RESP;
          thriftMsg.scanResp = std::move(scanResp);
          scanPartialReplies.erase(std::make_pair(radioMac, token));
          // TODO: Handle the case where we have end of scan markers for
          // multiple scans in one message. For now only the last one is
          // returned, the rest are deleted.
        }

        resp = reinterpret_cast<const tgfPtScanResp*>( (uint8_t*)(resp) +
                sizeof(tgfPtScanResp) +
                (resp->numRoutes * sizeof(tgfPtRouteInfo)) );
      }
      break;
    }

    case TGF_PT_NB_CODEBOOK_FETCH: {
      thriftMsg.msgType = thrift::PtMsgTypes::NB_CODEBOOK_FETCH;
      thrift::CodebookFetchReq& dest = thriftMsg.codebookFetchReq;
      const tgfPtCodebookFetchReq& src = buffMsg->data.codebookFetchReq;
      dest.channel = src.channel;
      dest.beamStart = src.beamStart;
      dest.beamCount = src.beamCount;
      dest.stride = src.stride;
      dest.strideCount = src.strideCount;
      break;
    }

    case TGF_PT_NB_GET_FW_PARAMS_RESP: {
      bool success = true;

      if (len < ptHdrLen + sizeof(tgfPtGetFwParamsResp)) {
        LOG(ERROR)
            << "TGF_PT_NB_GET_FW_PARAMS_RESP: Too short (1). Need at least "
            << ptHdrLen + sizeof(tgfPtGetFwParamsResp) << " bytes, have " << len
            << " bytes";
        thriftMsg.msgType = thrift::PtMsgTypes::NB_INVALID;
        break;
      }
      thriftMsg.msgType = thrift::PtMsgTypes::NB_GET_FW_PARAMS_RESP;

      /* Restore 64 bit bwgdIdx */
      thriftMsg.getFwParamsResp.bwgdIdx =
          (uint64_t)buffMsg->data.getFwParamsResp.bwgdIdxL |
          ((uint64_t)buffMsg->data.getFwParamsResp.bwgdIdxH << 32);
      /* Mask off lower 31 bits (NOTE: To be deprecated in a future release) */
      thriftMsg.getFwParamsResp.currentBWGD =
          (buffMsg->data.getFwParamsResp.bwgdIdxL & 0x7fffffff);

      switch (buffMsg->data.getFwParamsResp.resp_type) {
        case TGF_PT_FW_OPT_NODE_PARAMS: {
          thriftMsg.getFwParamsResp.fwParamsType =
              thrift::FwParamsType::FW_PARAMS_NODE_FW_CFG;

          success = fbFwParamsUnpack(
              buffMsg->data.getFwParamsResp.data.optParams.data,
              buffMsg->data.getFwParamsResp.data.optParams.len,
              thriftMsg.getFwParamsResp.optParams);
          break;
        }
        case TGF_PT_FW_OPT_LINK_PARAMS: {
          thriftMsg.getFwParamsResp.fwParamsType =
              thrift::FwParamsType::FW_PARAMS_LINK_FW_CFG;

          success = fbFwParamsUnpack(
              buffMsg->data.getFwParamsResp.data.optParams.data,
              buffMsg->data.getFwParamsResp.data.optParams.len,
              thriftMsg.getFwParamsResp.optParams);
          break;
        }
        case TGF_PT_FW_PARAMS_TYPE_INVALID:
          thriftMsg.getFwParamsResp.fwParamsType =
              thrift::FwParamsType::FW_PARAMS_INVALID;
          break;
        default:
          break;
      }

      if (!success) {
        LOG(ERROR) << "Unable to handle received GET_FW_PARAMS_RESP from FW "
                      "with type: "
                   << buffMsg->data.getFwParamsResp.resp_type;
        return thriftMsg;
      }
      break;
    }

    case TGF_PT_NB_SET_FW_PARAMS_RESP: {
      thriftMsg.msgType = thrift::PtMsgTypes::NB_SET_FW_PARAMS_RESP;

      thriftMsg.setFwParamsResp.status =
          (buffMsg->data.setFwParamsResp.status != 0);
      break;
    }

    case TGF_PT_NB_ROUTING_INFO:
      thriftMsg.msgType = thrift::PtMsgTypes::NB_ROUTING_INFO;

      thriftMsg.fwRoutingInfo.addr =
          folly::MacAddress::fromBinary(
              folly::ByteRange(buffMsg->data.fwRoutingInfo.addr, 6))
              .toString();
      thriftMsg.fwRoutingInfo.mcs =
          static_cast<int16_t>(buffMsg->data.fwRoutingInfo.mcs);
      break;

    case TGF_PT_NB_TOPO_OPENR_ADJ_REQ:
      thriftMsg.msgType = thrift::PtMsgTypes::NB_OPENR_ADJ_REQ;
      break;

    case TGF_PT_NB_PPS_HTSF_INFO:
      thriftMsg.msgType = thrift::PtMsgTypes::NB_PPS_HTSF_INFO;

      thriftMsg.htsfInfo.ppsHwTsfUs =
          static_cast<int64_t>(buffMsg->data.ppsHtsfInfo.ppsHwTsfUs);
      thriftMsg.htsfInfo.ppsHwTsfNs = buffMsg->data.ppsHtsfInfo.ppsHwTsfNs;
      thriftMsg.htsfInfo.ppsSwTsfUs =
          static_cast<int64_t>(buffMsg->data.ppsHtsfInfo.ppsSwTsfUs);
      thriftMsg.htsfInfo.ppsSwTsfNs = buffMsg->data.ppsHtsfInfo.ppsSwTsfNs;
      thriftMsg.htsfInfo.delayEstNs = buffMsg->data.ppsHtsfInfo.delayEstNs;
      break;

    case TGF_PT_NB_PHY_TPC_ADJ_TBL_FETCH:
      thriftMsg.msgType = thrift::PtMsgTypes::NB_PHY_TPC_ADJ_TBL_FETCH;
      thriftMsg.tpcAdjTblFetchReq.channel =
          buffMsg->data.tpcAdjTblFetchReq.channel;
      break;

    default:
      LOG(ERROR) << "unknown, buffMsg->msgType: " << buffMsg->msgType;
      thriftMsg.msgType = thrift::PtMsgTypes::NB_INVALID;
      return thriftMsg;
  }

  return thriftMsg;
}

thrift::StatsSample
createSample(
    const uint8_t macAddr[6],
    const std::string& name,
    int64_t value,
    int64_t tsf) {

  const std::string& key = folly::to<std::string>(
      kSamplePrefix,
      folly::MacAddress::fromBinary(folly::ByteRange(macAddr, 6)),
      ".",
      name);

  thrift::StatsSample statsSample;
  statsSample.key = key;
  statsSample.value = value;
  statsSample.tsf = tsf;
  return statsSample;
}

#define PUSH_KV(NAME) \
  samps.push_back(    \
      createSample(buffSample->addr, #NAME, buffSample->data.NAME, tsf));

#define PUSH_KV_VEC(VAR, NAME) \
  samps.push_back(             \
      createSample(buffSample->addr, NAME, buffSample->data.VAR, tsf));

#define PUSH_KV_STATS(VAR, NAME) \
  samps.push_back(createSample(buffSample->addr, NAME, VAR, tsf));

thrift::Stats
getStats(const tgfStatsMsgHdr* buffStats, size_t len) {
  thrift::Stats thriftStats;

  size_t statsHdrLen = sizeof(tgfStatsMsgHdr);
  if (len < statsHdrLen) {
    // return empty stats
    return thriftStats;
  } else {
    len -= statsHdrLen;
  }

  std::vector<thrift::StatsSample>& samps = thriftStats.statsSamples;

  // point buffSample to end of buffStats struct
  // actual stats sample starts right after the tgfStatsMsgHdr
  const tgfStatsSample* buffSample = (const tgfStatsSample*)(buffStats + 1);
  bool parseError = false;
  for (uint32_t i = 0; i < buffStats->numSamples; i++) {

    size_t statHdrLen = offsetof(tgfStatsSample, data);
    if (len < statHdrLen) {
      LOG(ERROR) << "parseError";
      break;
    } else {
      len -= statHdrLen;
    }

    // tsf is fw uint64_t timing in us
    // truncate msb of tsf to fit in int64_t, using 63 bits
    // the real values from hardware would be far less than 63 bits
    int64_t tsf = static_cast<int64_t>(buffSample->tsfL) +
                  (static_cast<int64_t>(buffSample->tsfH & 0x7fffffff) << 32);
    // parse according to specific sample type
    size_t sampDataLen = 0;
    switch (buffSample->type) {
      case TGF_STATS_STA_PKT:
        sampDataLen = sizeof(tgfStatsStaPkt);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "staPkt.tsf", tsf, tsf));
          PUSH_KV(staPkt.txOk);
          PUSH_KV(staPkt.txFail);
          PUSH_KV(staPkt.rxOk);
          PUSH_KV(staPkt.rxFail);
          PUSH_KV(staPkt.rxPlcpFil);
          PUSH_KV(staPkt.perE6);
          PUSH_KV(staPkt.mcs);
          PUSH_KV(staPkt.txBa);
          PUSH_KV(staPkt.txPpdu);
          PUSH_KV(staPkt.rxBa);
          PUSH_KV(staPkt.rxPpdu);
          PUSH_KV(staPkt.txPowerIndex);
          PUSH_KV(staPkt.txLifetimeExp);
          PUSH_KV(staPkt.rxDiscBuf);
          PUSH_KV(staPkt.rxDiscEnc);
          PUSH_KV(staPkt.rxDiscRa);
          PUSH_KV(staPkt.rxDiscUnexp);
          PUSH_KV(staPkt.txSlotTime);
          PUSH_KV(staPkt.txAirTime);
          PUSH_KV(staPkt.linkAvailable);
          PUSH_KV(staPkt.txSlotEff);
          PUSH_KV(staPkt.mgmtLinkUp);
          PUSH_KV(staPkt.rxPerE6);
          PUSH_KV(staPkt.txMpduCount);
          PUSH_KV(staPkt.rxMpduCount);
        }
        break;

      case TGF_STATS_TEST_B:
        sampDataLen = sizeof(tgfStatsTestB);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "testB.tsf", tsf, tsf));
          PUSH_KV(testB.txOk);
          PUSH_KV(testB.txFail);
          PUSH_KV(testB.rxOk);
          PUSH_KV(testB.rxFail);
          PUSH_KV(testB.rxPlcpFil);
        }
        break;

      case TGF_STATS_GPS:
        sampDataLen = sizeof(tgfStatsGps);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(createSample(buffSample->addr, "gps.tsf", tsf, tsf));
          PUSH_KV(gps.numPpsErr);
          PUSH_KV(gps.driverDelay);
          PUSH_KV(gps.maxDriverDelay);
          PUSH_KV(gps.numTimelineErr);
          PUSH_KV(gps.numMissedSec);
          PUSH_KV(gps.ppsJitter);
          PUSH_KV(gps.maxPpsJitter);
          PUSH_KV(gps.tsfDrift);
          // Pack TSF into int64_t (actual value is less than 63 bits)
          int64_t ppsHwTsf = TGF_PACK_INT64(
              buffSample->data.gps.ppsHwTsfL, buffSample->data.gps.ppsHwTsfH);
          samps.push_back(
              createSample(buffSample->addr, "gps.ppsHwTsf", ppsHwTsf, tsf));
          PUSH_KV(gps.ppsHwTsfNs);
          int64_t ppsSwTsf = TGF_PACK_INT64(
              buffSample->data.gps.ppsSwTsfL, buffSample->data.gps.ppsSwTsfH);
          samps.push_back(
              createSample(buffSample->addr, "gps.ppsSwTsf", ppsSwTsf, tsf));
          PUSH_KV(gps.ppsSwTsfNs);
        }
        break;

      case TGF_STATS_PHYSTATUS_MGMT_HF:
        sampDataLen = sizeof(tgfStatsPhystatus);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          const tgfStatsPhystatus* phystatus = &buffSample->data.phystatus;
          PUSH_KV_STATS(
              phystatus->snrEstQ8, "phystatusmgmtHF.ssnrEst");
          PUSH_KV_STATS(phystatus->srssi, "phystatusmgmtHF.srssi");
        }
        break;

      case TGF_STATS_PHYSTATUS:
        sampDataLen = sizeof(tgfStatsPhystatus);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          const tgfStatsPhystatus* phystatus = &buffSample->data.phystatus;
          std::string key;
          if (phystatus->type == STATS_TYPE_DATA) {
            key = "phystatusdata.";
          } else if (phystatus->type == STATS_TYPE_MGMT) {
            // for historical reasons, just call it phystatus
            key = "phystatus.";
          }
          samps.push_back(createSample(
              buffSample->addr, folly::to<std::string>(key, "tsf"), tsf, tsf));
          PUSH_KV_STATS(
              TGF_ROUND(phystatus->snrEstQ8, Q(8)),
              folly::to<std::string>(key, "ssnrEst"));
          PUSH_KV_STATS(
              TGF_ROUND(phystatus->postSNRdBQ1, Q(1)),
              folly::to<std::string>(key, "spostSNRdB"));
          PUSH_KV_STATS(phystatus->srssi, folly::to<std::string>(key, "srssi"));
          PUSH_KV_STATS(
              phystatus->gainIndexIf,
              folly::to<std::string>(key, "gainIndexIf"));
          PUSH_KV_STATS(
              phystatus->gainIndexRf,
              folly::to<std::string>(key, "gainIndexRf"));
          PUSH_KV_STATS(
              phystatus->rawAdcRssi, folly::to<std::string>(key, "rawAdcRssi"));
          PUSH_KV_STATS(
              phystatus->rxStartNormalized,
              folly::to<std::string>(key, "rxStartNormalized"));
          PUSH_KV_STATS(
              phystatus->maxGainIndexIf,
              folly::to<std::string>(key, "maxGainIndexIf"));
          PUSH_KV_STATS(
              phystatus->maxGainIndexRf,
              folly::to<std::string>(key, "maxGainIndexRf"));
          PUSH_KV_STATS(
              phystatus->numTotalSyndromes,
              folly::to<std::string>(key, "numTotalSyndromes"));
          PUSH_KV_STATS(
              phystatus->numTotalCodewords,
              folly::to<std::string>(key, "numTotalCodewords"));
          PUSH_KV_STATS(
              phystatus->plcpLength, folly::to<std::string>(key, "plcpLength"));
          PUSH_KV_STATS(
              phystatus->ldpcIterations,
              folly::to<std::string>(key, "ldpcIterations"));
          PUSH_KV_STATS(phystatus->rxMcs, folly::to<std::string>(key, "rxMcs"));
          PUSH_KV_STATS(phystatus->dbg16, folly::to<std::string>(key, "dbg16"));
        }
        break;

      case TGF_STATS_PHYSTATUS_PRINT_HF:
        sampDataLen = sizeof(tgfStatsPhystatus);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          const tgfStatsPhystatus* phystatus = &buffSample->data.phystatus;
          std::string type;
          len -= sampDataLen;
          if (phystatus->type == STATS_TYPE_DATA) {
            type = "data, ";
          } else if (phystatus->type == STATS_TYPE_MGMT) {
            type = "mgmt, ";
          }
          LOG(INFO)
              << "phy: " << type << tsf << ", " << phystatus->rxStartNormalized
              << ", " << phystatus->snrEstQ8 << ", "
              << phystatus->postSNRdBQ1 << ", "
              << (int)phystatus->gainIndexIf << ", "
              << (int)phystatus->gainIndexRf << ", " << phystatus->rawAdcRssi
              << ", " << phystatus->numTotalSyndromes << ", "
              << phystatus->numTotalCodewords << ", "
              << phystatus->ldpcIterations << ", " << phystatus->rxMcs << ", "
              << phystatus->dbg16;
        }
        break;

      case TGF_STATS_LA_TPC_PRINT_HF:
        sampDataLen = sizeof(tgfStatsLaTpc);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          const tgfStatsLaTpc* latpcStats = &buffSample->data.latpcStats;
          const std::string& key =
              folly::to<std::string>(folly::MacAddress::fromBinary(
                  folly::ByteRange(buffSample->addr, 6)));
          LOG(INFO) << tsf << ", " << key.substr(12, 5)
                    << ", " // xx:xx:xx:xx:YY:YY
                    << latpcStats->laTpcOffsetdBQ24 << ", "
                    << latpcStats->noTrafficCountSF << ", "
                    << latpcStats->nIterOrNumOfLongCalibSlots << ", "
                    << latpcStats->nSyn << ", " << latpcStats->nCW
                    << ", " << (int)latpcStats->txPowerIndex << ", "
                    << (int)latpcStats->mcs << ", " << latpcStats->snrAndRssi
                    << ", " << latpcStats->txOk << ", " << latpcStats->txFail
                    << ", " << latpcStats->rxOk << ", " << latpcStats->rxFail
                    << ", " << latpcStats->hfLogsInfo;
        }
        break;

      case TGF_STATS_MGMT_TX:
        sampDataLen = sizeof(tgfStatsMgmt);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "mgmtTx.tsf", tsf, tsf));
          PUSH_KV(mgmtTx.bfTrainingReq);
          PUSH_KV(mgmtTx.bfTrainingRsp);
          PUSH_KV(mgmtTx.bfTrainingRspAck);
          PUSH_KV(mgmtTx.bfTrainingUrx);
          PUSH_KV(mgmtTx.assocReq);
          PUSH_KV(mgmtTx.assocRsp);
          PUSH_KV(mgmtTx.assocRspAck);
          PUSH_KV(mgmtTx.keepAlive);
          PUSH_KV(mgmtTx.heartBeat);
          PUSH_KV(mgmtTx.uplinkBwreq);
          PUSH_KV(mgmtTx.bfRetrainingReq);
          PUSH_KV(mgmtTx.bfRetrnUrxChgReq);
          PUSH_KV(mgmtTx.bfRetrnUrxChgReqAck);
          PUSH_KV(mgmtTx.dissocReq);
        }
        break;

      case TGF_STATS_MGMT_RX:
        sampDataLen = sizeof(tgfStatsMgmt);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "mgmtRx.tsf", tsf, tsf));
          PUSH_KV(mgmtRx.bfTrainingReq);
          PUSH_KV(mgmtRx.bfTrainingRsp);
          PUSH_KV(mgmtRx.bfTrainingRspAck);
          PUSH_KV(mgmtRx.bfTrainingUrx);
          PUSH_KV(mgmtRx.assocReq);
          PUSH_KV(mgmtRx.assocRsp);
          PUSH_KV(mgmtRx.assocRspAck);
          PUSH_KV(mgmtRx.keepAlive);
          PUSH_KV(mgmtRx.heartBeat);
          PUSH_KV(mgmtRx.uplinkBwreq);
          PUSH_KV(mgmtRx.bfRetrainingReq);
          PUSH_KV(mgmtRx.bfRetrnUrxChgReq);
          PUSH_KV(mgmtRx.bfRetrnUrxChgReqAck);
          PUSH_KV(mgmtRx.dissocReq);
        }
        break;

      case TGF_STATS_SLOT:
        sampDataLen = sizeof(tgfStatsSlot);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(createSample(buffSample->addr, "slot.tsf", tsf, tsf));
          PUSH_KV(slot.numOfTxBfSlotsPgmrd);
          PUSH_KV(slot.numOfRxBfSlotsPgmrd);
          PUSH_KV(slot.numOfTxDataSlotsPgmrd);
          PUSH_KV(slot.numOfRxDataSlotsPgmrd);
          PUSH_KV(slot.numOfShortCalibSlots);
          PUSH_KV(slot.numOfLongCalibSlots);
        }
        break;

      case TGF_STATS_BWHAN_SYS:
        sampDataLen = sizeof(tgfStatsBwhanSys);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "bwhanSys.tsf", tsf, tsf));
          PUSH_KV(bwhanSys.totalTxAssocTime);
          PUSH_KV(bwhanSys.totalRxAssocTime);
        }
        break;

      case TGF_STATS_BWHAN_LINK:
        sampDataLen = sizeof(tgfStatsBwhanLink);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "bwhanLink.tsf", tsf, tsf));
          PUSH_KV(bwhanLink.totalTxDataTimeUs);
          PUSH_KV(bwhanLink.totalRxDataTimeUs);
          PUSH_KV(bwhanLink.totalTxDataSlots);
          PUSH_KV(bwhanLink.totalRxDataSlots);
          PUSH_KV(bwhanLink.currTxTimePercent);
          PUSH_KV(bwhanLink.currRxTimePercent);
          PUSH_KV(bwhanLink.currTxSlotPercent);
          PUSH_KV(bwhanLink.currRxSlotPercent);
          PUSH_KV(bwhanLink.txCtrlFallbackCount);
          PUSH_KV(bwhanLink.rxCtrlFallbackCount);
          PUSH_KV(bwhanLink.localBytesPending);
          PUSH_KV(bwhanLink.localArrivalRate);
          PUSH_KV(bwhanLink.peerBytesPending);
          PUSH_KV(bwhanLink.peerArrivalRate);
        }
        break;

      case TGF_STATS_LA_HIGH_SPEED:
        sampDataLen = sizeof(tgfStatsLaHighSpeed);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          PUSH_KV(laHighSpeed.mcs);
        }
        break;

      case TGF_STATS_TPC_HIGH_SPEED:
        sampDataLen = sizeof(tgfStatsTpcHighSpeed);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          PUSH_KV(tpcHighSpeed.txPowerIndex);
        }
        break;

      case TGF_STATS_TPC:
        sampDataLen = sizeof(tgfStatsTpc);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "tpcStats.tsf", tsf, tsf));

          const tgfStatsTpc* tpcStats = &buffSample->data.tpcStats;
          PUSH_KV_STATS(
              (tpcStats->effSnrQ10 + Q(9)) / Q(10), "tpcStats.effSnr");
          PUSH_KV_STATS(
              (tpcStats->tsIirRssiQ10 + Q(9)) / Q(10), "tpcStats.tsIirRssi");
          PUSH_KV_STATS(
              (tpcStats->tsIirRssiTargetMgmtQ10 + Q(9)) / Q(10),
              "tpcStats.tsIirRssiTargetMgmt");
        }
        break;

      case TGF_STATS_LA_TPC:
        sampDataLen = sizeof(tgfStatsLaTpc);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "latpcStats.tsf", tsf, tsf));

          PUSH_KV(latpcStats.laTpcOffsetdBQ24);
          PUSH_KV(latpcStats.noTrafficCountSF);
          PUSH_KV(latpcStats.numSFsAtLowerLimit);
          PUSH_KV(latpcStats.nCW);
          PUSH_KV(latpcStats.nSyn);
          PUSH_KV(latpcStats.nIter);
          PUSH_KV(latpcStats.synPERQ16);
          PUSH_KV(latpcStats.txPowerIndex);
          PUSH_KV(latpcStats.maxTxPowerIndex);
        }
        break;

      case TGF_STATS_MAX_AGC:
        sampDataLen = sizeof(tgfStatsAgc);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "maxAgcStats.tsf", tsf, tsf));
          bool isZero = true;
          for (unsigned int i = 0; i < sizeof(tgEthAddr); i++) {
            if (buffSample->addr[i] != 0) {
              isZero = false;
              break;
            }
          }
          if (isZero) {
            PUSH_KV(maxAgcStats.maxGainIndexIf);
            PUSH_KV(maxAgcStats.maxGainIndexRf);
            PUSH_KV_STATS(
                (buffSample->data.maxAgcStats.minRssiQ8 + Q(7)) / Q(8),
                "maxAgcStats.minRssi");
          } else {
            PUSH_KV(maxAgcStats.maxGainIndexIf);
            PUSH_KV(maxAgcStats.maxGainIndexRf);
            PUSH_KV(maxAgcStats.numBwgdsInFreeRun);
            PUSH_KV(maxAgcStats.iirAvgRssi);
            PUSH_KV_STATS(
                (buffSample->data.maxAgcStats.minRssiQ8 + Q(7)) / Q(8),
                "maxAgcStats.minRssi");
          }
        }
        break;

      case TGF_STATS_MAX_AGC_HIST:
        sampDataLen = sizeof(tgfStatsAgc);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "maxAgcHistStats.tsf", tsf, tsf));

          for (int rssiIdx = 0; rssiIdx < AGC_RSSI_HIST_SIZE; rssiIdx++) {
            if (buffSample->data.maxAgcStats.rssiHistMgmt[rssiIdx]) {
              const std::string& key = folly::to<std::string>(
                  "maxAgcHistStats.rssiHistMgmt[", rssiIdx, "]");
              PUSH_KV_VEC(maxAgcStats.rssiHistMgmt[rssiIdx], key);
            }
          }
          for (int rssiIdx = 0; rssiIdx < AGC_RSSI_HIST_SIZE; rssiIdx++) {
            if (buffSample->data.maxAgcStats.rssiHistData[rssiIdx]) {
              const std::string& key = folly::to<std::string>(
                  "maxAgcHistStats.rssiHistData[", rssiIdx, "]");
              PUSH_KV_VEC(maxAgcStats.rssiHistData[rssiIdx], key);
            }
          }
        }
        break;

      case TGF_STATS_BF:
        sampDataLen = sizeof(tgfStatsBf);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(createSample(buffSample->addr, "bf.tsf", tsf, tsf));

          PUSH_KV(bf.mode);
          PUSH_KV(bf.msgType);
          PUSH_KV(bf.txBeamIdx);
          PUSH_KV(bf.rxBeamIdx);
          PUSH_KV(bf.pktLqm);
          PUSH_KV(bf.pktRssi);
          PUSH_KV(bf.rxStart);
          PUSH_KV(bf.dblPktIdx);
          PUSH_KV(bf.frmNumBfWin);
          PUSH_KV(bf.frmNumInSf);
        }
        break;

      case TGF_STATS_MTPO:
        sampDataLen = sizeof(tgfStatsMtpo);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "mtpoStats.tsf", tsf, tsf));

          const tgfStatsMtpo* mtpoStats =
              &buffSample->data.mtpoStats;
          PUSH_KV_STATS(mtpoStats->mtpoSector, "mtpoStats.mtpoSector");
          PUSH_KV_STATS(
              mtpoStats->mtpoPhaseChanged, "mtpoStats.mtpoPhaseChanged");
          PUSH_KV_STATS(mtpoStats->mtpoStatus, "mtpoStats.mtpoStatus");

          for (uint32_t phaseIdx = 0; phaseIdx < TGF_MAX_NUM_MTPO_PHASES;
               phaseIdx++) {
            const std::string& key = folly::to<std::string>(
                    "mtpoStats.mtpoPhases[", phaseIdx, "]");
            PUSH_KV_STATS(mtpoStats->mtpoPhases[phaseIdx], key);
          }

          for (uint32_t sectorIdx = 0; sectorIdx < TGF_MTPO_SWEEP_SECTORS;
               sectorIdx++) {
            const std::string& key = folly::to<std::string>(
                    "mtpoStats.mtpoSingleTileSectorGolayPeak[", sectorIdx, "]");
            PUSH_KV_STATS(mtpoStats->mtpoSectorSweepGolayPeak[sectorIdx], key);
          }

          for (uint32_t phaseIdx = 0; phaseIdx < TGF_MTPO_SWEEP_PHASES;
               phaseIdx++) {
            const std::string& key = folly::to<std::string>(
                    "mtpoStats.mtpoMultiTilePhaseGolayPeak[", phaseIdx, "]");
            PUSH_KV_STATS(mtpoStats->mtpoPhaseSweepGolayPeak[phaseIdx], key);
          }
        }
        break;

      case TGF_STATS_PHY_PERIODIC:
        sampDataLen = sizeof(tgfStatsBf);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "phyPeriodic.tsf", tsf, tsf));

          PUSH_KV(phyPeriodic.txBeamIdx);
          PUSH_KV(phyPeriodic.rxBeamIdx);
          PUSH_KV(phyPeriodic.txRficBitmap);
          PUSH_KV(phyPeriodic.rxRficBitmap);
          PUSH_KV(phyPeriodic.pktLqm);
          PUSH_KV(phyPeriodic.pktRssi);
        }
        break;

      case TGF_STATS_RECV_MGMT:
        sampDataLen = sizeof(tgfStatsRecvMgmt);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "recvMgmt.tsf", tsf, tsf));
          PUSH_KV(recvMgmt.actionCode);
          PUSH_KV(recvMgmt.rxstart);
          PUSH_KV(recvMgmt.size);
          PUSH_KV(recvMgmt.beamRx);
          PUSH_KV_STATS(
              TGF_ROUND(buffSample->data.recvMgmt.phyStatus.snrEstQ8, Q(8)),
              "recvMgmt.phyStatus.ssnrEst");
          PUSH_KV_STATS(
              TGF_ROUND(buffSample->data.recvMgmt.phyStatus.postSNRdBQ1, Q(1)),
              "recvMgmt.phyStatus.spostSNRdB");
          PUSH_KV(recvMgmt.phyStatus.srssi);
          PUSH_KV(recvMgmt.phyStatus.gainIndexIf);
          PUSH_KV(recvMgmt.phyStatus.gainIndexRf);
          PUSH_KV(recvMgmt.phyStatus.rawAdcRssi);
          PUSH_KV(recvMgmt.phyStatus.rxStartNormalized);
          PUSH_KV(recvMgmt.phyStatus.numTotalSyndromes);
          PUSH_KV(recvMgmt.phyStatus.numTotalCodewords);
          PUSH_KV(recvMgmt.phyStatus.plcpLength);
          PUSH_KV(recvMgmt.phyStatus.ldpcIterations);
          PUSH_KV_STATS(
              buffSample->data.recvMgmt.phyStatus.dbg16,
              "recvMgmt.phyStatus.AFC");
        }
        break;

      case TGF_STATS_MGMT_DATA:
        sampDataLen = sizeof(tgfStatsMgmtData);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "mgmtData.tsf", tsf, tsf));

          // push this array as mgmtData.w[n] as n key/value/tsf
          for (uint32_t wIdx = 0; wIdx < TGF_STATS_MGMT_DATA_W_LEN; wIdx++) {
            std::string key = folly::sformat("mgmtData.w[{}]", wIdx);
            samps.push_back(createSample(
                buffSample->addr, key, buffSample->data.mgmtData.w[wIdx], tsf));
          }
        }
        break;

      case TGF_STATS_MEM:
        sampDataLen = sizeof(tgfStatsMem);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(createSample(buffSample->addr, "mem.tsf", tsf, tsf));
          PUSH_KV(mem.mallocSize);
          PUSH_KV(mem.mfreeSize);
        }
        break;

      case TGF_STATS_MISC_SYS:
        sampDataLen = sizeof(tgfStatsMiscSys);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "miscSys.tsf", tsf, tsf));
          PUSH_KV(miscSys.numMissedSfm);
          PUSH_KV(miscSys.malloc);
          PUSH_KV(miscSys.free);
          PUSH_KV(miscSys.numFrameTimer);
          PUSH_KV(miscSys.rfToGps);
          PUSH_KV(miscSys.gpsToRf);
          PUSH_KV(miscSys.cpuLoadAvg);
          PUSH_KV(miscSys.rftemperature0);
          PUSH_KV(miscSys.rftemperature1);
          PUSH_KV(miscSys.rftemperature2);
          PUSH_KV(miscSys.rftemperature3);
          PUSH_KV(miscSys.iftemperature);
          PUSH_KV(miscSys.getPktBuf);
          PUSH_KV(miscSys.recvMgmt);
          PUSH_KV(miscSys.freePktBuf);
          PUSH_KV(miscSys.sendMgmtCB);
          PUSH_KV(miscSys.txstatusNoSta);
          PUSH_KV(miscSys.mgmtRxIncorrectHdr);
          PUSH_KV(miscSys.numBcastImTrnReqSent);
          PUSH_KV(miscSys.numBcastImTrnReqRecvd);
          PUSH_KV(miscSys.numIncorrectBcastPktsRecvd);
        }
        break;

      case TGF_STATS_MISC_LINK:
        sampDataLen = sizeof(tgfStatsMiscLink);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "miscLink.tsf", tsf, tsf));
          PUSH_KV(miscLink.dataTxSlotDur);
          PUSH_KV(miscLink.dataRxSlotDur);
          PUSH_KV(miscLink.bfTxSlotDur);
          PUSH_KV(miscLink.bfRxSlotDur);
          PUSH_KV(miscLink.txstatusFlagAck);
          PUSH_KV(miscLink.txstatusLifetimeExp);
          PUSH_KV(miscLink.txstatusFlushed);
          PUSH_KV(miscLink.currentLinkState);
          PUSH_KV(miscLink.mtpoRunCounter);
          PUSH_KV(miscLink.mtpoSuccessCounter);
          PUSH_KV(miscLink.mtpoApplyNewPhaseCounter);
          PUSH_KV(miscLink.mtpoRejectCounter);
          PUSH_KV(miscLink.mtpoFailCounter);
          PUSH_KV(miscLink.mtpoResponderTimeoutCounter);
          PUSH_KV(miscLink.mtpoCurrentPhases);
        }
        break;

      case TGF_STATS_TSF:
        sampDataLen = sizeof(tgfStatsTsf);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(createSample(buffSample->addr, "tsf.tsf", tsf, tsf));
          PUSH_KV(tsf.syncModeGps);
          PUSH_KV(tsf.syncModeRf);
          PUSH_KV(tsf.numRfFix);
          PUSH_KV(tsf.numGpsFix);
          PUSH_KV(tsf.rfDrift);
          PUSH_KV(tsf.sumRfFix);
          PUSH_KV(tsf.sumGpsFix);
          PUSH_KV(tsf.offsetL);
          PUSH_KV(tsf.offsetH);
          PUSH_KV(tsf.driftPerWin);
        }
        break;

      case TGF_STATS_LIFETIME_EXPIRED:
        sampDataLen = sizeof(tgfStatsLifetime);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "lifetimeExpired.tsf", tsf, tsf));
          for (int action = 0; action < TGF_NUM_MESSAGE_ACTIONS; action++) {
            if (buffSample->data.lifetime.counter[action] != 0) {
              const std::string& key =
                  folly::to<std::string>("lifetime.expired[", action, "]");

              PUSH_KV_VEC(lifetime.counter[action], key);
            }
          }
        }
        break;

      case TGF_STATS_LIFETIME_OK:
        sampDataLen = sizeof(tgfStatsLifetime);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "lifetimeOk.tsf", tsf, tsf));
          for (int action = 0; action < TGF_NUM_MESSAGE_ACTIONS; action++) {
            if (buffSample->data.lifetime.counter[action] != 0) {
              const std::string& key =
                  folly::to<std::string>("lifetime.ok[", action, "]");

              PUSH_KV_VEC(lifetime.counter[action], key);
            }
          }
        }
        break;

      case TGF_STATS_CALIBRATE:
        sampDataLen = sizeof(tgfStatsCalibrate);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "calibrate.tsf", tsf, tsf));
          for (int idx = 0; idx < TGF_STATS_CALIB_NUM; idx++) {
            const std::string& key =
                folly::to<std::string>("calibrate.idx[", idx, "]");
            PUSH_KV_VEC(calibrate.idx[idx], key);
          }
        }
        break;

      case TGF_STATS_CHN:
        sampDataLen = sizeof(tgfStatsChn);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(createSample(buffSample->addr, "tsf.tsf", tsf, tsf));
          PUSH_KV(chn.maxTickCodebookFetch)
          PUSH_KV(chn.maxTickChannelChange)
          PUSH_KV(chn.errInvalidChnIn)
          PUSH_KV(chn.errSetChn)
          PUSH_KV(chn.errAssoc)
          PUSH_KV(chn.state)
          PUSH_KV(chn.channel)
          PUSH_KV(chn.configuredChannel)
          PUSH_KV(chn.numSwitches)
        }
        break;

      case TGF_STATS_LINK_DOWN:
        sampDataLen = sizeof(tgfStatsLinkDown);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(createSample(buffSample->addr, "tsf.tsf", tsf, tsf));
          PUSH_KV(linkDown.cause)
        }
        break;

      case TGF_STATS_SECURITY:
        sampDataLen = sizeof(tgfStatsSecurity);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "security.tsf", tsf, tsf));
          PUSH_KV(security.status)
        }
        break;

      case TGF_STATS_RADIO:
        sampDataLen = sizeof(tgfStatsRadio);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "radio.tsf", tsf, tsf));
          for (int idx = 0; idx < TGF_STATS_NUM_RADIO; idx++) {
            const std::string& key = folly::to<std::string>(
                "radioStats.radioTestReadErr[", idx, "]");
            PUSH_KV_VEC(radioStats.radioTestReadErr[idx], key);
            const std::string& key2 = folly::to<std::string>(
                "radioStats.radioTestWriteErr[", idx, "]");
            PUSH_KV_VEC(radioStats.radioTestWriteErr[idx], key2);
          }
          PUSH_KV(radioStats.radioTestTotalRuns);
          PUSH_KV(radioStats.radioCalibrationRuns);
          PUSH_KV(radioStats.radioCalibrationFailures);
        }
        break;

      case TGF_STATS_BF_SCAN:
        sampDataLen = sizeof(tgfStatsBfScan);
        if (len < sampDataLen) {
          parseError = true;
        } else {
          len -= sampDataLen;
          samps.push_back(
              createSample(buffSample->addr, "bfScanStats.tsf", tsf, tsf));
          PUSH_KV(bfScanStats.numOfScanReqRecvd);
          PUSH_KV(bfScanStats.numOfScanCompleted);
          PUSH_KV(bfScanStats.numOfScanDropped);
          PUSH_KV(bfScanStats.numOfScanAborted);
          PUSH_KV(bfScanStats.numOfScanAsInitiator);
          PUSH_KV(bfScanStats.numOfScanAsResponder);
          PUSH_KV(bfScanStats.numOfPbfScan);
          PUSH_KV(bfScanStats.numOfImScan);
          PUSH_KV(bfScanStats.numOfRtCalScan);
          PUSH_KV(bfScanStats.numOfVbsScan);
          PUSH_KV(bfScanStats.numOfCbfScan);
          PUSH_KV(bfScanStats.txScanBeams.selfAzimuthBeam);
          PUSH_KV(bfScanStats.txScanBeams.rtCalTopPanelBeam);
          PUSH_KV(bfScanStats.txScanBeams.rtCalBottomPanelBeam);
          PUSH_KV(bfScanStats.txScanBeams.vbsBeam);
          PUSH_KV(bfScanStats.txScanBeams.cbfNullingBeam);
          PUSH_KV(bfScanStats.rxScanBeams.selfAzimuthBeam);
          PUSH_KV(bfScanStats.rxScanBeams.rtCalTopPanelBeam);
          PUSH_KV(bfScanStats.rxScanBeams.rtCalBottomPanelBeam);
          PUSH_KV(bfScanStats.rxScanBeams.vbsBeam);
          PUSH_KV(bfScanStats.rxScanBeams.cbfNullingBeam);
          PUSH_KV(bfScanStats.txScanBeams.codebookBeam);
          PUSH_KV(bfScanStats.rxScanBeams.codebookBeam);
          PUSH_KV(bfScanStats.localTxPower);
        }
        break;

      default:
        parseError = true;
        LOG(ERROR) << "unknown, buffSample->type: " << buffSample->type;
    }

    if (parseError) {
      LOG(ERROR) << "Failed to parse firmware stats";
      break;
    } else {
      const uint8_t* ptr = reinterpret_cast<const uint8_t*>(buffSample) +
                           offsetof(tgfStatsSample, data) + sampDataLen;
      buffSample = reinterpret_cast<const tgfStatsSample*>(ptr);
    }
  }

  if (!thriftStats.statsSamples.empty()) {
    // dropped sample is not link stats, setting link mac = 0
    uint8_t macAddr[6] = {0};
    // use last known sample tsf value for droppedSamples
    int64_t tsf = thriftStats.statsSamples.back().tsf;
    // Number of dropped samples due to full buffer in wireless-fw
    samps.push_back(createSample(
        macAddr, "droppedSamples", buffStats->droppedSamples, tsf));
    // Buffer index of this buffer
    // This will detect if any stats buffer was sent by wireless-fw
    // but got dropped by pcie, DriverIf, and other higher layers
    samps.push_back(createSample(macAddr, "buffIdx", buffStats->buffIdx, tsf));
    // number of stats (key-value) exposed for this buffer
    // this will detect if any stat gets dropped by layer above DriverIf
    samps.push_back(createSample(
        macAddr, "numBuffKv", thriftStats.statsSamples.size() + 1, tsf));
  }

  return thriftStats;
}
} // namespace terragraph
} // namespace facebook
