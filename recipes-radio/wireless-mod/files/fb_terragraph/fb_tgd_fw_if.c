/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Firmware interface related API's */

#include <fb_tg_fw_driver_if.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>

#include <net/genetlink.h>
#include "fb_tgd_backhaul.h"
#include "fb_tgd_nlsdn.h"
#include "fb_tgd_fw_if.h"
#include "fb_tgd_gps_if.h"
#include "fb_tgd_terragraph.h"
#include "fb_tgd_debug.h"
#include "fb_tgd_queue_stats.h"
#include "fb_tgd_cfg80211.h"

#define VENDOR_IOCTL_MAX_SIZE 1024
#define LOCAL_IOCTL_BUF_SIZE 512
#define RESPONSE_BUF_SIZE 128

/*
 * Declarations for HTSF handling for MBH boards
 */

// devidx - Index of baseband device sending this event
// macaddr - macaddr for the device owned by this driver (as u64)
// txRxDiffNs - Time difference between Tx/Rx timestamps of Keepalives (in ns)
// delayEstNs - Propagation delay estimate (in ns)
// rxStartUs- Rx Start HW TSF timestamp (in us)
typedef void (*tgd_htsf_info_handler_t)(int devidx, uint64_t macaddr,
					int32_t txRxDiffNs, int32_t delayEstNs,
					uint32_t rxStartUs);

// Handler for HTSF messages from the firmware. It is intended to be used on
// MBH systems (i.e., with a Microsemi DPLL chip).
static tgd_htsf_info_handler_t tgd_htsf_info_handler;

int tgd_register_htsf_info_handler(tgd_htsf_info_handler_t handler)
{
	if (cmpxchg(&tgd_htsf_info_handler, NULL, handler) == NULL) {
		return 0;
	}
	return -EEXIST;
}
EXPORT_SYMBOL(tgd_register_htsf_info_handler);

int tgd_unregister_htsf_info_handler(tgd_htsf_info_handler_t handler)
{
	if (cmpxchg(&tgd_htsf_info_handler, handler, NULL) == handler) {
		return 0;
	}
	return -EINVAL;
}
EXPORT_SYMBOL(tgd_unregister_htsf_info_handler);

/****************************************************************************/
int add_var_data(tgVarData *dst_var_dp, // variable data pointer
		 size_t hdr_size,       // numb Bytes before dst_var_dp->data
		 size_t max_buf_size,   // Maximum allowed size for full pkt
		 size_t var_data_len,   // number of bytes to be copied
		 unsigned char *var_data_ptr)
{

	TGD_DBG_CTRL_INFO("hdr_size: %zu ============\n", hdr_size);
	if (dst_var_dp == NULL) {
		TGD_DBG_CTRL_ERROR("dst_var_dp == NULL \n");
		return -1;
	}
	dst_var_dp->len = 0;
	if (var_data_len <= 0) {
		return 0;
	}
	if ((hdr_size + var_data_len) > max_buf_size) {
		TGD_DBG_CTRL_ERROR("Len: %zu > MaxLen: %zu\n",
				   hdr_size + var_data_len, max_buf_size);
		return -1;
	}
	TGD_DBG_CTRL_INFO("FW Cfg IoCtl: len %zu \n", var_data_len);
	memcpy(dst_var_dp->data, var_data_ptr, var_data_len);
	dst_var_dp->len = var_data_len;
	return 0;
}

#ifdef PRINT_MSG_TO_FW
static inline void dump_msg_to_fw(unsigned char *buf, size_t len)
{
	int i;

	for (i = 0; i < len; i++)
		printk(KERN_WARNING "<%2.2X> ", buf[i]);
	printk(KERN_WARNING "\n");
}
#endif

