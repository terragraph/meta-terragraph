/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * Interface file for firmware pass through messages
 * used __packed___ for structs
 * ideally keep struct member in order of decreasing alignments
 *
 * copied from struct_alignment.py
 * definitions
 * 1) Alignment constraint: 8bit=1, 16bit=2, 32bit/64bit=4
 * 2) Alignment of struct/union = max of alignment of members
 * throws exception if input violates:
 * 1) All structure structure members should be aligned
 * 2) Size of struct/union should be multiple of its alignment
 * 3) Enums should not be used (just definition is fine)
 *
 * stats are cross referenced here, please update:
 * https://fb.quip.com/UlzeALfZdqfe#fCbACAjTi0O
 *
 */

#ifndef FB_TG_FW_PT_IF_H
#define FB_TG_FW_PT_IF_H

#ifndef TG_FIRMWARE
// Anything in this block will be hidden to firmware compiler
#include <stdint.h>
#endif

#define TG_ENABLE_COMPAT_M44_20 1

#define TGF_BW_NF 4
#define TGF_BW_NSF 16
#define TGF_BW_SLOTSPERFRAME 3
#define TGF_INVALID_LINKID 0xff
// number of (32-bit) words used for tgfStatsType = TGF_STATS_MGMT_DATA
#define TGF_STATS_MGMT_DATA_W_LEN 20
// LQM for each MCS....for LA Lookup
#define TGF_MCS_LQM_NUM 17
#define TGF_MAX_MCS_DMG 12
#define TGF_MAX_MCS_EDMG 16
// The maximum number of responses per sweep in topology scan
// This macro should be synced up with MAX_NUM_TOPO_RESP in BF module
#define TGF_MAX_TOPO_RESP 20

// Should be greater or equal to FB_MAX_ACTION_TYPE
#define TGF_NUM_MESSAGE_ACTIONS 16

#define TGF_NUM_GOLAY_SEQ_COUNT 8

// number of key value pushed to per call
#define TGF_STATS_CALIB_NUM 100

// free running AGC means RF = 15, IF = 31
// the AGC is a bitmap bitmap with bit 15 indicating that the value is a max
// bits 4:8 are the IF and bits 0:3 are the RF
// therefore, a free-running AGC is 2^15 + 31*16 + 15 = 33279
#define FREE_RUNNING_AGC 33279
// RF gain [0:3]: 0 --> G0, 1 --> G1
#define FREE_RUNNING_AGC_G0 33264
// for scans, to signify an invalid power index
#define POWER_INDEX_NOT_USED 0xff
// indicates log is because of periodic timer
#define POWER_INDEX_PERIODIC 0xfe
// indicates runtime configuration
#define POWER_INDEX_RUNTIME 0xfd

// these values should match with those in fb_backhaul.h
#define TGF_MAX_NUM_MTPO_PHASES 4 /**< support up to 4 antenna tiles */
#define TGF_MTPO_SWEEP_SECTORS 64 /**< support up to 64 sectors (beams) */

#define TGF_MTPO_SWEEP_PHASES 64 /**< support up to 64 phases (4 x 4 x 4) */

/**
 * enum for type of statistics
 */
typedef enum {
    TGF_STATS_STA_PKT,
    TGF_STATS_TEST_A,
    TGF_STATS_TEST_B,
    TGF_STATS_GPS,
    TGF_STATS_PHYSTATUS,
    TGF_STATS_PHYSTATUS_MGMT_HF,
    TGF_STATS_MGMT_TX,
    TGF_STATS_MGMT_RX,
    TGF_STATS_SLOT,
    TGF_STATS_BWHAN_LINK,
    TGF_STATS_BWHAN_SYS,
    TGF_STATS_BF,
    TGF_STATS_RECV_MGMT,
    TGF_STATS_MGMT_DATA,
    TGF_STATS_MEM,
    TGF_STATS_MISC_SYS,
    TGF_STATS_MISC_LINK,
    TGF_STATS_LA_HIGH_SPEED,
    TGF_STATS_TPC_HIGH_SPEED,
    TGF_STATS_TSF,
    TGF_STATS_TPC,
    TGF_STATS_LA_TPC,
    TGF_STATS_LIFETIME_EXPIRED,
    TGF_STATS_LIFETIME_OK,
    TGF_STATS_MAX_AGC,
    TGF_STATS_CALIBRATE,
    TGF_STATS_PHY_PERIODIC,
    TGF_STATS_PHYSTATUS_PRINT_HF,
    TGF_STATS_CHN,
    TGF_STATS_LA_TPC_PRINT_HF,
    TGF_STATS_LINK_DOWN,
    TGF_STATS_SECURITY,
    TGF_STATS_RADIO,
    TGF_STATS_BF_SCAN,
    TGF_STATS_MAX_AGC_HIST,
    TGF_STATS_MTPO,
    TGF_STATS_NUM
} tgfStatsType;

#define SNR_HISTOGRAM_LEN 32
#define TGF_HF_MACS_NUM 2 // up to 2 MAC addresses

/**
 * struct for high speed link adaptation statistics
 * tgfStatsType = TGF_STATS_LA_HIGH_SPEED
 */
typedef struct __attribute__((__packed__)) {
    uint16_t mcs;
} tgfStatsLaHighSpeed;

/**
 * struct for high speed TPC statistics
 * tgfStatsType = TGF_STATS_TPC_HIGH_SPEED
 */
typedef struct __attribute__((__packed__)) {
    uint16_t txPowerIndex;
} tgfStatsTpcHighSpeed;

/**
 * struct for transmit power control (TPC)
 * tgfStatsType = TGF_STATS_TPC
 */
typedef struct __attribute__((__packed__)) {
    // Effective SNR
    int32_t effSnrQ10;
    // the filtered RSSI
    int32_t tsIirRssiQ10;
    // filtered target RSSI based on management packets
    int32_t tsIirRssiTargetMgmtQ10;
} tgfStatsTpc;

// parameters that apply to all links on a node for TPC
#define TGF_TPC_GAIN_INDEX_TBL_SIZE 32
typedef struct __attribute__((__packed__)) {
    // power index to transmit power table; table is approximate
    // absolute values are not used, only the differences are used
    int16_t powerQ2AtIndex[TGF_TPC_GAIN_INDEX_TBL_SIZE];
} tgfPhyTpcNodeConfig;

// parameters that apply to each link for TPC
typedef struct __attribute__((__packed__)) {
    // for internal debugging
    uint32_t debugtpc;
    // reference (target) SNR used for step1: if the reported SNR > refStfSnr
    // and the reported RSSI > refRssi then lower the txPower index according to
    // reported RSSI - refRssi but not more than delPowerStep1
    int16_t refStfSnrStep1Q2;
    // reference (target) SNR used for step2: if the reported SNR > refStfSnr
    // then lower the txPower index according to reported reported STF SNR -
    // refStfSnrStep2Q2 but not more than delPowerStep2
    int16_t refStfSnrStep2Q2;
    // see refStfSnrStep1Q2
    int16_t refRssiStep1Q2;
    // see refStfSnrStep1Q2
    int16_t delPowerStep1Q2;
    // see refStfSnrStep2Q2
    int16_t delPowerStep2Q2;

    // step3 parameters - step 3 means LINK_UP (run-time) tracking

    // limit the change of txPower in any one step to delPowerStep3 (in dB)
    uint16_t delPowerStep3Q2;
    // target STF SNR for step 3
    int16_t refStfSnrStep3Q2;
    // a single pole IIR filter is applied on the RSSI. If the new value
    // is greater than the average, then alphaUpRssiQ10 is used, otherwise,
    // alphaDownRssiQ10 is used; use alphaUpRssiQ10 >
    // alphaDownRssiQ10 to set the power conservatively because a lower
    // estimated RSSI results in higher power
    uint16_t alphaUpRssiQ10;
    uint16_t alphaDownRssiQ10;
    // a single pole IIR filter is applied on the target RSSI. If the new value
    // is greater than the average, then alphaUpTargetRssiQ10 is used,
    // otherwise, alphaDownTargetRssiQ10 is used; use
    // alphaDownTargetRssiQ10 >
    // alphaUpTargetRssiQ10 to set the power conservatively because a higher
    // estimate RSSI target results in higher power
    uint16_t alphaUpTargetRssiQ10;
    uint16_t alphaDownTargetRssiQ10;
    // TPC statistics reporting to kern.log and fw_stats done at this interval
    // in BWGDs (25.6ms)
    uint16_t statsReportingPeriodBwgd;
    // don't change the txPower index unless the absolute value of the
    // requested change exceeds the hysteresis
    uint16_t tpcHysteresisdBQ2;
    // set to 1 to enable, 0 to disable tracking, if disabled, transmit power
    // will be fixed to txPowerIndex
    uint8_t tpcEnable;
    // see tpcEnable
    int8_t txPowerIndex;
    // see tpcEnable, this is the upper limit for step 3;
    // used in diversity mode, single tile, massive for set2RficBitmap
    // or massive for set1RficBitmap if set1RficBitmap == set2RficBitmap
    int8_t maxTxPowerIndex;
    // used for massive mode while using set1RficBitmap if
    // set1RficBitmap != set2RficBitmap
    int8_t maxTxPowerIndexSet1;
    // minimum txPower index level for step 3
    uint8_t minTxPowerIndex;
    // 1 - Enable, 0 - Disable. If enabled, the maxTxPowerIndex gets adjusted
    // per beam in specific channel
    uint8_t tpcPBEnable;
} tgfPhyTpcLinkConfig;

