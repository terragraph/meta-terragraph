/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

namespace cpp2 facebook.terragraph.thrift
namespace py terragraph_thrift.PassThru
namespace lua terragraph_thrift.PassThru

cpp_include "<unordered_map>"

include "BWAllocation.thrift"
include "Controller.thrift"
include "FwOptParams.thrift"

// Holds a stats sample
struct StatsSample {
  1: string key; // includes mac addr, struct name, field name, etc
  2: i64 value;
  3: i64 tsf; // 1us counter from fw, aka timing synchroniation function
}

// Type of pass-thru messages
enum PtMsgTypes {
  NB_STATS = 0, // statistics coming from fw
  NB_INVALID = 1, // represents invalid nb buff
  NB_ACK = 2, // ack for southbound pass-thru message
  NB_HEALTHY = 3, // periodic message for radio is healthy
  NB_SCAN_RESP = 4, // Beamforming/interference scan response
  NB_CODEBOOK_FETCH = 5, // fetch codebook for specific channel
  NB_GET_FW_PARAMS_RESP = 6, //fetch fw params of specific type
  NB_SET_FW_PARAMS_RESP = 7, //Response for setting fw req
  NB_ROUTING_INFO = 8, // periodic fw routing info message
  NB_OPENR_ADJ_REQ = 9, // request openr adjacencies information from fw
  NB_PPS_HTSF_INFO = 10, // periodic HTSF info sent every PPS
  NB_PHY_TPC_ADJ_TBL_FETCH = 11, // fetch txPower adj tbl for specific channel

  SB_DISSOC = 100, // dissociate a station
  // 101: SB_BWALLOC (deprecated in RELEASE_M27)
  SB_AIRTIMEALLOC = 102, // Configure per-link BW airtimes
  SB_STATS_CONFIGURE = 103, // Config stats e.g. turn stat on/off
  SB_PHY_LA_CONFIG = 104, // Config LA parameters
  SB_GPS_ENABLE = 105, // Enable GPS
  SB_POLARITY = 106, // Config node polarity
  SB_GOLAY_INDX = 107, // Config Golay code indices
  SB_PHY_ANT_WGT_TBL_CONFIG = 108, // Config Ant Wgt Code Book
  SB_ASSOC = 109, // associate a station
  SB_DEBUG = 110, // invoke debug modes
  SB_SCAN_REQ = 111, // Beamforming/interference scan request
  SB_BF_SLOT_EXCLUSION_REQ = 112, // BF Slot Exclusion Request
  SB_PHY_GOLAY_SEQUENCE_CONFIG = 113, // Golay Sequence Config
  SB_PHY_TPC_CONFIG = 114, // configure TPC parameters
  SB_PHY_AGC_CONFIG = 115, // configure AGC parameters
  SB_SET_FW_PARAMS = 116, // Set/Get fw params
  SB_CHANNEL_CONFIG = 117, // channel configurations
  SB_BF_RESP_SCAN_CONFIG = 118, // channel configurations
  SB_GET_FW_PARAMS = 119, // Get FW parameters request
  SB_SET_LOG_CONFIG = 120, // set log configurations
  SB_SET_ADJ = 121, // set local adjs in fw
  // 122 (deprecated in RELEASE_M48)
  SB_GPS_TIME = 123, // set GPS time
  SB_PHY_TPC_ADJ_TBL_CFG = 124, // Per beam txPower adj Tbl
  SB_INVALID = 255, // represents invalid sb type
}

// struct for ack to south bound passthru message
struct Ack {
  1: PtMsgTypes msgType;
  2: bool success;
}

// struct for PtMsgTypes = NB_STATS
struct Stats {
  1: list<StatsSample> statsSamples;
}

// struct for PtMsgTypes = SB_ASSOC
struct AssocMsg {
  1: string addr; // station (node at other end of the link) mac address
  2: FwOptParams.FwOptParams optionalParams;
}

