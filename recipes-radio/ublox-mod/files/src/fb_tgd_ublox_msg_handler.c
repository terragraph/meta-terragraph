/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <linux/timer.h>
#include <linux/version.h>

/* Ublox device configuration module */
#include "fb_tgd_ublox_gps.h"

#include "fb_tgd_ublox_msg_handler.h"
#include "fb_tg_drvr_app_if.h"
#include "fb_tgd_queue_mgr.h"

#include <fb_tgd_nlsdn_common.h>
#include <fb_tg_gps_driver_if.h>

struct ublox_msg_data {
	int init_flag;
	int timer_running;
	int tm_exp_in_ms;
	enum eUblox_init_state ublox_state;
	int ublox_sm_state_count;
	unsigned long gps_time_in_sec;
	int one_sec_count;
	int timer_shutting_down;
	unsigned int accmulated_ms;
	int i2c_data_len;
	char *i2c_data;
	int ublx_cfg_rsp_len;
	char *cfg_rsp_copy;
	unsigned char rsp_clsid[2];
	enum eUblox_cfg_rsp_dst rsp_dst;
	int cfg_cmd_ack_rsp_len;
	unsigned char cfg_cmd_ack_rsp[UBLX_CFG_ACK_MAX_SIZE];
	struct timer_list gps_timer;
	struct work_struct work_queue;
	tgd_q_hndlr q_hndlr;
	t_ublox_hndlr ublox_handle;
	spinlock_t data_lock;
	enum eTime_sync_mode time_sync_mode;
	int dev_busy_time;
	int srvy_in_start_time;
	int prev_one_sec_count;
	int stat_push_armed;
	unsigned int stat_push_interval; // Seconds
	unsigned long prev_time_sec;
	unsigned long gps_time_sec;
	int adj_timer_value_ms;

	struct mutex clnt_lock;
	struct list_head clnt_list;

	struct t_ublox_stat_data stat_d;
	struct platform_device *platform_dev;
};

struct ublox_msg_client {
	struct list_head link;
	struct ublox_msg_data *ub_data;
	struct fb_tgd_gps_clnt *gps_clnt;
	void *gps_clnt_data;
	bool send_to_clnt;
};

static int nmea_checksum(unsigned char *msg_ptr, int len,
			 unsigned char *checksum_ptr);
static int ublox_dev_rd(t_ublox_hndlr dev_hndl, unsigned char *buf,
			int buf_max_size);
static int ublox_dev_wr(t_ublox_hndlr dv_hndl, unsigned char *buf, int len);
static int ubx_write_cfg_msg_on_off(t_ublox_hndlr dev_hndl,
				    enum eUblox_msg_type msg_type,
				    char *nmea_msg,
				    enum eMsg_enable msg_enable);
static int ublox_msg_rate_set(t_ublox_hndlr dev_hndl, int time_in_ms);
static void ublox_gps_update_time(struct ublox_msg_data *ub_data,
				  struct timespec *ts);
static int tgd_ublox_gps_start_msgs(struct ublox_msg_data *ub_data);
static int tgd_ublox_gps_stop_msgs(struct ublox_msg_data *ub_data);

static int time_tos_handler(struct ublox_msg_data *ub_data, const char *msg_ptr,
			    int len);
static int nav_svinf_msg_handler(struct ublox_msg_data *ub_data,
				 const char *msg_ptr, int len);
static int nav_pvt_msg_handler(struct ublox_msg_data *ub_data,
			       const char *msg_ptr, int len);
static int time_srvyin_handler(struct ublox_msg_data *ub_data,
			       const char *msg_ptr, int len);
static int mon_smgr_msg_handler(struct ublox_msg_data *ub_data,
				const char *msg_ptr, int len);
static int ignore_cfg_rsp_hndlr(struct ublox_msg_data *ub_data,
				const char *msg_ptr, int len);
static int ignore_nmea_rsp_hndlr(struct ublox_msg_data *ub_data,
				 const char *msg_ptr, int len);
static int nmea_txt_msg_handler(struct ublox_msg_data *ub_data,
				const char *msg_ptr, int len);

static struct t_ublx_msg_desc table_nmea_ubx_msg[] = {
    INIT_UMSG("ZDA", ignore_nmea_rsp_hndlr, 0XF0, 0X08, eMSG_DISABLD, 0),
    INIT_UMSG("GSV", ignore_nmea_rsp_hndlr, 0XF0, 0X03, eMSG_DISABLD, 0),
    INIT_UMSG("VTG", ignore_nmea_rsp_hndlr, 0XF0, 0X05, eMSG_DISABLD, 0),
    INIT_UMSG("RMC", ignore_nmea_rsp_hndlr, 0XF0, 0X04, eMSG_DISABLD, 0),
    INIT_UMSG("GSA", ignore_nmea_rsp_hndlr, 0XF0, 0X02, eMSG_DISABLD, 0),
    INIT_UMSG("GLL", ignore_nmea_rsp_hndlr, 0XF0, 0X01, eMSG_DISABLD, 0),
    INIT_UMSG("GGA", ignore_nmea_rsp_hndlr, 0XF0, 0X00, eMSG_DISABLD, 0),
    INIT_UMSG("TXT", nmea_txt_msg_handler, 0XF0, 0X41, eMSG_DISABLD, 0)};

//------- Tag should have min length of 7, should be unique in first six char
static struct t_ublx_msg_desc table_cfg_ubx_msg[] = {
    INIT_UMSG("TIM_TOS", time_tos_handler, 0X0D, 0X12, eMSG_NA, 56),
    INIT_UMSG("NAV_SVIN", nav_svinf_msg_handler, 0X01, 0X30, eMSG_NA, 52),
    INIT_UMSG("NAV_PVT", nav_pvt_msg_handler, 0X01, 0X07, eMSG_NA, 92),
    INIT_UMSG("TIM_SVIN", time_srvyin_handler, 0X0D, 0X04, eMSG_NA, 28),
    INIT_UMSG("MON_SMGR", mon_smgr_msg_handler, 0X0A, 0X2E, eMSG_NA, 16),
    INIT_UMSG("CFG_RST", ignore_cfg_rsp_hndlr, 0X06, 0X04, eMSG_NA, 4),
    INIT_UMSG("CFG_SMGR", ignore_cfg_rsp_hndlr, 0X06, 0X62, eMSG_NA, 20),
    INIT_UMSG("CFG_NAV5", ignore_cfg_rsp_hndlr, 0X06, 0X24, eMSG_NA, 36),
    INIT_UMSG("CFG_TMODE2", ignore_cfg_rsp_hndlr, 0X06, 0X3D, eMSG_NA, 28),
};

#define MAX_FF_COUNT_FOR_DISCARD 16
#define TM_DSP_SRC_GNSS 1
#define SCHED_INTERVAL 2

#define TMODE_OFFSET 6
#define LAT_OFFSET 10
#define LONG_OFFSET 14
#define ALTI_OFFSET 18
#define POS_ACC_OFFSET 22
#define SURVY_MIN_DUR_OFFSET 26
#define SURVY_ACCURCY_OFFSET 30
#define CFG_TMODE2_CMD_LEN 28
#define CFG_TMODE2_CRC_CAL_LEN (CFG_TMODE2_CMD_LEN + 4)
#define NUM_SEC_IN_WEEK (7 * 24 * 60 * 60)

#define UBLX_TM_DRIFT_FOR_FF_BUG 20

#define TIM_DISP_SRC_INT 0
#define TIM_DISP_SRC_GNSS 1
#define TIM_DISP_SRC(flag) ((flag & TIM_TOS_DISP_SOURCE) >> 8)

static int sys_if_add(void);
static void sys_if_remove(void);
static int ublox_gps_register_device(struct ublox_msg_data *ub_data);
static void ublox_gps_unregister_device(struct ublox_msg_data *ub_data);
static int fb_strto_hex_array(const char *asc_data, int asc_data_len,
			      unsigned char *hex_ar, int hex_ar_len);
static int prep_on_off_cmd(enum eUblox_msg_type msg_type, char *ublox_msg_tag,
			   enum eMsg_enable msg_enable, char *cmd_dst_buf,
			   int dst_max_buf_len);
static int is_ublox_device_busy(struct ublox_msg_data *ub_data);
static void set_ublox_device_busy(struct ublox_msg_data *ub_data);
static void clear_ublox_device_busy(struct ublox_msg_data *ub_data);
static int process_cfg_data(struct ublox_msg_data *ub_data, char *rsp_p,
			    int rsp_l, int *cp_index);
static int process_nmea_data(struct ublox_msg_data *ub_data, char *msg_p,
			     int len);
static int parse_nmea_and_cfg_msg(struct ublox_msg_data *ub_data,
				  unsigned char *dtp, int len);
static int push_gps_stats_nb(struct ublox_msg_data *ub_data);
static int config_survey_in(struct ublox_msg_data *ub_data);

/*****************************************************************************/
static struct ublox_msg_data g_ub_data;
static unsigned int g_dbg_mask;

static const char *nav_fix_type_msg[] = {
    "No_Fix",	 "Dead_Reck", "2D_Fiz",      "3D_Fix",
    "GNSS+Dead_Reck", "Time_Only", "NotAvailable"};

static const char *disp_src_name[] = {
    "IntOsc",		"GNSS",   "ExtInt0", "ExtInt1", "IntOscMsrdByHost",
    "ExtOscMsrdByHost", "UnKnown"};

/****************************************************************************
 * Hex dump utility api.
 * cfg_data_ptr -data blob to dump
 * data_len     -len of the data blob
 *****************************************************************************/
static void ublox_hex_dump(const char *cfg_data_ptr, int data_len)
{
	int i;

	printk("ublox_hex_dump Len:%d \n", data_len);
	i = 0;
	while ((data_len - i) >= 8) {
		printk("0x%4.4X    %2.2x %2.2x %2.2x %2.2x "
		       "%2.2x %2.2x %2.2x %2.2x \n",
		       i, cfg_data_ptr[i + 0], cfg_data_ptr[i + 1],
		       cfg_data_ptr[i + 2], cfg_data_ptr[i + 3],
		       cfg_data_ptr[i + 4], cfg_data_ptr[i + 5],
		       cfg_data_ptr[i + 6], cfg_data_ptr[i + 7]);
		i += 8;
	}
	printk("0x%4.4X    \n", i);
	for (; i < data_len; i++) {
		printk("%2.2x \n", cfg_data_ptr[i]);
	}
	printk("\n");
}

/****************************************************************************
 * Prepare the Ublox Config message, for details refer the Ublox data sheet
 * cls_id        -messge class id
 * msg_id        -message id
 * cmd_p         -payload bytes in the message
 * cmd_size      -number of bytes in the pPayload
 * dst_ptr       -buffer to store the prepared message
 * dst_max_size  -max number of bytes that can be stored in dst_ptr
 * return  number of bytes for this message
 *         -1 for error
 *****************************************************************************/
static int prep_cmd_with_hdr_chk_sum(unsigned char cls_id, unsigned char msg_id,
				     void *cmd_p, int cmd_size,
				     unsigned char *dst_ptr, int dst_max_size)
{
	unsigned char checksum[2];

	if (dst_max_size < (UBX_MSG_HDR_AND_CHEKSUM_SIZE + cmd_size)) {
		pr_err("BufferSize: %d < RequiedSize: %d\n", dst_max_size,
		       (6 + cmd_size));
		return -1;
	}
	dst_ptr[0] = UBX_CHAR_SYNC0;
	dst_ptr[1] = UBX_CHAR_SYNC1;
	dst_ptr[2] = cls_id;
	dst_ptr[3] = msg_id;
	dst_ptr[4] = (unsigned char)(cmd_size & 0xFF);
	dst_ptr[5] = (unsigned char)((cmd_size >> 8) & 0xFF);
	memcpy(dst_ptr + MSG_HDR_SIZE, cmd_p, cmd_size);

	nmea_checksum(&dst_ptr[2], (cmd_size + 4), checksum);
	dst_ptr[6 + cmd_size] = checksum[0];
	dst_ptr[7 + cmd_size] = checksum[1];

	return cmd_size + UBX_MSG_HDR_AND_CHEKSUM_SIZE;
}

/****************************************************************************
 * Prepare the Ublox  message checksum, for details refer the Ublox data sheet
 * msg_ptr     - messge pointer
 * len         - number of bytes in the msg_ptr
 * cheksum_ptr - two bytes checksum computed as per Ublox specification
 ****************************************************************************/
static int nmea_checksum(unsigned char *msg_ptr, int len,
			 unsigned char *cheksum_ptr)
{
	unsigned char ckA;
	unsigned char ckB;
	int i;

	ckA = 0;
	ckB = 0;
	for (i = 0; i < len; i++) {
		ckA += msg_ptr[i];
		ckB += ckA;
	}
	cheksum_ptr[0] = ckA;
	cheksum_ptr[1] = ckB;

	return 0;
}

/****************************************************************************
 * Accept a Nmea message tag and return the index in the table_nmea_ubx_msg[]
 * for this message, used to get handle for processing this message
 * msg_tag_ptr - tag to compare with the table entries
 * return  - index in the table_nmea_ubx_msg[] for this message tag
 *         - -1 error unknow tag
 ****************************************************************************/
static int get_nmea_msg_table_index(const char *msg_tag_ptr)
{
	int table_size;
	int i;

	if (!msg_tag_ptr) {
		pr_err("NULL value for the tag pointer\n");
		return -1;
	}
	table_size = sizeof(table_nmea_ubx_msg) / sizeof(table_nmea_ubx_msg[0]);
	for (i = 0; i < table_size; i++) {
		if ((msg_tag_ptr[0] ==
		     table_nmea_ubx_msg[i].name[0]) && // No strncmp
		    (msg_tag_ptr[1] == table_nmea_ubx_msg[i].name[1]) &&
		    (msg_tag_ptr[2] == table_nmea_ubx_msg[i].name[2])) {
			return i;
		}
	}
	return -1;
}

/****************************************************************************
 * Process given Nmea message, msg starts with $Gx and tag stored from [3]
 * Use the table_nmea_ubx_msg[] to get the handler function and invoke it
 * dat_p  - nmea message pointer
 * len    - number of bytes in this nmea message
 * return - 0 success
 *        - nonzero error
 ****************************************************************************/
static int process_cur_nmea_msg(struct ublox_msg_data *ub_data,
				unsigned char *dat_p, int len)
{
	int table_index;

	table_index = get_nmea_msg_table_index(&dat_p[NMEA_MSG_TAG_START]);
	if (table_index < 0) {
		pr_err("Unhandled nmea message: %.*s", len, dat_p);
		return -1;
	}
	if (table_nmea_ubx_msg[table_index].proc_msg) {
		pr_devel("Invoking the msgHandler for %s\n",
			 table_nmea_ubx_msg[table_index].name);
		table_nmea_ubx_msg[table_index].proc_msg(ub_data, dat_p, len);
	}
	return 0;
}

static struct t_ublx_msg_desc *lookup_cfg_msg(char clsid0, char clsid1)
{
	int table_size;
	int i;

	table_size = sizeof(table_cfg_ubx_msg) / sizeof(table_cfg_ubx_msg[0]);
	for (i = 0; i < table_size; i++) {
		if ((clsid0 == table_cfg_ubx_msg[i].clsid[0]) &&
		    (clsid1 == table_cfg_ubx_msg[i].clsid[1])) {
			return &table_cfg_ubx_msg[i];
		}
	}
	return NULL;
}

/*****************************************************************************
 * Handle response from Ublox for config command
 * Look into the config data table for the class id,
 * invoke the handler function if clsid match
 *
 *****************************************************************************/
static int handle_config_resp(struct ublox_msg_data *ub_data,
			      unsigned char *cfg_rsp_data, int len)
{
	struct t_ublx_msg_desc *msg_desc;

	if (!cfg_rsp_data) {
		printk(KERN_WARNING "NULL value for the cfg_rsp_data\n");
		return -1;
	}
	if (len < 6) {
		printk(KERN_WARNING "Invalid len: %d for cfg_rsp_data\n", len);
		return -1;
	}
	msg_desc = lookup_cfg_msg(cfg_rsp_data[2], cfg_rsp_data[3]);
	if (msg_desc == NULL) {
		printk(
		    KERN_WARNING
		    "No handler for config response: %2.2X %2.2X %2.2X %2.2X\n",
		    cfg_rsp_data[0], cfg_rsp_data[1], cfg_rsp_data[2],
		    cfg_rsp_data[3]);
		return -1;
	}
	if (g_dbg_mask & DBG_MSG_CFG_STAT_RD) {
		printk("Received response: %s\n", msg_desc->name);
	}
	if (msg_desc->proc_msg) {
		msg_desc->proc_msg(ub_data, cfg_rsp_data, len);
	}
	return 0;
}

/**************************************************************************
 * Periodically called from the workQ
 * Read the cfg/nmea message from Ublox device, and do the  processing
 * return - 0 no error
 *        - nonzero error,
 **************************************************************************/
static int do_tgd_ublox_msg_processing(struct ublox_msg_data *ub_data)
{
	struct t_ublox_stat_data *ub_stat;
	unsigned char *data_p;
	int len;
	int ret_value;

	if (ub_data->i2c_data == NULL) {
		printk(KERN_WARNING
		       "Ublox Start data mem not allocated before\n");
		return -1;
	}
	data_p = ub_data->i2c_data;
	len = 0;
	do {
		ret_value = ublox_dev_rd(ub_data->ublox_handle, data_p + len,
					 I2C_MSG_MAX_SIZE - len);
		if ((g_dbg_mask & DBG_MSG_UBLX_WARNING) && (ret_value > 0) &&
		    len) {
			printk("^^^^^^^ I2C ReAsml: StoredLen:%d CurLen:%d\n",
			       len, ret_value);
		}
		if (ret_value < 0) {
			// We are seeing the FF's case, adjust one poll time
			ub_data->adj_timer_value_ms = UBLX_TM_DRIFT_FOR_FF_BUG;
#ifdef FORCE_FF_BUG
			ub_data->adj_timer_value_ms = -50;
#endif
		} else {
			len += ret_value;
		}
	} while ((ret_value > 0) && (len < I2C_MSG_MAX_SIZE));

	ub_stat = &ub_data->stat_d;
	if (len < UBX_MSG_HDR_AND_CHEKSUM_SIZE) {
		if (len) {
			ub_stat->ublox_stat.rd_pkt_len_error++;
		}
		if (g_dbg_mask & DBG_MSG_ENABLE_POLL) {
			printk("UbloxRead Invalid Len: %d\n", len);
		}
		return -1;
	}
	if (g_dbg_mask & DBG_MSG_ENABLE_POLL) {
		printk("Len: %d\n", len);
	}
	ub_stat->ublox_stat.rd_pkt_count++;
	ub_data->i2c_data_len = len;
	ret_value = parse_nmea_and_cfg_msg(ub_data, data_p, len);

	return ret_value;
}

