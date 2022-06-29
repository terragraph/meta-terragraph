/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "../PassThru.h"
#include <fb-fw-if/fb_tg_fw_pt_if.h>
#include <folly/MacAddress.h>
#include <folly/init/Init.h>
#include <gtest/gtest.h>
#include <nl-driver-if/fb_tg_fw_driver_if.h>

// IMPORTANT M44 NOTE
//
// This file needs to support wireless-fw-interface from both master
// and firmware available from QTI that matches the firmware
// in use. Handling the non-development interfaces is done
// via the TG_ENABLE_COMPAT_M44_* defines.

using namespace facebook::terragraph;

namespace facebook {
namespace terragraph {
thrift::Stats getStats(const tgfStatsMsgHdr* buffStats, size_t len);
}
} // namespace facebook

namespace {
const int kPosNum = 12357;
const int kNegNum = -12357;
} // namespace

TEST(PtCreate, AssocMsg) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_ASSOC;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;
  thriftMsg.assoc.addr = "1:2:3:4:5:6";
  uint8_t buff[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(thriftMsg, buff);
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;
  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_SB_ASSOC, buffMsg->msgType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);
  uint8_t* addr = buffMsg->data.assoc.addr;
  EXPECT_EQ(0x01, addr[0]);
  EXPECT_EQ(0x02, addr[1]);
  EXPECT_EQ(0x03, addr[2]);
  EXPECT_EQ(0x04, addr[3]);
  EXPECT_EQ(0x05, addr[4]);
  EXPECT_EQ(0x06, addr[5]);
  EXPECT_EQ(offsetof(tgfPtMsg, data) + sizeof(tgfPtAssocMsg) + 4, len);
}