typedef struct __attribute__((__packed__)) {
    tgfPhyTpcLinkConfig tpcLinkConfig;
    tgfPhyTpcNodeConfig tpcNodeConfig;
    uint8_t addr[6]; /**< station address */
    uint8_t pad[2];
} tgfPtPhyTpcConfig;

/**
 * structures for joint LA TPC algorithm
 * parameters that apply to all links on a node for LATPC
 */
typedef struct __attribute__((__packed__)) {
    // these values are the average number of iterations per codeword
    // to reach the target PER; iterAtPerTargetQ8[n] is for MCS=n
    uint16_t iterAtPerTargetQ8[TGF_MCS_LQM_NUM];
    // because of transmitter non-linearity, high order MCS
    // (those with 16QAM) can not be transmitted at the highest power
    // maxPowerPerMCS[n] is the highest power supported by MCS=n
    uint8_t maxPowerPerMCS[TGF_MCS_LQM_NUM];
    // multiplier factor to convert from syndrome error rate to PER
    // bler2perFactor[n] is for MCS=n
    uint8_t bler2perFactor[TGF_MCS_LQM_NUM];
} tgfPhyLaTpcNodeConfig;

/**
 * tgfStatsType = TGF_STATS_LA_TPC[_HIST]
 */
typedef struct __attribute__((__packed__)) {
    // offset used by the joint LA TPC algorithm - positive means conditions
    // are good - units are roughly in dB
    int32_t laTpcOffsetdBQ24;
    // number of superframes in which no traffic was detected
    uint32_t noTrafficCountSF;
    // LDPC statistics - running sum
    uint32_t nIter; // number of LDPC iterations
    uint32_t nSyn;  // number of syndromes
    uint32_t nCW;   // number of LDPC codewords
    int32_t effSnrQ10;
    uint32_t numSFsAtLowerLimit;
    int32_t txOk;
    int32_t txFail;
    int32_t rxOk;
    int32_t rxFail;
    uint32_t synPERQ16; // PER as calculated based on LDPC stats
    uint32_t nIterOrNumOfLongCalibSlots;
    uint32_t hfLogsInfo;
    uint32_t snrAndRssi;
    uint8_t txPowerIndex;
    uint8_t mcs;
    uint8_t maxTxPowerIndex;
    uint8_t pad8;
} tgfStatsLaTpc;

typedef struct __attribute__((__packed__)) {
    // reserved for debugging
    uint32_t debugtpc;
    // bits: 3:0 SFs with 100% PER
    // bits: 7:4 SFs for MISSED HB
    // bits 11:8 SFs for missed many HBs
    // bits 15:12 SFs for MCS@limit
    uint32_t latpcLinkImpairConfig;
    // time in BWGDs between fw_stats and kern.log reporting
    uint16_t statsReportingPeriodBwgd;
    // use LDPC iteration count if enabled
    // configure drop and TPC when there is 100% PER
    uint16_t latpc100PercentPERDrop;
    uint8_t latpcUseIterations;
    uint8_t pad[3];
} tgfPtPhyLaTpcConfig;

/**
 * struct for max AGC tracking
 * tgfStatsType = TGF_STATS_MAX_AGC[_HIST]
 */
#define AGC_RSSI_HIST_SIZE 90
typedef struct __attribute__((__packed__)) {
    // rssiHistXXX[rssidB] is the
    // number of BWGDs in the last interval (the interval length is configurable
    // - default is 390 BWGDs) during which the relative RSSI was reported as
    // rssidB (between rssidB-0.5 and rssi+0.499)
    // at the far-end receiver (for mgmt and data packets)
    uint16_t rssiHistMgmt[AGC_RSSI_HIST_SIZE];
    uint16_t rssiHistData[AGC_RSSI_HIST_SIZE];
    // current minimum RSSI
    int16_t minRssiQ8;
    // current setting for max IF gain
    uint8_t maxGainIndexIf;
    // current setting for max RF gain
    uint8_t maxGainIndexRf;
    // number of BWGDs in which the max AGC was free-running
    uint8_t numBwgdsInFreeRun;
    // IIR averaged RSSI
    int8_t iirAvgRssi;
    // number of BWGDs at which the gain values we read are set at the maxRssi
    // this indicates a problem; expectation is that actual gain is below
    // the max at all times
    uint8_t numBwgdsAtAgcMax;
    uint8_t pad[1];
} tgfStatsAgc;

/**
 * struct for station data packet statistics
 * generated at 1Hz (logInterval) from get_statistics()
 * tgfStatsType = TGF_STATS_STA_PKT
 */
typedef struct __attribute__((__packed__)) {
    uint32_t txOk;      /**< Number of successfully transmitted frames */
    uint32_t txFail;    /**< Number of transmission failures  */
    uint32_t rxOk;      /**< Number of successfully received frames */
    uint32_t rxFail;    /**< Number of CRC failed frames received
                         (Once CRC fail, cannot trust RA as well) */
    uint32_t rxPlcpFil; /**< Num received frames with HCS failed PLCP header */
    uint32_t perE6;     /**< instantaneous packet error rate x 10^6 */
    uint32_t rxPerE6;   /**< instantaneous packet error rate x 10^6 (rx) */
    uint32_t txBa;      /**< Number of block ack transmitted */
    uint32_t txPpdu;    /**< Number of ppdus transmitted */
    uint32_t rxBa;      /**< Number of block ack received */
    uint32_t rxPpdu;    /**< Number of ppdus received */
    uint32_t txLifetimeExp; /**< Total count of lifetime expired pkts */
    uint32_t rxDiscBuf;     /** Total RX Discard count by Buffer per STA */
    uint32_t
        rxDiscEnc; /** Total RX Discard count by Encryption failure per STA */
    uint32_t rxDiscRa;      /** Total RX Discard count by RA mismatch per STA */
    uint32_t rxDiscUnexp;   /** Total RX Discard count by Unexpected or PER
                               emulator per STA */
    uint32_t txSlotTime;    /** Total TX Data slot time in 256[us] unit */
    uint32_t txAirTime;     /** Total TX data air time in 256[us] unit */
    uint32_t linkAvailable; /** increments every BWGD if in LINK_UP state */
    uint32_t txSlotEff;     /** TX slot efficiency in units of 0.01% */
    uint32_t mgmtLinkUp;    /** increments every BWGD if link up for mgmt pkt */
    uint32_t txMpduCount;   /** Number of transmitted MPDUs */
    uint32_t rxMpduCount;   /** Number of received MPDUs */
    uint16_t mcs;           /**< MCS at the time of logging */
    uint8_t txPowerIndex;   /**< transmit power index */
    uint8_t pad[1];
} tgfStatsStaPkt;

/**
 * struct for data of test-A statistics
 * only used for unit tesing
 * tgfStatsType = TGF_STATS_TEST_A
 */
typedef struct __attribute__((__packed__)) {
    uint16_t beamRx;
    uint16_t phyStatus[4];
} tgfStatsTestA;

/**
 * struct for data of test-B statistics
 * only used for unit testing
 * tgfStatsType = TGF_STATS_TEST_B
 */
typedef struct __attribute__((__packed__)) {
    uint32_t txOk;
    uint32_t txFail;
    uint32_t rxOk;
    uint32_t rxFail;
    uint32_t rxPlcpFil;
} tgfStatsTestB;

/*
 * struct for gps module statistics
 * generated at 1Hz (logInterval)
 * tgfStatsType = TGF_STATS_GPS
 */
typedef struct __attribute__((__packed__)) {
    uint32_t numPpsErr;      /**< num of pps tsf read errors */
    uint32_t driverDelay;    /**< driver ioctl delay from pps tsf boundary */
    uint32_t maxDriverDelay; /**< max of driverDelay */
    uint32_t numTimelineErr; /**< num errors due to fw/driver taking moretime */
    uint32_t numMissedSec;   /**< num time driver did not send gps time */
    int32_t ppsJitter;       /**< jitter for the last pps tsf */
    uint32_t maxPpsJitter;   /**< max of ppsJitter */
    int32_t tsfDrift;        /**< cumulative drift in tsf */
    uint32_t ppsHwTsfL;      /**< HW TSF at last PPS, low word */
    uint32_t ppsHwTsfH;      /**< HW TSF at last PPS, high word */
    uint32_t ppsSwTsfL;      /**< SW TSF at last PPS, low word */
    uint32_t ppsSwTsfH;      /**< SW TSF at last PPS, high word */
    uint16_t ppsHwTsfNs;     /**< HW TSF at last PPS, ns portion */
    uint16_t ppsSwTsfNs;     /**< SW TSF at last PPS, ns portion */
} tgfStatsGps;

/*
 * struct for processed phystatus statistics from management and data packets
 * generated at 5KHz to 40Hz from fb_recv_mgmt()
 * tgfStatsType = TGF_STATS_PHYSTATUS
 */
#define PRINT_INTERVAL_UPDATE (1 << 0)
#define LOG_INTERVAL_UPDATE (1 << 1)
#define ALL_UPDATE 0xff

/* round x/2 */
#define TGF_DIV2(x) ((x + 1) / 2)

typedef enum {
    STATS_TYPE_DATA,
    STATS_TYPE_MGMT,
} tgfStatsEnum;