/*******************************************************************
 * Parse the data rxed from ublox, contain both NMEA and Config Resp
 * data_p - raw data from the ublox
 * len    - number of bytes in data_p
 * return 0 -parse OK
 *        nonzero on error
 ********************************************************************/
static int parse_nmea_and_cfg_msg(struct ublox_msg_data *ub_data,
				  unsigned char *dtp, int len)
{
	struct t_ublox_stat_data *ub_stat;
	int nmea_present;
	int cnfg_msg_index;
	int di;
	int ret_len;
	int ff_cont_count; // want to look into continuous FFs from Ublox

	ub_stat = &ub_data->stat_d;
	//---- May have multiple NMEA messages and config responses
	nmea_present = 0;
	cnfg_msg_index = 0;
	ff_cont_count = 0;
	di = 0;
	while (di < (len - UBX_MSG_HDR_AND_CHEKSUM_SIZE)) {
		if (g_dbg_mask & DBG_MSG_NMEA_CFG_PRSR) {
			printk("Total:%d CurI:%d  Rem:%d\n", len, di, len - di);
		}
		if ((dtp[di] == UBX_CHAR_SYNC0) &&
		    (dtp[di + 1] == UBX_CHAR_SYNC1)) {
			//------ Matching with Cfg Rsp Hdr -----
			ff_cont_count = 0;
			ret_len = process_cfg_data(ub_data, &dtp[di], len - di,
						   &cnfg_msg_index);
			if (ret_len < 0) {
				printk(KERN_WARNING
				       "Error: parse_nmea_and_cfg_msg Len:%d\n",
				       ret_len);
				return -1; // Error in cfg resp, discard all
					   // remaining byte
			}
			di += ret_len; // ret_len give number of bytes belongs
			// to this rsp
			continue;
		} else if ((dtp[di] == NMEA_START_CH0) &&
			   (dtp[di + 1] == NMEA_START_CH1)) {
			//--------- A Nmea message ------------
			ff_cont_count = 0;
			ret_len =
			    process_nmea_data(ub_data, &dtp[di], len - di);
			if (ret_len < 0) {
				return -1; // Error in cfg resp, discard all
					   // remaining byte
			}
			di += ret_len; // ret_len give number of bytes belongs
			// to this rsp
			continue;
		} else {
			if (dtp[di] == 0xFF) {
				ff_cont_count++;
				if (ff_cont_count > MAX_FF_COUNT_FOR_DISCARD) {
					printk(KERN_WARNING "%d Continued FFs, "
							    "discarding data "
							    "\n",
					       ff_cont_count);
					return -1;
				}
			}
			if (g_dbg_mask & DBG_MSG_NMEA_CFG_PRSR) {
				printk("FF_Count: %d Neither CFG Nor NMEA  "
				       "%2.2x %2.2x %2.2x %2.2x %2.2x %2.2x\n",
				       ff_cont_count, dtp[di + 0], dtp[di + 1],
				       dtp[di + 2], dtp[di + 3], dtp[di + 4],
				       dtp[di + 5]);
			}
			di++;
			ub_stat->ublox_stat.skip_invalid_msg_hdr++;
		}
	}

	return 0;
}
/*****************************************************************************
 * msg_p starts with $G***, search for the NMEA end with CR+LF
 *
 * msg_p - pointer to the NMEA response data
 * len - number of bytes in the NMEA response data
 * return the number of bytes occupied by this NMEA message (including CR+LF)
 *****************************************************************************/
static int process_nmea_data(struct ublox_msg_data *ub_data, char *msg_p,
			     int len)
{
	int nmea_i;
	int msg_len;

	if (g_dbg_mask & DBG_MSG_NMEA_CFG_PRSR) {
		printk("NMEA: %c%c%c%c%c%c\n", msg_p[0], msg_p[1], msg_p[2],
		       msg_p[3], msg_p[4], msg_p[5]);
	}
	nmea_i = -1;
	msg_len = 0;
	while (nmea_i < len) {
		nmea_i++;
		if ((msg_p[nmea_i] == MSG_CR) || (msg_p[nmea_i] == MSG_LF)) {
			msg_len = nmea_i + 1; //+1 to include the CR+LF

			if (g_dbg_mask & DBG_MSG_NMEA_CFG_PRSR) {
				printk("NmeaMsgEnd:%d Len:%d\n", nmea_i,
				       msg_len);
			}
			process_cur_nmea_msg(ub_data, msg_p, msg_len);
			if ((msg_p[nmea_i + 1] == MSG_CR) ||
			    (msg_p[nmea_i + 1] == MSG_LF)) {
				msg_len++;
			}
			return msg_len;
		}
		if (msg_p[nmea_i] > NMEA_MSG_MAX_CHR) {
			printk(KERN_WARNING "ERROR: NmeaMsg 0x%x at %d\n",
			       msg_p[nmea_i], nmea_i);
			return nmea_i; // skip upto one byte before this message
		}
	}
	if (nmea_i >= len) {
		printk(KERN_WARNING "ERROR: NmeaMsg No termination Index:%d  "
				    "Len:%d Char: 0x%x\n",
		       nmea_i, len, msg_p[nmea_i]);
		return -1;
	}
	return 1; // Not reach here, 1 to skip at least 1 byte from called
		  // function
}

/*************************************************************************
 * The config response read from the I2C processed here, this routine may get
 * called multiple time for a single read (eg: more than one pending response
 * or command ack + command response)
 * Looking three types of response
 *   1) Config command Ack, keep the latest copy for debug interface to read
 *   2) Config command resp for commands issued by the state machine
 *       Invoke the handler for the response based on the resp classid
 *   3) Config command resp for commands issued from the user/debug interface
 *       Keep the latest copy for the debug interface to read
 * rsp_p - pointer to the config response data
 * rsp_l - number of bytes in the response data
 * cp_index - Number of bytes used from the given buffer
 *return  >0, Total number of bytes that occupied by this config response
 *        <0  Error in the packet
 *************************************************************************/
static int process_cfg_data(struct ublox_msg_data *ub_data, char *rsp_p,
			    int rsp_len, int *cp_index)
{
	struct t_ublox_stat_data *ub_stat;
	int clen, cfg_total_len;
	unsigned char checksum[2];

	ub_stat = &ub_data->stat_d;
	clen = rsp_p[5];
	clen = ((clen << 8) & 0xFF00) + (rsp_p[4] & 0xFF);
	if (g_dbg_mask & DBG_MSG_NMEA_CFG_PRSR) {
		printk("CfgRspL:%d %2.2x %2.2x %2.2x %2.2x %2.2x %2.2x\n", clen,
		       rsp_p[0], rsp_p[1], rsp_p[2], rsp_p[3], rsp_p[4],
		       rsp_p[5]);
	}
	cfg_total_len = clen + UBX_MSG_HDR_AND_CHEKSUM_SIZE;
	if (cfg_total_len > rsp_len) {
		printk(KERN_WARNING "cfg_total_len:%d>len:%d\n", cfg_total_len,
		       rsp_len);
		return -1;
	}
	//---- verify the checksum ------
	nmea_checksum(&rsp_p[2], clen + UBX_MSG_CHEKSUM_HDR_SIZE, checksum);
	if ((checksum[0] != rsp_p[clen + MSG_HDR_SIZE]) ||
	    (checksum[1] != rsp_p[clen + MSG_HDR_SIZE + 1])) {
		ub_stat->ublox_stat.cheksum_error++;
		printk(KERN_WARNING "Chksum ERROR\n");
		return 1; // return 1 to skip only one byte and look new data
			  // start
	}
	//------ Checksum OK look for response ids -------
	if (g_dbg_mask & DBG_MSG_NMEA_CFG_PRSR) {
		printk("Prsr RxClsId: %2.2x %2.2x  SmClsId: %2.2x %2.2x\n",
		       rsp_p[2], rsp_p[3], ub_data->rsp_clsid[0],
		       ub_data->rsp_clsid[1]);
	}

	//-------- Check for config command  Ack B5 62 05 01 02 00 06 01 0F 38
	if ((rsp_p[2] == CFG_ACK_CLS_ID1) && (rsp_p[3] == CFG_ACK_CLS_ID2) &&
	    (ub_data->rsp_clsid[0] == rsp_p[6]) &&
	    (ub_data->rsp_clsid[1] == rsp_p[7])) {
		ub_data->cfg_cmd_ack_rsp_len = cfg_total_len;
		if (cfg_total_len > UBLX_CFG_ACK_MAX_SIZE) {
			ub_data->cfg_cmd_ack_rsp_len = UBLX_CFG_ACK_MAX_SIZE;
		}
		memcpy(ub_data->cfg_cmd_ack_rsp, rsp_p,
		       ub_data->cfg_cmd_ack_rsp_len);
		clear_ublox_device_busy(ub_data);
	} else if ((ub_data->rsp_clsid[0] == rsp_p[2]) &&
		   (ub_data->rsp_clsid[1] == rsp_p[3])) {
		if (ub_data->rsp_dst ==
		    eUBLOX_RSP_DST_TMR) { // SM/Timer issued command resp
			handle_config_resp(ub_data, rsp_p, cfg_total_len);
		} else { // User issued command response
			if (ub_data->cfg_rsp_copy &&
			    (((*cp_index) + cfg_total_len) < CFG_RSP_MAX)) {
				memcpy(ub_data->cfg_rsp_copy + (*cp_index),
				       rsp_p, cfg_total_len);
				(*cp_index) += cfg_total_len;
				ub_data->ublx_cfg_rsp_len = (*cp_index);
			}
		}
		ub_data->rsp_clsid[0] = 0;
		ub_data->rsp_clsid[1] = 0;
		ub_data->rsp_dst = eUBLOX_RSP_DST_NONE;
		clear_ublox_device_busy(ub_data);

	} else if ((rsp_p[2] == CFG_TIM_TOS_ID1) &&
		   (rsp_p[3] == CFG_TIM_TOS_ID2)) {
		//---- TIM-TOS periodic update
		handle_config_resp(ub_data, rsp_p, cfg_total_len);
	}
	return cfg_total_len;
}

/****************************************************************************
 * Handler for U-Blox TXT messages. Just dump them into the log.
 ****************************************************************************/
static int nmea_txt_msg_handler(struct ublox_msg_data *ub_data,
				const char *msg_ptr, int len)
{
	/*
	 * Unsolicited text messages from GPS device are almost
	 * certainly error conditions we'd like to know about.
	 */
	pr_err("%.*s\n", len, msg_ptr);
	return 0;
}

/****************************************************************************
 * Dummy handler for all messages that are not enabled,
 * This function dont get invoked as these messages are disabled from Ublox
 ****************************************************************************/
static int ignore_nmea_rsp_hndlr(struct ublox_msg_data *ub_data,
				 const char *msg_ptr, int len)
{
	char temp_msg[8];
	if (len < 6) {
		return 0;
	}
	//---------- Stop this message as we are not using it ---
	temp_msg[0] = msg_ptr[3];
	temp_msg[1] = msg_ptr[4];
	temp_msg[2] = msg_ptr[5];
	temp_msg[3] = 0;
	ubx_write_cfg_msg_on_off(ub_data->ublox_handle, eUBLOX_MSG_NMEA,
				 temp_msg, eMSG_DISABLD);
	memcpy(temp_msg, msg_ptr, 6);
	temp_msg[6] = 0;
	return 0;
}

/****************************************************************************
 * Dummy handler for all config commands for which not expecting a response,
 ****************************************************************************/
static int ignore_cfg_rsp_hndlr(struct ublox_msg_data *ub_data,
				const char *msg_ptr, int len)
{

	return 0;
}

/****************************************************************************
 * Accept a config message tag and return the index in the table_cfg_ubx_msg[]
 * for this message, used to get calss id for config read command
 * msg_tag_ptr - tag to compare with the table entries
 * return  - index in the table_cfg_ubx_msg[] for this message tag
 *         - -1 error unknown tag
 ****************************************************************************/
static int get_cfg_msg_table_index(unsigned char *msg_tag_ptr)
{
	int table_size;
	int i;
	int str_cmp_len;

	if (!msg_tag_ptr) {
		pr_err("NULL for tag pointer get_cfg_msg_table_index\n");
		return -1;
	}
	str_cmp_len = strlen(msg_tag_ptr);
	if (str_cmp_len <= 0) {
		pr_err("Invalid stringLen:%d for tag\n", str_cmp_len);
		return -1;
	}
	if (str_cmp_len > 7) {
		str_cmp_len = 7;
	}
	table_size = sizeof(table_cfg_ubx_msg) / sizeof(table_cfg_ubx_msg[0]);
	for (i = 0; i < table_size; i++) {
		if (strncmp(msg_tag_ptr, table_cfg_ubx_msg[i].name,
			    str_cmp_len) == 0) {
			break;
		}
	}
	if (i >= table_size) {
		pr_err("%s Not in table_cfg_ubx_msg table\n", msg_tag_ptr);
		return -1;
	}
	return i;
}

/*****************************************************************************
 * cfg_tag  - tag should match with table in the fb_tgd_ublox_msg_handler.h file
 * cfg_data -   config data to append after the header
 * cfg_data_len - number of bytes to be copied from the cfg_data
 * For a read command, cfg_data should be NULL and cfb_data_len should be zero
 * cfg_cmd_buf - full config command with checksum will be copied to this buffer
 * cfg_cmd_buf_len - cfg_cmd_buf MaxSize
 *****************************************************************************/
static int prep_cfg_cmd(char *cfg_tag, char *cfg_data, int data_len,
			char *cfg_cmd_buf, int cfg_cmd_buf_len)
{
	unsigned char checksum[2];
	int index;

	if ((data_len) && (cfg_data == NULL)) {
		printk(KERN_WARNING "prep_cfg_cmd: DataLen:%d DataPtr=NULL\n",
		       data_len);
		return -1;
	}
	if (cfg_cmd_buf == NULL) {
		printk(KERN_WARNING "prep_cfg_cmd: WorkBuffer=NULL\n");
		return -1;
	}
	if (cfg_cmd_buf_len < (data_len + UBX_MSG_HDR_AND_CHEKSUM_SIZE)) {
		printk(KERN_WARNING
		       "cfg_cmd_buf_len:%d < (data_len:%d + Hdr:%d)\n",
		       cfg_cmd_buf_len, data_len, UBX_MSG_HDR_AND_CHEKSUM_SIZE);
		return -1;
	}
	index = get_cfg_msg_table_index(cfg_tag);
	if (index < 0) {
		printk(KERN_WARNING "No TableEntryForCfgCommand %s\n", cfg_tag);
		return -1;
	}
	cfg_cmd_buf[0] = UBX_CHAR_SYNC0;
	cfg_cmd_buf[1] = UBX_CHAR_SYNC1;
	cfg_cmd_buf[2] = table_cfg_ubx_msg[index].clsid[0];
	cfg_cmd_buf[3] = table_cfg_ubx_msg[index].clsid[1];
	cfg_cmd_buf[4] = data_len & 0xFF;
	cfg_cmd_buf[5] = (data_len >> 8) & 0xFF;
	memcpy(&cfg_cmd_buf[MSG_HDR_SIZE], cfg_data, data_len);
	nmea_checksum(&cfg_cmd_buf[2], data_len + UBX_MSG_CHEKSUM_HDR_SIZE,
		      checksum);
	cfg_cmd_buf[MSG_HDR_SIZE + data_len] = checksum[0];
	cfg_cmd_buf[MSG_HDR_SIZE + data_len + 1] = checksum[1];

	if (g_dbg_mask & DBG_MSG_CFG_STAT_RD) {
		printk("Scheduling %s CfgCmd\n", cfg_tag);
	}

	return data_len + UBX_MSG_HDR_AND_CHEKSUM_SIZE;
}

/*****************************************************************************
 * B5 62 06 04 04 00 FF FF 00 00  //cold start + hw reset cmd array
 *****************************************************************************/
static void schedule_ublox_reset(struct ublox_msg_data *ub_data,
				 enum eUblox_cfg_rsp_dst rsp_dst)
{
	int rlen;
	char rst_msg[] = {0xFF, 0xFF, 0x00, 0x00}; // cold start + hw reset
	unsigned int priv_data;
	char work_buf[32];

	rlen = prep_cfg_cmd("CFG_RST", rst_msg, 4, work_buf, sizeof(work_buf));

	priv_data = PREP_PRIV_DATA(work_buf[2], work_buf[3], rsp_dst);
	tgd_queue_create_new_entry(ub_data->q_hndlr, work_buf, rlen, priv_data);
}

