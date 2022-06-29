/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * Facebook Terragraph driver - firmware message definitions
 */

#ifndef FB_TG_FB_DRIVER_H
#define FB_TG_FB_DRIVER_H

#include "fb_tg_queue_stats.h" // for QUEUE_STATS_MAX_LINKS
#include "fb_tgd_fw_common.h"

// MAX_VAR_DATA_LEN <= WGC_BH_IOCTL_BUFF_MAX
#define MAX_VAR_DATA_LEN 512

#if defined(TG_DRIVER)
typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef unsigned int uint32_t;
typedef signed int int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
#elif defined(TG_FIRMWARE)
// fw source files include types header
#else // e2e
#include <stdint.h>
#endif

typedef struct __attribute__((__packed__)) _tgEthAddr {
  uint8_t addr[6];
} tgEthAddr;

// BF Role
typedef enum _tgBfRole {
  eBF_ROLE_INITIATOR,
  eBF_ROLE_RESPONDER,
} tgBfRole;

typedef struct __attribute__((__packed__)) _tgVarData {
  uint32_t len;
  uint8_t data[0];
} tgVarData;
// eTG_IOCTL_GEN_RSP : Driver to Fw ioctl is blocking command
// Firmware can respond immediately using this message
// TG_NB_IOCTL_RESP
typedef struct __attribute__((__packed__)) _ioctlGenRsp {
  // tgIoctlErrCode
  uint8_t errCode;
} ioctlGenRsp;

// eTG_FW_INIT_REQ : Driver calls this to initialize the firmware
// TG_SB_INIT_REQ
typedef struct __attribute__((__packed__)) _fwInitReq {
  tgVarData varData;
} fwInitReq;

typedef struct __attribute__((__packed__)) _fwFbFwVersion {
  // Place Holder - may one or more fields
  uint32_t versionNum;
} fbFwVersion;

typedef struct __attribute__((__packed__)) _fwFbFwChipId {
  // Place Holder - may one or more fields
  uint32_t chipId;
} fbFwChipId;

// eTG_FW_INIT_RSP
// TG_NB_INIT_RESP
typedef struct __attribute__((__packed__)) _fwInitRsp {
  // tgIoctlErrCode
  uint8_t errCode;
  tgEthAddr macAddr;
  fbFwVersion fwVersion;
  fbFwChipId fwChipId;
  char vendorStr[16];
} fwInitRsp;

// eTG_SB_START_BF_ACQ_REQ
typedef struct __attribute__((__packed__)) _tgFwStartBfAcqReq {
  tgEthAddr linkStaAddr;
  uint8_t bfAcqRole;
  tgVarData varData;
} sTgFwStartBfAcqReq;

// eTG_NB_START_BF_ACQ_RESP
typedef struct __attribute__((__packed__)) _tgFwStartBfAcqRsp {
  tgEthAddr linkStaAddr;
  uint8_t bfAcqRole;
  uint8_t errCode;
  tgVarData varData;
} sTgFwStartBfAcqRsp;

// eTG_FW_LINK_STATUS
// TG_NB_UPDATE_LINK_REQ
typedef struct __attribute__((__packed__)) _tgFwLinkStatus {
  // tgLinkStatus
  uint8_t linkStatus;
  tgEthAddr linkStaAddr;
  uint8_t linkFailureCause;
  uint8_t linkStaNodeType;
  uint8_t peerNodeType;
  uint8_t assocReqIeLen;
  uint8_t assocRespIeLen;
  // tgWsecAuthType
  uint8_t wsecAuthType;
  /* One or more tlvs: assoc req tlvs are followed by assoc resp tlvs */
  uint8_t tlvs[0];
} sTgFwLinkStatus;

// TG_SB_DISASSOC_REQ
typedef struct __attribute__((__packed__)) _tgFwDisassocReq {
  tgEthAddr linkStaAddr;
} sTgFwDisassocReq;

// TG_NB_LINK_INFO
typedef struct __attribute__((__packed__)) _tgNbLinkInfo {
  tgEthAddr linkStaAddr;
  uint8_t rxLink;
  uint8_t txLink;
} tgNbLinkInfo;

// TG_NB_DEL_LINK_REQ
typedef struct __attribute__((__packed__)) _tgNbDelLinkReq {
  tgEthAddr linkStaAddr;
} tgNbDelLinkReq;