typedef struct __attribute__((__packed__)) {
    // normalized rxStart for last management packet
    uint32_t rxStartNormalized;
    // the PLCP packet length in Bytes
    uint32_t plcpLength;
    int16_t snrEstQ8;
    int16_t postSNRdBQ1;
    // receiver signal strength indicator (RSSI) (in dBm) measured during
    // management packets (KA,HB,ULBWREQ)
    int16_t srssi;
    // the raw ADC RSSI is the raw calculated RSSI after the ADC output but
    // before any post-processing to refer the RSSI back to the input
    // mainly used for debugging - units are related to dB but we're not
    // exactly sure
    int16_t rawAdcRssi;
    // LDPC statistics
    // number of syndromes is the number of failed LDPC codewords
    uint16_t numTotalSyndromes;
    // total number of codewords (N_CW in 802.11ad)
    uint16_t numTotalCodewords;
    // the total number of LDPC iterations over all N_CW codewords
    uint16_t ldpcIterations;
    // rx mcs calculated from plcp_0
    uint16_t rxMcs;
    // 16bit bus, can configure what PHY register is routed to it
    // on the fly by setting a register in ucode
    uint16_t dbg16;
    uint8_t hcs;
    uint8_t ldpc;
    // the IF gain index (range 0-31) is read when we receive a mgmt or data
    // packet; it is the gain setting that was used to receive the packet
    uint8_t gainIndexIf;
    // the RF gain index (range 0-15) is read when we receive a mgmt or data
    // packet; it is the gain setting that was used to receive the packet
    uint8_t gainIndexRf;
    // for logging, set the updatedFlag when these parameters are updated and
    // clear it when they are logged
    uint8_t updatedFlag;
    // the maximum IF and RF gains set by max AGC tracking
    uint8_t maxGainIndexIf;
    uint8_t maxGainIndexRf;
    // tgfStatsEnum
    uint8_t type;
    uint8_t pad[2];
} tgfStatsPhystatus;

/*
 * struct for tx management packet  statistics
 * generated at 1Hz (logInterval)
 * tgfStatsType = TGF_STATS_MGMT_TX, TGF_STATS_MGMT_RX
 */
typedef struct __attribute__((__packed__)) {
    uint32_t bfTrainingReq;       /**< counter for BF_TRAINING_REQ */
    uint32_t bfTrainingRsp;       /**< counter for BF_TRAINING_RSP */
    uint32_t bfTrainingRspAck;    /**< counter for BF_TRAINING_RSP_ACK */
    uint32_t bfTrainingUrx;       /**< counter for BF_TRAINING_URX */
    uint32_t assocReq;            /**< counter for ASSOC_REQ */
    uint32_t assocRsp;            /**< counter for ASSOC_RSP */
    uint32_t assocRspAck;         /**< counter for ASSOC_RSP_ACK */
    uint32_t keepAlive;           /**< counter for KEEP_ALIVE */
    uint32_t heartBeat;           /**< counter for HEART_BEAT */
    uint32_t uplinkBwreq;         /**< counter for UPLINK_BWREQ */
    uint32_t bfRetrainingReq;     /**< counter for BF_RETRAINING_REQ */
    uint32_t dissocReq;           /**< counter for DISASSOC_REQ */
    uint32_t bfRetrnUrxChgReq;    /**< counter for BF_RETRN_URX_CHG_REQ */
    uint32_t bfRetrnUrxChgReqAck; /**< counter for BF_RETRN_URX_CHG_REQ_ACK */
} tgfStatsMgmt;

/*
 * struct for slot programming statistics
 * generated at 1Hz (logInterval)
 * tgfStatsType = TGF_STATS_SLOT
 */
typedef struct __attribute__((__packed__)) {
    uint32_t
        numOfTxBfSlotsPgmrd; /**< counter for BH_SLOT_TYPE_BEAMFORMING_TX */
    uint32_t
        numOfRxBfSlotsPgmrd; /**< counter for BH_SLOT_TYPE_BEAMFORMING_RX */
    uint32_t numOfTxDataSlotsPgmrd; /**< counter for BH_SLOT_TYPE_TX */
    uint32_t numOfRxDataSlotsPgmrd; /**< counter for BH_SLOT_TYPE_RX */
    uint32_t numOfShortCalibSlots;  /**< counter for short calibration slots
                                       (duration <= 200us) */
    uint32_t numOfLongCalibSlots;   /**< counter for long calibration slots
                                       (duration > 200us) */
} tgfStatsSlot;

/*
 * struct for bandwidth handler statistics
 * generated at 1Hz (logInterval)
 * tgfStatsType = TGF_STATS_BWHAN_SYS
 */
typedef struct __attribute__((__packed__)) {
    uint32_t totalTxAssocTime; /**< Total TX time used for Assoc phase (in us)
                                */
    uint32_t totalRxAssocTime; /**< Total RX time used for Assoc phase (in us)
                                */
} tgfStatsBwhanSys;

/*
 * struct for per link bandwidth handler statistics
 * generated at 1Hz (logInterval)
 * tgfStatsType = TGF_STATS_BWHAN_LINK
 */
typedef struct __attribute__((__packed__)) {
    uint32_t totalTxDataTimeUs;   /**< Total TX time (in us) */
    uint32_t totalRxDataTimeUs;   /**< Total RX time (in us) */
    uint32_t totalTxDataSlots;    /**< Total number of TX slots */
    uint32_t totalRxDataSlots;    /**< Total number of RX slots */
    uint32_t txCtrlFallbackCount; /**< Tx map control-only fallback counter */
    uint32_t rxCtrlFallbackCount; /**< Rx map control-only fallback counter */
    uint32_t localBytesPending;   /**< Queue size (in bytes) */
    uint32_t localArrivalRate;    /**< Arrival rate (in bytes/ms) */
    uint32_t peerBytesPending;    /**< Queue size at peer (in bytes) */
    uint32_t peerArrivalRate;     /**< Arrival rate at peer (in bytes/ms) */
    uint8_t currTxTimePercent;    /**< Current L2 TX time percentage */
    uint8_t currRxTimePercent;    /**< Current L2 RX time percentage */
    uint8_t currTxSlotPercent;    /**< Current TX slot percentage */
    uint8_t currRxSlotPercent;    /**< Current RX slot percentage */
} tgfStatsBwhanLink;

/*
 * struct for BF statistics
 * generated at 5KHz to 40Hz (TGF_STATS_BF)
 * generated every 5 minutes (TGF_STATS_PHY_PERIODIC)
 * tgfStatsType = TGF_STATS_BF/TGF_STATS_PHY_PERIODIC
 */
typedef struct __attribute__((__packed__)) {
    int16_t pktLqm;       /**< Packet LQM */
    int16_t pktRssi;      /**< Packet RSSI */
    uint16_t rxStart;     /**< rx Start based on TSF */
    uint8_t mode;         /**< SYNC or ASYNC mode */
    uint8_t msgType;      /**< REQ / RSP / ACK ? */
    uint8_t txBeamIdx;    /**< Transmit Beam Index */
    uint8_t rxBeamIdx;    /**< Receive Beam Index */
    int8_t dblPktIdx;     /**< Double Packet Index */
    int8_t frmNumBfWin;   /**< Frame number in BF window */
    int8_t frmNumInSf;    /**< Frame number in SF */
    uint8_t txRficBitmap; /**< Transmit RFIC Bitmap */
    uint8_t rxRficBitmap; /**< Receive RFIC Bitmap */
    uint8_t pad[1];
} tgfStatsBf;

/*
 * struct for received management packet info, sent as statistics
 * generated at 5KH to 40Hz from fb_recv_mgmt()
 * tgfStatsType = TGF_STATS_RECV_MGMT
 */
typedef struct __attribute__((__packed__)) {
    tgfStatsPhystatus phyStatus;
    uint16_t rxstart;   /**< same as fb_mgmt_rxdesp_t.rxstart */
    uint16_t size;      /**< same as fb_mgmt_rxdesp_t.size */
    uint16_t beamRx;    /**< same as fb_rx_phystatus_t.beamRx */
    uint8_t actionCode; /**< same as sMgmtPkt.mgmtHdr.actionCode */
    uint8_t pad[1];
} tgfStatsRecvMgmt;

/*
 * struct for received management packet data, sent as statistics
 * generated at 5KHz to 40Hz from fb_recv_mgmt()
 * tgfStatsType = TGF_STATS_MGMT_DATA
 */
typedef struct __attribute__((__packed__)) {
    uint32_t w[TGF_STATS_MGMT_DATA_W_LEN];
} tgfStatsMgmtData;

// This has to be the same as FB_RF_MAX_NUM_STATS
#define TGF_STATS_NUM_RADIO (7)
/*
 * struct for miscellaneous system statistics
 * generated at 1Hz (logInterval)
 * eTgfStatsType = TGF_STATS_MISC_SYS
 */