TEST(PtParse, StatsSingleSample) {
  uint8_t buff[1024];
  tgfPtMsg* ptMsg = (tgfPtMsg*)buff;
  ptMsg->msgType = TGF_PT_NB_STATS;
  ptMsg->dest = TGF_PT_DEST_E2E;
  ptMsg->cookie = kNegNum;
  ptMsg->data.statsHdr.numSamples = 1;
  ptMsg->data.statsHdr.droppedSamples = 0;
  ptMsg->data.statsHdr.buffIdx = 0;
  // fill the sample
  tgfStatsSample* sample = (tgfStatsSample*)(&ptMsg->data.statsHdr + 1);
  sample->type = TGF_STATS_TEST_B;
  sample->addr[0] = 1;
  sample->addr[1] = 2;
  sample->addr[2] = 3;
  sample->addr[3] = 0xaa;
  sample->addr[4] = 5;
  sample->addr[5] = 0xf0;
  sample->tsfL = 0x11223344;
  sample->tsfH = 0xaabbccdd;
  sample->data.testB.rxFail = 2;
  sample->data.testB.rxOk = 3;
  sample->data.testB.rxPlcpFil = 5;
  sample->data.testB.txOk = 7;
  sample->data.testB.txFail = 11;
  // get thrift obj
  thrift::PassThruMsg thriftMsg = getPtThrift(buff, sizeof(buff));
  int64_t expTsf = 0xaabbccdd11223344 & 0x7fffffffffffffff;
  EXPECT_EQ(thrift::PtMsgTypes::NB_STATS, thriftMsg.msgType);
  EXPECT_EQ(thrift::PtMsgDest::E2E, thriftMsg.dest);
  EXPECT_EQ(kNegNum, thriftMsg.cookie);
  ASSERT_EQ(9, thriftMsg.stats.statsSamples.size());
  std::vector<thrift::StatsSample>& samples = thriftMsg.stats.statsSamples;
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.tsf", samples[0].key.c_str());
  EXPECT_EQ(expTsf, samples[0].value);
  EXPECT_EQ(expTsf, samples[0].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.txOk", samples[1].key.c_str());
  EXPECT_EQ(7, samples[1].value);
  EXPECT_EQ(expTsf, samples[1].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.txFail", samples[2].key.c_str());
  EXPECT_EQ(11, samples[2].value);
  EXPECT_EQ(expTsf, samples[2].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.rxOk", samples[3].key.c_str());
  EXPECT_EQ(3, samples[3].value);
  EXPECT_EQ(expTsf, samples[3].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.rxFail", samples[4].key.c_str());
  EXPECT_EQ(2, samples[4].value);
  EXPECT_EQ(expTsf, samples[4].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.rxPlcpFil", samples[5].key.c_str());
  EXPECT_EQ(5, samples[5].value);
  EXPECT_EQ(expTsf, samples[5].tsf);
  EXPECT_STREQ("tgf.00:00:00:00:00:00.droppedSamples", samples[6].key.c_str());
  EXPECT_EQ(0, samples[6].value);
  EXPECT_EQ(expTsf, samples[6].tsf);
  EXPECT_STREQ("tgf.00:00:00:00:00:00.buffIdx", samples[7].key.c_str());
  EXPECT_EQ(0, samples[7].value);
  EXPECT_EQ(expTsf, samples[7].tsf);
  EXPECT_STREQ("tgf.00:00:00:00:00:00.numBuffKv", samples[8].key.c_str());
  EXPECT_EQ(9, samples[8].value);
  EXPECT_EQ(expTsf, samples[8].tsf);
}

TEST(PtParse, StatsTwoSample) {
  uint8_t buff[1024];
  tgfPtMsg* ptMsg = (tgfPtMsg*)buff;
  ptMsg->msgType = TGF_PT_NB_STATS;
  ptMsg->dest = TGF_PT_DEST_E2E;
  ptMsg->cookie = kPosNum;
  ptMsg->data.statsHdr.numSamples = 2;
  ptMsg->data.statsHdr.droppedSamples = 1;
  ptMsg->data.statsHdr.buffIdx = 3;
  // fill the sample
  tgfStatsSample* sample = (tgfStatsSample*)(&ptMsg->data.statsHdr + 1);
  sample->type = TGF_STATS_TEST_B;
  sample->addr[0] = 1;
  sample->addr[1] = 2;
  sample->addr[2] = 3;
  sample->addr[3] = 0xaa;
  sample->addr[4] = 5;
  sample->addr[5] = 0xf0;
  sample->tsfL = 0x11223344;
  sample->tsfH = 0xaabbccdd;
  sample->data.testB.rxFail = 2;
  sample->data.testB.rxOk = 3;
  sample->data.testB.rxPlcpFil = 5;
  sample->data.testB.txOk = 7;
  sample->data.testB.txFail = 11;
  // fill the next sample
  sample =
      (tgfStatsSample*)((uint8_t*)sample + offsetof(tgfStatsSample, data) + sizeof(tgfStatsTestB));
  sample->type = TGF_STATS_TEST_B;
  sample->addr[0] = 0xaa;
  sample->addr[1] = 0xbb;
  sample->addr[2] = 0xcc;
  sample->addr[3] = 0xdd;
  sample->addr[4] = 0xee;
  sample->addr[5] = 0xff;
  sample->tsfL = 0xaabbccdd;
  sample->tsfH = 0x01234567;
  sample->data.testB.rxFail = 1;
  sample->data.testB.rxOk = 2;
  sample->data.testB.rxPlcpFil = 3;
  sample->data.testB.txOk = 4;
  sample->data.testB.txFail = 5;
  // get thrift obj
  thrift::PassThruMsg thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(thrift::PtMsgTypes::NB_STATS, thriftMsg.msgType);
  EXPECT_EQ(thrift::PtMsgDest::E2E, thriftMsg.dest);
  EXPECT_EQ(kPosNum, thriftMsg.cookie);
  ASSERT_EQ(15, thriftMsg.stats.statsSamples.size());
  std::vector<thrift::StatsSample>& samples = thriftMsg.stats.statsSamples;
  // first testB stats
  int64_t expTsf = 0xaabbccdd11223344 & 0x7fffffffffffffff;
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.tsf", samples[0].key.c_str());
  EXPECT_EQ(expTsf, samples[0].value);
  EXPECT_EQ(expTsf, samples[0].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.txOk", samples[1].key.c_str());
  EXPECT_EQ(7, samples[1].value);
  EXPECT_EQ(expTsf, samples[1].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.txFail", samples[2].key.c_str());
  EXPECT_EQ(11, samples[2].value);
  EXPECT_EQ(expTsf, samples[2].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.rxOk", samples[3].key.c_str());
  EXPECT_EQ(3, samples[3].value);
  EXPECT_EQ(expTsf, samples[3].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.rxFail", samples[4].key.c_str());
  EXPECT_EQ(2, samples[4].value);
  EXPECT_EQ(expTsf, samples[4].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.rxPlcpFil", samples[5].key.c_str());
  EXPECT_EQ(5, samples[5].value);
  EXPECT_EQ(expTsf, samples[5].tsf);
  // second testB stats
  expTsf = 0x1234567aabbccdd;
  EXPECT_STREQ("tgf.aa:bb:cc:dd:ee:ff.testB.tsf", samples[6].key.c_str());
  EXPECT_EQ(expTsf, samples[6].value);
  EXPECT_EQ(expTsf, samples[6].tsf);
  EXPECT_STREQ("tgf.aa:bb:cc:dd:ee:ff.testB.txOk", samples[7].key.c_str());
  EXPECT_EQ(4, samples[7].value);
  EXPECT_EQ(expTsf, samples[7].tsf);
  EXPECT_STREQ("tgf.aa:bb:cc:dd:ee:ff.testB.txFail", samples[8].key.c_str());
  EXPECT_EQ(5, samples[8].value);
  EXPECT_EQ(expTsf, samples[8].tsf);
  EXPECT_STREQ("tgf.aa:bb:cc:dd:ee:ff.testB.rxOk", samples[9].key.c_str());
  EXPECT_EQ(2, samples[9].value);
  EXPECT_EQ(expTsf, samples[9].tsf);
  EXPECT_STREQ("tgf.aa:bb:cc:dd:ee:ff.testB.rxFail", samples[10].key.c_str());
  EXPECT_EQ(1, samples[10].value);
  EXPECT_EQ(expTsf, samples[10].tsf);
  EXPECT_STREQ(
      "tgf.aa:bb:cc:dd:ee:ff.testB.rxPlcpFil", samples[11].key.c_str());
  EXPECT_EQ(3, samples[11].value);
  EXPECT_EQ(expTsf, samples[11].tsf);
  // dropped samples
  EXPECT_STREQ("tgf.00:00:00:00:00:00.droppedSamples", samples[12].key.c_str());
  EXPECT_EQ(1, samples[12].value);
  EXPECT_EQ(expTsf, samples[12].tsf);
  EXPECT_STREQ("tgf.00:00:00:00:00:00.buffIdx", samples[13].key.c_str());
  EXPECT_EQ(3, samples[13].value);
  EXPECT_EQ(expTsf, samples[13].tsf);
  EXPECT_STREQ("tgf.00:00:00:00:00:00.numBuffKv", samples[14].key.c_str());
  EXPECT_EQ(15, samples[14].value);
  EXPECT_EQ(expTsf, samples[14].tsf);
}

void
getStatsHelper(const uint16_t type, const uint8_t expectedSampleSize) {
  uint8_t buffer[sizeof(tgfStatsMsgHdr) + sizeof(tgfStatsSample)];
  memset(buffer, 0, sizeof(buffer));
  tgfStatsMsgHdr* buffStats = (tgfStatsMsgHdr*)&buffer[0];
  buffStats->numSamples = 1;
  tgfStatsSample* buffSample = (tgfStatsSample*)(buffStats + 1);
  buffSample->type = type;

  auto thriftStats =
      getStats(buffStats, sizeof(tgfStatsMsgHdr) + sizeof(tgfStatsSample));
  EXPECT_EQ(expectedSampleSize, thriftStats.statsSamples.size());
}

#define MISC_LINK 19

#define STATS_BF_SCAN 28

#define STATS_PHY_PERIODIC 10

#define STATS_MTPO 139

#define STATS_GPS 16

#define STATS_LA_TPC 13

TEST(PtParse, GetStats) {
  const std::vector<
      std::pair<uint16_t /* type */, uint8_t /* expected sample size */>>
      typeSamples = {{TGF_STATS_STA_PKT, 29},
                     {TGF_STATS_GPS, STATS_GPS},
                     {TGF_STATS_PHYSTATUS, 19},
                     {TGF_STATS_PHYSTATUS_MGMT_HF, 5},
                     {TGF_STATS_PHYSTATUS_PRINT_HF, 0},
                     {TGF_STATS_LA_TPC_PRINT_HF, 0},
                     {TGF_STATS_MGMT_TX, 18},
                     {TGF_STATS_MGMT_RX, 18},
                     {TGF_STATS_SLOT, 10},
                     {TGF_STATS_BWHAN_SYS, 6},
                     {TGF_STATS_BWHAN_LINK, 18},
                     {TGF_STATS_BF, 14},
                     {TGF_STATS_PHY_PERIODIC, STATS_PHY_PERIODIC},
                     {TGF_STATS_RECV_MGMT, 20},
                     {TGF_STATS_MGMT_DATA, 24},
                     {TGF_STATS_MEM, 6},
                     {TGF_STATS_MISC_SYS, 25},
                     {TGF_STATS_MISC_LINK, MISC_LINK},
                     {TGF_STATS_LA_HIGH_SPEED, 4},
                     {TGF_STATS_TPC_HIGH_SPEED, 4},
                     {TGF_STATS_TPC, 7},
                     {TGF_STATS_LA_TPC, STATS_LA_TPC},
                     {TGF_STATS_TSF, 14},
                     {TGF_STATS_LIFETIME_EXPIRED, 4},
                     {TGF_STATS_MAX_AGC, 7},
                     {TGF_STATS_LIFETIME_OK, 4},
                     {TGF_STATS_CHN, 13},
                     {TGF_STATS_LINK_DOWN, 5},
                     {TGF_STATS_SECURITY, 5},
                     {TGF_STATS_RADIO, 21},
                     {TGF_STATS_CALIBRATE, 104},
                     {TGF_STATS_BF_SCAN, STATS_BF_SCAN},
                     {TGF_STATS_MAX_AGC_HIST, 4},
                     {TGF_STATS_MTPO, STATS_MTPO},
      };

  for (const auto& typeSample : typeSamples) {
    getStatsHelper(typeSample.first, typeSample.second);
  }
}

TEST(PtParse, StatsInvalidBuff) {
  uint8_t buff[1024];
  tgfPtMsg* ptMsg = (tgfPtMsg*)buff;
  ptMsg->msgType = TGF_PT_NB_STATS;
  ptMsg->dest = TGF_PT_DEST_E2E;
  ptMsg->cookie = kPosNum;
  ptMsg->data.statsHdr.numSamples = 1;
  ptMsg->data.statsHdr.droppedSamples = 0;
  ptMsg->data.statsHdr.buffIdx = 3;
  // fill the sample
  tgfStatsSample* sample = (tgfStatsSample*)(&ptMsg->data.statsHdr + 1);
  sample->type = TGF_STATS_TEST_B;
  sample->addr[0] = 1;
  sample->addr[1] = 2;
  sample->addr[2] = 3;
  sample->addr[3] = 0xaa;
  sample->addr[4] = 5;
  sample->addr[5] = 0xf0;
  sample->tsfL = 0x11223344;
  sample->tsfH = 0xaabbccdd;
  sample->data.testB.rxFail = 2;
  sample->data.testB.rxOk = 3;
  sample->data.testB.rxPlcpFil = 5;
  sample->data.testB.txOk = 7;
  sample->data.testB.txFail = 11;
  // fill the next sample
  sample =
      (tgfStatsSample*)((uint8_t*)sample + offsetof(tgfStatsSample, data) + sizeof(tgfStatsTestB));
  sample->type = TGF_STATS_TEST_B;
  sample->addr[0] = 0xaa;
  sample->addr[1] = 0xbb;
  sample->addr[2] = 0xcc;
  sample->addr[3] = 0xdd;
  sample->addr[4] = 0xee;
  sample->addr[5] = 0xff;
  sample->tsfL = 0xaabbccdd;
  sample->tsfH = 0x01234567;
  sample->data.testB.rxFail = 1;
  sample->data.testB.rxOk = 2;
  sample->data.testB.rxPlcpFil = 3;
  sample->data.testB.txOk = 4;
  sample->data.testB.txFail = 5;
  thrift::PassThruMsg thriftMsg;

  // len cuts off part of second sample, so it outputs only first sample
  uint32_t len = offsetof(tgfPtMsg, data) + sizeof(tgfStatsMsgHdr) +
                 offsetof(tgfStatsSample, data) + sizeof(tgfStatsTestB) +
                 (sizeof(tgfStatsTestB) / 2);
  thriftMsg = getPtThrift(buff, len);
  EXPECT_EQ(thrift::PtMsgTypes::NB_STATS, thriftMsg.msgType);
  EXPECT_EQ(thrift::PtMsgDest::E2E, thriftMsg.dest);
  EXPECT_EQ(kPosNum, thriftMsg.cookie);
  ASSERT_EQ(9, thriftMsg.stats.statsSamples.size());
  std::vector<thrift::StatsSample>& samples = thriftMsg.stats.statsSamples;
  int64_t expTsf = 0xaabbccdd11223344 & 0x7fffffffffffffff;
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.tsf", samples[0].key.c_str());
  EXPECT_EQ(expTsf, samples[0].value);
  EXPECT_EQ(expTsf, samples[0].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.txOk", samples[1].key.c_str());
  EXPECT_EQ(7, samples[1].value);
  EXPECT_EQ(expTsf, samples[1].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.txFail", samples[2].key.c_str());
  EXPECT_EQ(11, samples[2].value);
  EXPECT_EQ(expTsf, samples[2].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.rxOk", samples[3].key.c_str());
  EXPECT_EQ(3, samples[3].value);
  EXPECT_EQ(expTsf, samples[3].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.rxFail", samples[4].key.c_str());
  EXPECT_EQ(2, samples[4].value);
  EXPECT_EQ(expTsf, samples[4].tsf);
  EXPECT_STREQ("tgf.01:02:03:aa:05:f0.testB.rxPlcpFil", samples[5].key.c_str());
  EXPECT_EQ(5, samples[5].value);
  EXPECT_EQ(expTsf, samples[5].tsf);
  EXPECT_STREQ("tgf.00:00:00:00:00:00.droppedSamples", samples[6].key.c_str());
  EXPECT_EQ(0, samples[6].value);
  EXPECT_EQ(expTsf, samples[6].tsf);
  EXPECT_STREQ("tgf.00:00:00:00:00:00.buffIdx", samples[7].key.c_str());
  EXPECT_EQ(3, samples[7].value);
  EXPECT_EQ(expTsf, samples[7].tsf);
  EXPECT_STREQ("tgf.00:00:00:00:00:00.numBuffKv", samples[8].key.c_str());
  EXPECT_EQ(9, samples[8].value);
  EXPECT_EQ(expTsf, samples[8].tsf);

  // invalid len
  len = 1;
  thriftMsg = getPtThrift(buff, len);
  EXPECT_EQ(thrift::PtMsgTypes::NB_INVALID, thriftMsg.msgType);

  // invalid msgType
  ptMsg->msgType = 150;
  len = 1024;
  thriftMsg = getPtThrift(buff, len);
  EXPECT_EQ(thrift::PtMsgTypes::NB_INVALID, thriftMsg.msgType);

  // invalid dest
  ptMsg->msgType = TGF_PT_NB_STATS;
  ptMsg->dest = 150;
  thriftMsg = getPtThrift(buff, len);
  EXPECT_EQ(thrift::PtMsgTypes::NB_INVALID, thriftMsg.msgType);
}

TEST(PtParse, Ack) {
  uint8_t buff[1024];
  tgfPtMsg* ptMsg = (tgfPtMsg*)buff;
  ptMsg->msgType = TGF_PT_NB_ACK;
  ptMsg->dest = TGF_PT_DEST_DBG;
  ptMsg->cookie = kPosNum;
  ptMsg->data.ack.msgType = TGF_PT_SB_ASSOC;
  // get thrift obj
  auto thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(thrift::PtMsgTypes::NB_ACK, thriftMsg.msgType);
  EXPECT_EQ(thrift::PtMsgDest::DBG, thriftMsg.dest);
  EXPECT_EQ(kPosNum, thriftMsg.cookie);
  EXPECT_EQ(thrift::PtMsgTypes::SB_ASSOC, thriftMsg.ack.msgType);

  ptMsg->data.ack.msgType = TGF_PT_SB_DISSOC;
  thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(thrift::PtMsgTypes::SB_DISSOC, thriftMsg.ack.msgType);

  ptMsg->data.ack.msgType = TGF_PT_SB_STATS_CONFIGURE;
  thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(thrift::PtMsgTypes::SB_STATS_CONFIGURE, thriftMsg.ack.msgType);

  ptMsg->data.ack.msgType = TGF_PT_SB_PHY_LA_CONFIG;
  thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(thrift::PtMsgTypes::SB_PHY_LA_CONFIG, thriftMsg.ack.msgType);

  ptMsg->data.ack.msgType = TGF_PT_SB_PHY_AGC_CONFIG;
  thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(thrift::PtMsgTypes::SB_PHY_AGC_CONFIG, thriftMsg.ack.msgType);

  ptMsg->data.ack.msgType = TGF_PT_SB_PHY_TPC_CONFIG;
  thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(thrift::PtMsgTypes::SB_PHY_TPC_CONFIG, thriftMsg.ack.msgType);

  ptMsg->data.ack.msgType = TGF_PT_SB_GPS_ENABLE;
  thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(thrift::PtMsgTypes::SB_GPS_ENABLE, thriftMsg.ack.msgType);

  ptMsg->data.ack.msgType = TGF_PT_SB_PHY_ANT_WGT_TBL_CONFIG;
  thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(
      thrift::PtMsgTypes::SB_PHY_ANT_WGT_TBL_CONFIG, thriftMsg.ack.msgType);

  ptMsg->data.ack.msgType = TGF_PT_SB_POLARITY;
  thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(thrift::PtMsgTypes::SB_POLARITY, thriftMsg.ack.msgType);

  ptMsg->data.ack.msgType = TGF_PT_SB_GOLAY_INDX;
  thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(thrift::PtMsgTypes::SB_GOLAY_INDX, thriftMsg.ack.msgType);

  ptMsg->data.ack.msgType = TGF_PT_SB_DEBUG;
  thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(thrift::PtMsgTypes::SB_DEBUG, thriftMsg.ack.msgType);

  ptMsg->data.ack.msgType = TGF_PT_SB_BF_RESPONDER_MODE_CONFIG;
  thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(thrift::PtMsgTypes::SB_BF_RESP_SCAN_CONFIG, thriftMsg.ack.msgType);

  // different type
  ptMsg->msgType = TGF_PT_NB_HEALTHY;
  thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(thrift::PtMsgTypes::NB_HEALTHY, thriftMsg.msgType);

  // different destination
  ptMsg->dest = TGF_PT_DEST_E2E_DBG;
  thriftMsg = getPtThrift(buff, sizeof(buff));
  EXPECT_EQ(thrift::PtMsgDest::E2E_DBG, thriftMsg.dest);
}

TEST(PtCreate, DissocMsg) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_DISSOC;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;
  thriftMsg.dissoc.addr = "1:2:3:4:5:6";
  uint8_t buff[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(thriftMsg, buff);
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;
  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_SB_DISSOC, buffMsg->msgType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);
  uint8_t* addr = buffMsg->data.dissoc.addr;
  EXPECT_EQ(0x01, addr[0]);
  EXPECT_EQ(0x02, addr[1]);
  EXPECT_EQ(0x03, addr[2]);
  EXPECT_EQ(0x04, addr[3]);
  EXPECT_EQ(0x05, addr[4]);
  EXPECT_EQ(0x06, addr[5]);
  EXPECT_EQ(offsetof(tgfPtMsg, data) + sizeof(tgfPtDissocMsg), len);
}

TEST(PtCreate, ConfigStats) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_STATS_CONFIGURE;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;

  thriftMsg.statsConfigure.configs["TGF_STATS_STA_PKT"] = true;
  thriftMsg.statsConfigure.configs["TGF_STATS_TEST_A"] = true;
  thriftMsg.statsConfigure.configs["TGF_STATS_TEST_B"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_GPS"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_PHYSTATUS"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_MGMT_TX"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_SLOT"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_BWHAN_LINK"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_BWHAN_SYS"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_BF"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_PHY_PERIODIC"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_RECV_MGMT"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_MGMT_DATA"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_MEM"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_MISC_SYS"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_MISC_LINK"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_TPC"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_LA_TPC"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_MAX_AGC"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_TSF"] = false;
  thriftMsg.statsConfigure.configs["TGF_STATS_UNKNOWN"] = false;
  thriftMsg.statsConfigure.onDuration = 4;
  thriftMsg.statsConfigure.period = 100;

  uint8_t buff[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(thriftMsg, buff);
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;

  EXPECT_TRUE(len != 0);
  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_SB_STATS_CONFIGURE, buffMsg->msgType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);

  tgfPtStatConfig* configStatBuff =
      &buffMsg->data.statsConfigure.statsConfigs[0];
  for (uint32_t i = 0; i < TGF_STATS_NUM; i++) {
    if (i == TGF_STATS_STA_PKT) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(1, configStatBuff[i].enable);
    } else if (i == TGF_STATS_TEST_A) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(1, configStatBuff[i].enable);
    } else if (i == TGF_STATS_TEST_B) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_GPS) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_PHYSTATUS) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_MGMT_TX) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_SLOT) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_BWHAN_LINK) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_BWHAN_SYS) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_BF) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_PHY_PERIODIC) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_RECV_MGMT) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_MGMT_DATA) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_MEM) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_MISC_SYS) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_MISC_LINK) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_TPC) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_LA_TPC) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_TSF) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else if (i == TGF_STATS_MAX_AGC) {
      EXPECT_EQ(1, configStatBuff[i].configure);
      EXPECT_EQ(0, configStatBuff[i].enable);
    } else {
      EXPECT_EQ(0, configStatBuff[i].configure);
    }
  }
  EXPECT_EQ(4, buffMsg->data.statsConfigure.statsCycle.onDuration);
  EXPECT_EQ(100, buffMsg->data.statsConfigure.statsCycle.period);
}

