/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Ublox device configuration module */

#ifndef FB_TGD_UBLOX_GPS_MESGH_H_
#define FB_TGD_UBLOX_GPS_MESGH_H_

#define I2C_MSG_MAX_SIZE 2048
#define CFG_RSP_MAX 512
#define UBLX_CFG_ACK_MAX_SIZE 16

#define DFLT_UPDATE_MS 1000
#define UBLOX_POLL_1000MS 1000
#define UBLOX_POLL_200MS 200

#define UBX_CLS_ID 0x06
#define UBX_MSG_ID_RATE_SET 0x08
#define UBX_MSG_ID_MSG_CFG 0x01

#define UBX_CHAR_SYNC0 0xB5
#define UBX_CHAR_SYNC1 0x62
#define NMEA_START_CH0 '$'
#define NMEA_START_CH1 'G'
#define MSG_CR 0x0D
#define MSG_LF 0x0A
#define NMEA_MSG_MAX_CHR 0x7F

#define CFG_ACK_CLS_ID1 0x05
#define CFG_ACK_CLS_ID2 0x01

#define CFG_TIM_TOS_ID1 0x0D
#define CFG_TIM_TOS_ID2 0x12

#define UBX_MSG_CTRL_SIZE 8
#define MSG_HDR_SIZE 6
#define UBX_MSG_CHEKSUM_SIZE 2
#define UBX_MSG_CLSID_SIZE 2
#define UBX_MSG_LEN_SIZE 2

#define UBX_MSG_HDR_AND_CHEKSUM_SIZE (MSG_HDR_SIZE + UBX_MSG_CHEKSUM_SIZE)
#define UBX_MSG_CHEKSUM_HDR_SIZE (UBX_MSG_CLSID_SIZE + UBX_MSG_LEN_SIZE)

#define NMEA_MSG_TAG_START 3

#define DBG_MSG_ENABLE_SYNC 0x01
#define DBG_MSG_QUEUE_DESC 0x02
#define DBG_MSG_ENABLE_POLL 0x04
#define DBG_MSG_CFG_STAT_RD 0x08
#define DBG_MSG_NMEA_CFG_PRSR 0x10
#define DBG_MSG_CFG_RSP_PARSED 0x20
#define DBG_MSG_CFG_RSP_RAW 0x40
#define DBG_MSG_UBLX_WARNING 0x80

#define I2C_RSP_MAX_WAIT 10

#define LE_HOST_UINT(strt_addr) le32_to_cpu(*((unsigned int *)(strt_addr)))
#define LE_HOST_INT(strt_addr) le32_to_cpu(*((int *)(strt_addr)))
#define LE_HOST_SHORT(strt_addr) le16_to_cpu(*((short *)(strt_addr)))

#define HOST_TO_LE_UINT(strt_addr) cpu_to_le32(*((unsigned int *)(strt_addr)))

//--------- Time Pulse Time and Frequency Data ------
#define TIM_TOS_LEAP_SECOND 0x0007   // First 3 bits leap second info
#define TIM_TOS_PULSE_IN_TOL 0x0008  // time pulse within tolerance limit
#define TIM_TOS_INTOSC_IN_TOL 0x0010 // Int osc within tolerance limit
#define TIM_TOS_EXTOSC_IN_TOL 0x0020 // Ext osc within tolerance limit
#define TIM_TOS_GNSS_TM_VALD 0x0040  // GNSS time is valid
#define TIM_TOS_UTC_TM_VALD 0x0080   // UTC time is valid
#define TIM_TOS_DISP_SOURCE 0x0700   // D10-D8  Disciplining source id
#define TIM_TOS_PULS_IN_COH 0x1000   // D12 coherent pulse in operation
#define TIM_TOS_PULS_LOCKED 0x2000   // D13 time pulse is locked

#define PREP_PRIV_DATA(_b1, _b2, _rsp_dst)                                     \
	(((_rsp_dst << 16) & 0xFFFF0000) + ((_b1 << 8) & 0xFF00) + (_b2 & 0xFF))