typedef struct __attribute__((__packed__)) {
    uint32_t numMissedSfm; /**< num superframes with missed slots programming */
    uint32_t malloc;       /**< malloc'ed bytes */
    uint32_t free;         /**< free'ed bytes */
    uint32_t numFrameTimer; /**< number of times tgfFrameTimer() got called */
    uint32_t rfToGps;       /**< number of transitions from rf to gps sync */
    uint32_t gpsToRf;       /**< number of transitions from gps to rf sync */
    uint32_t getPktBuf;     /**< counter for getPktBuf() call */
    uint32_t recvMgmt;      /**< counter for fb_recv_mgmt() call */
    uint32_t freePktBuf;    /**< counter for freePktBuf() call */
    uint32_t sendMgmtCB;    /**< counter for fb_send_mgmtCB() call */
    uint32_t txstatusNoSta; /**< counter for FB_TXSTATUS_NO_STA */
    uint32_t
        mgmtRxIncorrectHdr; /**< counter for correct hdr.category and hdr.oui */
    uint32_t
        numBcastImTrnReqSent; /**< counter for Bcast BF_RETRAINING_REQ sent */
    uint32_t numBcastImTrnReqRecvd;      /**< counter for Bcast
                                               BF_RETRAINING_REQ received */
    uint32_t numIncorrectBcastPktsRecvd; /**< counter for unexpected
                                            Broadcast messages */
    uint16_t cpuLoadAvg;                 /**< expressed as an integer percent */
    int16_t rftemperature0;              /**< RFIC-0 temperature, degrees C */
    int16_t rftemperature1;              /**< RFIC-1 temperature, degrees C */
    int16_t rftemperature2;              /**< RFIC-2 temperature, degrees C */
    int16_t rftemperature3;              /**< RFIC-3 temperature, degrees C */
    int16_t iftemperature; /**< Baseband (IF) temperature, degrees C */
} tgfStatsMiscSys;

/*
 * struct for topology scan statistics
 */
typedef struct __attribute__((__packed__)) {
    uint32_t numReqRecvd;  /* The number of REQs received */
    uint32_t numRespRecvd; /* The number of RSPs received */
    uint32_t sweepRespStats[TGF_MAX_TOPO_RESP]; /* sweepRespStats[i]: the number
                                                   of sweeps with i RSPs */
} tgfStatsTopo;

/*
 * struct for miscellaneous per link statistics
 * generated at 1Hz (logInterval)
 * eTgfStatsType = TGF_STATS_MISC_LINK
 */
typedef struct __attribute__((__packed__)) {
    uint32_t dataTxSlotDur; /**< duration in us of BH_SLOT_TYPE_TX */
    uint32_t dataRxSlotDur; /**< duration in us of BH_SLOT_TYPE_RX */
    uint32_t bfTxSlotDur; /**< duration in us of BH_SLOT_TYPE_BEAMFORMING_TX */
    uint32_t bfRxSlotDur; /**< duration in us of BH_SLOT_TYPE_BEAMFORMING_RX */
    uint32_t txstatusFlagAck;     /**< counter for FB_TXSTATUS_FLAG_ACK */
    uint32_t txstatusLifetimeExp; /**< counter for
                                     FB_TXSTATUS_LIFETIME_EXPIRED */
    uint32_t txstatusFlushed;     /**< counter for FB_TXSTATUS_FLUSHED */
    uint32_t currentLinkState;    /**< from fsmState */
    uint16_t mtpoRunCounter;      /**< increments when MTPO is triggered (and FB
                                     response is OK) at initiator */
    uint16_t mtpoSuccessCounter;  /**< increments when MTPO runs successfully at
                                     initiator */
    uint16_t
        mtpoApplyNewPhaseCounter; /**< increments when MTPO runs successfully at
                              initiator and applies a new phase */
    uint16_t
        mtpoRejectCounter; /**< increments when MTPO requested but rejected */
    uint16_t mtpoFailCounter; /**< initiator response indicates failure */
    uint16_t mtpoResponderTimeoutCounter; /**< response from responder with
                                             timeout */
    uint16_t
        mtpoCurrentPhases; /**< 0xABCD where A,B,C,D are the phases (D is tile
                              0); only populated after MTPO succeeds */
    uint16_t pad16[1];
} tgfStatsMiscLink;

/*
 * struct for memory management statistics
 * generated at 10KHz from fb_malloc(), mfree()
 * eTgfStatsType = TGF_STATS_MEM
 */
typedef struct __attribute__((__packed__)) {
    uint32_t mallocSize; /**< counter for fb_malloc() */
    uint32_t mfreeSize;  /**< counter for mfree() */
} tgfStatsMem;

/*
 * struct for tsf module statistics
 * generated at 10Hz
 * eTgfStatsType = TGF_STATS_TSF
 */
typedef struct __attribute__((__packed__)) {
    uint32_t numRfFix;   /**< num of times tsf offset fixed on rf sync */
    uint32_t numGpsFix;  /**< num of times tsf offset fixed on gps sync */
    int32_t rfDrift;     /**< current tsf drift wrt rf link */
    int32_t sumRfFix;    /**< sum of tsf fixes for rf sync */
    int32_t sumGpsFix;   /**< sum of tsf fixes for gps sync */
    uint32_t offsetL;    /**< current offset = sw_tsf - hw_tsf */
    uint32_t offsetH;    /**< high word of current offset */
    int16_t driftPerWin; /**< average drift per window */
    uint8_t syncModeGps; /**< set if tsf is gps synced */
    uint8_t syncModeRf;  /**< set if tsf is synced through rf links */
} tgfStatsTsf;

/*
 * struct for channel module statistics
 * generated at once per minute
 * eTgfStatsType = TGF_STATS_CHN
 */
typedef struct __attribute__((__packed__)) {
    uint32_t maxTickCodebookFetch; /**< max time spent to fetch codebook */
    uint32_t maxTickChannelChange; /**< max time spent to change channel */
    uint32_t errInvalidChnIn;      /**< count for invalid input error */
    uint32_t errSetChn;            /**< count for error in set_channel() */
    uint32_t errAssoc;             /**< count for errors in assoc */
    uint32_t numSwitches;          /**< count for channel switch attempts */
    uint8_t state;                 /**< last state */
    uint8_t channel;               /**< last operating channel */
    uint8_t configuredChannel;     /**< last configured Channel */
    uint8_t pad[1];
} tgfStatsChn;

/*
 * struct for tsf module statistics
 * generated at 1Hz (logInterval) - counters are accumlative
 * eTgfStatsType = TGF_STATS_LIFETIME_EXPIRED or TGF_STATS_LIFETIME_OK
 */
typedef struct __attribute__((__packed__)) {
    uint32_t counter[TGF_NUM_MESSAGE_ACTIONS]; /* counter per action type */
} tgfStatsLifetime;

// TGF_STATS_CALIBRATE
typedef struct __attribute__((__packed__)) {
    uint64_t idx[TGF_STATS_CALIB_NUM];
} tgfStatsCalibrate;

/**
 * struct for pushing link down cause as stats
 * tgfStatsType = TGF_STATS_LINK_DOWN
 */
typedef struct __attribute__((__packed__)) {
    uint32_t cause;
} tgfStatsLinkDown;

/**
 * struct for pushing security stats
 * tgfStatsType = TGF_STATS_SECURITY
 */
typedef struct __attribute__((__packed__)) {
    uint8_t status; /**< 0 => disabled, 1 => enabled with PSK and 2 => enabled
                       with EAP. */
} tgfStatsSecurity;

/**
 * struct for pushing radio stats
 * tgfStatsType = TGF_STATS_RADIO
 */
typedef struct __attribute__((__packed__)) {
    uint32_t radioTestTotalRuns;       /**< Total runs of the test. */
    uint32_t radioCalibrationRuns;     /**< Calibration runs */
    uint32_t radioCalibrationFailures; /**< Calibration failures */
    uint16_t radioTestReadErr[TGF_STATS_NUM_RADIO];  /**< Count of read
                                             errors during test. */
    uint16_t radioTestWriteErr[TGF_STATS_NUM_RADIO]; /**< Count of write
                                            errors during test. */
} tgfStatsRadio;

/**
 * struct for pushing per node scan beams
 */
typedef struct __attribute__((__packed__)) {
    uint8_t selfAzimuthBeam;      // Based on Initial or PBF or RT Config
    uint8_t codebookBeam;         // Index to current beam in master codebook
    uint8_t rtCalTopPanelBeam;    // Based on RTCAL Top Panel
    uint8_t rtCalBottomPanelBeam; // Based on RTCAL Bottom Panel
    uint8_t vbsBeam;              // Based on VBS scan
    uint8_t cbfNullingBeam;       // Based on CBF nulling scan
} tgfScanBeams;

/**
 * struct for pushing scan stats
 * tgfStatsType = TGF_STATS_BF_SCAN
 */
typedef struct __attribute__((__packed__)) {
    uint32_t numOfScanReqRecvd;
    uint32_t numOfScanCompleted;
    uint32_t numOfScanDropped; /**< dropped because error in params,
                                scan not performed */
    uint32_t numOfScanAborted; /**< aborted because error encountered
                                during scan procedure */
    uint32_t numOfScanAsInitiator;
    uint32_t numOfScanAsResponder;
    uint32_t numOfPbfScan;
    uint32_t numOfImScan;
    uint32_t numOfRtCalScan;
    uint32_t numOfVbsScan;
    uint32_t numOfCbfScan;
    uint32_t numOfTopoScan;
    uint32_t numOfTestUpdAwv;
    uint32_t localTxPower; /**< set when stats are sent at the end of scan, or
                              in cases where localTxPower does not apply,
                               POWER_INDEX_PERIODIC (254) for periodic stats,
                               POWER_INDEX_RUNTIME (253) when configuring
                              parameters at runtime, POWER_INDEX_NOT_USED (255)
                              for BF scan as responder */
    tgfScanBeams txScanBeams;
    tgfScanBeams rxScanBeams;
} tgfStatsBfScan;

/*
 * struct for MTPO statistics
 * generated when MTPO completes at the initiator
 */