/***************************************************************************/
int tgd_send_fw_init(struct tgd_terra_driver *fb_drv_data,
		     unsigned int var_data_len, unsigned char *var_data_ptr)
{
	unsigned char ioctl_req_buff[LOCAL_IOCTL_BUF_SIZE];
	unsigned char ioctl_rsp_buff[RESPONSE_BUF_SIZE];
	fbTgIfEvent *ioctl;
	size_t ioctl_len;
	size_t hdr_size;

	ioctl = (fbTgIfEvent *)ioctl_req_buff;
	ioctl->type = TG_SB_INIT_REQ;

	hdr_size =
	    offsetof(fbTgIfEvent, data) + sizeof(ioctl->data.tgFwInitReq);
	TGD_DBG_CTRL_INFO("hdr_size: %zu (%zu + %zu)\n", hdr_size,
			  offsetof(fbTgIfEvent, data),
			  sizeof(ioctl->data.tgFwInitReq));
	add_var_data(&ioctl->data.tgFwInitReq.varData, hdr_size,
		     LOCAL_IOCTL_BUF_SIZE, var_data_len, var_data_ptr);
	/*If the var data size > LOCAL_IOCTL_BUF_SIZE, send only the base
	 * message*/
	ioctl_len = hdr_size + ioctl->data.tgFwInitReq.varData.len;
	TGD_DBG_CTRL_INFO("Ioctl %d (TG_SB_INIT_REQ) Len:%zu\n", ioctl->type,
			  ioctl_len);
#ifdef PRINT_MSG_TO_FW
	dump_msg_to_fw(ioctl_req_buff, ioctl_len);
#endif
	fb_tgd_bh_ioctl(fb_drv_data, ioctl_req_buff, ioctl_len, ioctl_rsp_buff,
			RESPONSE_BUF_SIZE);
	ioctl = (fbTgIfEvent *)ioctl_rsp_buff;
	TGD_DBG_CTRL_INFO("FW IoCtl type %d  ErrCode %d\n", ioctl->type,
			  ioctl->data.tgIoctlGenRsp.errCode);

	return ioctl->data.tgIoctlGenRsp.errCode;
}

/***************************************************************************/
int tgd_send_queue_stats(struct tgd_terra_driver *fb_drv_data,
			 const tgSbQueueStats *queueStats, int numLinks)
{
	unsigned char ioctl_req_buff[sizeof(fbTgIfEvent)];
	unsigned char ioctl_rsp_buff[RESPONSE_BUF_SIZE];
	fbTgIfEvent *ioctl;
	size_t ioctl_len;
	size_t stats_len;

	if (numLinks <= 0) {
		return 0;
	}

	ioctl = (fbTgIfEvent *)ioctl_req_buff;
	ioctl->type = TG_SB_QUEUE_STATS;

	numLinks = min(QUEUE_STATS_MAX_LINKS, numLinks);

	stats_len = numLinks * sizeof(*queueStats);
	memcpy(ioctl->data.queueStats, queueStats, stats_len);

	ioctl_len = offsetof(fbTgIfEvent, data) + stats_len;

	TGD_DBG_CTRL_INFO("Ioctl %d (TG_SB_QUEUE_STATS) Len:%zu \n",
			  ioctl->type, ioctl_len);
#ifdef PRINT_MSG_TO_FW
	dump_msg_to_fw(ioctl_req_buff, ioctl_len);
#endif

	fb_tgd_bh_ioctl(fb_drv_data, ioctl_req_buff, ioctl_len, ioctl_rsp_buff,
			RESPONSE_BUF_SIZE);

	ioctl = (fbTgIfEvent *)ioctl_rsp_buff;
	TGD_DBG_CTRL_INFO("FW IoCtl type %d  ErrCode %d\n", ioctl->type,
			  ioctl->data.tgIoctlGenRsp.errCode);
	return ioctl->data.tgIoctlGenRsp.errCode;
}

