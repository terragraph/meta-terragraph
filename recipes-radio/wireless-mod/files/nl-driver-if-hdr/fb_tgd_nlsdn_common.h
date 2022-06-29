/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * Facebook Terragraph driver - netlink related interfaces
 */

#ifndef TGD_NLSDN_COMMON_H
#define TGD_NLSDN_COMMON_H

#include "fb_tgd_fw_common.h"

/* enum helper */
#define TGENUM_VALUE(name) name,
#define TGENUM_NAME(name) #name,
#define TGENUM_DEF(enumname, enumdef) enum enumname { enumdef(TGENUM_VALUE) };

#define TGENUM_DEF_NAMES(enumname, enumdef) \
  const char* enumname##_values_to_names[] = {enumdef(TGENUM_NAME)};

#define TGD_NLSDN_GENL_NAME "nlsdn"
#define TGD_NLSDN_GENL_GROUP_NAME "nlsdn_mc"
#define TGD_NLSDN_VERSION 0x1

/* attribute enum */
#define TGENUM_ATTR(E)              \
  E(TGD_NLSDN_ATTR_UNSPEC)          \
  E(TGD_NLSDN_ATTR_SUCCESS)         \
  E(TGD_NLSDN_ATTR_MACADDR)         \
  E(TGD_NLSDN_ATTR_LINK_STATUS)     \
  E(TGD_NLSDN_ATTR_GPSSTAT)         \
  E(TGD_NLSDN_ATTR_POLARITY)        \
  E(TGD_NLSDN_ATTR_TXOFFSET)        \
  E(TGD_NLSDN_ATTR_RXOFFSET)        \
  E(TGD_NLSDN_ATTR_TXDURATION)      \
  E(TGD_NLSDN_ATTR_RXDURATION)      \
  E(TGD_NLSDN_ATTR_NUMGRANTS)       \
  E(TGD_NLSDN_ATTR_DBGMASK)         \
  E(TGD_NLSDN_ATTR_STATS)           \
  E(TGD_NLSDN_ATTR_VARDATA)         \
  E(TGD_NLSDN_ATTR_BMFMROLE)        \
  E(TGD_NLSDN_ATTR_PASSTHRU_TYPE)   \
  E(TGD_NLSDN_ATTR_RESP_MODE)       \
  E(TGD_NLSDN_ATTR_IFINDEX)         \
  E(TGD_NLSDN_ATTR_LINK_DOWN_CAUSE) \
  E(TGD_NLSDN_ATTR_WSEC_STATUS)     \
  E(TGD_NLSDN_ATTR_PASSTHRU_NOACK)  \
  E(TGD_NLSDN_ATTR_RADIO_MACADDR)   \
  E(TGD_NLSDN_ATTR_SELF_NODE_TYPE)  \
  E(TGD_NLSDN_ATTR_PEER_NODE_TYPE)  \
  E(TGD_NLSDN_ATTR_UPDOWN_STATUS)   \
  E(TGD_NLSDN_ATTR_PAD)             \
  E(TGD_NLSDN_ATTR_GPS_TIME_S)      \
  E(TGD_NLSDN_ATTR_GPS_TIME_NS)     \
  E(__TGD_NLSDN_ATTR_MAX)
TGENUM_DEF(tgd_nlsdn_attrs, TGENUM_ATTR)

/* helpers for the size and number of attributes */
#define TGD_NLSDN_ATTR_MAX (__TGD_NLSDN_ATTR_MAX - 1)
#define TGD_NLSDN_NUM_ATTR __TGD_NLSDN_ATTR_MAX

/* attribute policy declaration */
#define TGD_NLSDN_POLICY_DECL() struct nla_policy tgd_nlsdn_policy[];

