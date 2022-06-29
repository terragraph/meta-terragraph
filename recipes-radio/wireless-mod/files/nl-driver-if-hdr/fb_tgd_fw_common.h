/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef FB_TGD_FW_COMMON_H
#define FB_TGD_FW_COMMON_H

typedef enum _tgLinkStatus {
  TG_LINKINIT = 1,
  TG_LINKDOWN,
  TG_LINKUP,
  TG_LINKPAUSE,
} tgLinkStatus;

typedef enum _tgLinkFailureCause {
  TG_NOT_APPLICABLE = 0,
  TG_UNEXPECTED_COMMAND = 1,
  TG_SYSTEM_GPS_SYNC_IN_PROG = 2,
  TG_SYSTEM_NOT_CONFIGURED = 3,
  TG_BF_TRAINING_FAILED = 4,
  TG_BF_TRAINING_SHUTDOWN = 5,
  TG_HB_KA_LOSS_DETECTED = 6,
  TG_LINK_SHUTDOWN_RECVD = 7,
  TG_CHANNEL_NOT_CONFIGURED = 8,
  TG_CHANNEL_NOT_SET_WAITING_LINK_DOWN = 9,
  TG_CHANNEL_IS_BEING_SET = 10,
  TG_UNSUPPORTED_CAPABILITY = 11,
  TG_WSEC_MISMATCH = 12,
  TG_DISASSOC_RCVD_FROM_PEER = 13,
} tgLinkFailureCause;

typedef enum _tgIoctlErrCode {
  TG_IOCTL_SUCCESS = 0,
  TG_IOCTL_ERROR,
  TG_IOCTL_UNEXPECTED_REQ,
  TG_IOCTL_GPS_ERROR,
} tgIoctlErrCode;

// Security authentication types
typedef enum _tgWsecAuthType {
  TGF_WSEC_DISABLE = 0,
  TGF_WSEC_PSK = 1,
  TGF_WSEC_EAP = 2
} tgWsecAuthType;

#endif