/************************************************************************
 *  Configure ublox synchronization manager (UBX-CFG-SMGR) for:
 *  (A) TPCoherent: Control time pulse coherency,
 *      Ublox default (2) - Post-initialization coherent pulses.
 *      Change to (1) - Non-coherent pulses, correct time offsets quickly
 *  (B) useAnyFix
 *      Ublox default (0) - use over-determined navigation solutions only
 *      Change to (1) - use any fix
 *      with default ublox needs atleast 2 sat in time-only mode
 *      with this change ublox can work with just 1 sat
 *  Change the default falg value in  UBX-CFG-SMGR (0x06 0x62)
 *  00 0F 1E 00 50 00 00 00 FA 00 D0 07 0F 00 10 27 CA B0 00 00 //default
 *  00 0F 1E 00 50 00 00 00 FA 00 D0 07 0F 00 10 27 CA 70 00 00 //TPCoherent
 *  00 0F 1E 00 50 00 00 00 FA 00 D0 07 0F 00 10 27 CA 74 00 00 //useAnyFix
 ***********************************************************************/
static int config_sync_manager(struct ublox_msg_data *ub_data)
{
	int rlen;
	unsigned int priv_data;
	char work_buf[32];
	unsigned char cfg_smgr_ar[] = {0x00, 0x0F, 0x1E, 0x00, 0x50, 0x00, 0x00,
				       0x00, 0xFA, 0x00, 0xD0, 0x07, 0x0F, 0x00,
				       0x10, 0x27, 0xCA, 0x74, 0x00, 0x00};

	rlen = prep_cfg_cmd("CFG_SMGR", cfg_smgr_ar, sizeof(cfg_smgr_ar),
			    work_buf, sizeof(work_buf));
	if (g_dbg_mask & DBG_MSG_CFG_RSP_RAW) {
		printk("config_sync_manager cmd string\n");
		ublox_hex_dump(work_buf, rlen);
	}

	priv_data =
	    PREP_PRIV_DATA(work_buf[2], work_buf[3], eUBLOX_RSP_DST_NONE);
	tgd_queue_create_new_entry(ub_data->q_hndlr, work_buf, rlen, priv_data);

	return 0;
}

/************************************************************************
 *  Configure ublox navigation engine settings (UBX-CFG-NAV5) for:
 *  (A) minElev: Minimum Elevation for a GNSS satellite to be used in NAV
 *      Ublox default = 5 degrees
 *      Change to 15 degrees
 *  Change the default falg value in  UBX-CFG-NAV5 (0x06 0x24)
 *  Default:
 *  FF FF 02 03 00 00 00 00 10 27 00 00 05 00 FA 00 FA 00
 *  64 00 2C 01 00 3C 00 00 00 00 C8 00 03 00 00 00 00 00
 *  Change:
 *  02 00 02 03 00 00 00 00 10 27 00 00 0F 00 FA 00 FA 00
 *  64 00 2C 01 00 3C 00 00 00 00 C8 00 03 00 00 00 00 00
 ***********************************************************************/
static int config_nav_engine(struct ublox_msg_data *ub_data)
{
	int rlen;
	unsigned int priv_data;
	char work_buf[64];
	unsigned char cfg_smgr_ar[] = {
	    0x02, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x10,
	    0x27, 0x00, 0x00, 0x0F, 0x00, 0xFA, 0x00, 0xFA, 0x00,
	    0x64, 0x00, 0x2C, 0x01, 0x00, 0x3C, 0x00, 0x00, 0x00,
	    0x00, 0xC8, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00};

	rlen = prep_cfg_cmd("CFG_NAV5", cfg_smgr_ar, sizeof(cfg_smgr_ar),
			    work_buf, sizeof(work_buf));
	if (g_dbg_mask & DBG_MSG_CFG_RSP_RAW) {
		printk("nav_engine cmd string\n");
		ublox_hex_dump(work_buf, rlen);
	}

	priv_data =
	    PREP_PRIV_DATA(work_buf[2], work_buf[3], eUBLOX_RSP_DST_NONE);
	tgd_queue_create_new_entry(ub_data->q_hndlr, work_buf, rlen, priv_data);

	return 0;
}

/*****************************************************************************
 * Schedule a config command, will be sent during the next timer/worker
 * cfg_tag  - tag should match with table in the fb_tgd_ublox_msg_handler.h file
 * cfg_data -   config data to append after the header
 * data_len - number of bytes to be copied from the cfg_data
 * For a read command, cfg_data should be NULL and cfb_data_len should be zero
 *****************************************************************************/
static int schedule_config_cmd(struct ublox_msg_data *ub_data, char *cfg_tag,
			       char *cfg_data, int data_len)
{
	int rlen;
	unsigned int priv_data;
	char wbuf[32];

	rlen = prep_cfg_cmd(cfg_tag, cfg_data, data_len, wbuf, sizeof(wbuf));
	priv_data = PREP_PRIV_DATA(wbuf[2], wbuf[3], eUBLOX_RSP_DST_TMR);
	return tgd_queue_create_new_entry(ub_data->q_hndlr, wbuf, rlen,
					  priv_data);
}

/*****************************************************************************
*****************************************************************************/
static int schedule_cfg_msg_on_off(struct ublox_msg_data *ub_data,
				   enum eUblox_msg_type mtype, char *mtag,
				   enum eMsg_enable msg_enable)
{
	int rsize;
	unsigned int priv_data;
	char wbuf[32];

	rsize = prep_on_off_cmd(mtype, mtag, msg_enable, wbuf, sizeof(wbuf));
	priv_data = PREP_PRIV_DATA(wbuf[6], wbuf[7], eUBLOX_RSP_DST_TMR);
	return tgd_queue_create_new_entry(ub_data->q_hndlr, wbuf, rsize,
					  priv_data);
}

/*****************************************************************************
*****************************************************************************/
static int update_cfg_stat(struct ublox_msg_data *ub_data)
{
	// schedule_config_cmd("MON_SMGR", NULL, 0);
	schedule_config_cmd(ub_data, "NAV_PVT", NULL, 0);
	schedule_cfg_msg_on_off(ub_data, eUBLOX_MSG_CFG, "TIM_TOS",
				eMSG_REPT_RD);
	schedule_config_cmd(ub_data, "NAV_SVIN", NULL, 0);

	return 0;
}

/*****************************************************************************
*****************************************************************************/
static enum eUblox_init_state
check_ublox_sync_stat(struct ublox_msg_data *ub_data)
{
	struct t_ublox_stat_data *ub_stat;
	int disp_src;
	int pps_locked;

	ub_stat = &ub_data->stat_d;
	// D10-D8  Disciplining source id
	disp_src =
	    ((ub_stat->tm_puls_info.flags & TIM_TOS_DISP_SOURCE) >> 8) & 0x07;
	pps_locked =
	    (ub_stat->tm_puls_info.flags & TIM_TOS_PULS_LOCKED) ? 1 : 0;
	if (pps_locked && (disp_src == TM_DSP_SRC_GNSS)) {
		printk("$$$$$$ PPS in Sync\n");
		update_cfg_stat(ub_data);
		return eGPS_STATE_TIME_IN_SYNC;
	}
	return eGPS_STATE_WAIT_SYNC;
}

/*****************************************************************************
 * WorkQ handler, getting scheduled from the timer
 * Do the initialization during the first pass, periodically read the status
 * till get the time sync information
 * Poll the Ublox, read and process the pending data
 * Check the Tx Queue, and start transmission any pending config command
 *****************************************************************************/
static void tgd_ublox_msg_handler_worker(struct work_struct *work)
{
	struct ublox_msg_data *ub_data;
	struct t_ublox_stat_data *ub_stat;
	char *data_p;
	int len;
	char *cfg_data_p;
	int cfg_d_len;
	unsigned int priv_data;
	int in_new_sec = 0;

	ub_data = container_of(work, struct ublox_msg_data, work_queue);
	ub_stat = &ub_data->stat_d;

	if (g_dbg_mask & DBG_MSG_ENABLE_POLL) {
		printk("WorkHandler: %d\n", ub_data->ublox_state);
	}
	if (ub_data->one_sec_count != ub_data->prev_one_sec_count) {
		ub_data->prev_one_sec_count = ub_data->one_sec_count;
		in_new_sec = 1;
	}

	switch (ub_data->ublox_state) {
	case eUBLX_STATE_DOWN:
		return;

	case eGPS_STATE_INIT:
		data_p = ub_data->i2c_data;
		len = ublox_dev_rd(ub_data->ublox_handle, data_p,
				   I2C_MSG_MAX_SIZE);
		if (len == 0) {
			schedule_cfg_msg_on_off(ub_data, eUBLOX_MSG_CFG,
						"TIM_TOS", eMSG_REPT_RD);
			config_sync_manager(ub_data);
			config_nav_engine(ub_data);
			config_survey_in(ub_data);
			memset(ub_stat, 0, sizeof(*ub_stat));
			ub_data->ublox_state = eGPS_STATE_WAIT_SYNC;
		}
		return;

	case eGPS_STATE_WAIT_SYNC:
		if (in_new_sec) {
			ub_data->ublox_state = check_ublox_sync_stat(ub_data);
		}
		break;

	case eGPS_STATE_TIME_IN_SYNC:
		if (in_new_sec) {
		}
		break;

	default:
		break;
	}

	ub_data->ublox_sm_state_count++;
	do_tgd_ublox_msg_processing(
	    ub_data); // Check/process the Ublox for Rx data

	if (is_ublox_device_busy(ub_data)) {
		return;
	}
	//------------ Look for any cfgCmd to send -----
	cfg_data_p = tgd_queue_get(ub_data->q_hndlr, &cfg_d_len, &priv_data);
	if (cfg_data_p != NULL && cfg_d_len > 0) {
		if (g_dbg_mask & DBG_MSG_ENABLE_POLL) {
			printk("NexTxDesc: of Len:%d prvData: 0x%4.4x\n",
			       cfg_d_len, priv_data);
		}
		EXTRACT_PRIV_DATA(priv_data, ub_data->rsp_clsid[0],
				  ub_data->rsp_clsid[1], ub_data->rsp_dst);
		set_ublox_device_busy(ub_data);
		ublox_dev_wr(ub_data->ublox_handle, cfg_data_p, cfg_d_len);
		tgd_queue_free_queue(ub_data->q_hndlr, cfg_data_p);
	}
	if ((in_new_sec) && (ub_data->stat_push_interval)) {
		if (ub_data->stat_push_armed) {
			if (g_dbg_mask & DBG_MSG_CFG_STAT_RD) {
				printk("%d Pushing GPS stat\n",
				       ub_data->prev_one_sec_count);
			}
			ub_data->stat_push_armed = 0;
			push_gps_stats_nb(ub_data);
		}
		if ((ub_data->prev_one_sec_count) &&
		    (ub_data->prev_one_sec_count %
		     ub_data->stat_push_interval) == 0) {
			if (g_dbg_mask & DBG_MSG_CFG_STAT_RD) {
				printk("%d Enabling GPS stat push\n",
				       ub_data->prev_one_sec_count);
			}
			ub_data->stat_push_armed = 1;
			schedule_config_cmd(ub_data, "NAV_SVIN", NULL, 0);
			schedule_config_cmd(ub_data, "NAV_PVT", NULL, 0);
			schedule_config_cmd(ub_data, "TIM_SVIN", NULL, 0);
		}
	}
}

/************************************************************************
 * rearm the timer to trigger it after the given time in ms.
 * Gets invoked from the timer handler
 * return 0 success
 *        nonzero, if this timer is getting stopped by stop_ublox_polling()
 **************************************************************************/
static int rearm_timer(struct ublox_msg_data *ub_data, int time_intrvl_ms)
{
	struct timer_list *gps_timer_ptr;
	unsigned long next_jif, cur_jif;

	/* Detected the read 0xFF's error case during the previous read
	 * Adjust one poll interval to make the driver read is not happening
	 * at the time of Ublox internal update
	 */
	if (ub_data->adj_timer_value_ms) {
		// printk("Adjusting timer for %d ms\n",
		// ub_data->adj_timer_value_ms);
		time_intrvl_ms += ub_data->adj_timer_value_ms;
		ub_data->adj_timer_value_ms = 0;
	}
	next_jif = msecs_to_jiffies(time_intrvl_ms);
	cur_jif = jiffies;

	ub_data->accmulated_ms += time_intrvl_ms;
	if (ub_data->accmulated_ms >= 1000) {
		ub_data->one_sec_count += ub_data->accmulated_ms / 1000;
		ub_data->accmulated_ms = ub_data->accmulated_ms % 1000;
	}

	gps_timer_ptr = &ub_data->gps_timer;
	if (ub_data->timer_shutting_down == 0) {
		gps_timer_ptr->expires += next_jif;
		add_timer(gps_timer_ptr);
		if (g_dbg_mask & DBG_MSG_ENABLE_POLL) {
			printk("cur_jif:%lu  next_jif:%lu\n", cur_jif,
			       next_jif);
		}
		return 0;
	}
	return -1;
}

/**************************************************************************
 * Timer handler function
 * Rearm the timer for periodic invocation and do the Ublox processing in
 * work queue
 * data - Initialized to give g_ub_data pointer
 **************************************************************************/
static void ublox_timer_handler_fn(unsigned long data)
{
	struct ublox_msg_data *ub_data;
	int ret_value;

	if (g_dbg_mask & DBG_MSG_ENABLE_POLL) {
		printk("In Timer\n");
	}

	//--  global g_ub_data received as data parameter, using the data
	ub_data = (struct ublox_msg_data *)data;
	ret_value = rearm_timer(ub_data, ub_data->tm_exp_in_ms);
	if (ret_value) {
		return; // Timer stopped, not queuing work, need start cmd to
			// re-enable
	}
	schedule_work(&ub_data->work_queue);
}

/**************************************************************************
 * Invoked from module load init, set the Ublox messge rate
 * Called only when timer is not active
 * Stops all Nmea messges coming from the Ublox chip
 * dev_hndl    - The Ubolox device handler
 * msg_rate_ms - Ublox message update rate
 **************************************************************************/
static int stop_all_nmea_msgs(t_ublox_hndlr dev_hndl, int msg_rate_ms)
{
	ublox_msg_rate_set(dev_hndl, msg_rate_ms);
	ubx_write_cfg_msg_on_off(dev_hndl, eUBLOX_MSG_NMEA, "VTG",
				 eMSG_DISABLD);
	ubx_write_cfg_msg_on_off(dev_hndl, eUBLOX_MSG_NMEA, "RMC",
				 eMSG_DISABLD);
	ubx_write_cfg_msg_on_off(dev_hndl, eUBLOX_MSG_NMEA, "GSV",
				 eMSG_DISABLD);
	ubx_write_cfg_msg_on_off(dev_hndl, eUBLOX_MSG_NMEA, "GSA",
				 eMSG_DISABLD);
	ubx_write_cfg_msg_on_off(dev_hndl, eUBLOX_MSG_NMEA, "GLL",
				 eMSG_DISABLD);
	ubx_write_cfg_msg_on_off(dev_hndl, eUBLOX_MSG_NMEA, "GGA",
				 eMSG_DISABLD);
	ubx_write_cfg_msg_on_off(dev_hndl, eUBLOX_MSG_NMEA, "ZDA",
				 eMSG_DISABLD);

	return 0;
}

/**************************************************************************
 * Prepare the rate set message and send it to the Ublox device
 * Called only when timer is not active
 * dev_hndl - The Ubolox device handler
 * time_in_ms - Messge generation rate in ms
 * return   0  Success
 *          nonzero error
 **************************************************************************/
static int ublox_msg_rate_set(t_ublox_hndlr dev_hndl, int time_in_ms)
{
	unsigned char payload_r[8];
	unsigned char cfg_ubx_msg[32];
	int ret_size;
	int ret_stat;
	int msg_len;

	payload_r[0] = time_in_ms & 0xFF;
	payload_r[1] = (time_in_ms >> 8) & 0xFF;
	payload_r[2] = 0x01;
	payload_r[3] = 0x00;
	payload_r[4] = 0x01;
	payload_r[5] = 0x00;
	msg_len = 6;
	ret_size = prep_cmd_with_hdr_chk_sum(UBX_CLS_ID, UBX_MSG_ID_RATE_SET,
					     payload_r, msg_len, cfg_ubx_msg,
					     sizeof(cfg_ubx_msg));
	if (ret_size < UBX_MSG_HDR_AND_CHEKSUM_SIZE) {
		return -1;
	}
	ret_stat = ublox_dev_wr(dev_hndl, cfg_ubx_msg, ret_size);
	if (ret_stat) {
		pr_err("Failed ublox_dev_wr\n");
		return -1;
	}
	return 0;
}

/**************************************************************************
 * Prepare the config command for enable/disble the given message
 * Get the class id from the table and use prep_cmd_with_hdr_chk_sum to get
 * the header and checksum added
 * msg_type          - eUBLOX_MSG_NMEA or eUBLOX_MSG_CFG
 * ublox_msg_tag     - the nmea/config message tag like ZDA or GSV
 * msg_enable        - 0-disable the message, nonzero-enable the message
 * cmd_dst_buf       - Destination buffer for the created config command
 * dst_max_buf_len   - Max size for the destination buffer
 * return   Size for the prepared config command
 *          -1 for error
 **************************************************************************/