/* attribute policy definition */
#define TGD_NLSDN_POLICY_DEFN() \
  struct nla_policy tgd_nlsdn_policy[TGD_NLSDN_NUM_ATTR] = {  \
   /* TGD_NLSDN_ATTR_UNSPEC          */ { NLA_UNSPEC,  0, },  \
   /* TGD_NLSDN_ATTR_SUCCESS         */ { NLA_U8    ,  0, },  \
   /* TGD_NLSDN_ATTR_MACADDR         */ { NLA_U64   ,  0, },  \
   /* TGD_NLSDN_ATTR_LINK_STATUS     */ { NLA_U8    ,  0, },  \
   /* TGD_NLSDN_ATTR_GPSSTAT         */ { NLA_U8    ,  0, },  \
   /* TGD_NLSDN_ATTR_POLARITY        */ { NLA_U8    ,  0, },  \
   /* TGD_NLSDN_ATTR_TXOFFSET        */ { NLA_U32   ,  0, },  \
   /* TGD_NLSDN_ATTR_RXOFFSET        */ { NLA_U32   ,  0, },  \
   /* TGD_NLSDN_ATTR_TXDURATION      */ { NLA_U32   ,  0, },  \
   /* TGD_NLSDN_ATTR_RXDURATION      */ { NLA_U32   ,  0, },  \
   /* TGD_NLSDN_ATTR_NUMGRANTS       */ { NLA_U32   ,  0, },  \
   /* TGD_NLSDN_ATTR_DBGMASK         */ { NLA_U32   ,  0, },  \
   /* TGD_NLSDN_ATTR_STATS           */ { NLA_UNSPEC,  0, },  \
   /* TGD_NLSDN_ATTR_VARDATA         */ { NLA_UNSPEC,  0, },  \
   /* TGD_NLSDN_ATTR_BMFMROLE        */ { NLA_U32   ,  0, },  \
   /* TGD_NLSDN_ATTR_PASSTHRU_TYPE   */ { NLA_U8    ,  0, },  \
   /* TGD_NLSDN_ATTR_RESP_MODE       */ { NLA_U32   ,  0, },  \
   /* TGD_NLSDN_ATTR_IFINDEX         */ { NLA_U32   ,  0, },  \
   /* TGD_NLSDN_ATTR_LINK_DOWN_CAUSE */ { NLA_U32   ,  0, },  \
   /* TGD_NLSDN_ATTR_WSEC_STATUS     */ { NLA_U8    ,  0, },  \
   /* TGD_NLSDN_ATTR_PASSTHRU_NOACK  */ { NLA_U8    ,  0, },  \
   /* TGD_NLSDN_ATTR_RADIO_MACADDR   */ { NLA_U64   ,  0, },  \
   /* TGD_NLSDN_ATTR_SELF_NODE_TYPE  */ { NLA_U8    ,  0, },  \
   /* TGD_NLSDN_ATTR_PEER_NODE_TYPE  */ { NLA_U8    ,  0, },  \
   /* TGD_NLSDN_ATTR_UPDOWN_STATUS   */ { NLA_U8    ,  0, },  \
   /* TGD_NLSDN_ATTR_GPS_TIME_S      */ { NLA_U64   ,  0, },  \
   /* TGD_NLSDN_ATTR_GPS_TIME_NS     */ { NLA_U64   ,  0, },  \
};

/* command enum */
#define TGENUM_CMD(E)                                                \
  /* don't change the order or add anything between, this is ABI! */ \
  E(TGD_NLSDN_CMD_UNSPEC)                                            \
  E(TGD_NLSDN_CMD_NOTIFY)                                            \
  E(TGD_NLSDN_CMD_TGINIT)                                            \
  E(TGD_NLSDN_CMD_NOTIFY_TGINIT)                                     \
  E(TGD_NLSDN_CMD_NOTIFY_DRVR_RSP)                                   \
  E(TGD_NLSDN_CMD_SET_NODECONFIG)                                    \
  E(TGD_NLSDN_CMD_NOTIFY_NODECONFIG)                                 \
  E(TGD_NLSDN_CMD_NOTIFY_LINK_STATUS)                                \
  E(TGD_NLSDN_CMD_NOTIFY_ASSOC)                                      \
  E(TGD_NLSDN_CMD_GRANTALLOC)                                        \
  E(TGD_NLSDN_CMD_NOTIFY_GRANTALLOC)                                 \
  E(TGD_NLSDN_CMD_SET_DBGMASK)                                       \
  E(TGD_NLSDN_CMD_GET_STATS)                                         \
  E(TGD_NLSDN_CMD_SET_DRVR_CONFIG)                                   \
  E(TGD_NLSDN_CMD_PASSTHRU_NB)                                       \
  E(TGD_NLSDN_CMD_PASSTHRU_SB)                                       \
  E(TGD_NLSDN_CMD_BF_SCAN)                                           \
  E(TGD_NLSDN_CMD_SET_BMFMCONFIG)                                    \
  E(TGD_NLSDN_CMD_NOTIFY_BMFMCONFIG)                                 \
  E(TGD_NLSDN_CMD_DRVRSTAT_NB)                                       \
  E(TGD_NLSDN_CMD_DEV_ALLOC)                                         \
  E(TGD_NLSDN_CMD_DEV_ALLOC_RSP)                                     \
  E(TGD_NLSDN_CMD_NOTIFY_WSEC_STATUS)                                \
  E(TGD_NLSDN_CMD_NOTIFY_WSEC_LINKUP_STATUS)                         \
  E(TGD_NLSDN_CMD_NOTIFY_DEV_UPDOWN_STATUS)                          \
  E(TGD_NLSDN_CMD_SET_GPS_TIME)                                      \
  E(TGD_NLSDN_CMD_SET_GPS_POS)                                       \
  E(__TGD_NLSDN_CMD_AFTER_LAST)
TGENUM_DEF(tgd_nlsdn_commands, TGENUM_CMD)

/* groups enum */
#define TGENUM_MC_GROUP(E) \
  E(TGD_NLSDN_GROUP)       \
  E(__TGD_NLSDN_GROUP_AFTER_LAST)
TGENUM_DEF(tgd_nlsdn_mc_groups, TGENUM_MC_GROUP)