int tgd_send_disassoc_req(struct tgd_terra_driver *fb_drv_data,
			  tgEthAddr *link_sta_mac_addr)
{
	unsigned char ioctl_req_buff[LOCAL_IOCTL_BUF_SIZE];
	unsigned char ioctl_rsp_buff[RESPONSE_BUF_SIZE];
	fbTgIfEvent *ioctl;
	int ioctl_len;

	ioctl = (fbTgIfEvent *)ioctl_req_buff;
	ioctl->type = TG_SB_DISASSOC_REQ;
	memcpy(&ioctl->data.tgFwDisassocReq.linkStaAddr, link_sta_mac_addr,
	       sizeof(*link_sta_mac_addr));

	ioctl_len =
	    offsetof(fbTgIfEvent, data) + sizeof(ioctl->data.tgFwDisassocReq);

	TGD_DBG_CTRL_INFO("Ioctl %d (TG_SB_DISASSOC_REQ) Len:%d \n",
			  ioctl->type, ioctl_len);
#ifdef PRINT_MSG_TO_FW
	dump_msg_to_fw(ioctl_req_buff, ioctl_len);
#endif
	fb_tgd_bh_ioctl(fb_drv_data, ioctl_req_buff, ioctl_len, ioctl_rsp_buff,
			RESPONSE_BUF_SIZE);
	ioctl = (fbTgIfEvent *)ioctl_rsp_buff;
	TGD_DBG_CTRL_INFO("FW IoCtl type %d  ErrCode %d\n", ioctl->type,
			  ioctl->data.tgIoctlGenRsp.errCode);
	return ioctl->data.tgIoctlGenRsp.errCode;
}

/*****************************************************************************/
int tgd_send_bmfm_cfg_req(struct tgd_terra_driver *fb_drv_data,
			  tgEthAddr *link_sta_mac_addr, tgBfRole bf_role,
			  unsigned int var_data_len,
			  unsigned char *var_data_ptr)
{
	unsigned char ioctl_req_buff[LOCAL_IOCTL_BUF_SIZE];
	unsigned char ioctl_rsp_buff[RESPONSE_BUF_SIZE];
	fbTgIfEvent *ioctl;
	size_t ioctl_len;
	size_t hdr_size;

	ioctl = (fbTgIfEvent *)ioctl_req_buff;
	ioctl->type = TG_SB_START_BF_SCAN_REQ;
	memcpy(&ioctl->data.tgFwStartBfAcqReq.linkStaAddr, link_sta_mac_addr,
	       sizeof(tgEthAddr));
	ioctl->data.tgFwStartBfAcqReq.bfAcqRole = bf_role;
	hdr_size =
	    offsetof(fbTgIfEvent, data) + sizeof(ioctl->data.tgFwStartBfAcqReq);
	TGD_DBG_CTRL_INFO("MandatoryDataSize: %zu (%zu + %zu)\n", hdr_size,
			  offsetof(fbTgIfEvent, data),
			  sizeof(ioctl->data.tgFwStartBfAcqReq));

	add_var_data(&ioctl->data.tgFwStartBfAcqReq.varData, hdr_size,
		     LOCAL_IOCTL_BUF_SIZE, var_data_len, var_data_ptr);
	ioctl_len = hdr_size + ioctl->data.tgFwStartBfAcqReq.varData.len;
	TGD_DBG_CTRL_INFO("Ioctl %d (TG_SB_START_BF_SCAN_REQ) Len:%zu\n",
			  ioctl->type, ioctl_len);
#ifdef PRINT_MSG_TO_FW
	dump_msg_to_fw(ioctl_req_buff, ioctl_len);
#endif
	fb_tgd_bh_ioctl(fb_drv_data, ioctl_req_buff, ioctl_len, ioctl_rsp_buff,
			RESPONSE_BUF_SIZE);
	ioctl = (fbTgIfEvent *)ioctl_rsp_buff;
	TGD_DBG_CTRL_INFO("FW IoCtl type %d  ErrCode %d\n", ioctl->type,
			  ioctl->data.tgIoctlGenRsp.errCode);
	return ioctl->data.tgIoctlGenRsp.errCode;
}