/// Fault flags in FwHealthReport
enum FaultFlags {
    FW_NOLINK_FAULT = 1, // no RF link formed for 15 minutes
    FW_GPS_FAULT = 2     // gps module is in a bad state or gps is unlocked
}

// struct for PtMsgTypes = NB_HEALTHY
struct FwHealthReport {
   1: i32 faultFlags;
   2: string selfMacAddr;
   3: i64 tsf (cpp.type = "std::uint64_t");
}

//struct for PtMsgTypes = SB_SET_FW_PARAMS
struct SetFwParamsMsg {
  1: string addr; // station (node at other end of the link) mac address
  2: optional i64 bwgdIdx; //BWGD index on which the commands should be applied
  3: FwOptParams.FwOptParams optionalParams; //list of fw params to be set
}

enum FwParamsType {
    FW_PARAMS_INVALID = 0, // Default invalid value
    FW_PARAMS_NODE_FW_CFG = 1, // TLV optional Node FW params FwOptParams
    FW_PARAMS_LINK_FW_CFG = 2, // TLV optional Link FW params FwOptParams
}

struct GetFwParamsReq {
    1: optional string addr;
    2: FwParamsType requestedParamsType;
}

struct GetFwParamsResp{
    1: FwParamsType fwParamsType;
    2: i32 currentBWGD; // To be deprecated in a future release
    3: i64 bwgdIdx; // Current 64 bit BWGD index

    //Holds link and node opt params
    5: FwOptParams.FwOptParams optParams;
}

struct SetFwParamsResp{
    1: bool status;
}

// struct for PtMsgTypes = NB_ROUTING_INFO
struct FwRoutingInfo {
   1: string addr; // station (node at other end of the link) mac address
   2: i16 mcs;
}

// struct for PtMsgTypes = SB_DISSOC
struct DissocMsg {
    1: string addr; // station (node at other end of the link) mac address
}

// struct for configuring one stats
// used in PtMsgTypes = SB_STATS_CONFIGURE
struct StatsConfigure {
  1: map<string /* name=#eTgfStatsType */, bool /* enable=on/off */> configs
  2: i32 onDuration;
  3: i32 period;
  4: list<string> hfMacAddressFilter; // to prevent huge logfiles
} (no_default_comparators)

// this should match laMcsMode_t
enum laMcsModeEnum {
  LA_DISABLED = 0, // this value should not be set
  MCS1 = 1,  // the minimum MCS is 1
  MCS2 = 2,
  MCS3 = 3,
  MCS4 = 4,
  MCS5 = 5,
  MCS6 = 6,
  MCS7 = 7,
  MCS8 = 8,
  MCS9 = 9,
  MCS10 = 10,
  MCS11 = 11,
  MCS12 = 12,
  MIN_MCS_OFDM = 13, // not currently supported
  MAX_MCS_OFDM = 24, // not currently supported
  JOINT_LA_TPC = 35,
  LA_NUM_MODES = 36,
}

/**
 * struct for configuring Link Adaptation
 * used in PtMsgTypes = SB_PHY_LA_CONFIG
 */
struct PhyLALinkParams {
  1: optional list<i32> mcsLqmQ8; // deprecated
  2: optional laMcsModeEnum laMcs;  // see laMcsMode_t
  3: optional i16 laMaxMcs;  // maximum allowed MCS value
  4: optional i16 laMinMcs;  // minimum allowed MCS value
  5: optional i16 laMargindBQ2 ; // added margin to the algorithm
  10: optional i16 laConvergenceFactordBperSFQ8;
  11: optional i16 nackWeightFactor;
  18: optional i32 debug; // used for general debugging of link adaptation
}

struct PhyLANodeParams {
  1: optional list<i32> mcsLqmQ8; // LQM for MCS index
}

struct PhyLAParams {
  1: PhyLALinkParams laParams;
  2: PhyLANodeParams laNodeParams;
}

struct PhyLAConfig {
  1: string addr; // station (node at other end of the link) mac address
  2: PhyLALinkParams laParams;
  3: PhyLANodeParams laNodeParams;
}