/* station mode enum */
#define TGENUM_MODE(E) \
  E(TGD_NLSDN_MODE_CN) \
  E(TGD_NLSDN_MODE_DN) \
  E(__TGD_NLSDN_MODE_AFTER_LAST)
TGENUM_DEF(tgd_nlsdn_mode, TGENUM_MODE)

/* Beamform  enum */
#define TGENUM_BMFMROLE(E) \
  E(TGD_NLSDN_BMFM_INIT)   \
  E(TGD_NLSDN_BMFM_RESP)   \
  E(__TGD_NLSDN_BMFM_AFTER_LAST)
TGENUM_DEF(tgd_nlsdn_bmfm_role, TGENUM_BMFMROLE)

/* shared structs */

struct tgd_tdd_config {
  __u32 tgd_txSlotWidth;
  __u32 tgd_rxSlotWidth;
  __u8 tgd_polarity;
  __u32 tgd_startFrame;
};

struct tgd_grant_config {
  __u32 tgd_txOffset;
  __u32 tgd_rxOffset;
  __u32 tgd_txDuration;
  __u32 tgd_rxDuration;
  __u32 tgd_numGrants;
};

typedef struct _fb_tgd_link_stats {
  int pipe;
  int link;
  int link_state;
  unsigned long rx_packets;
  unsigned long tx_packets;
  unsigned long rx_bytes;
  unsigned long tx_bytes;
  unsigned long tx_errors;
  unsigned char src_mac_addr[6];
  unsigned char dst_mac_addr[6];
  unsigned char dev_index;
} fb_tgd_link_stats_t;

typedef struct _tgd_stats {
  int num_links;
  fb_tgd_link_stats_t link_stat[0];
} tgd_stats_t;

//============= Ublox GPS related status ========
#define DRVR_CFG_CMD_ECHO 01
#define DRVR_CFG_CMD_VER 02
#define DRVR_CFG_CMD_GPS 03

//-------------- Satellite in view info for SNR ---
#define GPS_STAT_CMD_SVINFO 0x01
#define GPS_STAT_CMD_TMPLFQ 0x02
#define GPS_STAT_CMD_LATLONG 0x03
#define GPS_SET_CMD_SING_SAT 0x04
#define GPS_SET_UBLX_RESET 0x05
#define NB_DRVR_STAT_GPS 0x06
#define GPS_GET_CMD_POS 0x07

//--------- Time Pulse Time and Frequency Data ------
#define GPS_TIME_LEAP_SECOND 0x0007 // First 3 bits leap second info
#define GPS_TIME_PULSE_IN_TOL 0x0008 // time pulse within tolerance limit
#define GPS_TIME_INTOSC_IN_TOL 0x0010 // Int osc within tolerance limit
#define GPS_TIME_EXTOSC_IN_TOL 0x0020 // Ext osc within tolerance limit
#define GPS_TIME_GNSS_TM_VALD 0x0040 // GNSS time is valid
#define GPS_TIME_UTC_TM_VALD 0x0080 // UTC time is valid
#define GPS_TIME_DISP_SOURCE 0x0700 // D10-D8  Disciplining source id
#define GPS_TIME_PULS_IN_COH 0x1000 // D12 coherent pulse in operation
#define GPS_TIME_PULS_LOCKED 0x2000 // D13 time pulse is locked

struct __attribute__((__packed__)) t_gps_time_pulse_info {
  int year;
  int gns_top_ofst;
  int int_osc_ofst;
  int utc_tm_ofst;
  unsigned int flags;
  unsigned int utc_uncert;
  unsigned int gnss_uncert;
  unsigned int int_osc_uncert;
  unsigned char month;
  unsigned char day;
  unsigned char hour;
  unsigned char minute;
  unsigned char seconds;
};

struct __attribute__((__packed__)) t_gps_time_pulse_rsp_data {
  unsigned char hdr[4];
  struct t_gps_time_pulse_info tm_puls_info;
};

struct __attribute__((__packed__)) t_gps_pos_info {
  unsigned char fix_type; // 0:no fix 1:dead reckoning 2:2D-fix 3:3D-fix
  // 4:GNSS+dead reckoning combined 5:time only fix
  unsigned char num_sat_used; // Number of satellites used in Nav Solution
  unsigned int long_value; //(1e-7)eg: B731_CF96 = -48CE306A = 122.1472362
  unsigned int lat_value; // Latitude  (1e-7) 0X1657_B6EF   37.4847215
  unsigned int hgt_ellipsoid; // Height above ellipsoid (mm)
  unsigned int hgt_sea_lvl; // Height above mean sea level  (mm)
};

struct __attribute__((__packed__)) t_gps_pos_rsp_data {
  unsigned char hdr[4];
  struct t_gps_pos_info pos_fix_info;
};

struct __attribute__((__packed__)) t_gps_self_pos {
  unsigned short cmd;
  unsigned short sub_cmd;
  signed int latitude;
  signed int longitude;
  signed int height;
  signed int ecef_x;
  signed int ecef_y;
  signed int ecef_z;
  signed int accuracy;
};

#endif