static int prep_on_off_cmd(enum eUblox_msg_type msg_type, char *ublox_msg_tag,
			   enum eMsg_enable msg_enable, char *cmd_dst_buf,
			   int dst_max_buf_len)
{
	int table_index;
	int ret_size;
	unsigned char cfg_ubx_payload[16];
	unsigned char enable_flag = 0;
	int i;

	if (dst_max_buf_len <
	    (UBX_MSG_HDR_AND_CHEKSUM_SIZE + UBX_MSG_CTRL_SIZE)) {
		pr_err("dst_max_buf_len: %d  < %d\n", dst_max_buf_len,
		       (UBX_MSG_HDR_AND_CHEKSUM_SIZE + UBX_MSG_CTRL_SIZE));
		return -1;
	}

	if (msg_enable != eMSG_DISABLD) {
		enable_flag = 1;
	}
	if (msg_type == eUBLOX_MSG_NMEA) {
		table_index = get_nmea_msg_table_index(ublox_msg_tag);
		if (table_index < 0) {
			pr_err("Not found %s in nmea table\n", ublox_msg_tag);
			return -1;
		}
		cfg_ubx_payload[0] = table_nmea_ubx_msg[table_index].clsid[0];
		cfg_ubx_payload[1] = table_nmea_ubx_msg[table_index].clsid[1];
		table_nmea_ubx_msg[table_index].msg_enable = msg_enable;
	} else if (msg_type == eUBLOX_MSG_CFG) {
		table_index = get_cfg_msg_table_index(ublox_msg_tag);
		if (table_index < 0) {
			printk(KERN_WARNING "No TableEntryForCfgCmd %s\n",
			       ublox_msg_tag);
			return -1;
		}
		cfg_ubx_payload[0] = table_cfg_ubx_msg[table_index].clsid[0];
		cfg_ubx_payload[1] = table_cfg_ubx_msg[table_index].clsid[1];
	}
	//[0]-DDC, [1]-UART1, [2]-UART2, [3]USB, [4]SPI
	cfg_ubx_payload[2] = enable_flag; // Enable only the DDC/I2C
	for (i = 1; i < 6; i++)
		cfg_ubx_payload[2 + i] =
		    0; // Disable [1]-UART1 [2]-UART2, [3]USB, [4]SPI

	ret_size = prep_cmd_with_hdr_chk_sum(UBX_CLS_ID, UBX_MSG_ID_MSG_CFG,
					     cfg_ubx_payload, UBX_MSG_CTRL_SIZE,
					     cmd_dst_buf, dst_max_buf_len);

	return ret_size;
}

/**************************************************************************
 * Use prep_on_off_cmd to prepare the full config message with hdr + Chksum
 * Send the prepared command to the Ublox
 *
 * Called from timer/worker context or when the timer is not active (init time)
 * dev_hndl - The Ubolox device handler
 * msg_type - eUBLOX_MSG_NMEA for NMEA message, eUBLOX_MSG_CFG config message
 * nmea_msg - the nmea message 3 byte tag like ZDA or GSV
 * enable   - 0-disable the message, nonzero-enable the message
 * ret_cls_id - return place holder for class id of this message
 * return   0  Success
 *          nonzero error
 **************************************************************************/
static int ubx_write_cfg_msg_on_off(t_ublox_hndlr dev_hndl,
				    enum eUblox_msg_type msg_type,
				    char *nmea_msg, enum eMsg_enable msg_enable)
{
	int ret_size;
	int ret_stat;
	unsigned char cfg_ubx_msg[48];

	ret_size = prep_on_off_cmd(msg_type, nmea_msg, msg_enable, cfg_ubx_msg,
				   sizeof(cfg_ubx_msg));
	if (ret_size < UBX_MSG_HDR_AND_CHEKSUM_SIZE) {
		return -1;
	}

	ret_stat = ublox_dev_wr(dev_hndl, cfg_ubx_msg, ret_size);
	if (ret_stat) {
		pr_err("Failed ublox_dev_wr\n");
		return -1;
	} else {
		pr_debug("%s message for %s\n",
			 (msg_enable != eMSG_DISABLD) ? "Enabled" : "Disabled",
			 nmea_msg);
	}
	return 0;
}

/****************************************************************************
 * Wrapper function to invoke the I2C specific read
 * dev_hndl     - The Ubolox device handler
 * buf          - place holder for the read data
 * buf_max_size - max read buffer space
 * return - number of bytes received from the Ublox device
 ****************************************************************************/
static int ublox_dev_rd(t_ublox_hndlr dev_hndl, unsigned char *buf,
			int buf_max_size)
{
	return ublox_i2c_receive(dev_hndl, buf, buf_max_size);
}

/****************************************************************************
 * Wrapper function to invoke the I2C specific write
 * dev_hndl  - The Ubolox device handler
 * buf       - place holder for the read data
 * len       - number of bytes to write
 * return    - 0 - writ OK
 *             nonzero write error
 ****************************************************************************/
static int ublox_dev_wr(t_ublox_hndlr dev_hndl, unsigned char *buf, int len)
{
	return ublox_i2c_send(dev_hndl, buf, len);
}

/*************************************************************************
 * ------------ From Sysfs_Start or FW_Start -----
 * Start the timer and workerQ for periodic polling of the Ublox device
 * Enable the ZDA message from the Ublox
 * Initialize  global data g_ub_data, prepared during ublox_dev_load_init()
 * tm_exp_in_ms - timer interval value in ms
 * start_state  -
 *************************************************************************/
static int start_ublox_polling(struct ublox_msg_data *ub_data, int tm_exp_in_ms)
{
	struct t_ublox_stat_data *ub_stat;

	if (g_dbg_mask & DBG_MSG_ENABLE_POLL) {
		printk("In start_ublox_polling\n");
	}
	if (ub_data->init_flag == 0) {
		printk(KERN_WARNING
		       "In start_ublox_polling, NOT Invoked Init\n");
		return -1;
	}

	spin_lock_bh(&ub_data->data_lock);
	if (ub_data->timer_running) {
		spin_unlock_bh(&ub_data->data_lock);
		printk(KERN_WARNING
		       "In start_ublox_polling, Already started\n");
		return -1;
	}

	INIT_WORK(&ub_data->work_queue, tgd_ublox_msg_handler_worker);
	ub_data->timer_shutting_down = 0;
	ub_data->one_sec_count = 0;
	ub_data->accmulated_ms = 0;
	ub_data->tm_exp_in_ms = tm_exp_in_ms;
	ub_data->ublox_state = eGPS_STATE_INIT;
	ub_data->cfg_cmd_ack_rsp_len = 0;
	ub_data->rsp_clsid[0] = 0;
	ub_data->rsp_clsid[1] = 0;
	ub_data->time_sync_mode = eTIME_MODE_UNKNOWN;
	ub_data->dev_busy_time = 0;
	ub_data->srvy_in_start_time = 0;
	ub_data->stat_push_interval = 5; /* in seconds */

	ub_stat = &ub_data->stat_d;
	ub_stat->gnss_fix = eNO_FIX;
	ub_stat->sync_mgr_stat.sync_sgnl_state = eSIGNAL_NOT_DSPLNED;
	ub_stat->meas_valid_falg = 0;

	//------- Start the timer --------------------
	init_timer(&ub_data->gps_timer);
	ub_data->gps_timer.function = (void *)ublox_timer_handler_fn;
	ub_data->gps_timer.data = (unsigned long)ub_data;
	ub_data->gps_timer.expires =
	    msecs_to_jiffies(ub_data->tm_exp_in_ms) + jiffies;
	add_timer(&ub_data->gps_timer);
	ub_data->timer_running = 1;

	spin_unlock_bh(&ub_data->data_lock);

	return 0;
}

/****************************************************************************
 * -------------  Module unload, FW_stop, Sysfs_stop
 * Delete the timer and stop the schedule work in the workQ
 * Invoked as first step to do the Ublox deinit
 ****************************************************************************/
static int stop_ublox_polling(struct ublox_msg_data *ub_data)
{
	int ret;

	if (ub_data->i2c_data == NULL) {
		return -1;
	}
	spin_lock_bh(&ub_data->data_lock);
	if (ub_data->timer_running == 1) {
		ub_data->timer_shutting_down = 1;
		ret = del_timer_sync(&ub_data->gps_timer);
		ub_data->timer_running = 0;
	}
	ub_data->ublox_state = eUBLX_STATE_DOWN;
	spin_unlock_bh(&ub_data->data_lock);

	flush_work(&ub_data->work_queue);
	return 0;
}

/*************************************************************************
 *------------- Module loadtime only
 * Invoked from the tgd_ublox_msg_handler_init() as part of module load
 * initialization. This init gets triggered from the probe function of
 * the Ublox device getting registered with the I2C bus subsystem in Linux
 * Make the data structure ready, timer is not started from here
 *************************************************************************/
static int ublox_dev_load_init(struct ublox_msg_data *ub_data,
			       t_ublox_hndlr dev_hndl)
{
	int ret;

	if (ub_data->init_flag) {
		pr_err("Ublox already initialized only one instance\n");
		return -1;
	}

	if (!dev_hndl) {
		pr_err("NULL Ublox handler in Init\n");
		return -1;
	}
	//-------- Buffer to read raw data from Ublox ----
	ub_data->i2c_data = kmalloc(I2C_MSG_MAX_SIZE, GFP_KERNEL);
	if (ub_data->i2c_data == NULL) {
		pr_err("Ublox Init data mem allocation failed\n");
		return -1;
	}

	//-------- Keep the copy of latest Ublox config response ----
	ub_data->cfg_rsp_copy = kmalloc(CFG_RSP_MAX, GFP_KERNEL);
	if (ub_data->cfg_rsp_copy == NULL) {
		pr_err("Ublox Init cfg_rsp_copy mem alloc failed\n");
		kfree(ub_data->i2c_data);
		ub_data->i2c_data = NULL;
		return -1;
	}

	spin_lock_init(&ub_data->data_lock);
	mutex_init(&ub_data->clnt_lock);
	INIT_LIST_HEAD(&ub_data->clnt_list);

	ub_data->ublox_handle = dev_hndl; // store the hndl for local use
	ub_data->init_flag = 1;
	ub_data->timer_running = 0;
	ub_data->ublx_cfg_rsp_len = 0;

	// Init TX Q for scheduling commands
	ub_data->q_hndlr =
	    init_tgd_message_queue(TXQ_DATA_MAX_LEN, &ub_data->data_lock);

	ret = stop_all_nmea_msgs(ub_data->ublox_handle, DFLT_UPDATE_MS);
	if (ret != 0) {
		pr_err("Ublox stop_all_nmea_msgs failed\n");
	} else {
		ret = tgd_ublox_gps_start_msgs(ub_data);
	}

	// Cleanup on failure
	if (ret != 0)
		tgd_ublox_msg_handler_deinit(ub_data);

	return ret;
}

/**************************************************************************
 *------------- Module loadtime only
 * Gets invoked from the Ublox I2C dev moduleLoad/probe function
 *
 **************************************************************************/
void *tgd_ublox_msg_handler_init(t_ublox_hndlr ublox_dev_handler)
{
	struct ublox_msg_data *ub_data = &g_ub_data;
	int ret;

	ret = ublox_dev_load_init(ub_data, ublox_dev_handler);
	if (ret != 0)
		return NULL;

	ret = ublox_gps_register_device(ub_data);
	if (ret != 0) {
		tgd_ublox_msg_handler_deinit(ub_data);
		return NULL;
	}
	sys_if_add();
	return ub_data;
}

/*************************************************************************
 *------------- Module Unloadtime only
 ** This should be called only after a call to stop_ublox_polling() that
 * take care about cleaning the workQ, deleting the timer etc
 *************************************************************************/
int tgd_ublox_msg_handler_deinit(void *data)
{
	struct ublox_msg_data *ub_data = data;

	if (ub_data->timer_running) {
		pr_debug("ublox_dev_load_deinit: stopping Ublox messages\n");
		tgd_ublox_gps_stop_msgs(ub_data);
	}

	/*
	 * sysfs interface is created only if platform device
	 * creation did succeed, use that to determine if we
	 * should remove it.
	 */
	if (ub_data->platform_dev != NULL)
		sys_if_remove();

	ublox_gps_unregister_device(ub_data);

	ub_data->init_flag = 0;

	kfree(ub_data->cfg_rsp_copy);
	ub_data->cfg_rsp_copy = NULL;

	kfree(ub_data->i2c_data);
	ub_data->i2c_data = NULL;

	tgd_queue_deinit_cleanup(ub_data->q_hndlr);
	return 0;
}

/*************************************************************************
 *-------- FW start or sysfs start
 * Invoked after notification from FW to start the GPS messages to FW or
 * enable through the sysfs
 *************************************************************************/
static int tgd_ublox_gps_start_msgs(struct ublox_msg_data *ub_data)
{

	spin_lock_bh(&ub_data->data_lock);
	if (ub_data->init_flag == 0) {
		pr_err("In start_ublox_polling, not initialized\n");
		spin_unlock_bh(&ub_data->data_lock);
		return -1;
	}
	if (ub_data->timer_running) {
		pr_err("tgd_ublox_gps_start_msgs, already started\n");
		spin_unlock_bh(&ub_data->data_lock);
		return -1;
	}
	ub_data->ublox_sm_state_count = 0;
	spin_unlock_bh(&ub_data->data_lock);

	ublox_msg_rate_set(ub_data->ublox_handle, UBLOX_POLL_1000MS);
#ifdef FORCE_FF_BUG
	start_ublox_polling(ub_data, 210);
#else
	start_ublox_polling(ub_data, UBLOX_POLL_200MS);
#endif
	return 0;
}

/*************************************************************************
 *-------- FW stop or sysfs stop
 * Invoked after notification from FW to stop the GPS messages to FW or
 * sysfs_stop
 *************************************************************************/
static int tgd_ublox_gps_stop_msgs(struct ublox_msg_data *ub_data)
{
	stop_ublox_polling(ub_data);
	return 0;
}

/*****************************************************************************
 *-------- sysfs read
 * API provided to get the current epoch time, return the ascii string value
 * sysfs read and netlink command read use this function to gather the info
 * return 0 on success
 *****************************************************************************/
static int tgd_get_gps_epoch(struct ublox_msg_data *ub_data, char *buf, int len)
{
	struct t_ublox_stat_data *ub_stat;
	int rsp_len;
	int displn_src_i;
	unsigned int l_f;

	if (!buf) {
		return 0;
	}

	ub_stat = &ub_data->stat_d;
	spin_lock_bh(&ub_data->data_lock);
	rsp_len =
	    scnprintf(buf, len, "\nEpochTime: %lu\n", ub_data->gps_time_in_sec);
	l_f = ub_stat->tm_puls_info.flags;

	rsp_len += scnprintf(
	    buf + rsp_len, len - rsp_len, "Date: %d/%d/%d   Time: %d:%d:%d\n\n",
	    ub_stat->tm_puls_info.month, ub_stat->tm_puls_info.day,
	    ub_stat->tm_puls_info.year, ub_stat->tm_puls_info.hour,
	    ub_stat->tm_puls_info.minute, ub_stat->tm_puls_info.seconds);
	if (rsp_len >= len) {
		goto exit_1;
	}

	rsp_len += scnprintf(buf + rsp_len, len - rsp_len,
			     "PulsTolrnc: %s  IntOscTolrnc: %s  GnssTimValid: "
			     "%s  Flags  : 0x%8.8X\n",
			     (l_f & TIM_TOS_PULSE_IN_TOL) ? "YES" : "NO ",
			     (l_f & TIM_TOS_INTOSC_IN_TOL) ? "YES" : "NO ",
			     (l_f & TIM_TOS_GNSS_TM_VALD) ? "YES" : "NO ", l_f);
	if (rsp_len >= len) {
		goto exit_1;
	}

	displn_src_i = (l_f & TIM_TOS_DISP_SOURCE) >> 8;
	if (displn_src_i > 6) {
		displn_src_i = 6;
	}
	rsp_len += scnprintf(
	    buf + rsp_len, len - rsp_len,
	    "UtcTmValid: %s  PulsInCohrnc: %s  PulseLocked : %s  DispSrc: %s\n",
	    (l_f & TIM_TOS_UTC_TM_VALD) ? "YES" : "NO ",
	    (l_f & TIM_TOS_PULS_IN_COH) ? "YES" : "NO ",
	    (l_f & TIM_TOS_PULS_LOCKED) ? "YES" : "NO ",
	    disp_src_name[displn_src_i]);
	if (rsp_len >= len) {
		goto exit_1;
	}

	rsp_len += scnprintf(buf + rsp_len, len - rsp_len,
			     "UTC_Stat  : Offset %8.8d   Uncertainty %d\n",
			     ub_stat->tm_puls_info.utc_tm_ofst,
			     ub_stat->tm_puls_info.utc_uncert);
	if (rsp_len >= len) {
		goto exit_1;
	}

	rsp_len += scnprintf(buf + rsp_len, len - rsp_len,
			     "GNSS_Stat : Offset %8.8d   Uncertainty %d\n",
			     ub_stat->tm_puls_info.gns_top_ofst,
			     ub_stat->tm_puls_info.gnss_uncert);
	if (rsp_len >= len) {
		goto exit_1;
	}
	rsp_len += scnprintf(buf + rsp_len, len - rsp_len,
			     "Int_Osc   : Offset %8.8d   Uncertainty %d\n",
			     ub_stat->tm_puls_info.int_osc_ofst,
			     ub_stat->tm_puls_info.int_osc_uncert);

exit_1:
	spin_unlock_bh(&ub_data->data_lock);
	return rsp_len;
}

/*****************************************************************************
 *-------- sysfs read
 * API provided to get the current latt/long, return the ascii string value
 * sysfs read and netlink command read use this function to gather the info
 * Returned value may be cached during previous invocation of this API
 * return 0 on success
 *****************************************************************************/
static int tgd_get_gps_lat_long(struct ublox_msg_data *ub_data, char *buf,
				int buf_size)
{
	struct t_ublox_stat_data *ub_stat;
	int rsp_len;
	char lat_n_s;
	char long_e_w;

	ub_stat = &ub_data->stat_d;
	spin_lock_bh(&ub_data->data_lock);
	long_e_w = 'E';
	if (ub_stat->pos_time_info.long_value < 0) {
		long_e_w = 'W';
	}
	lat_n_s = 'N';
	if (ub_stat->pos_time_info.lat_value < 0) {
		lat_n_s = 'S';
	}
	rsp_len = scnprintf(buf, buf_size,
			    "\nLat:%d %c   Long:%d %c (1e-7)  "
			    "h_msl: %d (mm) h_ellipsoid: %d (mm)\n",
			    ub_stat->pos_time_info.lat_value, lat_n_s,
			    ub_stat->pos_time_info.long_value, long_e_w,
			    ub_stat->pos_time_info.hgt_sea_lvl,
			    ub_stat->pos_time_info.hgt_ellipsoid);

	rsp_len += scnprintf(buf + rsp_len, buf_size - rsp_len,
			     "NumSatUsedForFix: %2.2d FixType: %s\n",
			     ub_stat->pos_time_info.num_sat_used,
			     nav_fix_type_msg[ub_stat->gnss_fix]);
	spin_unlock_bh(&ub_data->data_lock);

	//---------- Schedule the Navigation Position Velocity Time Solution
	// read
	schedule_config_cmd(ub_data, "NAV_PVT", NULL, 0);

	return rsp_len;
}