TEST(PtCreate, PhyLATableConfig) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_PHY_LA_CONFIG;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;

  thriftMsg.phyLAConfig.addr = "55:44:33:22:11:00";
  thriftMsg.phyLAConfig.laNodeParams.mcsLqmQ8_ref() = std::vector<int32_t>();
  for (int ii = 0; ii < TGF_MCS_LQM_NUM; ii++) {
    thriftMsg.phyLAConfig.laNodeParams.mcsLqmQ8_ref()->push_back(240 + ii);
  }
  thriftMsg.phyLAConfig.laParams.laMcs_ref() =
      thrift::laMcsModeEnum::JOINT_LA_TPC;
  thriftMsg.phyLAConfig.laParams.nackWeightFactor_ref() = 2001;
  thriftMsg.phyLAConfig.laParams.laConvergenceFactordBperSFQ8_ref() = 23;
  thriftMsg.phyLAConfig.laParams.laMaxMcs_ref() = 10;
  thriftMsg.phyLAConfig.laParams.laMinMcs_ref() = 3;
  thriftMsg.phyLAConfig.laParams.laMargindBQ2_ref() = -4;
  thriftMsg.phyLAConfig.laParams.debug_ref() = 99;

  uint8_t buff[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(thriftMsg, buff);
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;

  EXPECT_TRUE(len != 0);
  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_SB_PHY_LA_CONFIG, buffMsg->msgType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);

  tgfPtPhyLAConfig* laConfig = &buffMsg->data.phyLAConfig;
  EXPECT_EQ(0x55, laConfig->addr[0]);
  EXPECT_EQ(0x44, laConfig->addr[1]);
  EXPECT_EQ(0x33, laConfig->addr[2]);
  EXPECT_EQ(0x22, laConfig->addr[3]);
  EXPECT_EQ(0x11, laConfig->addr[4]);
  EXPECT_EQ(0x00, laConfig->addr[5]);

  for (int ii = 0; ii < TGF_MCS_LQM_NUM; ii++) {
    EXPECT_EQ(240 + ii, laConfig->laNodeParams.mcsLqmQ8[ii]);
  }
  EXPECT_EQ(JOINT_LA_TPC, laConfig->laParams.laMcs);
  EXPECT_EQ(2001, laConfig->laParams.nackWeightFactor);
  EXPECT_EQ(23, laConfig->laParams.laConvergenceFactordBperSFQ8);
  EXPECT_EQ(10, laConfig->laParams.laMaxMcs);
  EXPECT_EQ(3, laConfig->laParams.laMinMcs);
  EXPECT_EQ(-4, laConfig->laParams.laMargindBQ2);
  EXPECT_EQ(99, laConfig->laParams.debug);
}