/**
 * struct for configuring max AGC tracking
 * used in PtMsgTypes = SB_PHY_AGC_CONFIG
 */
struct PhyAgcNodeParams {
  1: optional i16 ifGaindBperIndexQ8; // IF gain index to RSSI slope
  2: optional i16 rfGaindBperIndexQ8; // RF gain index to RSSI slope
  3: optional i16 rawAdcScaleFactorQ8; // raw RSSI to RSSI slope
  4: optional i16 targetRawAdc; // nominal raw RSSI
  // 5-8: (deprecated in RELEASE_M30)
  9: optional i16 maxIfGainIndex; // maximum allowed IF gain index
  10: optional i16 minIfGainIndex; // minimum allowed IF gain index
  11: optional i16 maxRfGainIndex; // maximum allowed RF gain index
  12: optional i16 minRfGainIndex; // minimum allowed RF gain index
  13: optional i16 maxIfSweetGainRange; // preferred upper range for IF
  14: optional i16 minIfSweetGainRange; // preferred lower range for IF
  15: optional i16 crsScale; // carrier sense threshold; set to 0 for default
  16: optional i32 logIntervalBwgd; // num BWGDs between log reporting
  17: optional i32 useMinRssi; // flag, if 1 use minRSSI, otherwise use max AGC
  18: optional i32 useSameForAllSta; // flag, if 1, set max AGC same on all sta
  19: optional i32 debugagc; // enables additional logging
  20: optional i32 rfGainHiLo; // vendor proprietary option
}

struct PhyAgcLinkParams {
  1: optional i16 maxAgcTrackingMargindB; // margin relative to current RSSI
  2: optional i16 maxAgcTrackingEnabled; // enable ot disable LINK_UP tracking
  3: optional i32 linkAgc; // set maxAGC to this value if tracking is disabled
  4: optional i16 maxNumberMissingBWGDs; // go to free-run if we miss mgmt
  5: optional i16 iirAlphaUpQ8; // IIR filter convergence for rising RSSI
  6: optional i16 iirAlphaDownQ8; // IIR filter convergence for falling RSSI
  // 7-9: (deprecated in RELEASE_M30)
  10: optional i32 minRssi; // like linkAgc when useMinRssi is set
}

struct PhyAgcParams {
  1: PhyAgcNodeParams agcNodeParams;
  2: PhyAgcLinkParams agcLinkParams;
}

struct PhyAgcConfig {
  1: string addr;
  2: PhyAgcNodeParams agcNodeParams;
  3: PhyAgcLinkParams agcLinkParams;
}

/*
 * struct for configuring transmit power control (TPC)
 * used in PtMsgTypes = SB_PHY_TPC_CONFIG
 */
struct PhyTpcNodeParams {
 1: optional list<i16> powerQ2AtIndex;
  // 1: optional i16 powerQ2AtIndex;
}

struct PhyTpcLinkParams {
  1: optional i16 tpcEnable;
  2: optional i16 txPower;
  3: optional i16 tpcRefStfSnrStep3;
  4: optional i16 tpcDelPowerStep3;
  5: optional i16 minTxPower;
  6: optional i16 tpcAlphaUpRssiStep3Q10;
  7: optional i16 tpcAlphaDownRssiStep3Q10;
  8: optional i16 tpcAlphaUpTargetRssiStep3Q10;
  9: optional i16 tpcAlphaDownTargetRssiStep3Q10;
  10: optional i32 statsReportingPeriodBwgd;
  11: optional i16 tpcHysteresisdBStep3Q2;
  12: optional i32 debugtpc;
  13: optional i16 maxTxPower;
  14: optional i16 tpcPBEnable;
}

struct PhyTpcParams {
  1: PhyTpcNodeParams tpcNodeParams;
  2: PhyTpcLinkParams tpcLinkParams;
}

struct PhyTpcConfig {
  1: string addr;
  2: PhyTpcNodeParams tpcNodeParams;
  3: PhyTpcLinkParams tpcLinkParams;
}