/*****************************************************************************
 * *-------- sysfs read for config command response
 *****************************************************************************/
static int tgd_get_ublox_cfg_data(struct ublox_msg_data *ub_data, char *buf,
				  int buf_size)
{
	int rsp_len = 0;
	int i;

	for (i = 0; (i < ub_data->ublx_cfg_rsp_len) && (rsp_len < buf_size);
	     i++) {
		rsp_len += scnprintf(buf + rsp_len, buf_size - rsp_len,
				     "%2.2X ", ub_data->cfg_rsp_copy[i]);
	}
	if (rsp_len < buf_size) {
		*(buf + rsp_len) = '\n';
		rsp_len++;
	}

	for (i = 0; (i < ub_data->cfg_cmd_ack_rsp_len) && (rsp_len < buf_size);
	     i++) {
		rsp_len += scnprintf(buf + rsp_len, buf_size - rsp_len,
				     "%2.2X ", ub_data->cfg_cmd_ack_rsp[i]);
	}
	if (rsp_len < buf_size) {
		*(buf + rsp_len) = '\n';
		rsp_len++;
	}

	return rsp_len;
}

/*****************************************************************************
 *-------- sysfs read
 * API provided to get the current satellite in view, return ascii string value
 * return 0 on success
 *****************************************************************************/
static int tgd_get_gps_sat_in_view(struct ublox_msg_data *ub_data, char *buf,
				   int buf_size)
{
	struct t_ublox_stat_data *ub_stat;
	int gsv_len;
	int i;

	ub_stat = &ub_data->stat_d;
	spin_lock_bh(&ub_data->data_lock);
	gsv_len = scnprintf(buf, buf_size, "\n%d Space Vehicle Info\n\n",
			    ub_stat->num_space_veh);

	for (i = 0; ((i < ub_stat->num_space_veh) && (buf_size > gsv_len));
	     i++) {
		gsv_len += scnprintf(buf + gsv_len, buf_size - gsv_len,
				     "%2.2d) SatId:%3.3d  SNR:%2.2d "
				     "Flag:0x%2.2x Qlty:0x%2.2x Elev:%d\n",
				     i + 1, ub_stat->space_veh_info[i].sat_id,
				     ub_stat->space_veh_info[i].snr,
				     ub_stat->space_veh_info[i].flags,
				     ub_stat->space_veh_info[i].qlty,
				     ub_stat->space_veh_info[i].elev);
	}
	spin_unlock_bh(&ub_data->data_lock);
	if (buf_size <= gsv_len) {
		buf[buf_size - 1] = 0;
	}
	//---------- Schedule the Space Vehicle Information read
	schedule_config_cmd(ub_data, "NAV_SVIN", NULL, 0);

	return gsv_len;
}

/*****************************************************************************
 *-------- sysfs write
 * sysfs/class/fb_tgd_gps/poll_start write handler, only for testing
 * writing '1' enable the gps messge collection
 * All other value disable the gps message collection
 *****************************************************************************/
static ssize_t poll_start_store(struct class *class,
				struct class_attribute *attr, const char *buf,
				size_t count)
{
	struct ublox_msg_data *ub_data = &g_ub_data;

	if (*buf == '1') {
		printk("Starting the gps poll\n");
		tgd_ublox_gps_start_msgs(ub_data);
	} else {
		printk("Stopping the gps poll\n");
		tgd_ublox_gps_stop_msgs(ub_data);
	}
	return count;
}

/*****************************************************************************
 *-------- sysfs write
 * sysfs/class/fb_tgd_gps/time_enable write handler, only for test
 * writing '1' enable the periodic time collection,
 * All other value disable the time collection
 *****************************************************************************/
static ssize_t time_enable_store(struct class *class,
				 struct class_attribute *attr, const char *buf,
				 size_t count)
{
	struct ublox_msg_data *ub_data = &g_ub_data;

	if (*buf == '1') {
		printk("Enabling time from Ublox\n");
		schedule_cfg_msg_on_off(ub_data, eUBLOX_MSG_CFG, "TIM_TOS",
					eMSG_REPT_RD);
	} else {
		printk("Stopping time from Ublox\n");
		schedule_cfg_msg_on_off(ub_data, eUBLOX_MSG_CFG, "TIM_TOS",
					eMSG_DISABLD);
	}
	return count;
}

/*****************************************************************************
 *-------- sysfs write
 * sysfs/class/fb_tgd_gps/ublox_reset write handler
 * writing '1' resets the ublox device
 *****************************************************************************/
static ssize_t ublox_reset_store(struct class *class,
				 struct class_attribute *attr, const char *buf,
				 size_t count)
{
	struct ublox_msg_data *ub_data = &g_ub_data;

	printk("Scheduling Ublox Reset\n");
	schedule_ublox_reset(ub_data, eUBLOX_RSP_DST_NONE);
	printk("Scheduling TIM_TOS Enable Cmd\n");
	stop_all_nmea_msgs(ub_data->ublox_handle, DFLT_UPDATE_MS);
	schedule_cfg_msg_on_off(ub_data, eUBLOX_MSG_CFG, "TIM_TOS",
				eMSG_REPT_RD);
	config_sync_manager(ub_data);
	config_nav_engine(ub_data);
	config_survey_in(ub_data);

	return count;
}

/*****************************************************************************
 *-------- sysfs write
 * sysfs/class/fb_tgd_gps/stat_push_intrvl write handler
 * Accept two digit Hex value from application
 * 00 - Disable the stats push
 *      Minimum time interval restricted to 3 seconds
 *      Maximum time interval 255
 *****************************************************************************/
static ssize_t stat_push_intrvl_store(struct class *class,
				      struct class_attribute *attr,
				      const char *buf, size_t count)
{
	struct ublox_msg_data *ub_data = &g_ub_data;

	unsigned char hex_ar[4];
	int ret_len;
	unsigned int new_stat_push_intrvl;

	ret_len = fb_strto_hex_array(buf, count, hex_ar, 2);
	if (ret_len <= 0) {
		printk(KERN_WARNING "Error: stat_push_intrvl_store %d\n",
		       ret_len);
		return count;
	}
	new_stat_push_intrvl = hex_ar[0] & 0xFF;
	if (new_stat_push_intrvl == 0) { // disable the push
		printk("Disbling the GPS stats push\n");
		ub_data->stat_push_interval = 0;
	} else if (new_stat_push_intrvl < 3) {
		ub_data->stat_push_interval = 3;
	} else {
		ub_data->stat_push_interval = new_stat_push_intrvl;
	}
	printk("Setting StatsPushInterval : %d\n", ub_data->stat_push_interval);
	return count;
}

/*****************************************************************************
 *-------- sysfs write
 *****************************************************************************/
static ssize_t tmr_adj_store(struct class *class, struct class_attribute *attr,
			     const char *buf, size_t count)
{
	struct ublox_msg_data *ub_data = &g_ub_data;
	unsigned char hex_ar[4];
	int ret_len;
	unsigned int adj_timer_value_ms;

	ret_len = fb_strto_hex_array(buf, count, hex_ar, 2);
	if (ret_len <= 0) {
		printk(KERN_WARNING "Error: tmr_adj_store %d\n", ret_len);
		return count;
	}
	adj_timer_value_ms = hex_ar[0] & 0xFF;

	if (adj_timer_value_ms < 200) {
		printk("Adding %d offset to timer\n", adj_timer_value_ms);
		ub_data->adj_timer_value_ms = adj_timer_value_ms;
	} else {
		printk("Invalid TimerDeltaValue: %d\n", adj_timer_value_ms);
	}

	return count;
}

/*****************************************************************************
 *-------- sysfs write
 * sysfs/class/fb_tgd_gps/set_dbglvl write handler
 * Using only the LS 3bits of the first byte, as given below
 *   Bit-0   enable the start/time sync state machine debug messages
 *   Bit-1   enable Ublox message/nmea message read/pars debug information
 *   Bit-3   enable timer and workQ dbg messages
 *****************************************************************************/
static ssize_t dbg_lvl_store(struct class *class, struct class_attribute *attr,
			     const char *buf, size_t count)
{
	struct ublox_msg_data *ub_data = &g_ub_data;
	unsigned char hex_ar[4];
	int ret_len;
	unsigned int new_dbg_lvl;

	ret_len = fb_strto_hex_array(buf, count, hex_ar, 2);
	if (ret_len <= 0) {
		printk(KERN_WARNING "Error: set_dbglvl_store %d\n", ret_len);
		return count;
	}
	new_dbg_lvl = hex_ar[0] & 0xFF;
	if ((g_dbg_mask & DBG_MSG_QUEUE_DESC) ^
	    (new_dbg_lvl & DBG_MSG_QUEUE_DESC)) {
		tgd_queue_set_dbg_lvl(ub_data->q_hndlr,
				      new_dbg_lvl & DBG_MSG_QUEUE_DESC);
	}
	g_dbg_mask = new_dbg_lvl;
	printk("Setting the Debug Levels to 0x%X\n", g_dbg_mask);

	return count;
}
/*****************************************************************************
 *-------- sysfs write
 * sysfs/class/fb_tgd_gps/cmd_cfg write handler, only for test
 * Use the give array as raw config command, add the checksum to this and
 * send it to the Ublox.
 *****************************************************************************/
static ssize_t cmd_cfg_store(struct class *class, struct class_attribute *attr,
			     const char *buf, size_t count)
{
	struct ublox_msg_data *ub_data = &g_ub_data;
	unsigned char hex_ar[256];
	int ret_len;
	unsigned char checksum[2];
	unsigned int priv_data;

	ret_len = fb_strto_hex_array(buf, count, hex_ar, sizeof(hex_ar) - 8);
	if (ret_len < 6) {
		printk(KERN_WARNING "Error: gps_cmd_cfg Rxed Byte %d\n",
		       ret_len);
		return count;
	}
	nmea_checksum(&hex_ar[2], ret_len - 2, checksum);
	hex_ar[ret_len] = checksum[0];
	hex_ar[ret_len + 1] = checksum[1];
	priv_data = PREP_PRIV_DATA(hex_ar[2], hex_ar[3], eUBLOX_RSP_DST_USR);
	tgd_queue_create_new_entry(ub_data->q_hndlr, hex_ar, ret_len + 2,
				   priv_data);
	return count;
}

/*****************************************************************************
 *-------- sysfs read
 * sysfs/class/fb_tgd_gps/epoch_time read handler
 *****************************************************************************/
static ssize_t epoch_time_show(struct class *class,
			       struct class_attribute *attr, char *buf)
{
	return tgd_get_gps_epoch(&g_ub_data, buf, PAGE_SIZE);
}

/*****************************************************************************
 *-------- sysfs read
 * sysfs/class/fb_tgd_gps/lat_long read handler
 *****************************************************************************/
static ssize_t lat_long_show(struct class *class, struct class_attribute *attr,
			     char *buf)
{
	return tgd_get_gps_lat_long(&g_ub_data, buf, PAGE_SIZE);
}

/*****************************************************************************
 *-------- sysfs read
 * sysfs/class/fb_tgd_gps/resp_cfg read handler
 *****************************************************************************/
static ssize_t resp_cfg_show(struct class *class, struct class_attribute *attr,
			     char *buf)
{
	return tgd_get_ublox_cfg_data(&g_ub_data, buf, PAGE_SIZE);
}

/*****************************************************************************
 *-------- sysfs read
 * sysfs/class/fb_tgd_gps/i2c_stat read handler
 *****************************************************************************/
static ssize_t i2c_stat_show(struct class *class, struct class_attribute *attr,
			     char *buf)
{
	struct ublox_msg_data *ub_data = &g_ub_data;
	struct t_ublox_stat_data *ub_stat = &ub_data->stat_d;

	int len;

	len = tgd_get_i2c_stat(ub_data->ublox_handle, buf, PAGE_SIZE);
	if (len >= PAGE_SIZE)
		goto exit_1;

	len += scnprintf(buf + len, PAGE_SIZE - len,
			 "------- Rxed Pkt Stats ----\n");

	len += scnprintf(buf + len, PAGE_SIZE - len, "%-20s: %d\n",
			 "RD_Pkt_count", ub_stat->ublox_stat.rd_pkt_count);

	len +=
	    scnprintf(buf + len, PAGE_SIZE - len, "%-20s: %d\n",
		      "RD_Pkt_Len_Error", ub_stat->ublox_stat.rd_pkt_len_error);

	len +=
	    scnprintf(buf + len, PAGE_SIZE - len, "%-20s: %d\n",
		      "Pkt_Checksum_Error", ub_stat->ublox_stat.cheksum_error);

	len +=
	    scnprintf(buf + len, PAGE_SIZE - len, "%-20s: %d\n",
		      "Pkt_Tim_Tos_Count", ub_stat->ublox_stat.tim_tos_count);

	len += scnprintf(buf + len, PAGE_SIZE - len, "%-20s: %d\n",
			 "Pkt_Tim_Tos_Error",
			 ub_stat->ublox_stat.tim_tos_pkt_error);

	len += scnprintf(buf + len, PAGE_SIZE - len, "%-20s: %d\n",
			 "Pkt_Tim_Tos_To_Fw",
			 ub_stat->ublox_stat.tim_tos_to_fw_count);

	len += scnprintf(buf + len, PAGE_SIZE - len, "%-20s: %d\n",
			 "Invalid_hdr_char",
			 ub_stat->ublox_stat.skip_invalid_msg_hdr);

	len +=
	    scnprintf(buf + len, PAGE_SIZE - len, "%-20s: %d\n", "TimeDispGnss",
		      ub_stat->ublox_stat.gnss_fix_time_count);

	len += scnprintf(buf + len, PAGE_SIZE - len, "%-20s: %d\n",
			 "TimeDispInternal",
			 ub_stat->ublox_stat.int_osc_fix_time_count);

exit_1:
	return len;
}

/*****************************************************************************
 *-------- sysfs read
 * sysfs/class/fb_tgd_gps/sat_in_view read handler
 * Retrurn the raw NMEA message
 *****************************************************************************/
static ssize_t sat_in_view_show(struct class *class,
				struct class_attribute *attr, char *buf)
{
	return tgd_get_gps_sat_in_view(&g_ub_data, buf, PAGE_SIZE);
}

/*****************************************************************************
 *-------- sysfs read
 * API provided to get the survey in results, return ascii string value
 * return 0 on success
 *****************************************************************************/
static int tgd_get_gps_survey_results(struct ublox_msg_data *ub_data, char *buf,
				      int buf_size)
{
	struct t_ublox_stat_data *ub_stat = &ub_data->stat_d;
	int gsv_len;

	spin_lock_bh(&ub_data->data_lock);
	gsv_len = scnprintf(buf, buf_size, "\n Survey In Results\n\n");
	gsv_len +=
	    scnprintf(buf + gsv_len, buf_size - gsv_len,
		      "StartTime  : %d  CurrTime   : %d\n",
		      ub_data->srvy_in_start_time, ub_data->one_sec_count);

	gsv_len += scnprintf(
	    buf + gsv_len, buf_size - gsv_len,
	    "Mean_ECEF_X: %d  Mean_ECEF_Y: %d  Mean_ECEF_Z: %d\n",
	    ub_stat->srvy_in_stat.mean_x, ub_stat->srvy_in_stat.mean_y,
	    ub_stat->srvy_in_stat.mean_z);

	gsv_len += scnprintf(buf + gsv_len, buf_size - gsv_len,
			     "Variance_3D: %d  PosObserved: %d\n",
			     ub_stat->srvy_in_stat.variance_3d,
			     ub_stat->srvy_in_stat.num_pos_observed);

	gsv_len += scnprintf(buf + gsv_len, buf_size - gsv_len,
			     "InProgress : %s  SurveyValid: %s\n",
			     ub_stat->srvy_in_stat.in_progress ? "YES" : "NO ",
			     ub_stat->srvy_in_stat.valid_flag ? "YES" : "NO ");
	spin_unlock_bh(&ub_data->data_lock);
	if (buf_size <= gsv_len) {
		buf[buf_size - 1] = 0;
	}

	return gsv_len;
}

/*****************************************************************************
 *-------- sysfs read
 * sysfs/class/fb_tgd_gps/survey_result read handler
 *****************************************************************************/
static ssize_t survey_result_show(struct class *class,
				  struct class_attribute *attr, char *buf)
{
	return tgd_get_gps_survey_results(&g_ub_data, buf, PAGE_SIZE);
}

//-------------- Sysfs attributes ----------------------------
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
static struct class_attribute gps_class_attrs[] = {
    __ATTR(sat_in_view, S_IRUGO, sat_in_view_show, NULL),
    __ATTR(lat_long, S_IRUGO, lat_long_show, NULL),
    __ATTR(epoch_time, S_IRUGO, epoch_time_show, NULL),
    __ATTR(resp_cfg, S_IRUGO, resp_cfg_show, NULL),
    __ATTR(i2c_stat, S_IRUGO, i2c_stat_show, NULL),
    __ATTR(poll_start, S_IWUSR, NULL, poll_start_store),
    __ATTR(dbg_lvl, S_IWUSR, NULL, dbg_lvl_store),
    __ATTR(tmr_adj, S_IWUSR, NULL, tmr_adj_store),
    __ATTR(cmd_cfg, S_IWUSR, NULL, cmd_cfg_store),
    __ATTR(time_enable, S_IWUSR, NULL, time_enable_store),
    __ATTR(ublox_reset, S_IWUSR, NULL, ublox_reset_store),
    __ATTR(stat_push_intrvl, S_IWUSR, NULL, stat_push_intrvl_store),
    __ATTR(survey_result, S_IRUGO, survey_result_show, NULL),
    __ATTR_NULL};