typedef struct __attribute__((__packed__)) {
    uint16_t mtpoSectorSweepGolayPeak[TGF_MTPO_SWEEP_SECTORS]; /* measured Golay
                                       peak values (linear) over single-tile
                                       sector sweep */
    uint16_t
        mtpoPhaseSweepGolayPeak[TGF_MTPO_SWEEP_PHASES]; /* measured Golay peak
                                  values (linear) over massive phase  sweep */
    uint8_t mtpoPhases[TGF_MAX_NUM_MTPO_PHASES]; /* all phases selected */
    uint8_t mtpoSector; /* which sector was selected during single tile phase */
    uint8_t
        mtpoPhaseChanged; /* flag, 1 if MTPO applied a new phase, 0 otherwise */
    uint8_t
        mtpoStatus; /* returned status 0=success,1=timeout,2+ QCOM-internal */
    uint8_t pad[1];
} tgfStatsMtpo;

/**
 * struct for statistics sample
 */
typedef struct __attribute__((__packed__)) {
    uint32_t tsfL; /**< tsf when the stats got logged with module */
    uint32_t tsfH;
    uint16_t type;   /**< as defined by tgfStatsType */
    uint8_t addr[6]; /**< relevant station address, 0:0:0:0:0:0 otherwise */
    union /* implicit */ __attribute__((__packed__)) { /**< data of stats */
        tgfStatsStaPkt staPkt;
        tgfStatsTestA testA;
        tgfStatsTestB testB;
        tgfStatsGps gps;
        tgfStatsPhystatus phystatus;
        tgfStatsPhystatus phystatusdata;
        tgfStatsMgmt mgmtTx;
        tgfStatsMgmt mgmtRx;
        tgfStatsSlot slot;
        tgfStatsBwhanSys bwhanSys;
        tgfStatsBwhanLink bwhanLink;
        tgfStatsBf bf;
        tgfStatsBf phyPeriodic;
        tgfStatsRecvMgmt recvMgmt;
        tgfStatsMgmtData mgmtData;
        tgfStatsMiscSys miscSys;
        tgfStatsMiscLink miscLink;
        tgfStatsLaHighSpeed laHighSpeed;
        tgfStatsTpcHighSpeed tpcHighSpeed;
        tgfStatsAgc maxAgcStats;
        tgfStatsTpc tpcStats;
        tgfStatsLaTpc latpcStats;
        tgfStatsMem mem;
        tgfStatsTsf tsf;
        tgfStatsLifetime lifetime;
        tgfStatsCalibrate calibrate;
        tgfStatsChn chn;
        tgfStatsLinkDown linkDown;
        tgfStatsSecurity security;
        tgfStatsRadio radioStats;
        tgfStatsBfScan bfScanStats;
        tgfStatsMtpo mtpoStats;
    } data;
} tgfStatsSample;

/**
 * struct for statistics header for pass-thru
 * used by TGF_PT_NB_STATS
 */
typedef struct __attribute__((__packed__)) {
    uint16_t numSamples;     /**< number of samples */
    uint16_t droppedSamples; /**< samples dropped due to buffer full */
    uint16_t buffIdx;        /**< buffer index */
} tgfStatsMsgHdr;

/**
 * struct for ack to south bound passthru message
 * used by TGF_PT_NB_ACK
 */
typedef struct __attribute__((__packed__)) {
    uint16_t msgType; /**< as defined by tgfPtMsgTypes */
    uint8_t success;  /**< 1:successful 0:failure */
    uint8_t pad[1];
} tgfAck;

/**
 * struct for station dissociation
 * used by TGF_PT_SB_DISSOC
 */
typedef struct __attribute__((__packed__)) {
    uint8_t addr[6]; /**< station address */
} tgfPtDissocMsg;

/**
 * struct for station association
 * used by TGF_PT_SB_ASSOC
 */
typedef struct __attribute__((__packed__)) {
    uint32_t len;
    uint8_t addr[6]; /**< station address */
    uint8_t pad[2];
    uint8_t data[0];
} tgfPtAssocMsg;

/**
 * struct for setting fw params
 * used by TGF_PT_SB_SET_FW_PARAMS
 */
typedef struct __attribute__((__packed__)) {
    /** BWGD indicating when the command should be applied
     *  bwgdIDxL and bwgdIdxH values of zero means to ignore what is sent and
     *  apply the command in the next BWGD */
    uint32_t bwgdIdxL; /**<  BWGD Idx Least significant 32-bits */
    uint32_t bwgdIdxH; /**<  BWGD Idx Most significant 32-bits */
    uint32_t len;
    uint8_t addr[6]; /**< station address */
    uint8_t pad[2];
    uint8_t data[0];
} tgfPtSetFwParamsReq;

/**
 * struct for status of set fw params request
 * used by TGF_PT_NB_SET_FW_PARAMS_RESP
 */
typedef struct __attribute__((__packed__)) {
    uint8_t status;
} tgfPtSetFwParamsResp;

/**
 * structs for Airtime allocation message
 * used by TGF_PT_SB_AIRTIME_ALLOC
 */
typedef struct __attribute__((__packed__)) {
    /**< NOTE: All airtime allocations below are in units of 0.01% */
    /**< Desired Tx allocation when all peer links are backlogged */
    uint16_t txIdeal; /**< Ideal Tx airtime allocation for peer link */
    uint16_t txMin;   /**< Minimum Tx airtime allocation for peer link */
    uint16_t txMax;   /**< Maximum Tx airtime allocation for peer link */
    /**< Desired Rx allocation when all peer links are backlogged */
    uint16_t rxIdeal;   /**< Ideal Rx airtime allocation for peer link */
    uint16_t rxMin;     /**< Minimum Rx airtime allocation for peer link */
    uint16_t rxMax;     /**< Maximum Rx airtime allocation for peer link */
    uint8_t macAddr[6]; /**< station MAC address */
} tgfAddrAirtimeTable;

#define TGF_MAX_PEER_ENTRIES 16

typedef struct __attribute__((__packed__)) {
    /**< (MAC addr, airtime allocations) table */
    tgfAddrAirtimeTable addrAirtimeTable[TGF_MAX_PEER_ENTRIES];
} tgfPtAirtimeAllocMsg;

/**
 * struct for configuring stats module
 * used by TGF_PT_SB_STATS_CONFIGURE
 */
typedef struct __attribute__((__packed__)) {
    uint8_t configure; /**< configure this stat or not */
    uint8_t enable;    /**< configuration = enable/disable */
} tgfPtStatConfig;

typedef struct __attribute__((__packed__)) {
    uint8_t macAddr[6]; /**< station MAC address */
} tgfPtMacAddr;

typedef struct __attribute__((__packed__)) {
    uint32_t onDuration; /**< number of bwgd for which stats would be ON */
    uint32_t period;     /**< period (in num bwgd) for stats ON-OFF cycle */
    tgfPtMacAddr hfMacAddressFilter[TGF_HF_MACS_NUM]; /**< station address, used
                                                         to filter HF logs */
} tgfPtStatsCycle;

typedef struct __attribute__((__packed__)) {
    tgfPtStatsCycle statsCycle;
    tgfPtStatConfig statsConfigs[TGF_STATS_NUM];
} tgfPtStatsConfigure;

/* if you specify the MCS, link adaptation is disabled */
typedef enum {
    LA_DISABLED = 0, // MCS 0 can not be configured for data
    MCS1 = 1,        // the minimum MCS is 1
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
    MCS12 = 12, // the maximum DMG single carrier PHY MCS is 12
    MCS13 = 13,
    MCS14 = 14,
    MCS15 = 15,
    MCS16 = 16,        // maximum 16-QAM SC PHY MCS
    JOINT_LA_TPC = 35, // joint LA/TPC
    LA_NUM_MODES
} laMcsMode_t;

#define LA_DEFAULT_STATE JOINT_LA_TPC
#define Q(N) (1 << (N))

typedef struct __attribute__((__packed__)) {
    // if you set laMcs to a value between 1 and 16, it will fix the MCS
    // at that value and will not run link adaptation (it ignores laMinMcs and
    // and laMaxMcs); otherwise, set laMcs to JOINT_LA_TPC
    // same as laMcsMode_t
    uint32_t laMcs;
    int32_t debug; // used for general debugging of link adaptation
    // global max when LA is enabled - MCS will not go higher
    uint16_t laMaxMcs;
    // global min when LA is enabled - MCS will not go lower
    uint16_t laMinMcs;
    // the margin is added to mcsLqmQ8 to select the MCS; positive margin means
    // being more conservative and generally selecting a lower MCS
    int16_t laMargindBQ2;

    // laConvergenceFactordBperSFQ8 controls how quickly the outer loop
    // offset (in dB) will change, for reference, setting the value to 1 (256)
    // means 1dB/SF when the packet size is 5000 bytes assuming a TDD duty
    // cycle of 1/2; a larger laConvergenceFactordBperSFQ8 means faster changes
    // in the offset; default value is 256 corresponding to 1dB/SF
    int16_t laConvergenceFactordBperSFQ8;
    // nackWeightFactor determines (approximately) the target PER, if the target
    // packet error rate is x, then nackWeigthFactor = 1/x
    // for example, if PER target is 1e-3, nackWeightFactor will be 1000
    int16_t nackWeightFactor;
    int16_t pad[1];
} tgfPhyLAParams;

typedef struct __attribute__((__packed__)) {
    // mcsLqmQ8 is the MCS table; we find the largest mcs value such that
    // the averaged STF SNR is >= mcsLqmQ8[mcs]
    int16_t mcsLqmQ8[TGF_MCS_LQM_NUM];
    int16_t pad[1];
} tgfPhyLANodeParams;