#define EXTRACT_PRIV_DATA(_priv_data, _b1, _b2, _rsp_dst)                      \
	do {                                                                   \
		_rsp_dst = (_priv_data >> 16) & 0xFF;                          \
		_b1 = (_priv_data >> 8) & 0xFF;                                \
		_b2 = (_priv_data)&0xFF;                                       \
	} while (0)

#define INIT_UMSG(_name, _fnptr, _clid1, _clid2, _msg_enable, _msg_len)        \
	{                                                                      \
		.name = _name, .proc_msg = _fnptr, .clsid[0] = _clid1,         \
		.clsid[1] = _clid2, .msg_enable = _msg_enable,                 \
		.msg_len = _msg_len,                                           \
	}

//---------- for UBX-CFG-TMODE2 configuration -----
enum eTime_sync_mode {
	eTIME_MODE_GNSS, // default mode, Oscillator disiplined
	eTIME_MODE_SURVEY,
	eTIME_MODE_FIXED,
	eTIME_MODE_UNKNOWN,
};

enum eMsg_enable {
	eMSG_NA,
	eMSG_DISABLD, // config Ublox to disable the NMEA msg
	eMSG_SING_RD, // Enable the NMEA msg, stop after first response
	eMSG_REPT_RD, // Enable the NMEA msg, continuous read
};

struct t_gps_pos_nmea {
	int latt_deg;
	int latt_deg_f;
	int long_deg;
	int long_deg_f;
	char latt_side;
	char long_side;
};

enum eUblox_init_state {
	eUBLX_STATE_DOWN, // keep zda collection disabled
	eGPS_STATE_INIT,
	eGPS_STATE_WAIT_SYNC,
	eGPS_STATE_TIME_IN_SYNC,
};

enum eUblox_cfg_rsp_dst {
	eUBLOX_RSP_DST_NONE,
	eUBLOX_RSP_DST_TMR,
	eUBLOX_RSP_DST_USR,
};

//------------------ Ublox stat data --------
// Should not change the order, keeping the samd order as Ublox register values
enum eUblox_gnss_fix {
	eNO_FIX,
	eDEAD_RECK,
	eFIX_2D,
	eFIX_3D,
	eGNSS_DEAD_RECK,
	eTIME_ONLY,
};

enum eUblox_sync_sgnl_state {
	eSIGNAL_NOT_DSPLNED,
	eSIGNAL_DSPLNED,
};

//----------------------------------------------
enum eUblox_msg_type {
	eUBLOX_MSG_NONE,
	eUBLOX_MSG_NMEA,
	eUBLOX_MSG_CFG,
};

enum eInt_osc_dsp_src {
	eINTOSC_DSCP_SRC_INT,      // 0: internal oscillator
	eINTOSC_DSCP_SRC_GNSS,     // 1: GNSS
	eINTOSC_DSCP_SRC_INT0,     // 2: EXTINT0
	eINTOSC_DSCP_SRC_INT1,     // 3: EXTINT1
	eINTOSC_DSCP_SRC_INT_HOST, // 4: internal osc measured by the host
	eINTOSC_DSCP_SRC_EXT_HOST, // 5: external osc measured by the host
	eINTOSC_DSCP_SRC_INVALID,
};

struct ublox_msg_data;

typedef int(ublox_msg_hndlr_t)(struct ublox_msg_data *ub_data,
			       const char *msg_ptr, int len);

struct t_ublx_msg_desc {
	const char *name;
	ublox_msg_hndlr_t *proc_msg;
	char clsid[2];
	enum eMsg_enable msg_enable;
	int msg_len;
};