#else
CLASS_ATTR_RO(sat_in_view);
CLASS_ATTR_RO(lat_long);
CLASS_ATTR_RO(epoch_time);
CLASS_ATTR_RO(resp_cfg);
CLASS_ATTR_RO(i2c_stat);
CLASS_ATTR_WO(poll_start);
CLASS_ATTR_WO(dbg_lvl);
CLASS_ATTR_WO(tmr_adj);
CLASS_ATTR_WO(cmd_cfg);
CLASS_ATTR_WO(time_enable);
CLASS_ATTR_WO(ublox_reset);
CLASS_ATTR_WO(stat_push_intrvl);
CLASS_ATTR_RO(survey_result);

static struct attribute *gps_class_attrs[] = {
    &class_attr_sat_in_view.attr,   &class_attr_lat_long.attr,
    &class_attr_epoch_time.attr,    &class_attr_resp_cfg.attr,
    &class_attr_i2c_stat.attr,      &class_attr_poll_start.attr,
    &class_attr_dbg_lvl.attr,       &class_attr_tmr_adj.attr,
    &class_attr_cmd_cfg.attr,       &class_attr_time_enable.attr,
    &class_attr_ublox_reset.attr,   &class_attr_stat_push_intrvl.attr,
    &class_attr_survey_result.attr, NULL,
};
ATTRIBUTE_GROUPS(gps_class);
#endif

/************************************************************************/
struct class gps_class = {
    .name = "fb_tgd_gps",
    .owner = THIS_MODULE,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
    .class_groups = gps_class_groups,
#else
    .class_attrs = gps_class_attrs,
#endif
};

/*****************************************************************************
 *-------- Module loadtime
 * Add all sysfs/class/fb_tgd_gps entries
 *****************************************************************************/
static int sys_if_add(void)
{
	return class_register(&gps_class);
}

/*****************************************************************************
 *-------- Module Unloadtime
 * Remove all all sysfs/class/fb_tgd_gps entries
 *****************************************************************************/
static void sys_if_remove(void)
{
	class_unregister(&gps_class);
}

/************************************************************************
 * Convert the given ascii array to hex array
 * Used to convert the array received through sysfs write to corresponding
 * hex array value.
 * return number of bytes in the hex array
 ************************************************************************/
static int fb_strto_hex_array(const char *asc_data, int asc_data_len,
			      unsigned char *hex_ar, int hex_ar_len)
{
	int index;
	int dst_index;
	unsigned int long_val;
	char *endptr;
	char *workptr;

	index = -1;
	dst_index = 0;
	workptr = (char *)asc_data;
	while (index < asc_data_len) {
		index++;
		if (*(workptr) == ' ') { // simple_strtol not skipping the
					 // spaces
			workptr++;
			continue;
		}
		long_val = simple_strtol(workptr, &endptr, 16);
		if ((endptr == workptr) ||
		    (endptr >= (asc_data + asc_data_len))) {
			break;
		}
		hex_ar[dst_index] = long_val & 0xFF;
		dst_index++;
		workptr = endptr;
	}
	return dst_index;
}

/*****************************************************************************
* Configuration done through UBX-CFG-TMODE2 (0x06 0x3D) Time Mode Settings 2
* B5 62 06 3D 1C 00
*6)  00          //timeMode, 0 Disabled; 1 Survey In; 2 Fixed Mode
*7)  00          //reserved1
*8)  00 00       //Time mode flags
*                //D0=1 Position is given in LAT/LON/ALT (default is ECEF)
*                //D1=1 Altitude is not valid, in case LAT/LON/ALT
*10) 00 00 00 00 //ECEF-X (cm) or latitude (1e-7)
*14) 00 00 00 00 //ECEF-Y (cm) or longitude (1e-7)
*18) 00 00 00 00 //ECEF-Z (cm) or altitude, (cm)
*22) 00 00 00 00 //Fixed position 3D accuracy (mm)
*26) 00 00 00 00 //Survey-in minimum duration (s)
*30) 00 00 00 00 //Survey-in position accuracy limit (mm)
*34) 5F 6B
------------------|-----|Flg|-| Latt   |--| Long   |--| Alt    |
B5 62 06 3D 1C 00 02 00 01 00 EF B6 57 16 96 CF 31 B7 00 07 00 00 00
		     00 00 00 00 00 00 00 00 00 00 00 //Fixed mode, LAT/LON/ALT
* cmd_ptr - given from the application layer through netlink interface
*           First 4 bytes (header used by fb_tgd_nlsdn.c) 0x03, 0x00, 0x04 0x00
*           B04-B07 Latitude  (1e-7)
*           B08-B11 Longitude (1e-7)
*           B12-B15 Altitude  (cm)
*
* cmd_ptr - 12 byte info with longitude(1e-7) + latitude(1e-7) + altitude(cm)
*           Example:FB lab with 37.4847215N 122.1472362 W  17.92 Meter
*                   D0-D3  96CF31B7(Le)->0xB731CF96 = -0x48CE306A = 122.1472362
*                   D4-D7  EFB65716(Le)->0X1657B6EF =                37.4847215
*                   D8-D11 00070000(Le)->0x00000700 = 1792cm         17.92
*****************************************************************************/
static int config_single_satellite(struct ublox_msg_data *ub_data,
				   unsigned char *cmd_ptr, int cmd_len,
				   unsigned char *rsp_buf, int rsp_buf_len)
{

	int i;
	unsigned int priv_data;
	unsigned char checksum[2];
	unsigned char hex_ar[40] = {
	    // Hardcoded UBX-CFG-TMODE2
	    // fixedMode/lat/long/pos
	    UBX_CHAR_SYNC0, UBX_CHAR_SYNC1, 0x06, 0x3D, 0x1C,
	    0x00,	   0x02,	   0x00, 0x01, 0x00,
	};
	const struct t_gps_self_pos *pos = (struct t_gps_self_pos *)cmd_ptr;

	if (cmd_len < 16) {
		printk(KERN_WARNING "Error: UBX-CFG-TMODE2 Len:%d\n", cmd_len);
		return -1;
	}

	printk("single satellite, latitude: %d, longitude: %d, altitude: %d, "
	       "accuracy: %d\n",
	       pos->latitude, pos->longitude, pos->height, pos->accuracy);

	*((int *)&hex_ar[LAT_OFFSET]) = HOST_TO_LE_UINT(&pos->latitude);
	*((int *)&hex_ar[LONG_OFFSET]) = HOST_TO_LE_UINT(&pos->longitude);
	*((int *)&hex_ar[ALTI_OFFSET]) = HOST_TO_LE_UINT(&pos->height);
	*((int *)&hex_ar[POS_ACC_OFFSET]) = HOST_TO_LE_UINT(&pos->accuracy);
	ub_data->time_sync_mode = eTIME_MODE_FIXED;
	for (i = POS_ACC_OFFSET + 4; i < sizeof(hex_ar);
	     i++) { // clear the remaining
		hex_ar[i] = 0;
	}

	nmea_checksum(&hex_ar[2], CFG_TMODE2_CRC_CAL_LEN, checksum);
	hex_ar[CFG_TMODE2_CMD_LEN + MSG_HDR_SIZE] = checksum[0];
	hex_ar[CFG_TMODE2_CMD_LEN + MSG_HDR_SIZE + 1] = checksum[1];
	priv_data = PREP_PRIV_DATA(hex_ar[2], hex_ar[3], eUBLOX_RSP_DST_USR);
	if (!tgd_queue_create_new_entry(ub_data->q_hndlr, hex_ar,
					CFG_TMODE2_CMD_LEN + UBX_MSG_CTRL_SIZE,
					priv_data)) {
	}
	if (g_dbg_mask & DBG_MSG_CFG_RSP_RAW) {
		printk("config_single_satellite cmd string\n");
		ublox_hex_dump(hex_ar, CFG_TMODE2_CMD_LEN + UBX_MSG_CTRL_SIZE);
	}

	return 0;
}
/*****************************************************************************
******************************************************************************/
static int config_survey_in(struct ublox_msg_data *ub_data)
{
	// Keep ublox in survey-in mode as much as possible
	// (Unless explicitly asked for single satellite mode)
	// minimum duration = 24 hours = 24 * 60 * 60 seconds
	// accuracy limit = 5 meters = 5000 mm
	const unsigned int srvy_min_dur = 24 * 60 * 60;
	const unsigned int srvy_pos_acc = 5 * 1000;
	int i;
	unsigned int priv_data;
	unsigned char checksum[2];
	unsigned char hex_ar[40] = {
	    // Hardcoded UBX-CFG-TMODE2, survey in
	    UBX_CHAR_SYNC0, UBX_CHAR_SYNC1, 0x06, 0x3D, 0x1C, 0x00, 0x01,
	};

	for (i = TMODE_OFFSET + 1; i < sizeof(hex_ar); i++) {
		hex_ar[i] = 0;
	}

	*((int *)&hex_ar[SURVY_MIN_DUR_OFFSET]) =
	    HOST_TO_LE_UINT(&srvy_min_dur);
	*((int *)&hex_ar[SURVY_ACCURCY_OFFSET]) =
	    HOST_TO_LE_UINT(&srvy_pos_acc);

	nmea_checksum(&hex_ar[2], CFG_TMODE2_CRC_CAL_LEN, checksum);
	hex_ar[CFG_TMODE2_CMD_LEN + MSG_HDR_SIZE] = checksum[0];
	hex_ar[CFG_TMODE2_CMD_LEN + MSG_HDR_SIZE + 1] = checksum[1];
	priv_data = PREP_PRIV_DATA(hex_ar[2], hex_ar[3], eUBLOX_RSP_DST_USR);
	if (tgd_queue_create_new_entry(ub_data->q_hndlr, hex_ar,
				       CFG_TMODE2_CMD_LEN + UBX_MSG_CTRL_SIZE,
				       priv_data)) {
		printk("Error: config_survey_in command add to queue failed\n");
		return -1;
	}

	ub_data->srvy_in_start_time = ub_data->one_sec_count;
	ub_data->time_sync_mode = eTIME_MODE_SURVEY;

	if (g_dbg_mask & DBG_MSG_CFG_RSP_RAW) {
		printk("Config_survey_in cmd string\n");
		ublox_hex_dump(hex_ar, CFG_TMODE2_CMD_LEN + UBX_MSG_CTRL_SIZE);
	}

	return 0;
}

/*****************************************************************************
*****************************************************************************/
static int is_ublox_device_busy(struct ublox_msg_data *ub_data)
{
	struct t_ublx_msg_desc *msg_desc;

	if (ub_data->dev_busy_time == 0 ||
	    ub_data->dev_busy_time++ < I2C_RSP_MAX_WAIT)
		return ub_data->dev_busy_time;

	msg_desc = lookup_cfg_msg(ub_data->rsp_clsid[0], ub_data->rsp_clsid[1]);
	if (msg_desc == NULL)
		pr_err("Unknown cfg command %02x, %02x timed out\n",
		       ub_data->rsp_clsid[0], ub_data->rsp_clsid[1]);
	else
		pr_err("Cfg command %s timed out\n", msg_desc->name);
	return 0;
}

/*****************************************************************************
*****************************************************************************/
static void set_ublox_device_busy(struct ublox_msg_data *ub_data)
{
	ub_data->dev_busy_time = 1;
}
/*****************************************************************************
*****************************************************************************/
static void clear_ublox_device_busy(struct ublox_msg_data *ub_data)
{
	ub_data->dev_busy_time = 0;
}

/*=============================================================================
==============================================================================*/
/**************************************************************************
* UBX-MON-SMGR (0x0A 0x2E) response handler, update the global data structure
*
* B5 62 0A 2E 10 00
06) 00 00 00 00
10) E8 E9 F6 1D     //Time of the week
14) 70 00           //A bit mask, indicating the status of the local oscillator
		    // D0-D3 State of the oscillator: 0: autonomous operation
		    //   1: calibration ongoing, 2: osc is steered by host
		    //   3: idle state
		    //D4-1 = oscillator gain is calibrated
		    //D5-1 = signal is disciplined (????????? Looks D6 toggled)
16) 03 00           //A bit mask, fo status of the external oscillator
		    // Format same as local oscillator
18) 01              //Disciplining source identifier: 0: internal oscillator
		    // 1: GNSS  2: EXTINT0  3: EXTINT1
		    // 4: internal oscillator measured by the host
		    // 5: external oscillator measured by the host
19) 01              //A bit mask, indicating the status of the GNSS
		    // D0-1 = GNSS is present
**************************************************************************/
static int mon_smgr_msg_handler(struct ublox_msg_data *ub_data,
				const char *cfg_data_ptr, int data_len)
{
	struct t_ublox_stat_data *ub_stat = &ub_data->stat_d;
	unsigned char temp_ch;

	if (g_dbg_mask & DBG_MSG_CFG_STAT_RD) {
		printk("In mon_smgr_msg_handler Len:%d\n", data_len);
	}
	if (data_len < 20) {
		printk(KERN_WARNING "mon_smgr_msg_handler InvalidLen:%d\n",
		       data_len);
		return -1;
	}
	if (g_dbg_mask & DBG_MSG_CFG_RSP_RAW) {
		ublox_hex_dump(cfg_data_ptr, data_len);
	}

	spin_lock_bh(&ub_data->data_lock);
	if (cfg_data_ptr[14] & 0x40) { //----- Local Osc status --------
		ub_stat->sync_mgr_stat.sync_sgnl_state = eSIGNAL_DSPLNED;
	} else {
		ub_stat->sync_mgr_stat.sync_sgnl_state = eSIGNAL_NOT_DSPLNED;
	}
	temp_ch = cfg_data_ptr[18] & 0xFF; //---------- Disciplining source
	if (temp_ch >= eINTOSC_DSCP_SRC_INVALID) {
		ub_stat->sync_mgr_stat.int_osc_dsp_src =
		    eINTOSC_DSCP_SRC_INVALID;
	} else {
		ub_stat->sync_mgr_stat.int_osc_dsp_src = temp_ch;
	}
	ub_stat->sync_mgr_stat.gnss_present =
	    cfg_data_ptr[19] & 0x01; // GNSS state
	spin_unlock_bh(&ub_data->data_lock);

	if (g_dbg_mask & DBG_MSG_CFG_RSP_PARSED) {
		if (ub_stat->sync_mgr_stat.sync_sgnl_state == eSIGNAL_DSPLNED) {
			printk("====Signal Disciplined====\n");
		} else {
			printk("====Signal NOT Disciplined=====\n");
		}
	}
	return 0;
}

/*****************************************************************************
 * response handler for UBX-NAV-PVT (0x01 0x07) Navigation Position Velocity
 * Time Solution
B5 62 01 07 5C 00
6)  C8 82 0F 1E    //GPS time of week of the navigation epoch (ms)
10) E0 07          //Year
12) 09             //Month
13) 02             //Day
14) 14             //Hour
15) 05             //minute
16) 10             //Second
17) 37             //Valid flag
18) 0B 00 00 00    //Time accuracy estimate (UTC)
22) 99 BF 06 00    //Fraction of second, range -1e9 .. 1e9 (UTC)
26) 03             //(offset 20)GNSSfix Type: 0: no fix 1: dead reckoning only
		   //2: 2D-fix 3: 3D-fix
		   //4: GNSS + dead reckoning combined 5: time only fix
		   //   (>>>>>>>>>>>look for 3 for 3D fix )
27) 01             //Fix status flags
28) 0A             //Additional flags
29) 08             //Number of satellites used in Nav Solution
30) 96 CF 31 B7    //Longitude (1e-7) 0xB731_CF96  -48CE306A = 122.1472362
34) EF B6 57 16    //Latitude  (1e-7) 0X1657_B6EF   37.4847215
38) 1F D1 FF FF    //Height above ellipsoid (mm)
42) 00 46 00 00    //Height above mean sea level  (mm)
46) A3 0C 00 00    //Horizontal accuracy estimate  (mm)
50) FE 14 00 00    //Vertical accuracy estimate (mm)
54) 10 00 00 00    //NED north velocity
58) DD FF FF FF    //NED east velocity
62) B1 FF FF FF    //NED down velocity
66) 27 00 00 00    //Ground Speed (2-D)
70) 00 00 00 00    //Heading of motion (2-D)
74) 5C 00 00 00    //Speed accuracy estimate
78) 8A 31 E5 00    //Heading accuracy estimate
82) D2 00          //Position DOP
84) 00 E0 12 7B 22 00   //Reserved
90)25 08 84 00         //Heading of vehicle
94)52 08 84 00         //Reserved
Lat: 37.48478 N    Long: 122.14719 W

Lab value from NMEA Lat: 37.48474 N    Long: 122.14719 W
  from Hexdump      4d af ed ef     53 36 6d e6     53 36 02 17
  HexValue          0xEFEDAF4D      0xE66D3653     0x17023653
ECEF Coordinate X: -2696.35763  Y: -4290.50285  Z: 3860.20947
    Latitude  : 37.48474   deg N
    Longitude : 237.85278  deg E  (360-237.85278) ->122.1472
    Height    : -17.9   m
**************************************************************************/
static int nav_pvt_msg_handler(struct ublox_msg_data *ub_data,
			       const char *cfg_data_ptr, int data_len)
{
	struct t_ublox_stat_data *ub_stat = &ub_data->stat_d;
	int msg_index;

	if (data_len < 48) {
		printk(KERN_WARNING "Invalid Len: %d for UBX-NAV-PVT\n",
		       data_len);
		return -1;
	}
	if (g_dbg_mask & DBG_MSG_CFG_STAT_RD) {
		printk("In nav_pvt_msg_handler Len:%d\n", data_len);
	}

	if (g_dbg_mask & DBG_MSG_CFG_RSP_RAW) {
		ublox_hex_dump(cfg_data_ptr, data_len);
	}
	msg_index = 6;
	if (cfg_data_ptr[26] <= 5) {
		msg_index = cfg_data_ptr[26] & 0xFF;
	}
	spin_lock_bh(&ub_data->data_lock);
	ub_stat->gnss_fix = msg_index; // Enum values match with UbloxCfg

	ub_stat->pos_time_info.tow_ms = LE_HOST_UINT(cfg_data_ptr + 6);
	ub_stat->pos_time_info.year = LE_HOST_SHORT(cfg_data_ptr + 10) & 0xFFFF;

	ub_stat->pos_time_info.month = cfg_data_ptr[12] & 0xFF;
	ub_stat->pos_time_info.day = cfg_data_ptr[13] & 0xFF;
	ub_stat->pos_time_info.hour = cfg_data_ptr[14] & 0xFF;
	ub_stat->pos_time_info.minute = cfg_data_ptr[15] & 0xFF;
	ub_stat->pos_time_info.second = cfg_data_ptr[16] & 0xFF;
	ub_stat->pos_time_info.f_second_ns = LE_HOST_UINT(cfg_data_ptr + 22);
	ub_stat->pos_time_info.valid_flag = cfg_data_ptr[17] & 0xFF;

	ub_stat->pos_time_info.fix_type = cfg_data_ptr[26] & 0xFF;
	ub_stat->pos_time_info.fix_status = cfg_data_ptr[27] & 0xFF;
	ub_stat->pos_time_info.num_sat_used = cfg_data_ptr[29] & 0xFF;

	ub_stat->pos_time_info.long_value = LE_HOST_INT(cfg_data_ptr + 30);
	ub_stat->pos_time_info.lat_value = LE_HOST_INT(cfg_data_ptr + 34);
	ub_stat->pos_time_info.hgt_ellipsoid = LE_HOST_UINT(cfg_data_ptr + 38);
	ub_stat->pos_time_info.hgt_sea_lvl = LE_HOST_UINT(cfg_data_ptr + 42);

	spin_unlock_bh(&ub_data->data_lock);

	if (g_dbg_mask & DBG_MSG_CFG_RSP_PARSED) {
		printk("====NAV FixType:(%d) %s ====\n", msg_index,
		       nav_fix_type_msg[msg_index]);
		printk("Lat: %d (0x%x)  Long:%d (0x%x)\n",
		       ub_stat->pos_time_info.lat_value,
		       ub_stat->pos_time_info.lat_value,
		       ub_stat->pos_time_info.long_value,
		       ub_stat->pos_time_info.long_value);
		printk("Alt:%d (0x%x)  MeanSeaLvl:%d (0x%x)\n",
		       ub_stat->pos_time_info.hgt_ellipsoid,
		       ub_stat->pos_time_info.hgt_ellipsoid,
		       ub_stat->pos_time_info.hgt_sea_lvl,
		       ub_stat->pos_time_info.hgt_sea_lvl);
	}
	return 0;
}