// struct for configuring node polarity
// used in PtMsgTypes = SB_POLARITY
struct PolarityConfig {
  1: i64 polarity; // node polarity (ODD, EVEN, ...)
}

// struct for configuring Golay code indices
// used in PtMsgTypes = SB_GOLAY_INDX
struct GolayConfig {
  1: i64 txGolayIndx; // Tx Golay code index
  2: i64 rxGolayIndx; // Rx Golay code index
}

// struct for configuring Antenna Weight Code Book
// used in PtMsgTypes = SB_PHY_ANT_WGT_TBL_CONFIG
// Contains weights for the specified beam indexes of a certain
// module/direction combination
// The beams sent are
// [beamStart, beamStart+1, ... beamStart+beamCount-1,
//  beamStart+stride, beamStart+stride+1, ..., beamStart+stride+beamCount-1,
//  ...
//  beamStart+stride*(strideCount-1), beamStart+stride*(strideCount-1)+1, ..., beamStart+stride*(strideCount-1)+beamCount-1]
// for a total of beamCount*strideCount beam indexes. Each beam index is a list of weights.
// Example: beamStart=5, beamCount=3, stride is whatever and strideCount=1.
// In this case beams 5,6,7 will be sent.
// If stride=5 and strideCount=2, then additionally beams 10,11,12 will be sent
// as the next 3 elements of antWgtCode.
// If strideCount=3, then additionally beams 15,16,17 will be sent.
// This is used to sample a big beam space (e.g. for 2d scan), e.g. if we want
// 3 beams out of every 5, out of a total of 500
struct PhyAntWgtTblPart {
  1: byte module; // RFIC Module
  2: byte isRx; // 0 - Tx, 1 - Rx
  3: i16 beamStart; // Index of first beam in list
  4: i16 beamCount; // Number of consecutive beams
  5: i16 stride;    // Difference between start beam indexes in consecutive strides
  6: i16 strideCount; // Number of strides (of beamCount beams each)
  7: list<list<i16>> antWgtCode; // Antenna Weight, beamCount*strideCount lists of i16's
}

// Codebook table part. Used in JSON files
struct PhyAntWgtTblPartJson {
  1: string rfModuleName; // RFIC Module Name
  2: byte module; // RFIC Module
  3: byte tablePartId; // Part of Table
  4: byte isRx; // 0 - Tx, 1 - Rx
  5: list<list<i16>> antWgtCode; // Antenna Weight
}

// struct for each RFIC, 4 parts per table
struct PhyAntWgtTable {
  1: list<PhyAntWgtTblPartJson> tableParts;
}

// List of codebooks for all RFIC Modules
// Contains multiple (14) tables to form a code book
// Contain 14 tables, 7 Tx and 7 Rx for all RFICs.
struct PhyAntWgtCodeBookConfig {
  1: string description; // Description of the codebook >
  2: list<PhyAntWgtTable> codeBookTables;
}

// Request the antenna weights for the given channel and beams
// See PhyAntWgtTblPart for how beam selection works
struct CodebookFetchReq {
  1: byte channel;
  2: i16 beamStart; // Index of first beam in list
  3: i16 beamCount; // Number of consecutive beams
  4: i16 stride;    // Difference between start beam indexes in consecutive strides
  5: i16 strideCount; // Number of strides (of beamCount beams each)
}

// Struct for setting GPS time.  Note there is already a
// GpsTime thrift object defined by controller, but it contains
// additional fields not useful for this purpose.
struct GpsTimeValue {
  // IMPORTANT!! This is actually GPS time, *NOT* UTC time.
  1: i64 unixTimeSecs;  // Seconds value at most recent PPS
  2: i64 unixTimeNsecs;  // Nanoseconds portion
}

// HTSF information (for OTA sync)
struct HtsfInfo {
  1: i64 ppsHwTsfUs;  // PPS HW TSF value (in us)
  2: i32 ppsHwTsfNs;  // PPS HW TSF value (ns portion)
  3: i64 ppsSwTsfUs;  // PPS SW TSF value (in us)
  4: i32 ppsSwTsfNs;  // PPS SW TSF value (ns portion)
  5: i32 delayEstNs;  // Propagation delay estimate (in ns)
}