TEST(PtCreate, EnableGps) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_GPS_ENABLE;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;
  uint8_t buff[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(thriftMsg, buff);
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;
  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_SB_GPS_ENABLE, buffMsg->msgType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);
  EXPECT_EQ(12, len);
}

TEST(PtCreate, PolarityConfig) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_POLARITY;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;
  thriftMsg.polarityCfg.polarity = 1;
  uint8_t buff[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(thriftMsg, buff);
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;
  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_SB_POLARITY, buffMsg->msgType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);
  EXPECT_EQ(1, buffMsg->data.polarityCfg.polarity);
  EXPECT_EQ(13, len);
}

TEST(PtCreate, BfRespScanConfig) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_BF_RESP_SCAN_CONFIG;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;
  thriftMsg.bfRespScanCfg.cfg = true;
  uint8_t buff[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(thriftMsg, buff);
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;
  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_SB_BF_RESPONDER_MODE_CONFIG, buffMsg->msgType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);
  EXPECT_EQ(BFRESP_MODE_ENABLE, buffMsg->data.bfRespModeCfg.config);
  EXPECT_EQ(13, len);
}

TEST(PtCreate, GolayConfig) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_GOLAY_INDX;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;
  thriftMsg.golayCfg.txGolayIndx = 5;
  thriftMsg.golayCfg.rxGolayIndx = 2;
  uint8_t buff[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(thriftMsg, buff);
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;
  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_SB_GOLAY_INDX, buffMsg->msgType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);
  EXPECT_EQ(5, buffMsg->data.golayCfg.txIndx);
  EXPECT_EQ(2, buffMsg->data.golayCfg.rxIndx);
  EXPECT_EQ(14, len);
}