int tgd_send_link_del_resp(struct tgd_terra_driver *fb_drv_data,
			   tgEthAddr *link_sta_mac_addr)
{
	unsigned char ioctl_req_buff[100];
	unsigned char ioctl_rsp_buff[RESPONSE_BUF_SIZE];
	fbTgIfEvent *ioctl;
	size_t ioctl_len;

	ioctl = (fbTgIfEvent *)ioctl_req_buff;
	ioctl->type = TG_SB_DEL_LINK_RESP;
	memcpy(&ioctl->data.tgDelLinkRsp.linkStaAddr, link_sta_mac_addr,
	       sizeof(tgEthAddr));
	TGD_DBG_CTRL_INFO("FW IoCtl Req cmd %d (TG_SB_DEL_LINK_RESP) mac %pM\n",
			  ioctl->type, link_sta_mac_addr);

	ioctl_len =
	    offsetof(fbTgIfEvent, data) + sizeof(ioctl->data.tgDelLinkRsp);
	TGD_DBG_CTRL_INFO("Ioctl %d (TG_SB_DEL_LINK_RESP) Len:%zu\n",
			  ioctl->type, ioctl_len);
#ifdef PRINT_MSG_TO_FW
	dump_msg_to_fw(ioctl_req_buff, ioctl_len);
#endif
	fb_tgd_bh_ioctl(fb_drv_data, ioctl_req_buff, ioctl_len, ioctl_rsp_buff,
			RESPONSE_BUF_SIZE);

	ioctl = (fbTgIfEvent *)ioctl_rsp_buff;
	TGD_DBG_CTRL_INFO("FW IoCtl type %d  ErrCode %d\n", ioctl->type,
			  ioctl->data.tgIoctlGenRsp.errCode);
	return ioctl->data.tgIoctlGenRsp.errCode;
}

int tgd_send_gps_time(struct tgd_terra_driver *fb_drv_data,
		      struct timespec *time)
{
	unsigned char ioctl_req_buff[100];
	unsigned char ioctl_rsp_buff[RESPONSE_BUF_SIZE];
	fbTgIfEvent *ioctl;
	size_t ioctl_len;

	ioctl = (fbTgIfEvent *)ioctl_req_buff;
	ioctl->type = TG_SB_GPS_TIME;

	ioctl->data.tgGpsTimeData.secondsL = time->tv_sec;
	ioctl->data.tgGpsTimeData.secondsH = ((uint64_t)time->tv_sec) >> 32;
	ioctl->data.tgGpsTimeData.nanoseconds = time->tv_nsec;

	ioctl_len =
	    offsetof(fbTgIfEvent, data) + sizeof(ioctl->data.tgGpsTimeData);
	TGD_DBG_CTRL_INFO("Ioctl %d (TG_SB_GPS_TIME) Len:%zu\n", ioctl->type,
			  ioctl_len);
#ifdef PRINT_MSG_TO_FW
	dump_msg_to_fw(ioctl_req_buff, ioctl_len);
#endif
	fb_tgd_bh_ioctl(fb_drv_data, ioctl_req_buff, ioctl_len, ioctl_rsp_buff,
			RESPONSE_BUF_SIZE);

	ioctl = (fbTgIfEvent *)ioctl_rsp_buff;
	TGD_DBG_CTRL_INFO("FW IoCtl type %d  ErrCode %d\n", ioctl->type,
			  ioctl->data.tgIoctlGenRsp.errCode);
	return ioctl->data.tgIoctlGenRsp.errCode;
}

int tgd_send_gps_pos(struct tgd_terra_driver *fb_drv_data, int latitude,
		     int longitude, int height, int accuracy)
{
	unsigned char ioctl_req_buff[LOCAL_IOCTL_BUF_SIZE];
	unsigned char ioctl_rsp_buff[RESPONSE_BUF_SIZE];
	fbTgIfEvent *ioctl;
	size_t ioctl_len;

	ioctl = (fbTgIfEvent *)ioctl_req_buff;
	ioctl->type = TG_SB_GPS_SET_SELF_POS;

	ioctl->data.tgGpsPosData.latitude = latitude;
	ioctl->data.tgGpsPosData.longitude = longitude;
	ioctl->data.tgGpsPosData.height = height;
	ioctl->data.tgGpsPosData.accuracy = accuracy;

	ioctl_len =
	    offsetof(fbTgIfEvent, data) + sizeof(ioctl->data.tgGpsPosData);

	fb_tgd_bh_ioctl(fb_drv_data, ioctl_req_buff, ioctl_len, ioctl_rsp_buff,
			RESPONSE_BUF_SIZE);

	ioctl = (fbTgIfEvent *)ioctl_rsp_buff;
	TGD_DBG_CTRL_INFO("FW IoCtl type %d  ErrCode %d\n", ioctl->type,
			  ioctl->data.tgIoctlGenRsp.errCode);
	return ioctl->data.tgIoctlGenRsp.errCode;
}