// TG_SB_DEL_LINK_RESP
typedef struct __attribute__((__packed__)) _tgSbDelLinkRsp {
  // tgIoctlErrCode
  uint8_t errCode;
  tgEthAddr linkStaAddr;
} tgSbDelLinkRsp;

// TG_SB_GPS_TIME
typedef struct __attribute__((__packed__)) _tgGpsTimeData {
  uint32_t secondsL;
  uint32_t secondsH;
  uint32_t nanoseconds;
} sTgGpsTimeData;

// TG_SB_GPS_SET_SELF_POS
typedef struct __attribute__((__packed__)) _tgGpsPosData {
  int32_t latitude;
  int32_t longitude;
  int32_t height;
  int32_t accuracy;
} sTgGpsPosData;

// TG_SB_PASSTHRU
typedef struct __attribute__((__packed__)) _tgFwPassThroughData {
  tgVarData varData;
} tgFwPassThroughData;

// TG_SB_QUEUE_STATS
typedef struct __attribute__((__packed__)) _tgSbQueueStats {
  uint32_t bytesPending;
  uint32_t arrivalRate; // unit: bytes per millisecond
  uint8_t dstMacAddr[6];
} tgSbQueueStats;

// TG_NB_HTSF_INFO
typedef struct __attribute__((__packed__)) _tgNbHtsfInfo {
  // Time difference between Tx/Rx timestamps of Keepalives (in ns)
  int32_t txRxDiffNs;
  // Propagation delay estimate (in ns)
  int32_t delayEstNs;
  // Rx Start HW TSF timestamp (in us)
  uint32_t rxStartUs;
} tgNbHtsfInfo;

// Msgs between driver and firmware
typedef enum _fbTgEventType {
  TG_SB_INIT_REQ = 100,
  TG_NB_INIT_RESP,
  TG_NB_LINK_INFO,
  TG_NB_UPDATE_LINK_REQ,
  TG_NB_DEL_LINK_REQ,
  TG_SB_DEL_LINK_RESP,
  TG_NB_PASSTHRU,
  TG_SB_PASSTHRU,
  TG_NB_IOCTL_RESP,
  // GPS Related messages
  TG_NB_GPS_START_TIME_ACQUISITION,
  TG_NB_GPS_STOP_TIME_ACQUISITION,
  // Driver posts this event to the msg q to send periodic gps time to fw
  TG_SB_GPS_TIME,

  TG_SB_START_BF_SCAN_REQ,
  TG_NB_START_BF_SCAN_RESP,

  // Ask FW to disassoc from the peer
  TG_SB_DISASSOC_REQ,

  TG_SB_QUEUE_STATS,
  TG_SB_SHUTDOWN_REQ,

  // Fw posts this event to request gps position from driver
  TG_NB_GPS_GET_SELF_POS,
  // Driver posts this event to deliver gps position to fw
  TG_SB_GPS_SET_SELF_POS,

  // FW sends this event to provide HTSF information required for SyncE
  TG_NB_HTSF_INFO,
} fbTgEventType;

// FB Driver - Firmware message type
// This messages could be command to sent from Driver to Fw
// Or Fw to Driver events
typedef struct __attribute__((__packed__)) _fbTgIfEvent {
  uint8_t type;
  uint8_t pad[3]; // Ensure 4 byte alignment for data payloads
  union /* implicit */ __attribute__((__packed__)) _data {
    ioctlGenRsp tgIoctlGenRsp;
    fwInitReq tgFwInitReq;
    fwInitRsp tgFwInitRsp;
    sTgFwStartBfAcqReq tgFwStartBfAcqReq;
    sTgFwStartBfAcqRsp tgFwStartBfAcqRsp;
    sTgFwLinkStatus tgFwLinkStatus;
    sTgFwDisassocReq tgFwDisassocReq;
    tgNbLinkInfo tgLinkInfo;
    tgNbDelLinkReq tgDelLinkReq;
    tgSbDelLinkRsp tgDelLinkRsp;
    sTgGpsTimeData tgGpsTimeData;
    sTgGpsPosData tgGpsPosData;
    tgFwPassThroughData tgPassThroughData;
    tgSbQueueStats queueStats[QUEUE_STATS_MAX_LINKS];
    tgNbHtsfInfo tgHtsfInfo;
  } data;
} fbTgIfEvent;

int8_t tgfGetSysConfigTxPower(void);

#endif