TEST(PtCreate, ScanReq) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_SCAN_REQ;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;

  thrift::ScanReq& scanReq = thriftMsg.scanReq;
  scanReq.token = 10;
  scanReq.scanType_ref() = thrift::ScanType::IM;
  scanReq.scanMode = thrift::ScanMode::COARSE;
  scanReq.startBwgdIdx = 0;
  scanReq.bfScanInvertPolarity = false;
  thrift::BeamIndices beams;
  beams.low = 0;
  beams.high = 0;
  scanReq.beams_ref() = beams;
  scanReq.apply_ref() = true;
  scanReq.subType_ref() = thrift::ScanSubType::NO_CAL;
  scanReq.bwgdLen_ref() = 16;
  scanReq.txPwrIndex_ref() = 16;
  scanReq.rxNodeMac_ref() = "38:3A:21:B0:06:2E";
  uint8_t peerMacAddr[6];
  memcpy(peerMacAddr, folly::MacAddress("38:3A:21:B0:06:2E").bytes(), 6);
  scanReq.nullAngle_ref() = 0;
  scanReq.cbfBeamIdx_ref() = 0;
  scanReq.isAggressor_ref() = false;

  uint8_t buff[SB_PT_BUFF_LEN];
  getPtBuff(thriftMsg, buff);
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;

  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_SB_SCAN_REQ, buffMsg->msgType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);

  EXPECT_EQ(10, buffMsg->data.scanReq.token);
  EXPECT_EQ(2, buffMsg->data.scanReq.scanType);
  EXPECT_EQ(1, buffMsg->data.scanReq.scanMode);
  EXPECT_EQ(0, buffMsg->data.scanReq.startBwgdIdxL);
  EXPECT_EQ(0, buffMsg->data.scanReq.startBwgdIdxH);
  EXPECT_EQ(0, buffMsg->data.scanReq.bfScanInvertPolarity);
  EXPECT_EQ(0, buffMsg->data.scanReq.beamIdxLow);
  EXPECT_EQ(0, buffMsg->data.scanReq.beamIdxHigh);
  EXPECT_EQ(1, buffMsg->data.scanReq.apply);
  EXPECT_EQ(0, buffMsg->data.scanReq.subType);
  EXPECT_EQ(16, buffMsg->data.scanReq.bwgdLen);
  EXPECT_EQ(16, buffMsg->data.scanReq.txPwrIndex);
  EXPECT_EQ(true, buffMsg->data.scanReq.isTx);
  EXPECT_EQ(0x38, buffMsg->data.scanReq.peerMacAddr[0]);
  EXPECT_EQ(0x3A, buffMsg->data.scanReq.peerMacAddr[1]);
  EXPECT_EQ(0x21, buffMsg->data.scanReq.peerMacAddr[2]);
  EXPECT_EQ(0xB0, buffMsg->data.scanReq.peerMacAddr[3]);
  EXPECT_EQ(0x06, buffMsg->data.scanReq.peerMacAddr[4]);
  EXPECT_EQ(0x2E, buffMsg->data.scanReq.peerMacAddr[5]);
  EXPECT_EQ(0, buffMsg->data.scanReq.nullAngle);
  EXPECT_EQ(0, buffMsg->data.scanReq.cbfBeamIdx);
}