/*****************************************************************************
UBX-NAV-SVINFO (0x01 0x30) Space Vehicle Information
B5 62 01 30 E0 00
6)       58 F1 4C 1E //GPS time of week of the navigation epoch
10)      12             //Number of channels
	 04             //globalFlags; 4: u-blox 8/u-blox M8
	 00 00
14+12*0) 04             //Channel number,
15+12*0) 02             //Satellite ID
16+12*0) 0D             //flags
17+12*0) 04             //quality
18+12*0) 1A     (dBHz)  //Carrier to Noise Ratio (Signal Strength)
19+12*0) 17     (deg)   //Elevation in integer degrees
20+12*0) 3A 00          //Azimuth in integer degrees
22+12*0) 4F FB FF FF    //Pseudo range residual in centimeters
22+12*1) ..........
*****************************************************************************/
static int nav_svinf_msg_handler(struct ublox_msg_data *ub_data,
				 const char *msg_ptr, int len)
{
	struct t_ublox_stat_data *ub_stat = &ub_data->stat_d;
	int num_channels;
	int i;

	if (g_dbg_mask & DBG_MSG_CFG_RSP_RAW) {
		ublox_hex_dump(msg_ptr, len);
	}
	if (len < 14) {
		printk(KERN_WARNING "Error UBX-NAV-SVINFO Len :%d\n", len);
		return -1;
	}
	num_channels = msg_ptr[10] & 0xFF;
	if (len < (num_channels * 12 + 14)) {
		printk(KERN_WARNING
		       "Error UBX-NAV-SVINFO Len :%d for Chnl: %d\n",
		       len, num_channels);
		return -1;
	}
	if (num_channels > MAX_NUM_SV) {
		num_channels = MAX_NUM_SV;
	}
	spin_lock_bh(&ub_data->data_lock);
	for (i = 0; i < num_channels; i++) {
		// ub_stat->space_veh_info[i].chnl_num  =
		// msg_ptr[i*12+14]&0xFF;
		ub_stat->space_veh_info[i].sat_id = msg_ptr[i * 12 + 15] & 0xFF;
		ub_stat->space_veh_info[i].flags = msg_ptr[i * 12 + 16] & 0xFF;
		ub_stat->space_veh_info[i].qlty = msg_ptr[i * 12 + 17] & 0xFF;
		ub_stat->space_veh_info[i].snr = msg_ptr[i * 12 + 18] & 0xFF;
		ub_stat->space_veh_info[i].elev = msg_ptr[i * 12 + 19] & 0xFF;
	}
	ub_stat->num_space_veh = num_channels;
	spin_unlock_bh(&ub_data->data_lock);

	if (g_dbg_mask & DBG_MSG_CFG_RSP_PARSED) {
		printk("UBX-NAV-SVINFO NumOfChnl: %d\n", num_channels);
		for (i = 0; i < num_channels; i++) {
			printk("%2.2d) SatId:%3.3d  SNR:%2.2d "
			       "Flag:0x%2.2x Quality:0x%2.2x Elev:%d\n",
			       i + 1, ub_stat->space_veh_info[i].sat_id,
			       ub_stat->space_veh_info[i].snr,
			       ub_stat->space_veh_info[i].flags,
			       ub_stat->space_veh_info[i].qlty,
			       ub_stat->space_veh_info[i].elev);
		}
	}
	return 0;
}

/*****************************************************************************
* UBX-TIM-TOS (0x0D 0x12) Time Pulse Time and Frequency Data
B5 62 0D 12 38 00
    00              //version
    00              //Gnssid 00-GPS
    06 00
10) D8 39 00 00     //Flags 0x39D8 - D0=0; currently in a leap second,
		    //D1=0 leap second scheduled in current minute
		    //D2=0 positive leap second,
		    //D3=1 time pulse is within tolerance limit
		    //D4=1 = internal oscillator is within tolerance limit
		    //D5=0 = external oscillator is within tolerance limit
		    //D6=1; GNSS time is valid
		    //D7=1; UTC time is valid
		    //D10-D8=001 Disciplining source identifier,
		    //      1:GNSS;     0:internal oscillator
		    //D11=1 (T)RAIM system is currently active
		    //D12=1 coherent pulse generation is currently in operation
		    //D13=1 time pulse is locked
14) E0 07           //Year of UTC 0x7E0 = 2016
16) 08              //Month of UTC time
    18              //Day of UTC time 24
18) 12              //Hour of UTC time
    28              //Minute of UTC
20) 32              //Second of UTC time
    03              //UTC standard identifier; 03 UTC as operated by U.S. Naval
22) 02 00 00 00     //Time offset between preceding pulse & UTC topOfSecond(ns)
26) 05 00 00 00     //Uncertainty of utcOffset (ns)
30) 77 07 00 00     //GNSS week number, 0x0777=1911 (from Jan6 1980) =13377days
		    //(includes 9 leapyear) 13377-9->13368-> 36 years, 228 days
34) 43 FB 04 00     //GNSS time of week
38) 03 00 00 00     //Time offset between preceding pulse & GNSStopOfSecond(ns)
42) 05 00 00 00     //Uncertainty of gnssOffset (ns)
46) 09 00 00 00     //Internal oscillator frequency offset ppb
50) 5E 00 00 00     //Internal oscillator frequency uncertainty
54) 00 00 00 00     //External oscillator frequency offset
58) 00 00 00 00     //External oscillator frequency uncertainty
    1A 67
*****************************************************************************/
static int time_tos_handler(struct ublox_msg_data *ub_data, const char *msg_ptr,
			    int len)
{
	struct t_ublox_stat_data *ub_stat = &ub_data->stat_d;
	int disp_name_i;
	struct timespec read_time;
	const int maxTimeErr = 500;      // ns
	const int maxFreqErr = 500 << 8; // 2^-8 ppb

	if (g_dbg_mask & DBG_MSG_CFG_RSP_RAW) {
		ublox_hex_dump(msg_ptr, len);
	}
	if (len < 60) {
		printk(KERN_WARNING "Error UBX-TIM-TOS Len :%d\n", len);
		ub_stat->ublox_stat.tim_tos_pkt_error++;
		return -1;
	}

	spin_lock_bh(&ub_data->data_lock);
	ub_stat->tm_puls_info.flags = LE_HOST_INT(msg_ptr + 10);
	ub_stat->tm_puls_info.year = LE_HOST_SHORT(msg_ptr + 14) & 0xFFFF;
	ub_stat->tm_puls_info.month = msg_ptr[16] & 0xFF;
	ub_stat->tm_puls_info.day = msg_ptr[17] & 0xFF;
	ub_stat->tm_puls_info.hour = msg_ptr[18] & 0xFF;
	ub_stat->tm_puls_info.minute = msg_ptr[19] & 0xFF;
	ub_stat->tm_puls_info.seconds = msg_ptr[20] & 0xFF;

	ub_stat->tm_puls_info.utc_tm_ofst = LE_HOST_INT(msg_ptr + 22);
	ub_stat->tm_puls_info.utc_uncert = LE_HOST_UINT(msg_ptr + 26);

	ub_stat->tm_puls_info.gnss_week_num = LE_HOST_UINT(msg_ptr + 30);
	ub_stat->tm_puls_info.gnss_week_time = LE_HOST_UINT(msg_ptr + 34);

	ub_stat->tm_puls_info.gns_top_ofst = LE_HOST_INT(msg_ptr + 38);
	ub_stat->tm_puls_info.gnss_uncert = LE_HOST_UINT(msg_ptr + 42);
	ub_stat->tm_puls_info.int_osc_ofst = LE_HOST_INT(msg_ptr + 46);
	ub_stat->tm_puls_info.int_osc_uncert = LE_HOST_UINT(msg_ptr + 50);
	spin_unlock_bh(&ub_data->data_lock);

	ub_data->gps_time_sec =
	    (ub_stat->tm_puls_info.gnss_week_num * NUM_SEC_IN_WEEK) +
	    ub_stat->tm_puls_info.gnss_week_time;
	if (g_dbg_mask & DBG_MSG_ENABLE_SYNC) {
		// Leap_second = ub_data->gps_time_sec-(epoch_time-315964800)
		printk("GPS_Sec:%lu  prev_tm_sec:%lu\n", ub_data->gps_time_sec,
		       ub_data->prev_time_sec);
	}

	if ((ub_data->prev_time_sec != ub_data->gps_time_sec) ||
	    (ub_data->gps_time_sec == 0)) {
		ub_data->prev_time_sec = ub_data->gps_time_sec;
		if (g_dbg_mask & DBG_MSG_ENABLE_SYNC) {
			printk("GPS Time -> FW\n");
		}
		ub_data->gps_time_in_sec = ub_data->gps_time_sec;
		read_time.tv_sec = ub_data->gps_time_sec;
		read_time.tv_nsec = 0;

		if (!list_empty(&ub_data->clnt_list)) {
			if (abs(ub_stat->tm_puls_info.gns_top_ofst) <
				maxTimeErr &&
			    ub_stat->tm_puls_info.gnss_uncert < maxTimeErr &&
			    abs(ub_stat->tm_puls_info.int_osc_ofst) <
				maxFreqErr &&
			    ub_stat->tm_puls_info.int_osc_uncert < maxFreqErr) {
				ublox_gps_update_time(ub_data, &read_time);
				ub_stat->ublox_stat.tim_tos_to_fw_count++;
			} else {
				printk(KERN_DEBUG
				       "ERROR, ublox timing is not accurate, "
				       "gns_top_ofst, %d, gnss_uncert, %d, "
				       "int_osc_ofst, %d, int_osc_uncert, %d\n",
				       ub_stat->tm_puls_info.gns_top_ofst,
				       ub_stat->tm_puls_info.gnss_uncert,
				       ub_stat->tm_puls_info.int_osc_ofst,
				       ub_stat->tm_puls_info.int_osc_uncert);
			}
		}

		if (TIM_DISP_SRC(ub_stat->tm_puls_info.flags) ==
		    TIM_DISP_SRC_GNSS)
			ub_stat->ublox_stat.gnss_fix_time_count++;

		if (TIM_DISP_SRC(ub_stat->tm_puls_info.flags) ==
		    TIM_DISP_SRC_INT)
			ub_stat->ublox_stat.int_osc_fix_time_count++;
	}
	ub_stat->ublox_stat.tim_tos_count++;

	if (g_dbg_mask & DBG_MSG_CFG_RSP_PARSED) {
		printk("Flag:0x%8.8X TmPlTlrnc : %s  "
		       "IntOscTlrnc: %s  GnssTmValid: %s\n",
		       ub_stat->tm_puls_info.flags,
		       (ub_stat->tm_puls_info.flags & TIM_TOS_PULSE_IN_TOL)
			   ? "YES"
			   : "NO ",
		       (ub_stat->tm_puls_info.flags & TIM_TOS_INTOSC_IN_TOL)
			   ? "YES"
			   : "NO ",
		       (ub_stat->tm_puls_info.flags & TIM_TOS_GNSS_TM_VALD)
			   ? "YES"
			   : "NO ");
		disp_name_i =
		    (ub_stat->tm_puls_info.flags & TIM_TOS_DISP_SOURCE) >> 8;
		if (disp_name_i > 6) {
			disp_name_i = 6;
		}
		printk(
		    "                UtcTmValid: %s  PlsInCoh:    %s"
		    "PulseLocked: %s DispSrc: %s\n",
		    (ub_stat->tm_puls_info.flags & TIM_TOS_UTC_TM_VALD) ? "YES"
									: "NO ",
		    (ub_stat->tm_puls_info.flags & TIM_TOS_PULS_IN_COH) ? "YES"
									: "NO ",
		    (ub_stat->tm_puls_info.flags & TIM_TOS_PULS_LOCKED) ? "YES"
									: "NO ",
		    disp_src_name[disp_name_i]);
		printk("Date:%d %d %d   Time:%d %d %d\n",
		       ub_stat->tm_puls_info.year, ub_stat->tm_puls_info.month,
		       ub_stat->tm_puls_info.day, ub_stat->tm_puls_info.hour,
		       ub_stat->tm_puls_info.minute,
		       ub_stat->tm_puls_info.seconds);
		printk("UTC_Stat : Offset %8.8d   Uncertainty %d\n",
		       ub_stat->tm_puls_info.utc_tm_ofst,
		       ub_stat->tm_puls_info.utc_uncert);
		printk("GNSS_Stat: Offset %8.8d   Uncertainty %d\n",
		       ub_stat->tm_puls_info.gns_top_ofst,
		       ub_stat->tm_puls_info.gnss_uncert);
		printk("Int_Osc  : Offset %8.8d   Uncertainty %d\n",
		       ub_stat->tm_puls_info.int_osc_ofst,
		       ub_stat->tm_puls_info.int_osc_uncert);
	}

	return 0;
}

/*****************************************************************************
 * UBX-TIM-SVIN (0x0D 0x04) Survey-in data
 *****************************************************************************/
static int time_srvyin_handler(struct ublox_msg_data *ub_data,
			       const char *msg_ptr, int len)
{
	struct t_ublox_stat_data *ub_stat = &ub_data->stat_d;

	ub_stat->srvy_in_stat.survey_time = LE_HOST_INT(msg_ptr + 6);
	ub_stat->srvy_in_stat.mean_x = LE_HOST_INT(msg_ptr + 10);
	ub_stat->srvy_in_stat.mean_y = LE_HOST_INT(msg_ptr + 14);
	ub_stat->srvy_in_stat.mean_z = LE_HOST_INT(msg_ptr + 18);
	ub_stat->srvy_in_stat.variance_3d = LE_HOST_INT(msg_ptr + 22);
	ub_stat->srvy_in_stat.num_pos_observed = LE_HOST_INT(msg_ptr + 26);
	ub_stat->srvy_in_stat.valid_flag = msg_ptr[30];
	ub_stat->srvy_in_stat.in_progress = msg_ptr[31];
	ub_stat->srvy_in_stat.is_stale = 0;
	return 0;
}

/*****************************************************************************
 * Handler for the netlink command, stats read or ublox config
 * Test command    sdnclient -g --datavalue 03 00 01 00
 *****************************************************************************/
