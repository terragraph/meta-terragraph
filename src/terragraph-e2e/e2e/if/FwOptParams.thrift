/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

namespace cpp2 facebook.terragraph.thrift
namespace py terragraph_thrift.FwOptParams
namespace lua terragraph_thrift.FwOptParams

// -----------------------------------------------------------------------------
// Firmware configuration parameters
// After making changes to this file, please update config metadata at:
//   ../config/config_metadata.json
// -----------------------------------------------------------------------------
struct FwOptParams {
  1:  optional i64 antCodeBook;
  // 4: numOfPeerSta (deprecated in RELEASE_M27)
  7:  optional i64 gpioConfig; // deprecated
  10: optional i64 mcs;
  11: optional i64 txPower;
  12: optional i64 rxBuffer;
  13: optional i64 beamConfig;
  14: optional i64 txBeamIndex;
  15: optional i64 rxBeamIndex;
  18: optional i64 numOfHbLossToFail;
  19: optional i64 statsLogInterval;
  20: optional i64 statsPrintInterval;
  21: optional i64 forceGpsDisable;
  22: optional i64 lsmAssocRespTimeout;
  23: optional i64 lsmSendAssocReqRetry;
  24: optional i64 lsmAssocRespAckTimeout;
  25: optional i64 lsmSendAssocRespRetry;
  26: optional i64 lsmRepeatAckInterval;
  27: optional i64 lsmRepeatAck;
  28: optional i64 lsmFirstHeartbTimeout;
  29: optional i64 txSlot0Start;
  30: optional i64 txSlot0End;
  31: optional i64 txSlot1Start;
  32: optional i64 txSlot1End;
  33: optional i64 txSlot2Start;
  34: optional i64 txSlot2End;
  35: optional i64 rxSlot0Start;
  36: optional i64 rxSlot0End;
  37: optional i64 rxSlot1Start;
  38: optional i64 rxSlot1End;
  39: optional i64 rxSlot2Start;
  40: optional i64 rxSlot2End;
  42: optional i64 linkAgc;
  43: optional i64 respNodeType;
  44: optional i64 txGolayIdx;
  45: optional i64 rxGolayIdx;
  46: optional i64 bfAgc;
  47: optional i64 tpcEnable;
  48: optional i64 tpcRefRssi;
  49: optional i64 tpcRefStfSnrStep1;
  50: optional i64 tpcRefStfSnrStep2;
  51: optional i64 tpcDelPowerStep1;
  52: optional i64 tpcDelPowerStep2;
  53: optional i64 bfMode;
  // 54: bwHandlerMode (deprecated in RELEASE_M27)
  55: optional i64 tpcRefStfSnrStep3;
  56: optional i64 tpcDelPowerStep3;
  57: optional i64 minTxPower;
  58: optional i64 tpcAlphaDownRssiStep3Q10;
  59: optional i64 tpcAlphaUpRssiStep3Q10;
  60: optional i64 laInvPERTarget;
  61: optional i64 laConvergenceFactordBperSFQ8;
  62: optional i64 laMaxMcs;
  63: optional i64 laMinMcs;
  64: optional i64 maxAgcTrackingMargindB;
  65: optional i64 maxAgcTrackingEnabled;
  66: optional i64 noLinkTimeout;
  67: optional i64 wsecEnable;
  68: optional i64 key0; // deprecated
  69: optional i64 key1; // deprecated
  70: optional i64 key2; // deprecated
  71: optional i64 key3; // deprecated
  72: optional i64 controlSuperframe;
  73: optional i64 tpcAlphaUpTargetRssiStep3Q10;
  74: optional i64 crsScale;
  75: optional i64 tpcAlphaDownTargetRssiStep3Q10;
  76: optional i64 tpcHysteresisdBStep3Q2;
  77: optional i64 measSlotEnable; // deprecated in RELEASE_M31
  78: optional i64 measSlotPeriod; // deprecated in RELEASE_M31
  79: optional i64 measSlotOffset; // deprecated in RELEASE_M31
  80: optional i64 latpcUseIterations;
  81: optional i64 maxTxPower;
  82: optional i64 polarity;
  83: optional i64 linkImpairmentDetectionEnable;
  84: optional i64 linkImpairmentShortPending;
  85: optional i64 linkImpairmentShortUp;
  86: optional i64 linkImpairmentLongPending;
  87: optional i64 maxTxPowerPerMcs;
  88: optional i64 topoScanEnable;
  89: optional i64 restrictToSfParity;
  90: optional i64 maxAgcIfGaindBperIndexQ8;
  91: optional i64 maxAgcMaxRfGainIndex;
  92: optional i64 maxAgcMinRfGainIndex;
  93: optional i64 maxAgcMaxIfGainIndex;
  94: optional i64 maxAgcMinIfGainIndex;
  95: optional i64 maxAgcMaxIfSweetGainRange;
  96: optional i64 maxAgcMinIfSweetGainRange;
  97: optional i64 maxAgcMinRssi;
  98: optional i64 maxAgcRawAdcScaleFactorQ8;
  99: optional i64 maxAgcRfGaindBperIndexQ8;
  100: optional i64 maxAgcTargetRawAdc;
  101: optional i64 maxAgcUseMinRssi;
  102: optional i64 maxAgcUseSameForAllSta;
  103: optional i64 maxAgcRfGainHiLo;
  104: optional i64 cb2Enable;
  105: optional i64 maxTxPowerPerMcsEdmg;
  106: optional i64 noTrafficMaxMcsFallback;
  107: optional i64 txPowerTableLinear;
  108: optional i64 autoPbfEnable;
  109: optional i64 channel;
  110: optional i64 ibfProcedureType;
  111: optional i64 ibfNumberOfBeams;
  112: optional i64 latpcLinkImpairConfig;
  113: optional i64 latpc100PercentPERDrop;
  114: optional i64 ibfSet1RficBitmap;
  115: optional i64 ibfSet2RficBitmap;
  116: optional i64 ibfCodebookVariant;
  117: optional i64 useUpdateAwvForPbf;
  118: optional i64 latpcBlerToPer;
  119: optional i64 mtpoEnabled;
  120: optional i64 mtpoPhaseHysteresis_dBQ2;
  121: optional i64 ibfUseRssi;
  122: optional i64 mcsLqmQ3_1_4;
  123: optional i64 mcsLqmQ3_5_8;
  124: optional i64 mcsLqmQ3_9_12;
  125: optional i64 mcsLqmQ3_13_16;
  126: optional i64 maxTxPowerSet1;
  127: optional i64 autoPbfMtpoTxPower;
  128: optional i64 rxMaxMcs;
  129: optional i64 tcpTuningConfig;
  130: optional i64 htsfMsgInterval;
  131: optional i64 htsfSyncEnable;
  132: optional i64 htsfRfSyncKpKi;
  133: optional i64 htsfPpsSyncKpKi;
  134: optional i64 tpcPBEnable;
  135: optional i64 msduPerMpdu;
}

// Firmware configuration parameters for a node
struct NodeFwParams {
  // optional parameters for node init
  1: FwOptParams nodeInitOptParams;
  // optional parameters for a link incident with this node
  2: FwOptParams linkOptParams;
}