TEST(PtCreate, BfSlotExclusionReq) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_BF_SLOT_EXCLUSION_REQ;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;
  thriftMsg.bfSlotExclusionReq.startBwgdIdx = 5;
  uint8_t buff[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(thriftMsg, buff);
  (void)len;
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;
  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);
  EXPECT_EQ(TGF_PT_SB_BF_SLOT_EXCLUSION_REQ, buffMsg->msgType);
  EXPECT_EQ(5, buffMsg->data.bfSlotExclusionReq.startBwgdIdxL);
  EXPECT_EQ(0, buffMsg->data.bfSlotExclusionReq.startBwgdIdxH);
  EXPECT_EQ(offsetof(tgfPtMsg, data) + sizeof(tgfPtBfSlotExclusionReq), len);
}

TEST(PtCreate, PhyGolaySequenceConfig) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_PHY_GOLAY_SEQUENCE_CONFIG;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;

  std::vector<std::vector<int64_t>> txSeq;
  for (int16_t i = 0; i < TGF_NUM_GOLAY_SEQ_COUNT; i++) {
    std::vector<int64_t> temp_vec;
    for (int16_t j = 0; j < TGF_NUM_GOLAY_SEQ_COUNT; j++) {
      temp_vec.push_back(j);
    }
    txSeq.push_back(temp_vec);
  }
  thriftMsg.phyGolaySequenceCfg.transmitSequence = txSeq;

  std::vector<int32_t> rxSeq;
  for (int16_t j = 0; j < TGF_NUM_GOLAY_SEQ_COUNT; j++) {
    rxSeq.push_back(j);
  }
  thriftMsg.phyGolaySequenceCfg.receiveSequence = rxSeq;

  uint8_t buff[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(thriftMsg, buff);
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;

  EXPECT_TRUE(len != 0);
  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_SB_PHY_GOLAY_SEQUENCE_CONFIG, buffMsg->msgType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);

  tgfPtPhyGolaySequenceConfig* golaySeqCfg = &buffMsg->data.phyGolaySequenceCfg;

  for (int16_t i = 0; i < TGF_NUM_GOLAY_SEQ_COUNT; i++) {
    for (int16_t j = 0; j < TGF_NUM_GOLAY_SEQ_COUNT; j++) {
      EXPECT_EQ(j, golaySeqCfg->transmitSequence[i][j]);
    }
  }
  for (int16_t j = 0; j < TGF_NUM_GOLAY_SEQ_COUNT; j++) {
    EXPECT_EQ(j, golaySeqCfg->receiveSequence[j]);
  }
}