int get_gps_nl_rsp(struct ublox_msg_data *ub_data, unsigned char *cmd_ptr,
		   int cmd_len, unsigned char *rsp_buf, int rsp_buf_len)
{
	struct t_ublox_stat_data *ub_stat = &ub_data->stat_d;
	int data_len = 0;
	struct t_gps_space_veh_rsp_data *svd;
	struct t_gps_time_pulse_rsp_data *tmpd;
	struct t_gps_pos_rsp_data *posd;
	struct t_gps_self_pos *pos;
	int i;
	struct t_ublox_srvy_in_result srvy_in_stat_copy;

	if ((!cmd_ptr) || (cmd_len < 4) || (!rsp_buf) || (rsp_buf_len < 4)) {
		printk(KERN_WARNING
		       "Invalid buff/buff_len in get_gps_nl_rsp\n");
		return -1;
	}
	if (cmd_ptr[2] == GPS_STAT_CMD_SVINFO) { // Space Vehicle info
		svd = (struct t_gps_space_veh_rsp_data *)rsp_buf;
		memcpy(svd->hdr, cmd_ptr, 4); // copy the reqest cmd to resp
		spin_lock_bh(&ub_data->data_lock);
		data_len = ub_stat->num_space_veh *
			       sizeof(struct t_ublox_space_veh_info) +
			   sizeof(struct t_gps_space_veh_rsp_data);
		if (rsp_buf_len < data_len) {
			printk(KERN_WARNING
			       "Needs BuffSize: %d Given only: %d\n",
			       data_len, rsp_buf_len);
			spin_unlock_bh(&ub_data->data_lock);
			return -1;
		}
		svd->num_space_veh = ub_stat->num_space_veh;
		for (i = 0; i < ub_stat->num_space_veh; i++) { // UBX-NAV-SVINFO
			svd->space_veh_info[i].sat_id =
			    ub_stat->space_veh_info[i].sat_id;
			svd->space_veh_info[i].flags =
			    ub_stat->space_veh_info[i].flags;
			svd->space_veh_info[i].qlty =
			    ub_stat->space_veh_info[i].qlty;
			svd->space_veh_info[i].snr =
			    ub_stat->space_veh_info[i].snr;
			svd->space_veh_info[i].elev =
			    ub_stat->space_veh_info[i].elev;
		}
		spin_unlock_bh(&ub_data->data_lock);
		if (g_dbg_mask & DBG_MSG_CFG_STAT_RD) {
			printk("SpaceVehLen: %d\n", data_len);
		}

	} else if (cmd_ptr[2] ==
		   GPS_STAT_CMD_TMPLFQ) { // Time Pulse Frequency Data
		tmpd = (struct t_gps_time_pulse_rsp_data *)rsp_buf;
		memcpy(tmpd->hdr, cmd_ptr, 4); // copy the reqest cmd to resp
		data_len = sizeof(struct t_gps_time_pulse_rsp_data);
		if (rsp_buf_len < data_len) {
			printk(KERN_WARNING
			       "TimePulse NeedBufLen: %d Given only: %d\n",
			       data_len, rsp_buf_len);
			return -1;
		}
		spin_lock_bh(&ub_data->data_lock); // UBX-TIM-TOS
		tmpd->tm_puls_info.year = ub_stat->tm_puls_info.year;
		tmpd->tm_puls_info.gns_top_ofst =
		    ub_stat->tm_puls_info.gns_top_ofst;
		tmpd->tm_puls_info.int_osc_ofst =
		    ub_stat->tm_puls_info.int_osc_ofst;
		tmpd->tm_puls_info.utc_tm_ofst =
		    ub_stat->tm_puls_info.utc_tm_ofst;

		tmpd->tm_puls_info.flags = ub_stat->tm_puls_info.flags;
		tmpd->tm_puls_info.utc_uncert =
		    ub_stat->tm_puls_info.utc_uncert;
		tmpd->tm_puls_info.gnss_uncert =
		    ub_stat->tm_puls_info.gnss_uncert;
		tmpd->tm_puls_info.int_osc_uncert =
		    ub_stat->tm_puls_info.int_osc_uncert;
		tmpd->tm_puls_info.month = ub_stat->tm_puls_info.month;
		tmpd->tm_puls_info.day = ub_stat->tm_puls_info.day;
		tmpd->tm_puls_info.hour = ub_stat->tm_puls_info.hour;
		tmpd->tm_puls_info.minute = ub_stat->tm_puls_info.minute;
		tmpd->tm_puls_info.seconds = ub_stat->tm_puls_info.seconds;

		spin_unlock_bh(&ub_data->data_lock);

	} else if (cmd_ptr[2] == GPS_STAT_CMD_LATLONG) {
		posd = (struct t_gps_pos_rsp_data *)rsp_buf;
		memcpy(posd->hdr, cmd_ptr, 4); // copy the reqest cmd to resp
		data_len = sizeof(struct t_gps_pos_rsp_data);
		if (rsp_buf_len < data_len) {
			printk(KERN_WARNING
			       "pos_fix_info NeedBufLen: %d Given only: %d\n",
			       data_len, rsp_buf_len);
			return -1;
		}
		spin_lock_bh(&ub_data->data_lock); // UBX-NAV-PVT
		posd->pos_fix_info.fix_type = ub_stat->pos_time_info.fix_type;
		posd->pos_fix_info.num_sat_used =
		    ub_stat->pos_time_info.num_sat_used;
		posd->pos_fix_info.long_value =
		    ub_stat->pos_time_info.long_value;
		posd->pos_fix_info.lat_value = ub_stat->pos_time_info.lat_value;
		posd->pos_fix_info.hgt_ellipsoid =
		    ub_stat->pos_time_info.hgt_ellipsoid;
		posd->pos_fix_info.hgt_sea_lvl =
		    ub_stat->pos_time_info.hgt_sea_lvl;
		spin_unlock_bh(&ub_data->data_lock);

	} else if (cmd_ptr[2] == GPS_SET_UBLX_RESET) {
		schedule_ublox_reset(ub_data, eUBLOX_RSP_DST_NONE);

	} else if (cmd_ptr[2] == GPS_SET_CMD_SING_SAT) {
		//-------- Single Satellite config values
		config_single_satellite(ub_data, cmd_ptr, cmd_len, rsp_buf,
					rsp_buf_len);
		printk("======== Config Single Sattelite Mode\n");
		data_len = sizeof(struct t_gps_self_pos);
		if (rsp_buf_len < data_len) {
			printk(KERN_WARNING "GPS_SET_CMD_SING_SAT NeedBufLen: "
					    "%d Given only: %d\n",
			       data_len, rsp_buf_len);
			return -1;
		}
		pos = (struct t_gps_self_pos *)rsp_buf;
		memcpy(pos, cmd_ptr, data_len);
	} else if (cmd_ptr[2] == GPS_GET_CMD_POS) {
		data_len = sizeof(struct t_gps_self_pos);
		if (rsp_buf_len < data_len) {
			printk(
			    KERN_WARNING
			    "GPS_GET_CMD_POS NeedBufLen: %d Given only: %d\n",
			    data_len, rsp_buf_len);
			return -1;
		}
		pos = (struct t_gps_self_pos *)rsp_buf;
		memcpy(pos, cmd_ptr, data_len);
		spin_lock_bh(&ub_data->data_lock);
		srvy_in_stat_copy = ub_stat->srvy_in_stat;
		ub_stat->srvy_in_stat.is_stale = 1;
		if (srvy_in_stat_copy.in_progress) {
			pos->latitude = ub_stat->pos_time_info.lat_value;
			pos->longitude = ub_stat->pos_time_info.long_value;
			pos->height = ub_stat->pos_time_info.hgt_ellipsoid;
			pos->ecef_x = srvy_in_stat_copy.mean_x;
			pos->ecef_y = srvy_in_stat_copy.mean_y;
			pos->ecef_z = srvy_in_stat_copy.mean_z;
			pos->accuracy = srvy_in_stat_copy.variance_3d;
		}
		spin_unlock_bh(&ub_data->data_lock);
		printk("survey in: stale, %d, num, %d, variance, %d, progress, "
		       "%d, valid, "
		       "%d, x, %d, y, %d, z, %d\n",
		       srvy_in_stat_copy.is_stale,
		       srvy_in_stat_copy.num_pos_observed,
		       srvy_in_stat_copy.variance_3d,
		       srvy_in_stat_copy.in_progress,
		       srvy_in_stat_copy.valid_flag, srvy_in_stat_copy.mean_x,
		       srvy_in_stat_copy.mean_y, srvy_in_stat_copy.mean_z);
		if (!srvy_in_stat_copy.in_progress) {
			printk("kick off survey in to send gps position "
			       "northbound\n");
			// ublox not in survey in mode
			// position is invalid
			pos->accuracy = -1;
			pos->latitude = 0;
			pos->longitude = 0;
			pos->height = 0;
			pos->ecef_x = 0;
			pos->ecef_y = 0;
			pos->ecef_z = 0;
			// kick off survey in
			config_survey_in(ub_data);
		}
		printk("sending northbound: latitude, %d, longitude, %d, "
		       "height, %d, accuracy, %d\n",
		       pos->latitude, pos->longitude, pos->height,
		       pos->accuracy);
	}
	return data_len;
}

/*****************************************************************/
static int update_gps_pos_fix_stat(struct t_ublox_stat_data *ub_stat,
				   struct t_gps_pos_fix *p_gps_pos_fix)
{

	if (!p_gps_pos_fix) {
		return 0;
	}
	p_gps_pos_fix->latitude = ub_stat->pos_time_info.lat_value;
	p_gps_pos_fix->longitude = ub_stat->pos_time_info.long_value;
	p_gps_pos_fix->hght_msl = ub_stat->pos_time_info.hgt_sea_lvl;
	p_gps_pos_fix->hght_elipsd = ub_stat->pos_time_info.hgt_ellipsoid;
	p_gps_pos_fix->num_sat_used = ub_stat->pos_time_info.num_sat_used;
	p_gps_pos_fix->fix_type = ub_stat->pos_time_info.fix_type;

	p_gps_pos_fix->ecef_x = ub_stat->srvy_in_stat.mean_x;
	p_gps_pos_fix->ecef_y = ub_stat->srvy_in_stat.mean_y;
	p_gps_pos_fix->ecef_z = ub_stat->srvy_in_stat.mean_z;
	p_gps_pos_fix->num_pos_observed =
	    ub_stat->srvy_in_stat.num_pos_observed;
	p_gps_pos_fix->variance_3d = ub_stat->srvy_in_stat.variance_3d;

	return sizeof(struct t_gps_pos_fix);
}

/*****************************************************************/
static int update_gps_tim_tos_stat(struct t_ublox_stat_data *ub_stat,
				   struct t_tim_puls_freq *p_tim_puls_freq)
{
	if (!p_tim_puls_freq) {
		return 0;
	}
	p_tim_puls_freq->gnss_tim_ofset_ns = ub_stat->tm_puls_info.gns_top_ofst;
	p_tim_puls_freq->gnss_tim_uncert_ns = ub_stat->tm_puls_info.gnss_uncert;
	p_tim_puls_freq->int_osc_ofset_ppb =
	    ub_stat->tm_puls_info.int_osc_ofst >> 8;
	p_tim_puls_freq->int_osc_uncert_ppb =
	    ub_stat->tm_puls_info.int_osc_uncert >> 8;
	p_tim_puls_freq->discp_src =
	    (ub_stat->tm_puls_info.flags & TIM_TOS_DISP_SOURCE) >> 8;
	p_tim_puls_freq->tim_tos_flag = ub_stat->tm_puls_info.flags;

	return sizeof(struct t_tim_puls_freq);
}

#define GPS_STAT_MAX_SIZE 512

/*****************************************************************/
int push_gps_stats_nb(struct ublox_msg_data *ub_data)
{
	struct t_ublox_stat_data *ub_stat = &ub_data->stat_d;
	struct ublox_msg_client *ub_client;
	struct TgdDrvrStat *gps_rsp_buf;
	unsigned char cmd_ptr[4];
	int stat_len;

	gps_rsp_buf =
	    (struct TgdDrvrStat *)kmalloc(GPS_STAT_MAX_SIZE, GFP_KERNEL);
	if (!gps_rsp_buf) {
		printk(KERN_WARNING "kmalloc error in push_gps_stats_nb\n");
		return -1;
	}
	cmd_ptr[2] = GPS_STAT_CMD_SVINFO;
	gps_rsp_buf->msgType = NB_DRVR_STAT_GPS;
	gps_rsp_buf->gps_time_in_sec = ub_data->gps_time_sec;

	stat_len = offsetof(struct TgdDrvrStat, data);

	stat_len += update_gps_pos_fix_stat(
	    ub_stat, &gps_rsp_buf->data.gps_stat.gps_pos_fix);
	stat_len += update_gps_tim_tos_stat(
	    ub_stat, &gps_rsp_buf->data.gps_stat.tim_pulse_freq);

	stat_len += get_gps_nl_rsp(
	    ub_data, cmd_ptr, sizeof(cmd_ptr),
	    (unsigned char *)(&gps_rsp_buf->data.gps_stat.sat_in_view),
	    GPS_STAT_MAX_SIZE - stat_len);
	if (g_dbg_mask & DBG_MSG_CFG_STAT_RD) {
		printk("GPS stat Pushing %d bytes\n", stat_len);
	}

	/* Update stats using first client available */
	mutex_lock(&ub_data->clnt_lock);
	ub_client = list_first_entry_or_null(&ub_data->clnt_list,
					     struct ublox_msg_client, link);
	if (ub_client != NULL)
		ub_client->gps_clnt->stat_update(ub_client->gps_clnt_data,
						 gps_rsp_buf, stat_len);
	mutex_unlock(&ub_data->clnt_lock);
	kfree(gps_rsp_buf);
	return 0;
}

/*
 * External interface used by clients (Terragraph kernel module)
 */
static int ublox_gps_init_client(struct fb_tgd_gps_clnt *clnt, void *clnt_data,
				 void **gps_data)
{
	struct ublox_msg_data *ub_data = &g_ub_data;
	struct ublox_msg_client *ub_client;

	/* Make sure device was attached */
	if (ub_data->init_flag == 0)
		return -ENODEV;

	ub_client = kzalloc(sizeof(*ub_client), GFP_KERNEL);
	if (ub_client == NULL)
		return -ENOMEM;

	/* Initialize the client with defaults */
	INIT_LIST_HEAD(&ub_client->link);
	ub_client->ub_data = ub_data;
	ub_client->gps_clnt = clnt;
	ub_client->gps_clnt_data = clnt_data;

	mutex_lock(&ub_data->clnt_lock);
	list_add_tail(&ub_client->link, &ub_data->clnt_list);
	mutex_unlock(&ub_data->clnt_lock);
	*gps_data = ub_client;
	return 0;
}

static void ublox_gps_fini_client(struct fb_tgd_gps_clnt *clnt, void *gps_data)
{
	struct ublox_msg_client *ub_client = gps_data;
	struct ublox_msg_data *ub_data;

	BUG_ON(ub_client == NULL);
	BUG_ON(ub_client->ub_data != &g_ub_data);
	BUG_ON(ub_client->gps_clnt != clnt);

	/*
	 * Protect againt worker thread running the client
	 * notification right now.
	 */
	ub_data = ub_client->ub_data;
	mutex_lock(&ub_data->clnt_lock);
	list_del(&ub_client->link);
	mutex_unlock(&ub_data->clnt_lock);
	kfree(ub_client);
}

static int ublox_gps_start_sync(void *gps_data)
{
	struct ublox_msg_client *ub_client = gps_data;

	BUG_ON(ub_client == NULL);
	BUG_ON(ub_client->ub_data != &g_ub_data);

	ub_client->send_to_clnt = true;
	return 0;
}

static void ublox_gps_stop_sync(void *gps_data)
{
	struct ublox_msg_client *ub_client = gps_data;
	struct ublox_msg_data *ub_data;

	BUG_ON(ub_client == NULL);
	BUG_ON(ub_client->ub_data != &g_ub_data);

	/*
	 * Grab the lock to ensure the barrier: no update
	 * will be sent to client after this call completes.
	 */
	ub_data = ub_client->ub_data;
	mutex_lock(&ub_data->clnt_lock);
	ub_client->send_to_clnt = false;
	mutex_unlock(&ub_data->clnt_lock);
}

static int ublox_gps_handle_nl_msg(void *gps_data, unsigned char *cmd_ptr,
				   int cmd_len, void *rsp_buf, int rsp_buf_len)
{
	struct ublox_msg_client *ub_client = gps_data;

	BUG_ON(ub_client == NULL);
	BUG_ON(ub_client->ub_data != &g_ub_data);

	return get_gps_nl_rsp(ub_client->ub_data, cmd_ptr, cmd_len, rsp_buf,
			      rsp_buf_len);
}

static void ublox_gps_update_time(struct ublox_msg_data *ub_data,
				  struct timespec *ts)
{
	struct ublox_msg_client *ub_client;

	BUG_ON(ub_data != &g_ub_data);

	mutex_lock(&ub_data->clnt_lock);
	list_for_each_entry(ub_client, &ub_data->clnt_list, link)
	{
		if (!ub_client->send_to_clnt)
			continue;
		ub_client->gps_clnt->time_update(ub_client->gps_clnt_data, ts);
	}
	mutex_unlock(&ub_data->clnt_lock);
}

const static struct fb_tgd_gps_impl ublox_gps_impl = {
    .init_client = ublox_gps_init_client,
    .fini_client = ublox_gps_fini_client,
    .start_sync = ublox_gps_start_sync,
    .stop_sync = ublox_gps_stop_sync,
    .handle_nl_msg = ublox_gps_handle_nl_msg,
};

const struct fb_tgd_gps_impl *fb_gps_impl = &ublox_gps_impl;
EXPORT_SYMBOL(fb_gps_impl);

static int ublox_gps_register_device(struct ublox_msg_data *ub_data)
{
	struct platform_device *pdev;
	struct platform_device_info pinfo;
	struct tgd_gps_platdata data;

	ub_data->platform_dev = NULL;

	memset(&pinfo, 0, sizeof(pinfo));
	memset(&data, 0, sizeof(data));

	pinfo.name = TGD_GPS_COMPATIBLE_STRING;
	pinfo.id = PLATFORM_DEVID_NONE;

	/* Wrap host pointers */
	data.drv_api_version = TGD_GPS_API_VERSION;
	data.drv_gps_ops = &ublox_gps_impl;

	pinfo.data = &data;
	pinfo.size_data = sizeof(data);

	pdev = platform_device_register_full(&pinfo);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	ub_data->platform_dev = pdev;
	return 0;
}

static void ublox_gps_unregister_device(struct ublox_msg_data *ub_data)
{
	if (ub_data->platform_dev != NULL) {
		platform_device_unregister(ub_data->platform_dev);
		ub_data->platform_dev = NULL;
	}
}