/*****************************************************************************
 * Get invoked from tgd_process_fb_events() the workQ handler for  FW message
 *****************************************************************************/

void tgd_fw_msg_handler(void *fb_drv_data, uint8_t *event, unsigned long size)
{
	fbTgIfEvent *fw_event;
	int event_type = 0;
	struct tgd_terra_driver *tgd_drv_data =
	    (struct tgd_terra_driver *)fb_drv_data;

	NL_STATS_INC(tgd_drv_data, NL_EVENTS);
	fw_event = (fbTgIfEvent *)event;
	event_type = fw_event->type;

	TGD_DBG_CTRL_INFO("%s: FW Event %d Rxed \n", __FUNCTION__, event_type);
	switch (event_type) {
	case TG_NB_INIT_RESP:
		NL_STATS_INC(tgd_drv_data, NL_NB_INIT_RESP);
		TGD_DBG_CTRL_INFO("Processing eTG_FW_INIT_RSP: %pM\n",
				  fw_event->data.tgFwInitRsp.macAddr.addr);
		tgd_nlsdn_trigger_notify(100, TGD_NLSDN_CMD_NOTIFY_TGINIT,
					 &fw_event->data.tgFwInitRsp,
					 sizeof(fwInitRsp),
					 tgd_nlsdn_tginit_msg, tgd_drv_data);
		break;

	case TG_NB_START_BF_SCAN_RESP:
		NL_STATS_INC(tgd_drv_data, NL_NB_START_BF_SCAN_RESP);
		tgd_nlsdn_trigger_notify(100, TGD_NLSDN_CMD_NOTIFY_BMFMCONFIG,
					 &fw_event->data.tgFwStartBfAcqRsp,
					 sizeof(sTgFwStartBfAcqRsp),
					 tgd_nlsdn_bmfmconfig_msg,
					 tgd_drv_data);
		break;
	case TG_NB_UPDATE_LINK_REQ: {
		struct tgd_terra_dev_priv *dev_priv = NULL;
		fb_tgd_link_status_t tgd_link_status;
		memset(&tgd_link_status, 0, sizeof(tgd_link_status));
		NL_STATS_INC(tgd_drv_data, NL_NB_UPDATE_LINK_REQ);
		dev_priv = tgd_terra_lookup_link_by_mac_addr(
		    tgd_drv_data, (tgEthAddr *)fw_event->data.tgFwLinkStatus
				      .linkStaAddr.addr);
		// Update the link status
		TGD_DBG_CTRL_INFO(
		    "LINK STATUS %d  Addr: %pM\n",
		    fw_event->data.tgFwLinkStatus.linkStatus,
		    fw_event->data.tgFwLinkStatus.linkStaAddr.addr);
		tgd_terra_set_link_status(
		    tgd_drv_data, &fw_event->data.tgFwLinkStatus.linkStaAddr,
		    fw_event->data.tgFwLinkStatus.linkStatus);
		// Send the linkstatus indication to SDN client
		if (dev_priv) {
			if (strscpy(tgd_link_status.ifname,
				    netdev_name(dev_priv->dev),
				    TGD_IFNAME_SZ) < 0) {
				TGD_DBG_CTRL_ERROR(
				    "UPDATE_LINK: interface name error %s\n",
				    netdev_name(dev_priv->dev));
			}
		}
		tgd_link_status.linkFailureCause =
		    fw_event->data.tgFwLinkStatus.linkFailureCause;
		tgd_link_status.linkStatus =
		    fw_event->data.tgFwLinkStatus.linkStatus;
		tgd_link_status.linkStaNodeType =
		    fw_event->data.tgFwLinkStatus.linkStaNodeType;
		tgd_link_status.peerNodeType =
		    fw_event->data.tgFwLinkStatus.peerNodeType;
		memcpy(&tgd_link_status.linkStaAddr,
		       (u8 *)&fw_event->data.tgFwLinkStatus.linkStaAddr,
		       sizeof(tgd_link_status.linkStaAddr));
		tgd_nlsdn_trigger_notify(
		    100, TGD_NLSDN_CMD_NOTIFY_LINK_STATUS, &tgd_link_status,
		    sizeof(tgd_link_status), tgd_nlsdn_linkup_status_msg,
		    tgd_drv_data);
#ifdef TG_ENABLE_CFG80211
		if (dev_priv) {
			if (fw_event->data.tgFwLinkStatus.linkStatus ==
			    TG_LINKUP) {
				sTgFwLinkStatus *status =
				    &fw_event->data.tgFwLinkStatus;
				tgWsecAuthType wsec_auth = status->wsecAuthType;

				tgd_cfg80211_evt_tg_connect(
				    dev_priv, (u8 *)status->linkStaAddr.addr,
				    status->assocReqIeLen,
				    status->assocRespIeLen, status->tlvs,
				    wsec_auth);

				if (wsec_auth == TGF_WSEC_DISABLE) {
					fb_tgd_link_wsec_status_t wsecStatus;
					memset(&wsecStatus, 0,
					       sizeof(wsecStatus));
					if (strscpy(wsecStatus.ifname,
						    netdev_name(dev_priv->dev),
						    TGD_IFNAME_SZ) < 0) {
						TGD_DBG_CTRL_ERROR(
						    "UPDATE_LINK: interface "
						    "name error %s\n",
						    netdev_name(dev_priv->dev));
					}
					wsecStatus.status = (uint8_t)wsec_auth;
					tgd_nlsdn_trigger_notify(
					    100,
					    TGD_NLSDN_CMD_NOTIFY_WSEC_STATUS,
					    &wsecStatus, sizeof(wsecStatus),
					    tgd_nlsdn_wsec_status_msg,
					    tgd_drv_data);
				}
			} else if (fw_event->data.tgFwLinkStatus.linkStatus ==
				   TG_LINKDOWN) {
				tgd_cfg80211_evt_tg_disconnect(
				    dev_priv,
				    (u8 *)fw_event->data.tgFwLinkStatus
					.linkStaAddr.addr);
			}
		} else
			TGD_DBG_CTRL_ERROR(
			    "dev not found for %pM\n",
			    (tgEthAddr *)
				fw_event->data.tgFwLinkStatus.linkStaAddr.addr);
#endif /* TG_ENABLE_CFG80211 */
	} break;
	case TG_NB_LINK_INFO:
		NL_STATS_INC(tgd_drv_data, NL_NB_LINK_INFO);
		tgd_terra_set_link_mac_addr(
		    fb_drv_data, &fw_event->data.tgLinkInfo.linkStaAddr,
		    fw_event->data.tgLinkInfo.rxLink,
		    fw_event->data.tgLinkInfo.txLink);
		break;
	case TG_NB_DEL_LINK_REQ:
		NL_STATS_INC(tgd_drv_data, NL_NB_DEL_LINK_REQ);
		tgd_terra_del_link_info(
		    fb_drv_data, &fw_event->data.tgDelLinkReq.linkStaAddr);
		tgd_send_link_del_resp(
		    tgd_drv_data, &fw_event->data.tgDelLinkReq.linkStaAddr);
		break;

	case TG_NB_PASSTHRU: // push from the FW
		NL_STATS_INC(tgd_drv_data, NL_NB_PASSTHRU);
		TGD_DBG_CTRL_INFO("tgd_fw_msg_handler StatPasThrough %lu\n",
				  size);
		tgd_nlsdn_trigger_notify(
		    100, TGD_NLSDN_CMD_PASSTHRU_NB, fw_event, size,
		    tgd_nlsdn_stats_passthrough, tgd_drv_data);
		break;

	case TG_NB_GPS_START_TIME_ACQUISITION:
		NL_STATS_INC(tgd_drv_data, NL_NB_GPS_START_TIME_ACQUISITION);
		tgd_gps_send_to_fw(fb_drv_data, true);
		break;

	case TG_NB_GPS_STOP_TIME_ACQUISITION:
		NL_STATS_INC(tgd_drv_data, NL_NB_GPS_STOP_TIME_ACQUISITION);
		tgd_gps_send_to_fw(fb_drv_data, false);
		break;

	case TG_NB_GPS_GET_SELF_POS: {
		struct t_gps_self_pos gps_cmd;
		struct t_gps_self_pos gps_self_pos;
		NL_STATS_INC(tgd_drv_data, NL_NB_GPS_GET_SELF_POS);
		gps_cmd.cmd = DRVR_CFG_CMD_GPS;
		gps_cmd.sub_cmd = GPS_GET_CMD_POS;

		if (tgd_gps_get_nl_rsp(fb_drv_data, (unsigned char *)&gps_cmd,
				       sizeof(struct t_gps_self_pos),
				       (unsigned char *)&gps_self_pos,
				       sizeof(struct t_gps_self_pos)) <= 0) {
			TGD_DBG_CTRL_ERROR(
			    "TG_NB_GPS_GET_SELF_POS,"
			    "Unable to get self GPS location.\n");
			break;
		}

		tgd_send_gps_pos(fb_drv_data, gps_self_pos.latitude,
				 gps_self_pos.longitude, gps_self_pos.height,
				 gps_self_pos.accuracy);
		break;
	}

	case TG_NB_HTSF_INFO:
		TGD_DBG_CTRL_INFO(
		    "HTSF INFO: txRxDiffNs %d delayEstNs %d rxStartUs %u\n",
		    fw_event->data.tgHtsfInfo.txRxDiffNs,
		    fw_event->data.tgHtsfInfo.delayEstNs,
		    fw_event->data.tgHtsfInfo.rxStartUs);
		if (tgd_htsf_info_handler) {
			tgd_htsf_info_handler(
			    tgd_drv_data->idx, tgd_drv_data->macaddr,
			    fw_event->data.tgHtsfInfo.txRxDiffNs,
			    fw_event->data.tgHtsfInfo.delayEstNs,
			    fw_event->data.tgHtsfInfo.rxStartUs);
		}
		break;

	default:
		TGD_DBG_CTRL_ERROR("Unexpected event %d\n", fw_event->type);
	};
}