TEST(PtCreate, PhyAntWgtTblConfig) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_PHY_ANT_WGT_TBL_CONFIG;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;

  const int kNumBeams = 5;
  const int kNumElements = 7;

  thriftMsg.phyAntWgtTblPart.module = 5;
  thriftMsg.phyAntWgtTblPart.isRx = 1;
  thriftMsg.phyAntWgtTblPart.beamStart = 1;
  thriftMsg.phyAntWgtTblPart.beamCount = kNumBeams;
  thriftMsg.phyAntWgtTblPart.stride = kNumBeams;
  thriftMsg.phyAntWgtTblPart.strideCount = 1;

  std::vector<std::vector<int16_t>> antWgtCode;
  for (int16_t i = 0; i < kNumBeams; i++) {
    std::vector<int16_t> temp_vec;
    for (int16_t j = 0; j < kNumElements; j++) {
      temp_vec.push_back(j);
    }
    antWgtCode.push_back(temp_vec);
  }
  thriftMsg.phyAntWgtTblPart.antWgtCode = antWgtCode;

  uint8_t buff[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(thriftMsg, buff);
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;

  EXPECT_TRUE(len != 0);
  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_SB_PHY_ANT_WGT_TBL_CONFIG, buffMsg->msgType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);

  tgfPtPhyAntWgtTblConfig* antWgtTblConfig = &buffMsg->data.phyAntWgtTblConfig;
  EXPECT_EQ(5, antWgtTblConfig->antWgtTblModule);
  EXPECT_EQ(1, antWgtTblConfig->antWgtTblIsRx);
  EXPECT_EQ(1, antWgtTblConfig->beamStart);
  EXPECT_EQ(kNumBeams, antWgtTblConfig->beamCount);
  EXPECT_EQ(kNumBeams, antWgtTblConfig->stride);
  EXPECT_EQ(1, antWgtTblConfig->strideCount);
  EXPECT_EQ(kNumElements, antWgtTblConfig->numElements);

  for (int16_t i = 0; i < kNumBeams; i++) {
    for (int16_t j = 0; j < kNumElements; j++) {
      EXPECT_EQ(j, antWgtTblConfig->antWgtCode[i * kNumElements + j]);
    }
  }
}

TEST(PtCreate, DebugMsg) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_DEBUG;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;
  thriftMsg.debug.cmdStr = "debug";
  thriftMsg.debug.value = 0xFFFFFFFFEEEEEEEE;
  uint8_t buff[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(thriftMsg, buff);
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;
  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_SB_DEBUG, buffMsg->msgType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);
  const auto debug = buffMsg->data.debug;
  EXPECT_EQ("debug", std::string(reinterpret_cast<const char*>(debug.cmdStr)));
  EXPECT_EQ(0xFFFFFFFF, debug.valueH);
  EXPECT_EQ(0xEEEEEEEE, debug.valueL);
  EXPECT_EQ(offsetof(tgfPtMsg, data) + sizeof(tgfPtDebug), len);
}