// struct for configuring Golay Tx and Rx Sequences
// used in PtMsgTypes = SB_PHY_GOLAY_SEQUENCE_CONFIG
struct PhyGolaySequenceConfig {
  1: string description; // Description
  2: list<list<i64>> transmitSequence; // Transmit Sequence
  3: list<i32> receiveSequence; // Receive Sequence
}

// struct for debugging
// used by  PtMsgTypes = SB_DEBUG
struct Debug {
  1: string cmdStr;
  2: i64 value;
}

// struct for channel configuration
// used by PtMsgTypes = SB_CHANNEL_CONFIG
struct ChannelConfig {
  1: byte channel; // operating channel for initiator
}

// struct for bf responder scan configuration
// used by PtMsgTypes = SB_BF_RESP_SCAN_CONFIG
struct BfRespScanConfig {
  1: bool cfg;
  2: optional string radioMac;  // !! for backwards compatibility !!
}

// struct for log configuration
// used by SB_SET_LOG_CONFIG
struct SetLogConfig {
  1: map<Controller.LogModule, Controller.LogLevel>
     (cpp.template = "std::unordered_map") configs;
} (no_default_comparators)

// struct for PtMsgTypes = SB_PHY_TPC_ADJ_TBL_CFG
struct PhyTpcAdjTblCfg {
  1: byte channel; // Operating channel
  2: list<byte> txPowerAdjTable;
}

// Request to fetch txPower adjustment table for the specified channel
struct TpcAdjTblFetchReq {
  1: byte channel; // Operating channel
}

// enum for the type of destination as marked by sender
enum PtMsgDest {
    E2E = 0, // e2e
    DBG = 1, // dbg app
    E2E_DBG = 2, // both e2e, dbg app
    SB = 3, // generic value for SB msg
}

// struct for pass through message
struct PassThruMsg {
  1: PtMsgTypes msgType;
  2: PtMsgDest dest;
  3: i32 cookie; // can be set to group together multiple messages
  // only one of the fields below would be used
  4: Stats stats;
  5: Ack ack;
  6: DissocMsg dissoc;
  // 7: (deprecated in RELEASE_M27)
  8: StatsConfigure statsConfigure;
  9: PhyLAConfig phyLAConfig;
  10: PolarityConfig polarityCfg;
  11: GolayConfig golayCfg;
  12: PhyAntWgtTblPart phyAntWgtTblPart;
  13: AssocMsg assoc;
  14: Debug debug;
  15: BWAllocation.NodeAirtime airtimeAllocMap;
  16: Controller.ScanReq scanReq;
  17: Controller.ScanResp scanResp;
  18: Controller.BfSlotExclusionReq bfSlotExclusionReq;
  19: PhyAgcConfig phyAgcConfig;
  20: PhyGolaySequenceConfig phyGolaySequenceCfg;
  21: SetFwParamsMsg setfwParamsReq;
  22: PhyTpcConfig phyTpcConfig;
  23: ChannelConfig channelCfg;
  24: BfRespScanConfig bfRespScanCfg;
  25: GetFwParamsReq getFwParamsReq;
  26: GetFwParamsResp getFwParamsResp;
  27: FwHealthReport fwHealthReport;
  28: SetFwParamsResp setFwParamsResp;
  29: FwRoutingInfo fwRoutingInfo;
  30: SetLogConfig setLogConfig;
  31: Controller.FwAdjResp fwAdjResp;
  32: CodebookFetchReq codebookFetchReq;
  // 33: (deprecated in RELEASE_M48)
  34: GpsTimeValue gpsTimeValue;
  35: HtsfInfo htsfInfo;
  36: PhyTpcAdjTblCfg phyTpcAdjTblCfg;
  37: TpcAdjTblFetchReq tpcAdjTblFetchReq;
}