int tgd_get_stats(struct tgd_terra_driver *fb_drv_data,
		  fb_tgd_link_stats_t *nl_buffer, int nl_max_size,
		  int *num_link)
{
	struct tgd_terra_dev_priv *priv;
	struct fb_tgd_bh_link_stats lstats;
	fb_tgd_link_stats_t *link_stats;
	int size = 0;

	*num_link = 0;
	link_stats = nl_buffer;

	/* Iterate over collected devices, try to fetch the state */
	list_for_each_entry(priv, &fb_drv_data->dev_q_head, list_entry)
	{
		tgd_terra_get_net_if_stat(priv->dev, &lstats);
		if (priv->link_state == TG_LINKINIT)
			continue;

		if (nl_max_size - size < sizeof(fb_tgd_link_stats_t))
			break;

		link_stats->link = lstats.link;
		link_stats->link_state = lstats.link_state;

		link_stats->rx_packets = lstats.pkts_recved;
		link_stats->tx_packets = lstats.pkts_sent;
		link_stats->rx_bytes = lstats.bytes_recved;
		link_stats->tx_bytes = lstats.bytes_sent;
		link_stats->tx_errors = lstats.tx_err;

		memcpy(link_stats->dst_mac_addr, lstats.dst_mac_addr,
		       sizeof(lstats.dst_mac_addr));
		memcpy(link_stats->src_mac_addr, lstats.src_mac_addr,
		       sizeof(lstats.src_mac_addr));
		link_stats->dev_index = lstats.dev_index;

		link_stats++;
		(*num_link)++;
		size += sizeof(fb_tgd_link_stats_t);
		TGD_DBG_CTRL_INFO("LinkCount: %d  Size: %d\n", *num_link, size);
	}

	return size;
}