TEST(PtCreate, airtimeAlloc) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_AIRTIMEALLOC;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;

  thrift::LinkAirtime linkAirtime;
  linkAirtime.macAddress = "55:44:33:22:11:00";
  linkAirtime.txIdeal = 5000;
  linkAirtime.txMin = 200;
  linkAirtime.txMax = 10000;
  linkAirtime.rxIdeal = 4500;
  linkAirtime.rxMin = 300;
  linkAirtime.rxMax = 9900;
  thriftMsg.airtimeAllocMap.linkAirtimes.push_back(linkAirtime);
  linkAirtime.macAddress = "99:88:77:66:55:44";
  linkAirtime.txIdeal = 2500;
  linkAirtime.txMin = 200;
  linkAirtime.txMax = 10000;
  linkAirtime.rxIdeal = 3500;
  linkAirtime.rxMin = 400;
  linkAirtime.rxMax = 8000;
  thriftMsg.airtimeAllocMap.linkAirtimes.push_back(linkAirtime);

  uint8_t buff[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(thriftMsg, buff);
  EXPECT_EQ(offsetof(tgfPtMsg, data) + sizeof(tgfPtAirtimeAllocMsg), len);

  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;
  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_SB_AIRTIME_ALLOC, buffMsg->msgType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);
  EXPECT_EQ(0x55, buffMsg->data.airtimeAlloc.addrAirtimeTable[0].macAddr[0]);
  EXPECT_EQ(0x44, buffMsg->data.airtimeAlloc.addrAirtimeTable[0].macAddr[1]);
  EXPECT_EQ(0x33, buffMsg->data.airtimeAlloc.addrAirtimeTable[0].macAddr[2]);
  EXPECT_EQ(0x22, buffMsg->data.airtimeAlloc.addrAirtimeTable[0].macAddr[3]);
  EXPECT_EQ(0x11, buffMsg->data.airtimeAlloc.addrAirtimeTable[0].macAddr[4]);
  EXPECT_EQ(0x00, buffMsg->data.airtimeAlloc.addrAirtimeTable[0].macAddr[5]);
  EXPECT_EQ(5000, buffMsg->data.airtimeAlloc.addrAirtimeTable[0].txIdeal);
  EXPECT_EQ(200, buffMsg->data.airtimeAlloc.addrAirtimeTable[0].txMin);
  EXPECT_EQ(10000, buffMsg->data.airtimeAlloc.addrAirtimeTable[0].txMax);
  EXPECT_EQ(4500, buffMsg->data.airtimeAlloc.addrAirtimeTable[0].rxIdeal);
  EXPECT_EQ(300, buffMsg->data.airtimeAlloc.addrAirtimeTable[0].rxMin);
  EXPECT_EQ(9900, buffMsg->data.airtimeAlloc.addrAirtimeTable[0].rxMax);
  EXPECT_EQ(0x99, buffMsg->data.airtimeAlloc.addrAirtimeTable[1].macAddr[0]);
  EXPECT_EQ(0x88, buffMsg->data.airtimeAlloc.addrAirtimeTable[1].macAddr[1]);
  EXPECT_EQ(0x77, buffMsg->data.airtimeAlloc.addrAirtimeTable[1].macAddr[2]);
  EXPECT_EQ(0x66, buffMsg->data.airtimeAlloc.addrAirtimeTable[1].macAddr[3]);
  EXPECT_EQ(0x55, buffMsg->data.airtimeAlloc.addrAirtimeTable[1].macAddr[4]);
  EXPECT_EQ(0x44, buffMsg->data.airtimeAlloc.addrAirtimeTable[1].macAddr[5]);
  EXPECT_EQ(2500, buffMsg->data.airtimeAlloc.addrAirtimeTable[1].txIdeal);
  EXPECT_EQ(200, buffMsg->data.airtimeAlloc.addrAirtimeTable[1].txMin);
  EXPECT_EQ(10000, buffMsg->data.airtimeAlloc.addrAirtimeTable[1].txMax);
  EXPECT_EQ(3500, buffMsg->data.airtimeAlloc.addrAirtimeTable[1].rxIdeal);
  EXPECT_EQ(400, buffMsg->data.airtimeAlloc.addrAirtimeTable[1].rxMin);
  EXPECT_EQ(8000, buffMsg->data.airtimeAlloc.addrAirtimeTable[1].rxMax);
  EXPECT_EQ(0x00, buffMsg->data.airtimeAlloc.addrAirtimeTable[2].macAddr[0]);
  EXPECT_EQ(0x00, buffMsg->data.airtimeAlloc.addrAirtimeTable[2].macAddr[1]);
  EXPECT_EQ(0x00, buffMsg->data.airtimeAlloc.addrAirtimeTable[2].macAddr[2]);
  EXPECT_EQ(0x00, buffMsg->data.airtimeAlloc.addrAirtimeTable[2].macAddr[3]);
  EXPECT_EQ(0x00, buffMsg->data.airtimeAlloc.addrAirtimeTable[2].macAddr[4]);
  EXPECT_EQ(0x00, buffMsg->data.airtimeAlloc.addrAirtimeTable[2].macAddr[5]);
  EXPECT_EQ(0, buffMsg->data.airtimeAlloc.addrAirtimeTable[2].txIdeal);
  EXPECT_EQ(0, buffMsg->data.airtimeAlloc.addrAirtimeTable[3].txMin);
  EXPECT_EQ(0, buffMsg->data.airtimeAlloc.addrAirtimeTable[4].txMax);
  EXPECT_EQ(0, buffMsg->data.airtimeAlloc.addrAirtimeTable[5].rxIdeal);
  EXPECT_EQ(0, buffMsg->data.airtimeAlloc.addrAirtimeTable[6].rxMin);
  EXPECT_EQ(0, buffMsg->data.airtimeAlloc.addrAirtimeTable[7].rxMax);
}

TEST(PtCreate, LogConfigMsg) {
  thrift::PassThruMsg thriftMsg;
  thriftMsg.msgType = thrift::PtMsgTypes::SB_SET_LOG_CONFIG;
  thriftMsg.dest = thrift::PtMsgDest::SB;
  thriftMsg.cookie = kPosNum;

  thriftMsg.setLogConfig.configs[thrift::LogModule::DEFAULT] =
      thrift::LogLevel::DEBUG;
  thriftMsg.setLogConfig.configs[thrift::LogModule::BF] =
      thrift::LogLevel::FATAL;

  uint8_t buff[SB_PT_BUFF_LEN];
  size_t len = getPtBuff(thriftMsg, buff);
  EXPECT_LT(0, len);
  tgfPtMsg* buffMsg = (tgfPtMsg*)buff;
  EXPECT_EQ(TG_SB_PASSTHRU, buffMsg->driverType);
  EXPECT_EQ(TGF_PT_SB_SET_LOG_CONFIG, buffMsg->msgType);
  EXPECT_EQ(TGF_PT_DEST_SB, buffMsg->dest);
  EXPECT_EQ(kPosNum, buffMsg->cookie);
  const auto level = buffMsg->data.setLogConfig.logLevel;
  EXPECT_EQ(TGF_LOG_LEVEL_DEBUG, level[TGF_LOG_MOD_DEFAULT]);
  EXPECT_EQ(TGF_LOG_NUM_LEVELS, level[TGF_LOG_MOD_AGC]);
  EXPECT_EQ(TGF_LOG_LEVEL_FATAL, level[TGF_LOG_MOD_BF]);
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
