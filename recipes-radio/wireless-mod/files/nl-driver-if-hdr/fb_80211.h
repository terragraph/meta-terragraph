/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef FB_802_11_H
#define FB_802_11_H

/*
 * Facebook Terragraph 802.11 definitions
 */

#ifdef TG_DRIVER
typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef unsigned int uint32_t;
typedef signed int int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
#else
#include <stdint.h>
#endif

// Maximum bytes for vendor specific IE data.
#define FB_MAX_VNDR_IE_SZ (2)
#define IE_HDR_ID_OFFSET (0)
#define IE_HDR_LEN_OFFSET (1)

typedef struct _ieHdr {
  uint8_t ieId;
  uint8_t ieLen;
} __attribute__((packed)) IEHdr;

typedef struct _vendorIeElement {
  IEHdr ieHdr;
  uint8_t vndrOui[3];
  uint8_t ieData[FB_MAX_VNDR_IE_SZ];
} __attribute__((packed)) VendorIeElement;

#define DOT11_RSN_IE (48) /* d11 RSN information element type */
#define VNDR_SPECIFIC_IE (221)
#define DOT11_OUI_LEN 3 /* d11 OUI length */
#define WPA2_OUI_STR "\x00\x0F\xAC" /* WPA2 OUI  */
#define WPA2_OUI_INT (0xAC0F00) /* WPA2 OUI */
#define AES_CIPHER_GCMP (0x8)
#define WPA2_AUTH_PSK (0x2)
#define WPA2_AUTH_8021X (0x1)
#define RSN_IE_VER1 (0x1)
#define BRCM_OUI_INT (0x181000) /* Broadcom OUI */
// The IE length field is 1 byte long.
#define MAX_80211_IE_LEN (256)
#define MAX_LINK_STATUS_TLV_LEN ((MAX_80211_IE_LEN + sizeof(IEHdr)) * 2)

typedef struct _rsnSuiteElement {
  uint8_t oui[DOT11_OUI_LEN];
  uint8_t type;
} __attribute__((__packed__)) RsnSuiteEl;

// Length of various fields in RSN IE in bytes.
#define RSN_IE_VERLEN (2)
#define RSN_IE_CIPHER_LEN (sizeof(RsnSuiteEl))
#define RSN_IE_SUITECNT_LEN (2)

typedef struct _rsnIeData {
  uint16_t version;
  RsnSuiteEl grpCipher;
  uint16_t ucastSuiteCnt;
  RsnSuiteEl ucastCipher;
  uint16_t akmSuiteCnt;
  RsnSuiteEl akmSuite;
  uint16_t rsnCapability;
} __attribute__((__packed__)) RsnIeData;
#define RSN_IE_DATA_LEN (sizeof(RsnIeData))

typedef struct _rsnIeElement {
  IEHdr ieHdr;
  RsnIeData rsnIeData;
} __attribute__((__packed__)) RsnIeElement;
#define FB_TG_RSN_IE_EL_SZ (sizeof(RsnIeElement))

#endif /* FB_802_11_H */