typedef struct __attribute__((__packed__)) {
    tgfPhyLAParams laParams;
    tgfPhyLANodeParams laNodeParams;
    uint8_t addr[6]; /**< station address */
    uint8_t pad[2];
} tgfPtPhyLAConfig;

// AGC configuration per node
typedef struct __attribute__((__packed__)) {
    // print logs to kern.log and send logs to fw_stats every logIntervalBwgd
    // BWGDs
    int32_t logIntervalBwgd;
    int32_t debugagc; // for internal debugging
    // number of dB RSSI changes with a change of one IF index
    uint16_t ifGaindBperIndexQ8;
    // rfGaindBperIndexQ4[n] is number of dB RSSI changes for RF index n
    // relative to RF index 0
    uint16_t rfGaindBperIndexQ8;
    // bit 0 indicates whether HiLo mode is enabled
    // bits [15:8] are the RSSI threshold
    // bits [7:1] are not used
    uint16_t rfGainHiLo;
    // rawAdcScaleFactor is the number of dB RSSI changes for a change of 1
    // in the rawADC value
    uint16_t rawAdcScaleFactorQ8;
    // the target for the raw ADC
    int8_t targetRawAdc;
    // the smallest allowed value for the max IF index
    uint8_t minIfGainIndex;
    // the largest allowed value for the max IF index
    uint8_t maxIfGainIndex;
    // minimum allowed RF gain index
    uint8_t minRfGainIndex;
    // maximum allowed RF gain index
    uint8_t maxRfGainIndex;
    // under normal conditions, IF gain stays within sweet range
    uint8_t maxIfSweetGainRange;
    uint8_t minIfSweetGainRange;
    // adjust the carrier sense threshold (default is 16)
    // increasing the threshold means receiver is less sensitive
    uint8_t crsScale;
    // useMinRssi tells the algorithm to use the reported RSSI rather
    // than the relative RSSI computed using IF/RF gains and rawADC
    uint8_t useMinRssi;
    // use the same max AGC/min RSSI for all connected stations
    uint8_t useSameForAllSta;
    uint8_t pad[2];
} tgfPhyAgcNodeConfig; // applies to the node, not per link

// AGC configuration per link
typedef struct __attribute__((__packed__)) {
    // the raw RSSI is normally around -14, if it is above this threshold,
    // throw out the sample (normally around 0)
    int32_t outlierThreshdB;
    // max AGC is set to this value only if maxAgcTrackingEnabled is FB_FALSE
    uint16_t linkAgc;
    // the minimum RSSI (used if hardware supports it) - otherwise, use linkAgc
    int8_t minRssi;
    // margin between the IIR average RSSI and the max AGC set value
    int8_t maxAgcTrackingMargindB;
    // set to 1 to enable max AGC tracking and 0 to disable it
    // when it is disabled, the max AGC will be set to linkAgc
    uint8_t maxAgcTrackingEnabled;
    // number of missing BWGDs before we move to free-running AGC
    uint8_t maxNumberMissingBWGDs;
    // controls the rate of convergence of the IIR filter - a larger value
    // means slower convergence
    // AlphaUp is for when the RSSI increases and AlphaDown is for when the
    // RSSI decreases - this way we can react more slowly, for example, to
    // increases in RSSI than to decreases
    uint8_t iirAlphaUpQ8;
    uint8_t iirAlphaDownQ8;
} tgfPhyAgcLinkConfig;

typedef struct __attribute__((__packed__)) {
    tgfPhyAgcNodeConfig agcNodeConfig;
    tgfPhyAgcLinkConfig agcLinkConfig;
    uint8_t addr[6]; /**< station address */
    uint8_t pad[2];
} tgfPtPhyAgcConfig;
/**
 * struct for configuring node polarity
 * used by TGF_PT_SB_POLARITY
 */
typedef struct __attribute__((__packed__)) {
    uint8_t polarity; /**< node polarity */
} tgfPtPolarityConfig;

/**
 * struct for configuring Golay code indices
 * used by TGF_PT_SB_GOLAY_INDX
 */
typedef struct __attribute__((__packed__)) {
    uint8_t txIndx; /**< Tx Golay code index */
    uint8_t rxIndx; /**< Rx Golay code index */
} tgfPtGolayConfig;

/**
 * struct for enabling Bf Slot Exclusion
 * used by TGF_PT_SB_BF_SLOT_EXCLUSION_REQ
 */
typedef struct __attribute__((__packed__)) {
    uint32_t startBwgdIdxL; /**< Start BWGD Idx */
    uint32_t startBwgdIdxH;
} tgfPtBfSlotExclusionReq;

typedef struct __attribute__((__packed__)) {
    uint8_t antWgtTblModule; // Identifies RFIC
    uint8_t antWgtTblIsRx;   // 0 - Tx, 1 - Rx
    uint16_t beamStart;      // First beam index in array
    uint16_t beamCount;      // Number of beam consecutive indexes
    uint16_t stride; // Difference between start beams in consecutive strides
    uint16_t strideCount; // Number of strides (of beamCount beams each)
    uint8_t numElements;  // Number of elements in one weight vector
    uint8_t pad[1];
    uint16_t antWgtCode[0]; // strideCount*beamCount*numElements elements
} tgfPtPhyAntWgtTblConfig;

/**
 * struct for codebook request messages
 * used by TGF_PT_NB_CODEBOOK_FETCH
 */
typedef struct __attribute__((__packed__)) {
    uint8_t channel;
    uint8_t pad[1];
    uint16_t beamStart; // First beam index in array
    uint16_t beamCount; // Number of beam consecutive indexes
    uint16_t stride;    // Difference between start beams in consecutive strides
    uint16_t strideCount; // Number of strides (of beamCount beams each)
} tgfPtCodebookFetchReq;

typedef struct __attribute__((__packed__)) {
    uint32_t transmitSequence[TGF_NUM_GOLAY_SEQ_COUNT][TGF_NUM_GOLAY_SEQ_COUNT];
    uint16_t receiveSequence[TGF_NUM_GOLAY_SEQ_COUNT];
} tgfPtPhyGolaySequenceConfig;

/**
 * struct for BF Responder mode config
 * used by TGF_PT_SB_BF_RESPONDER_MODE_CONFIG
 */
typedef struct __attribute__((__packed__)) {
    uint8_t config; /**< BF Responder mode configuration */
} tgfBfResponderModeConfig;

typedef enum {
    BFRESP_MODE_ENABLE = 1,  /**< enable BF Responder mode */
    BFRESP_MODE_DISABLE = 2, /**< disable BF Responder mode */
} bfRespModeConfig_t;

/**
 * struct for debugging
 * used by TGF_PT_SB_DEBUG
 */
typedef struct __attribute__((__packed__)) {
    uint32_t valueL;
    uint32_t valueH;
    uint8_t cmdStr[48];
} tgfPtDebug;

typedef enum {
    INVALID_BFSCAN = 0,
    COARSE = 1,    // coarse BF periodic scan (31 beams) - 16 BWGD
    FINE = 2,      // fine BF periodic scan (61 beams) - 64BWGD
    SELECTIVE = 3, // periodic uRoute table update scan  or periodic beam refine
    RELATIVE = 4,  // Relative to azimuth beam
    AUTO_PBF = 100, // Autonomous PBF
} bfScanMode_t;

typedef enum {
    INVALID_SCAN_TYPE = 0,
    PBF = 1,          // Periodic beamforming
    IM = 2,           // Interference measurement
    RTCAL = 3,        // Runtime calibration
    CBF_TX = 4,       // Coordinated beamforming tx side
    CBF_RX = 5,       // Coordinated beamforming rx side
    TOPO = 6,         // Topology scan
    TEST_UPD_AWV = 7, // Test Update AWV
    VBS_WO_RTCAL = 8, // VBS without RTCAL
} bfScanType;

typedef enum {
    NO_CAL = 0,     // No calibration, init state
    TOP_RX_CAL = 1, // Top Panel, responder Rx cal with fixed intiator Tx beam
    TOP_TX_CAL = 2, // Top Panel, intiator Tx cal with fixed responder Rx beam
    BOT_RX_CAL = 3, // Bot Panel, responder Rx cal with fixed intiator Tx beam
    BOT_TX_CAL = 4, // Bot Panel, intiator Tx cal with fixed responder Rx beam
    VBS_RX_CAL = 5, // Top + Bot, responder Rx cal with fixed intiator Tx beam
    VBS_TX_CAL = 6, // Top + Bot, intiator Tx cal with fixed responder Rx beam
    RX_CBF_AGGRESSOR = 7, // RX Coordinated BF Nulling, Aggressor link
    RX_CBF_VICTIM = 8,    // RX Coordinated BF Nulling, Victim link
    TX_CBF_AGGRESSOR = 9, // TX Coordinated BF Nulling, Aggressor link
    TX_CBF_VICTIM = 10,   // TX Coordinated BF Nulling, Victim link
    CAL_INVALID = 11,
} bfScanSubType_t;

/**
 * struct for beamforming/interference scan requests
 * used by TGF_PT_SB_SCAN_REQ
 */
typedef struct __attribute__((__packed__)) {
    uint32_t token;
    uint32_t startBwgdIdxL;
    uint32_t startBwgdIdxH;
    int16_t nullAngle; // For CBF
    uint8_t scanType;  // enum in Controller.thrift
    uint8_t scanMode;  // enum defined in Controller.thrift
    uint8_t isTx;
    uint8_t bfScanInvertPolarity;
    int8_t beamIdxLow; // -1 means not specified (aka use default)
    int8_t beamIdxHigh;
    uint8_t apply;   // 1- Apply new beams, 0- ignore new beams
    uint8_t subType; // enum in Controller.thrift
    uint8_t bwgdLen;
    uint8_t txPwrIndex;     // Tx Power Index [0 - 31, default:28]
    uint8_t peerMacAddr[6]; // Peer's MAC
    uint8_t cbfBeamIdx;
    uint8_t pad[1];
} tgfPtScanReq;