int tgd_send_passthrough_to_fw(struct tgd_terra_driver *fb_drv_data,
			       char *src_data_ptr, int len)
{
	fbTgIfEvent *ev_data;
	unsigned char ioctl_rsp_buff[RESPONSE_BUF_SIZE];
	unsigned char *alloc_buf;
	size_t ioctl_len;
	size_t hdr_size;

	hdr_size = offsetof(fbTgIfEvent, data) +
		   sizeof(ev_data->data.tgPassThroughData);
	alloc_buf = (unsigned char *)kmalloc(hdr_size + len, GFP_KERNEL);
	if (alloc_buf == NULL) {
		TGD_DBG_DATA_ERROR(
		    "Error: memory allocating for passthrough data\n");
		return -1;
	}
	ev_data = (fbTgIfEvent *)alloc_buf;
	ev_data->type = TG_SB_PASSTHRU;
	add_var_data(&ev_data->data.tgPassThroughData.varData, hdr_size,
		     VENDOR_IOCTL_MAX_SIZE, len, src_data_ptr);
	ioctl_len = hdr_size + ev_data->data.tgPassThroughData.varData.len;
	TGD_DBG_CTRL_INFO("Ioctl %d (TG_SB_PASSTHRU) Len:%zu\n", ev_data->type,
			  ioctl_len);

#ifdef PRINT_MSG_TO_FW
	dump_msg_to_fw(alloc_buf, ioctl_len);
#endif
	fb_tgd_bh_ioctl(fb_drv_data, alloc_buf, ioctl_len, ioctl_rsp_buff,
			RESPONSE_BUF_SIZE);

	ev_data = (fbTgIfEvent *)ioctl_rsp_buff;
	TGD_DBG_CTRL_INFO("FW IoCtl type %d  ErrCode %d\n", ev_data->type,
			  ev_data->data.tgIoctlGenRsp.errCode);

	kfree(alloc_buf);
	return ev_data->data.tgIoctlGenRsp.errCode;
}