//----- data from UBX-NAV-PVT Navigation Position Velocity Time Solution
struct t_nav_pos_vel_time {
	unsigned int tow_ms; // GPS time of week of the navigation epoch (ms)
	unsigned short year;
	unsigned char month;
	unsigned char day;
	unsigned char hour;
	unsigned char minute;
	unsigned char second;
	unsigned char valid_flag;
	unsigned int f_second_ns; // Fraction of second, range -1e9 .. 1e9 (UTC)
	unsigned char fix_type;   // 0:no fix 1:dead reckoning 2:2D-fix 3:3D-fix
	// 4:GNSS+dead reckoning combined 5:time onlyFix
	unsigned char fix_status;
	unsigned char num_sat_used; // UBX-NAV-PVT #29 satellites used in Nav
				    // Sol
	int long_value; //(1e-7)eg: B731_CF96 = -48CE306A = 122.1472362
	int lat_value;  // Latitude  (1e-7) 0X1657_B6EF   37.4847215
	unsigned int hgt_ellipsoid; // Height above ellipsoid (mm)
	unsigned int hgt_sea_lvl;   // Height above mean sea level  (mm)
};

#define MAX_NUM_SV 16

struct t_ublox_space_veh_info {
	// char chnl_num;
	char sat_id;
	char flags;
	char qlty;
	char snr;
	char elev;
};

//----- data from UBX-NAV-PVT Navigation Position Velocity Time Solution
struct t_ublox_pos_info {
	unsigned int lattitude;  // UBX-NAV-PVT (1e-7) 0X1657_B6EF  37.4847215
	unsigned int longitude;  //(1e-7)eg: B731_CF96 = -48CE306A = 122.1472362
	unsigned int ht_sea_lvl; // Height above mean sea level  (mm)
};

struct t_ublox_time_pulse_info {
	int year;
	int utc_tm_ofst;
	int gns_top_ofst;
	int int_osc_ofst;
	unsigned int flags;
	unsigned int utc_uncert;
	unsigned int gnss_uncert;
	unsigned int int_osc_uncert;
	unsigned int gnss_week_num;
	unsigned int gnss_week_time;
	unsigned char month;
	unsigned char day;
	unsigned char hour;
	unsigned char minute;
	unsigned char seconds;
};

struct t_sync_mgr_stat {
	enum eUblox_sync_sgnl_state sync_sgnl_state;
	enum eInt_osc_dsp_src int_osc_dsp_src;
	int gnss_present;
};

struct ublox_rd_wr_stat {
	unsigned int rd_pkt_count;
	unsigned int rd_pkt_len_error;
	unsigned int cheksum_error;

	unsigned int tim_tos_count;
	unsigned int tim_tos_pkt_error;
	unsigned int tim_tos_to_fw_count;
	unsigned int skip_invalid_msg_hdr;
	unsigned int gnss_fix_time_count;
	unsigned int int_osc_fix_time_count;
};

struct t_ublox_srvy_in_result {
	unsigned int survey_time;
	int mean_x;
	int mean_y;
	int mean_z;
	unsigned int variance_3d;
	unsigned int num_pos_observed;
	unsigned char valid_flag;
	unsigned char in_progress;
	unsigned char is_stale;
};
//==========================================================================
struct t_ublox_stat_data {
	//----Space Vehicle Information Updated From UBX-NAV-SVINFO
	int num_space_veh;
	struct t_ublox_space_veh_info space_veh_info[MAX_NUM_SV];

	//-----Navigation Position Velocity Time Solution Updated from
	// UBX-NAV-PVT
	enum eUblox_gnss_fix gnss_fix; // UBX-NAV-PVT #26
	struct t_ublox_pos_info pos_info;
	struct t_nav_pos_vel_time pos_time_info;
	struct t_ublox_time_pulse_info tm_puls_info;
	struct t_ublox_srvy_in_result srvy_in_stat;

	//-----Synchronization Manager Status from UBX-MON-SMGR
	struct t_sync_mgr_stat sync_mgr_stat;
	int meas_valid_falg;

	//--------- Ublox Message Stat -----------
	struct ublox_rd_wr_stat ublox_stat;
};

#endif // FB_TGD_UBLOX_GPS_MESGH_H_