/**
 * helper struct for tgfPtScanResp
 */
typedef struct __attribute__((__packed__)) {
    uint16_t txBeamIdx;
    uint16_t rxBeamIdx;
    int16_t rssidBm;
    int16_t snrEstdBQ8; // Q8 means that the real snr value is snrEstdBQ8/2^8
    int16_t postSnrdBQ1;
    uint16_t rxStart;
    uint16_t dblPktIdx;
    uint16_t sweepIdx;
} tgfPtRouteInfo;

typedef enum {
    SCAN_PROCEDURE_COMPLETE = 0,
    SCAN_PROCEDURE_INVALID_TYPE = 1,
    SCAN_PROCEDURE_INVALID_START_TSF = 2,
    SCAN_PROCEDURE_INVALID_STA = 3,
    SCAN_PROCEDURE_AWV_IN_PROG = 4,
    SCAN_PROCEDURE_STA_NOT_ASSOC = 5,
    SCAN_PROCEDURE_REQ_BUFFER_FULL = 6,
    SCAN_PROCEDURE_LINK_SHUT_DOWN = 7,
    SCAN_PROCEDURE_UNSPECIFIED_ERROR = 8,
    SCAN_PROCEDURE_UNEXPECTED_ERROR = 9,
    SCAN_PROCEDURE_EXPIRED_TSF = 10,
    SCAN_PROCEDURE_INCOMPL_RTCAL_BEAMS_FOR_VBS = 11,
} tgfScanStatus;

/**
 * struct for topology scan info in scan response
 */
#define TOPO_SCAN_RESP_UR_MAX_NUM 4
// The maximum number of local adjacencies for topology scan
#define TGF_MAX_TOPO_LOCAL_ADJ 3

typedef enum {
    TOPO_INFO_ROUTE = 0,     // The info carrying the uRoute information
    TOPO_INFO_MAC_ADDR = 1,  // The info carrying responder mac address
    TOPO_INFO_GPS_POS = 2,   // The info carrying responder gps position
    TOPO_INFO_LOCAL_ADJ = 3, // The info carrying responder local adjacencies
} bfTopoScanInfoType;

typedef struct __attribute__((__packed__)) {
    uint32_t type;   // same as bfTopoScanInfoType
    uint8_t respIdx; // responder index, assigned by the initiator
    uint8_t pad[3];
    union /* implicit */ __attribute__((__packed__)) {
        struct __attribute__((__packed__)) {
            uint16_t rxBeamLqms[TOPO_SCAN_RESP_UR_MAX_NUM];
            uint16_t rtoiLqm;  // the lqm of R to I link
            uint8_t txBeamIdx; // the tx beam at the initiator
            uint8_t rxBeamIdx; // the best rx beam at responder for txBeamIdx
            uint8_t rxBeamCnt;
            uint8_t rxBeamIdxs[TOPO_SCAN_RESP_UR_MAX_NUM];
            uint8_t pad[1];
        } routes;        // uRoutes
        uint8_t addr[6]; // responder's mac address
        struct __attribute__((__packed__)) {
            int32_t latitude;
            int32_t longitude;
            int32_t height;
            int32_t accuracy;
        } pos;
        uint8_t
            adjAddrs[6 * TGF_MAX_TOPO_LOCAL_ADJ]; // local adjs' mac addresses
        uint8_t pad[20];
    } data;
} tgfPtTopoScanInfo;

// Max 1 beam per peer
#define NUM_ENHANCED_IM_ROUTES TGF_MAX_PEER_ENTRIES

typedef struct __attribute__((__packed__)) {
    uint8_t addr[6];
    uint8_t beam;
} tgfPtBeamInfo;

/**
 * struct for beamforming/interference scan response
 * used by TGF_PT_SB_SCAN_RESP
 */
typedef struct __attribute__((__packed__)) {
    uint32_t token; // equal to token in request
    uint32_t curSuperframeNumL;
    uint32_t curSuperframeNumH;
    uint64_t startSFNum; // BF Slot Allocation Begins
    uint64_t endSFNum;   // BF Slot Allocation Ends
    tgfPtTopoScanInfo topoInfo;
    uint16_t azimuthBeam; // Intial BF or PBF
    uint16_t oldBeam;     // Specific to procedure
    uint16_t newBeam;
    uint16_t sweepStartBeam;
    uint16_t sweepEndBeam;
    uint8_t numRoutes;
    uint8_t txPowerIndex; // the power used during the scan
    uint8_t status;       // See Enum in controller.thrift
    uint8_t numSweeps;
    uint8_t topoRespFlg; // the flag indicating whether this RESP contains
                         // topology scan results
    uint8_t numBeamInfo;
    tgfPtBeamInfo beamInfo[NUM_ENHANCED_IM_ROUTES];
    tgfPtRouteInfo route[0]; // Route information, valid only if
                             // numRoutes is not 0
} tgfPtScanResp;

typedef struct __attribute__((__packed__)) {
    uint8_t numResponses;
    uint8_t pad[3];
    tgfPtScanResp responses[0]; // array of numResponses elements
} tgfPtScanRespHdr;

/**
 * struct for channel configuration
 * used by TGF_PT_SB_CHANNEL_CONFIG
 */
typedef struct __attribute__((__packed__)) {
    uint8_t channel; /**< operating channel for initiator (1-3) */
} tgfPtChannelConfig;

/**
 * f/w fault flags in tgfFwHealthReport
 */
typedef enum {
    FW_NOLINK_FAULT = 1, // no RF link formed for 15 minutes
    FW_GPS_FAULT = 2     // gps module is in a bad state or gps is unlocked
} tgfFaultFlags;

/**
 * struct for FW health report message
 * used by TGF_PT_NB_HEALTHY
 */
typedef struct __attribute__((__packed__)) {
    int32_t faultFlags;
    uint32_t tsfL;
    uint32_t tsfH;
    uint8_t selfMacAddr[6];
} tgfFwHealthReport;

/**
 * struct for setting fw params
 * used by TGF_PT_NB_GET_FW_PARAMS_RESP
 */
typedef struct __attribute__((__packed__)) {
    uint32_t len;
    uint8_t data[0];
} tgfOptTLVParams;

typedef enum {
    TGF_PT_FW_PARAMS_TYPE_INVALID = 0, /**< Invalid FW Params type */
    TGF_PT_FW_OPT_NODE_PARAMS = 1,     /**< Response for get Node params */
    TGF_PT_FW_OPT_LINK_PARAMS = 2      /**< Response for get Link params */
} tgfGetFwParamsTypes_t;

/**
 * struct for getting fw params
 * used by TGF_PT_SB_GET_FW_PARAMS_REQ
 */
typedef struct __attribute__((__packed__)) {
    uint32_t requestedFwParams; /**< same as tgfGetFwParamsTypes_t */
    uint8_t addr[6]; /**< station address needed for link specific params*/
    uint8_t pad[2];
} tgfPtGetFwParamsReq;

/**
 * struct for getting fw params
 * used by TGF_PT_NB_GET_FW_PARAMS_RESP
 */
typedef struct __attribute__((__packed__)) {
    uint32_t resp_type; /**< same as tgfGetFwParamsTypes_t */
    uint32_t bwgdIdxL;
    uint32_t bwgdIdxH;
    union /* implicit */ __attribute__((__packed__)) {
        tgfOptTLVParams optParams;
        // add new types here
    } data;
} tgfPtGetFwParamsResp;

/**
 * struct for FW routing info message
 * used by TGF_PT_NB_ROUTING_INFO
 */
typedef struct __attribute__((__packed__)) {
    uint8_t addr[6]; // station address
    uint8_t mcs;
} tgfFwRoutingInfo;

/**
 * fw log levels
 */
typedef enum {
    TGF_LOG_LEVEL_DEBUG = 0,
    TGF_LOG_LEVEL_INFO = 1,
    TGF_LOG_LEVEL_ERROR = 2,
    TGF_LOG_LEVEL_FATAL = 3,
    TGF_LOG_NUM_LEVELS
} tgfLogLevel;

/**
 * enum for associating logs with fw modules
 * each fw module has an independently settable log level
 */
typedef enum {
    TGF_LOG_MOD_DEFAULT = 0,  /**< default module - for all unclassified code */
    TGF_LOG_MOD_AGC = 1,      /**< automatic gain control */
    TGF_LOG_MOD_BF = 2,       /**< initial beam forming */
    TGF_LOG_MOD_BW = 3,       /**< bandwidth handler */
    TGF_LOG_MOD_FRAMER = 4,   /**< framer */
    TGF_LOG_MOD_FSM = 5,      /**< state machine */
    TGF_LOG_MOD_GPS = 6,      /**< GPS */
    TGF_LOG_MOD_IOCTL = 7,    /**< driver-firmware ioctl interface */
    TGF_LOG_MOD_LA = 8,       /**< link adaptation */
    TGF_LOG_MOD_MGMT_FRM = 9, /**< management frame handler */
    TGF_LOG_MOD_SLOT_PROG = 10,   /**< slot programmer */
    TGF_LOG_MOD_TPC = 11,         /**< transmit power control */
    TGF_LOG_MOD_LA_TPC = 12,      /**< joint TPC and LA */
    TGF_LOG_MOD_RUNTIME_CFG = 13, /**< runtime fw cfg */
    TGF_LOG_MOD_CHN = 14,         /**< channel */
    TGF_LOG_MOD_SCAN = 15,        /**< beamforming scan */
    TGF_LOG_MOD_ANT_CB = 16,      /**< Antenna Code Book */
    TGF_LOG_MOD_AWV_GEN = 17,     /**< AWV Generation for IBF,Scans */
    TGF_LOG_MOD_TSF = 18,         /**< TSF */
    TGF_LOG_MOD_MTPO = 19,        /**< Multi-tile phase offset calibration */
    TGF_LOG_NUM_MODS
} tgfLogModule;