/***************************************************************************/
void tgd_send_fw_shutdown(struct tgd_terra_driver *fb_drv_data)
{
	unsigned char ioctl_req_buff[LOCAL_IOCTL_BUF_SIZE];
	unsigned char ioctl_rsp_buff[RESPONSE_BUF_SIZE];
	fbTgIfEvent *ioctl;
	size_t ioctl_len;
	size_t hdr_size;

	ioctl = (fbTgIfEvent *)ioctl_req_buff;
	ioctl->type = TG_SB_SHUTDOWN_REQ;

	hdr_size = offsetof(fbTgIfEvent, data);
	ioctl_len = hdr_size;
	TGD_DBG_CTRL_INFO("Ioctl %d (TG_SB_SHUTDOWN_REQ) Len:%zu\n",
			  ioctl->type, ioctl_len);
#ifdef PRINT_MSG_TO_FW
	dump_msg_to_fw(ioctl_req_buff, ioctl_len);
#endif
	fb_tgd_bh_ioctl(fb_drv_data, ioctl_req_buff, ioctl_len, ioctl_rsp_buff,
			RESPONSE_BUF_SIZE);
	ioctl = (fbTgIfEvent *)ioctl_rsp_buff;
	TGD_DBG_CTRL_INFO("FW IoCtl type %d  ErrCode %d\n", ioctl->type,
			  ioctl->data.tgIoctlGenRsp.errCode);
}
