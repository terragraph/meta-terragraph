/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Netlink related functions. */

#ifndef TGD_NLSDN_H
#define TGD_NLSDN_H

#include <linux/workqueue.h>
#include <linux/if.h>

#include <fb_tgd_nlsdn_common.h>
#include <fb_tg_fw_driver_if.h>
#include "fb_tgd_backhaul.h"

#define EVNT_DATA_MAX_SIZE 128

/* Internal struct to hold netlink message parts */
struct tgd_nlmsg {
	struct sk_buff *tgd_skb;
	struct genl_info *tgd_info;
	struct genl_family *tgd_family;
	int tgd_cmd;
	void *tgd_msghdr;
	int tgd_msgsz;
};

#define TGD_IFNAME_SZ IFNAMSIZ

// Info for msg between TGD and minion for wsec status
// TGD_NLSDN_CMD_NOTIFY_WSEC_STATUS
typedef struct _fb_tgd_link_wsec_status {
	char ifname[TGD_IFNAME_SZ];
	uint8_t status;
} fb_tgd_link_wsec_status_t;

// Info for msg between TGD and minion for wsec link up status.
// This msg is sent from TGD to minion when secure link is up.
// TGD_NLSDN_CMD_NOTIFY_WSEC_LINKUP_STATUS
typedef struct _fb_tgd_link_wsec_link_status {
	char ifname[TGD_IFNAME_SZ];
} fb_tgd_link_wsec_link_status_t;

typedef enum _fb_tgd_device_status {
	DEVICE_DOWN = 0,
	DEVICE_UP,
} fb_tgd_device_status_t;

// Info for msg between TGD and minion for link status
typedef struct _fb_tgd_link_status {
	char ifname[TGD_IFNAME_SZ];
	tgEthAddr linkStaAddr;
	uint8_t linkStatus;
	uint8_t linkFailureCause;
	uint8_t linkStaNodeType;
	uint8_t peerNodeType;
} fb_tgd_link_status_t;

typedef int (*tgd_nlmsg_cb_t)(struct tgd_nlmsg *msg, void *data, int len);

void tgd_nlsdn_trigger_notify(int msecs, int cmd, void *event_data,
			      unsigned long event_data_size, tgd_nlmsg_cb_t cb,
			      struct tgd_terra_driver *fb_drv);
int tgd_nlsdn_tginit_msg(struct tgd_nlmsg *msg, void *event_data, int len);
int tgd_nlsdn_nodeconfig_msg(struct tgd_nlmsg *msg, void *event_data, int len);
int tgd_nlsdn_bmfmconfig_msg(struct tgd_nlmsg *msg, void *event_data, int len);
int tgd_nlsdn_linkup_status_msg(struct tgd_nlmsg *msg, void *event_data,
				int len);
int tgd_nlsdn_wsec_status_msg(struct tgd_nlmsg *msg, void *event_data, int len);
int tgd_nlsdn_stats_passthrough(struct tgd_nlmsg *msg, void *event_data,
				int len_passthrough);
int tgd_nlsdn_push_gps_stat_nb(struct tgd_terra_driver *fb_drv,
			       unsigned char *gps_rsp_buf, int len);
int tgd_get_stats(struct tgd_terra_driver *fb_drv_data,
		  fb_tgd_link_stats_t *nl_buffer, int nl_max_size,
		  int *num_link);
int tgd_nlsdn_send_wsec_linkup_status(struct tgd_terra_driver *fb_drv,
				      unsigned char *wsec_linkup_status_buf,
				      int len);
int tgd_nlsdn_send_device_updown_status(struct tgd_terra_driver *fb_drv,
					fb_tgd_device_status_t updown_status);

/* Module init and exit */
int tgd_nlsdn_init(void);
void tgd_nlsdn_exit(void);

#endif