/**
 * struct for log configuration
 * used by TGF_PT_SB_SET_LOG_CONFIG
 */
typedef struct __attribute__((__packed__)) {
    uint8_t logLevel[TGF_LOG_NUM_MODS]; /**< tgfLogLevel for tgfLogModule */
} tgfPtSetLogConfig;

/**
 * struct to set local adjacencies
 */
typedef struct __attribute__((__packed__)) {
    uint8_t numAdjs;
    uint8_t pad[3];
    uint8_t adjMacAddrs[0]; /** mac addresses of adjacencies */
} tgfPtSetAdjHdr;

/**
 * struct to set gps time
 */
typedef struct __attribute__((__packed__)) {
    uint32_t unixTimeSecsH;
    uint32_t unixTimeSecsL;
} tgfPtGpsTimeValue;

/**
 * struct to provide PPS HTSF information (for OTA sync)
 * used by TGF_PT_NB_PPS_HTSF_INFO
 */
typedef struct __attribute__((__packed__)) {
    uint64_t ppsHwTsfUs; /**< PPS HW TSF value (in us) */
    uint16_t ppsHwTsfNs; /**< PPS HW TSF value (ns portion) */
    uint64_t ppsSwTsfUs; /**< PPS SW TSF value (in us) */
    uint16_t ppsSwTsfNs; /**< PPS SW TSF value (ns portion) */
    int32_t delayEstNs;  /**< Propagation delay estimate (in ns) */
} tgfPtPpsHtsfInfo;

/**
 * struct for TPC txPower adjustment table config message
 * used by TGF_PT_SB_PHY_TPC_ADJ_TBL_CFG
 */
typedef struct __attribute__((__packed__)) {
    uint8_t numOfTxPowerAdjs;
    uint8_t pad[3];
    uint8_t txPowerAdjPerBeamTbl[0]; /* numOfTxPowerAdjs */
} tgfPtTpcAdjPerBeamTblCfg;

/**
 * struct for txPower adjustment table request messages
 * used by TGF_PT_NB_PHY_TPC_ADJ_TBL_FETCH
 */
typedef struct __attribute__((__packed__)) {
    uint8_t channel;
    uint8_t pad[3];
} tgfPtTpcAdjTblFetchReq;

/**
 * enum for type of pass-thru messages
 */
typedef enum {
    // North Bound
    TGF_PT_NB_STATS = 0,          /**< statistics coming from fw */
    TGF_PT_NB_ACK = 1,            /**< ack for southbound pass-thru message */
    TGF_PT_NB_HEALTHY = 2,        /**< periodic message to notify hw health */
    TGF_PT_NB_SCAN_RESP = 3,      /**< Beamforming/interference scan response */
    TGF_PT_NB_CODEBOOK_FETCH = 4, /**< fetch codebook for specific channel */
    TGF_PT_NB_GET_FW_PARAMS_RESP = 5, /**< Response for Get FW Params Req */
    TGF_PT_NB_SET_FW_PARAMS_RESP = 6, /**< Response for Set FW Params Req */
    TGF_PT_NB_ROUTING_INFO = 7,       /**< routing info coming from fw >**/
    TGF_PT_NB_TOPO_OPENR_ADJ_REQ = 8, /**< Request adjs from fw to openr >**/
    TGF_PT_NB_PPS_HTSF_INFO = 9,      /**< PPS HTSF info (for OTA sync) >**/
    TGF_PT_NB_PHY_TPC_ADJ_TBL_FETCH = 10, /**< Fetch TPC adjustment table */

    // South Bound
    TGF_PT_SB_DISSOC = 100,          /**< dissociate a station */
    TGF_PT_SB_STATS_CONFIGURE = 102, /**< Config stats e.g. turn stat on/off */
    TGF_PT_SB_PHY_LA_CONFIG = 103,   /**< Config LA Configuration */
    TGF_PT_SB_GPS_ENABLE = 104,      /**< Enable GPS */
    TGF_PT_SB_POLARITY = 105,        /**< Config node polarity */
    TGF_PT_SB_GOLAY_INDX = 106,      /**< Config Golay code indices */
    TGF_PT_SB_PHY_ANT_WGT_TBL_CONFIG = 107, /**< Antenna Code Config Table */
    TGF_PT_SB_ASSOC = 108,                  /**< associate a station */
    TGF_PT_SB_DEBUG = 109,                  /**< invoke debug modes */
    TGF_PT_SB_AIRTIME_ALLOC = 110,          /**< Airtime allocation message */
    TGF_PT_SB_SCAN_REQ = 111, /**< Beamforming/interference scan request */
    TGF_PT_SB_BF_SLOT_EXCLUSION_REQ = 112,     /**< BF Slot Exclusion Request */
    TGF_PT_SB_PHY_AGC_CONFIG = 113,            /**< max AGC tracking */
    TGF_PT_SB_PHY_GOLAY_SEQUENCE_CONFIG = 114, /**< Golay Sequence Cfg */
    TGF_PT_SB_BF_RESPONDER_MODE_CONFIG = 115,  /**< BF Responder mode config */
    TGF_PT_SB_SET_FW_PARAMS = 116,             /**< Set fw params in runtime */
    TGF_PT_SB_PHY_TPC_CONFIG = 117,            /**< Config TPC Configuration */
    TGF_PT_SB_CHANNEL_CONFIG = 118,            /**< channel configurations */
    TGF_PT_SB_GET_FW_PARAMS_REQ = 119,         /**< Get a set of fw params */
    TGF_PT_SB_SET_LOG_CONFIG = 120,            /**< set log configurations */
    TGF_PT_SB_TOPO_SET_ADJ = 121,              /**< set local adjacencies  */
    TGF_PT_SB_GPS_TIME = 123,                  /**< set GPS time */
    TGF_PT_SB_PHY_TPC_ADJ_TBL_CFG = 124,       /**< Per beam txPower adj Tbl*/
} tgfPtMsgTypes;

/**
 * enum for the type of destination as marked by sender
 */
typedef enum {
    TGF_PT_DEST_E2E,     /**< e2e */
    TGF_PT_DEST_DBG,     /**< dbg app */
    TGF_PT_DEST_E2E_DBG, /**< both e2e, dbg app */
    TGF_PT_DEST_SB,      /**< generic value for SB msg */
} tgfPtMsgDest;

/**
 * struct for data buffer exchanged between driver-fw-apps
 */
typedef struct __attribute__((__packed__)) {
    uint8_t driverType; /**< driver internal field */
    uint8_t pad[3];     // Ensure 4 byte alignment for data payloads
    uint16_t msgType;   /**< as defined by tgfPtMsgTypes */
    uint16_t dest;      /**< as defined by tgfPtMsgDest */
    int32_t cookie;     /**< can be set to group together multiple messages */

    union /* implicit */ __attribute__((__packed__)) {
        tgfStatsMsgHdr statsHdr;
        tgfAck ack;
        tgfPtDissocMsg dissoc;
        tgfPtStatsConfigure statsConfigure;
        tgfPtPhyLAConfig phyLAConfig;
        tgfPtPolarityConfig polarityCfg;
        tgfPtGolayConfig golayCfg;
        tgfPtPhyAntWgtTblConfig phyAntWgtTblConfig;
        tgfPtAssocMsg assoc;
        tgfPtSetFwParamsReq setfwParamsReq;
        tgfPtSetFwParamsResp setFwParamsResp;
        tgfPtDebug debug;
        tgfPtAirtimeAllocMsg airtimeAlloc;
        tgfPtScanReq scanReq;
        tgfPtScanRespHdr scanResp;
        tgfPtBfSlotExclusionReq bfSlotExclusionReq;
        tgfPtPhyAgcConfig phyAgcConfig;
        tgfPtPhyGolaySequenceConfig phyGolaySequenceCfg;
        tgfBfResponderModeConfig bfRespModeCfg;
        tgfPtPhyTpcConfig phyTpcConfig;
        tgfPtChannelConfig channelCfg;
        tgfPtCodebookFetchReq codebookFetchReq;
        tgfPtGetFwParamsReq getFwParamsReq;
        tgfPtGetFwParamsResp getFwParamsResp;
        tgfFwHealthReport fwHealthReport;
        tgfFwRoutingInfo fwRoutingInfo;
        tgfPtSetLogConfig setLogConfig;
        tgfPtSetAdjHdr setAdj;
        tgfPtGpsTimeValue gpsTimeValue;
        tgfPtPpsHtsfInfo ppsHtsfInfo;
        tgfPtTpcAdjPerBeamTblCfg tpcAdjPerBeamCfg;
        tgfPtTpcAdjTblFetchReq tpcAdjTblFetchReq;
    } data;
} tgfPtMsg;

#endif /* FB_TG_FW_PT_IF_H */
