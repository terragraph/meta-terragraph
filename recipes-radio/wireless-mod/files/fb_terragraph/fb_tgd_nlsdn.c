/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Terragraph Netlink src file */

#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/string.h>
#include <linux/timer.h>
#include <linux/preempt.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/atomic.h>

#include <net/genetlink.h>
#include "fb_tgd_backhaul.h"
#include "fb_tgd_nlsdn.h"
#include "fb_tgd_fw_if.h"
#include "fb_tgd_gps_if.h"
#include "fb_tgd_terragraph.h"
#include "fb_tgd_debug.h"

static TGD_NLSDN_POLICY_DEFN();

#define SB_PASSTHROUGH_MAX 1024
#define DRIVER_CFG_HDR_SIZE 2
#define PASSTHRU_SUCCESS_CODE 101 /* TODO replace with vendor definition */
#define TGC_VALID_DBG_MASK_BITS 0x00070007

/* define macros for kernel vs userspace */
#define TGDPRINT(fmt, arg...) pr_err("TGD: " fmt, ##arg)

/* common check macros */
#define CHECK_NULL(ptr, fmt, arg...)                                           \
	(((ptr) == 0) ? (TGDPRINT(fmt, ##arg) || 1) : 0)
#define CHECK_RET(var, fmt, arg...) CHECK_NULL(!(var), fmt, ##arg)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 7)
#define tgd_nla_put_u64(skb, attrtype, value)                                  \
	nla_put_u64_64bit(skb, attrtype, value, TGD_NLSDN_ATTR_PAD)
#else
#define tgd_nla_put_u64(skb, attrtype, value) nla_put_u64(skb, attrtype, value)
#endif

/* Global state struct TODO: export to proc */
static struct tgd_config {
	__u8 tgd_version;
} tgd_nlsdn_global_config;

#define TGD_NLMSG_INIT(_cmd, _info)                                            \
	{                                                                      \
		.tgd_cmd = _cmd, .tgd_family = TGD_NLMSG_DEFAULT_FAMILY_P,     \
		.tgd_info = _info, .tgd_skb = NULL, .tgd_msghdr = NULL,        \
		.tgd_msgsz = 0,                                                \
	}
#define TGD_NLMSG_DEFAULT_FAMILY tgd_nlsdn_fam
#define TGD_NLMSG_DEFAULT_FAMILY_P &tgd_nlsdn_fam

/* Netlink genl message helper functions */
static int tgd_new_genl_message(struct tgd_nlmsg *msg, int msgsz);
static int tgd_send_genl_message(struct tgd_nlmsg *msg,
				 struct tgd_terra_driver *fb_drv);
/* Handlers */
static int tgd_nlsdn_notify_cb(struct sk_buff *skb,
			       struct netlink_callback *cb);

static int tgd_nlsdn_tginit(struct sk_buff *skb2, struct genl_info *info);
static int tgd_handle_drv_cfg_data(struct tgd_terra_driver *fb_drv,
				   unsigned char *data_ptr, int data_len,
				   struct genl_info *info);

int tgd_nlsdn_set_bmfmconfig(struct sk_buff *skb2, struct genl_info *info);
static int tgd_nlsdn_grantalloc(struct sk_buff *skb2, struct genl_info *info);
static int tgd_nlsdn_set_dbgmask(struct sk_buff *skb2, struct genl_info *info);
static int tgd_nlsdn_get_stats(struct sk_buff *skb2, struct genl_info *info);
static int tgd_nlsdn_send_sb_passthrough(struct sk_buff *skb2,
					 struct genl_info *info);
static int tgd_nlsdn_handle_drvr_config(struct sk_buff *skb2,
					struct genl_info *info);
static int tgd_nlsdn_dev_alloc(struct sk_buff *skb2, struct genl_info *info);
static int tgd_nlsdn_set_gps_time(struct sk_buff *skb2, struct genl_info *info);
static int tgd_nlsdn_set_gps_pos(struct sk_buff *skb2, struct genl_info *info);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 2, 0)
#define TGDOP_CMD_DEFAULT(_cmd, _cb)                                           \
	{                                                                      \
		.cmd = _cmd, .doit = _cb, .dumpit = NULL, .done = NULL,        \
		.validate =                                                    \
		    GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP        \
	}
#define TGDOP_NOTIFY_DEFAULT(_cmd)                                             \
	{                                                                      \
		.cmd = _cmd, .doit = NULL, .dumpit = TGDOP_NOTIFY_CB_DEFAULT,  \
		.done = NULL,                                                  \
		.validate =                                                    \
		    GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP        \
	}
#else
#define TGDOP_CMD(_cmd, _cb, _policy)                                          \
	{                                                                      \
		.cmd = _cmd, .doit = _cb, .dumpit = NULL, .done = NULL,        \
		.policy = _policy,                                             \
	}
#define TGDOP_CMD_DEFAULT(_cmd, _cb) TGDOP_CMD(_cmd, _cb, TGDOP_POLICY_DEFAULT)
#define TGDOP_NOTIFY(_cmd, _cb, _policy)                                       \
	{                                                                      \
		.cmd = _cmd, .doit = NULL, .dumpit = _cb, .done = NULL,        \
		.policy = _policy,                                             \
	}
#define TGDOP_NOTIFY_DEFAULT(cmd)                                              \
	TGDOP_NOTIFY(cmd, TGDOP_NOTIFY_CB_DEFAULT, TGDOP_POLICY_DEFAULT)
#endif
#define TGDOP_NOTIFY_CB_DEFAULT tgd_nlsdn_notify_cb
#define TGDOP_POLICY_DEFAULT tgd_nlsdn_policy

static struct genl_ops tgd_nlsdn_ops[] = {
    TGDOP_NOTIFY_DEFAULT(TGD_NLSDN_CMD_NOTIFY),
    TGDOP_CMD_DEFAULT(TGD_NLSDN_CMD_TGINIT, tgd_nlsdn_tginit),
    TGDOP_NOTIFY_DEFAULT(TGD_NLSDN_CMD_NOTIFY_TGINIT),
    TGDOP_NOTIFY_DEFAULT(TGD_NLSDN_CMD_NOTIFY_DRVR_RSP),
    TGDOP_NOTIFY_DEFAULT(TGD_NLSDN_CMD_NOTIFY_NODECONFIG),
    TGDOP_NOTIFY_DEFAULT(TGD_NLSDN_CMD_NOTIFY_LINK_STATUS),
    TGDOP_NOTIFY_DEFAULT(TGD_NLSDN_CMD_NOTIFY_ASSOC),
    TGDOP_CMD_DEFAULT(TGD_NLSDN_CMD_GRANTALLOC, tgd_nlsdn_grantalloc),
    TGDOP_NOTIFY_DEFAULT(TGD_NLSDN_CMD_NOTIFY_GRANTALLOC),
    TGDOP_CMD_DEFAULT(TGD_NLSDN_CMD_SET_DBGMASK, tgd_nlsdn_set_dbgmask),
    TGDOP_CMD_DEFAULT(TGD_NLSDN_CMD_GET_STATS, tgd_nlsdn_get_stats),
    TGDOP_CMD_DEFAULT(TGD_NLSDN_CMD_PASSTHRU_SB, tgd_nlsdn_send_sb_passthrough),
    TGDOP_CMD_DEFAULT(TGD_NLSDN_CMD_SET_DRVR_CONFIG,
		      tgd_nlsdn_handle_drvr_config),
    TGDOP_CMD_DEFAULT(TGD_NLSDN_CMD_SET_BMFMCONFIG, tgd_nlsdn_set_bmfmconfig),
    TGDOP_NOTIFY_DEFAULT(TGD_NLSDN_CMD_NOTIFY_BMFMCONFIG),
    TGDOP_CMD_DEFAULT(TGD_NLSDN_CMD_DEV_ALLOC, tgd_nlsdn_dev_alloc),
    TGDOP_NOTIFY_DEFAULT(TGD_NLSDN_CMD_NOTIFY_WSEC_STATUS),
    TGDOP_NOTIFY_DEFAULT(TGD_NLSDN_CMD_NOTIFY_WSEC_LINKUP_STATUS),
    TGDOP_NOTIFY_DEFAULT(TGD_NLSDN_CMD_NOTIFY_DEV_UPDOWN_STATUS),
    TGDOP_CMD_DEFAULT(TGD_NLSDN_CMD_SET_GPS_TIME, tgd_nlsdn_set_gps_time),
    TGDOP_CMD_DEFAULT(TGD_NLSDN_CMD_SET_GPS_POS, tgd_nlsdn_set_gps_pos),
};

/* nlsdn multicast groups for daemon to listen for events */
static struct genl_multicast_group tgd_nlsdn_mc_groups[] = {
    {
	.name = TGD_NLSDN_GENL_GROUP_NAME,
    },
};

/* the sdn netlink family */
static struct genl_family tgd_nlsdn_fam = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
    .id = GENL_ID_GENERATE, /* don't bother with a hardcoded ID */
#endif
    .name = TGD_NLSDN_GENL_NAME, //"nlsdn"
    .hdrsize = 0,		 /* no private header */
    .version = 1,		 /* no particular meaning now */
    .maxattr = TGD_NLSDN_ATTR_MAX,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 2, 0)
    .policy = TGDOP_POLICY_DEFAULT,
#endif
    .netnsok = true,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
    .ops = tgd_nlsdn_ops,
    .n_ops = ARRAY_SIZE(tgd_nlsdn_ops),
    .mcgrps = tgd_nlsdn_mc_groups,
    .n_mcgrps = ARRAY_SIZE(tgd_nlsdn_mc_groups),
#endif
};

static struct tgd_terra_driver *tgd_nl_get_fb_drv(struct genl_info *info)
{
	struct nlattr *na;
	uint64_t macaddr;

	na = info->attrs[TGD_NLSDN_ATTR_RADIO_MACADDR];
	if (CHECK_NULL(na, "no macaddr attribute passed\n")) {
		return NULL;
	}
	macaddr = nla_get_u64(na);
	return tgd_find_fb_drv(macaddr);
}

/* create new genl msg */
static int tgd_new_genl_message(struct tgd_nlmsg *msg, int msgsz)
{
	int seq = 0;
	/* alloc the msg itself */
	if (msgsz == 0)
		msgsz = NLMSG_DEFAULT_SIZE;

	if (msg->tgd_info)
		seq = msg->tgd_info->snd_seq + 1;

	msg->tgd_skb = genlmsg_new(msgsz, GFP_KERNEL);
	if (CHECK_NULL(msg->tgd_skb, "failed to allocate skb\n")) {
		return -ENOMEM;
	}
	msg->tgd_msgsz = msgsz;

	/* create genl msg header */
	msg->tgd_msghdr =
	    genlmsg_put(msg->tgd_skb, 0, seq, msg->tgd_family, 0, msg->tgd_cmd);
	if (CHECK_NULL(msg->tgd_msghdr,
		       "failed to allocate genl message header\n")) {
		nlmsg_free(msg->tgd_skb);
		return -ENOMEM;
	}
	return 0;
}

static int tgd_send_genl_message(struct tgd_nlmsg *msg,
				 struct tgd_terra_driver *fb_drv)
{
	int err = 0;

	err = tgd_nla_put_u64(msg->tgd_skb, TGD_NLSDN_ATTR_RADIO_MACADDR,
			      fb_drv->macaddr);
	if (err) {
		TGD_DBG_CTRL_ERROR("failed to add attribute, err %d\n", err);
		goto nla_put_failure;
	}
	genlmsg_end(msg->tgd_skb, msg->tgd_msghdr);
	NL_STATS_INC(fb_drv, NL_MSG_SEND);

	/* if info assume we're in unicast context */
	if (msg->tgd_info) {
		err = genlmsg_unicast(genl_info_net(msg->tgd_info),
				      msg->tgd_skb, msg->tgd_info->snd_portid);
	} else {
		TGD_DBG_CTRL_INFO("tgd_send_genl_message : msg %p\n",
				  msg->tgd_skb);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
		err = genlmsg_multicast_allns(&tgd_nlsdn_fam, msg->tgd_skb,
					      0 /* no port id */,
					      TGD_NLSDN_GROUP, GFP_ATOMIC);
#else
		err = genlmsg_multicast_allns(
		    msg->tgd_skb, 0 /* no port id */,
		    tgd_nlsdn_mc_groups[TGD_NLSDN_GROUP].id, GFP_ATOMIC);
#endif
	}
	if (err == -ESRCH) {
		TGD_DBG_CTRL_INFO("no one subscribed at the moment\n");
		err = 0;
	}
	goto done;

nla_put_failure:
	nlmsg_free(msg->tgd_skb);
done:
	if (CHECK_RET(err, "failed to send message, err %d\n", err)) {
		NL_STATS_INC(fb_drv, NL_MSG_SEND_ERR);
	}
	return err;
}

/* TGD_NLSDN_CMD_NOTIFY */
static int tgd_nlsdn_notify_cb(struct sk_buff *skb, struct netlink_callback *cb)
{
	TGDPRINT("notify handler called\n");
	return 0;
}

/****************************************************************************
 * Invoked from tgd_fw_msg_handler, the handler for FW event workQ
 ****************************************************************************/
void tgd_nlsdn_trigger_notify(int msecs, int cmd, void *event_data,
			      unsigned long event_data_size,
			      tgd_nlmsg_cb_t cb_fn,
			      struct tgd_terra_driver *fb_drv)
{
	struct tgd_nlmsg msg = TGD_NLMSG_INIT(TGD_NLSDN_CMD_UNSPEC, NULL);
	int err = 0;

	/* create a message */
	msg.tgd_cmd = cmd;
	if ((err = tgd_new_genl_message(&msg, 0))) {
		return;
	}

	/* exec callback to pack attrs */
	if (cb_fn) {
		if ((err = cb_fn(&msg, event_data, event_data_size))) {
			TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n",
					 err);
			goto nla_put_failure;
		}
	}

	/* end the message */
	tgd_send_genl_message(&msg, fb_drv);
	return;

nla_put_failure:
	nlmsg_free(msg.tgd_skb);
	return;
}

/*************************************************************************
 * Variable data pointer through the function return value
 * Valiable data length information through the data_len_p pointer
 *************************************************************************/
unsigned char *tgd_get_nl_var_data(struct genl_info *info, int *data_len_p,
				   int var_max_len)
{
	struct nlattr *na;
	int var_data_len = 0;
	unsigned char *nl_var_data_ptr;

	*data_len_p = 0;
	na = info->attrs[TGD_NLSDN_ATTR_VARDATA];
	if (na == NULL) {
		return NULL;
	}
	var_data_len = nla_len(na);
	if ((var_data_len <= 0) || (var_data_len > var_max_len)) {
		TGD_DBG_CTRL_ERROR("ERROR: VarDatLen: %d\n", var_data_len);
		return NULL;
	}

	nl_var_data_ptr = (unsigned char *)nla_data(na);
	if (nl_var_data_ptr == NULL) {
		TGD_DBG_CTRL_ERROR("ERROR: NULL nl_var_data_ptr\n");
		return NULL;
	}
	*data_len_p = var_data_len;

	return nl_var_data_ptr;
}

// DN
// FW init rq
// return mac add / success (timeout 1s)
// return gps sync update
// bubble up mac and status to sdn controller (stub)
// Mode config (dn / cn, tdd config)
// returns success (timeout 1s)
// Start assoc (mac add)
// return success, mac
// return source mac, dest mac, link up, conn details (timeout 10s)
// Grant allocation (child mac, grant config)

static int tgd_nlsdn_tginit(struct sk_buff *skb2, struct genl_info *info)
{
	int err = 0;
	int ret = 0;
	int nl_var_length;
	unsigned char *nl_var_data;
	struct tgd_nlmsg msg = TGD_NLMSG_INIT(TGD_NLSDN_CMD_TGINIT, info);
	struct tgd_terra_driver *fb_drv;

	/* Note Right now portmacaddress is not filled. So we just
	 * go with the first fb_drv returned by tgd_find_fb_drv
	 */
	fb_drv = tgd_nl_get_fb_drv(info);
	if (!fb_drv) {
		TGD_DBG_CTRL_ERROR("tgd_terra_driver not found\n");
		return 1;
	}

	NL_CMD_STATS_INC(fb_drv, NL_CMD_TGINIT);
	nl_var_data =
	    tgd_get_nl_var_data(info, &nl_var_length, MAX_VAR_DATA_LEN);

	/*
	 * Sends TG_SB_INIT_REQ, returns ioctl response code, error not handled
	 */
	ret = tgd_send_fw_init(fb_drv, nl_var_length, nl_var_data);

	if ((err = tgd_new_genl_message(&msg, NLMSG_DEFAULT_SIZE))) {
		return err;
	}

	if ((err = nla_put_u8(msg.tgd_skb, TGD_NLSDN_ATTR_SUCCESS, ret))) {
		TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n", err);
		goto nla_put_failure;
	}

	tgd_send_genl_message(&msg, fb_drv);
	return 0;

nla_put_failure:
	nlmsg_free(msg.tgd_skb);
	return err;
}

/***************************************************************************/
int tgd_nlsdn_tginit_msg(struct tgd_nlmsg *msg, void *event_data, int len)
{
	fwInitRsp *fw_init_rsp;
	uint64_t macaddr;
	int err = 0;

	fw_init_rsp = (fwInitRsp *)event_data;
	TGD_DBG_CTRL_INFO("FW Init: %pM\n", fw_init_rsp->macAddr.addr);

	macaddr = TGD_CONVERT_MACADDR_TO_LONG(fw_init_rsp->macAddr);
	if ((err = tgd_nla_put_u64(msg->tgd_skb, TGD_NLSDN_ATTR_MACADDR,
				   macaddr)) ||
	    (err = nla_put_u8(msg->tgd_skb, TGD_NLSDN_ATTR_SUCCESS,
			      fw_init_rsp->errCode)) ||
	    (err = nla_put(msg->tgd_skb, TGD_NLSDN_ATTR_VARDATA,
			   sizeof(fw_init_rsp->vendorStr),
			   fw_init_rsp->vendorStr))) {
		TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n", err);
		goto nla_put_failure;
	}
	return 0;

nla_put_failure:
	// msg clean up in caller
	return err;
}

int tgd_nlsdn_set_bmfmconfig(struct sk_buff *skb2, struct genl_info *info)
{
	int err = 0;
	int ret = 0;
	int bf_role;
	int nl_var_length;
	unsigned char *nl_var_data;
	struct nlattr *na;
	uint64_t macaddr;
	tgEthAddr ethaddr;
	struct tgd_nlmsg msg =
	    TGD_NLMSG_INIT(TGD_NLSDN_CMD_SET_BMFMCONFIG, info);
	struct tgd_terra_driver *fb_drv;

	fb_drv = tgd_nl_get_fb_drv(info);
	if (!fb_drv) {
		TGD_DBG_CTRL_ERROR("tgd_terra_driver not found\n");
		return 1;
	}
	NL_CMD_STATS_INC(fb_drv, NL_CMD_SET_BMFMCONFIG);
	/* get beamform role from user */
	na = info->attrs[TGD_NLSDN_ATTR_BMFMROLE];
	if (CHECK_NULL(na, "no bf_role attribute passed\n")) {
		return 1; // check
	}
	bf_role = nla_get_u32(na);

	na = info->attrs[TGD_NLSDN_ATTR_MACADDR];
	if (CHECK_NULL(na, "no macaddr attribute passed\n")) {
		return 1; // check
	}
	macaddr = nla_get_u64(na);
	TGD_CONVERT_LONG_TO_MACADDR(macaddr, ethaddr);

	nl_var_data =
	    tgd_get_nl_var_data(info, &nl_var_length, MAX_VAR_DATA_LEN);
	TGD_DBG_CTRL_INFO(
	    "tgd_nlsdn_set_bmfmconfig bf_role:%d  varDataLen:%d\n", bf_role,
	    nl_var_length);
	/* ensure bf_role is valid (initiator or responder) */
	//--- Have separate hdr for APP-Driver and Driver-FW
	if (bf_role == TGD_NLSDN_BMFM_INIT) {
		ret =
		    tgd_send_bmfm_cfg_req(fb_drv, &ethaddr, eBF_ROLE_INITIATOR,
					  nl_var_length, nl_var_data);
	} else if (bf_role == eBF_ROLE_RESPONDER) {
		ret =
		    tgd_send_bmfm_cfg_req(fb_drv, &ethaddr, eBF_ROLE_RESPONDER,
					  nl_var_length, nl_var_data);
	} else {
		TGD_DBG_CTRL_ERROR("bf_role neither init nor resp\n");
		return -1;
	}
	if ((err = tgd_new_genl_message(&msg, NLMSG_DEFAULT_SIZE))) {
		return err;
	}
	if ((err = nla_put_u8(msg.tgd_skb, TGD_NLSDN_ATTR_SUCCESS, ret))) {
		TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n", err);
		goto nla_put_failure;
	}

	tgd_send_genl_message(&msg, fb_drv);
	return 0;

nla_put_failure:
	nlmsg_free(msg.tgd_skb);
	return -EMSGSIZE;
}

int tgd_nlsdn_bmfmconfig_msg(struct tgd_nlmsg *msg, void *event_data, int len)
{
	int err = 0;
	sTgFwStartBfAcqRsp *fw_bmfm_cfg_rsp;
	fw_bmfm_cfg_rsp = (sTgFwStartBfAcqRsp *)event_data;

	if ((err = nla_put_u8(msg->tgd_skb, TGD_NLSDN_ATTR_SUCCESS,
			      fw_bmfm_cfg_rsp->errCode))) {
		TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n", err);
		goto nla_put_failure;
	}
	return 0;

nla_put_failure:
	// msg clean up in caller
	return err;
}

static int tgd_nlsdn_set_dbgmask(struct sk_buff *skb2, struct genl_info *info)
{
	int err = 0;
	unsigned int dbg_mask, curr_mask;
	struct nlattr *na;
	struct tgd_nlmsg msg = TGD_NLMSG_INIT(TGD_NLSDN_CMD_SET_DBGMASK, info);
	struct tgd_terra_driver *fb_drv;

	fb_drv = tgd_nl_get_fb_drv(info);
	if (!fb_drv) {
		TGD_DBG_CTRL_ERROR("tgd_terra_driver not found\n");
		return 1;
	}
	NL_CMD_STATS_INC(fb_drv, NL_CMD_SET_DBGMASK);
	na = info->attrs[TGD_NLSDN_ATTR_DBGMASK];
	if (CHECK_NULL(na, "no dbgmask attribute passed\n")) {
		return 1; // check
	}
	dbg_mask = nla_get_u32(na);

	if (dbg_mask & ~TGC_VALID_DBG_MASK_BITS) {
		TGD_DBG_CTRL_DBG(
		    "Ignoring given dbg_mask, reading current value\n");
		curr_mask = set_debug_mask(0xFFFFFFFF);
	} else {
		TGD_DBG_CTRL_DBG("@@@@@@@@@@@@@@@@@@@ New DbgMask:0x%x\n",
				 dbg_mask);
		curr_mask = set_debug_mask(dbg_mask);
	}

	if ((err = tgd_new_genl_message(&msg, NLMSG_DEFAULT_SIZE))) {
		return err;
	}
	if ((err =
		 nla_put_u32(msg.tgd_skb, TGD_NLSDN_ATTR_DBGMASK, curr_mask)) ||
	    (err = nla_put_u8(msg.tgd_skb, TGD_NLSDN_ATTR_SUCCESS,
			      err))) { // check, why err
		TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n", err);
		goto nla_put_failure;
	}

	tgd_send_genl_message(&msg, fb_drv);
	return 0;

nla_put_failure:
	nlmsg_free(msg.tgd_skb);
	return err;
}

int tgd_nlsdn_linkup_status_msg(struct tgd_nlmsg *msg, void *event_data,
				int len)
{
	int err = 0;
	fb_tgd_link_status_t *fw_link_status;
	uint64_t macaddr;

	fw_link_status = (fb_tgd_link_status_t *)event_data;

	if (strlen(fw_link_status->ifname) > 0) {
		if ((err = nla_put(msg->tgd_skb, TGD_NLSDN_ATTR_VARDATA,
				   strlen(fw_link_status->ifname) + 1,
				   fw_link_status->ifname))) {
			TGD_DBG_CTRL_DBG(
			    "failed to add attribute intf name, err %d\n", err);
			goto nla_put_failure;
		}
	}

	macaddr = TGD_CONVERT_MACADDR_TO_LONG(fw_link_status->linkStaAddr);
	if ((err = tgd_nla_put_u64(msg->tgd_skb, TGD_NLSDN_ATTR_MACADDR,
				   macaddr)) ||
	    (err = nla_put_u8(msg->tgd_skb, TGD_NLSDN_ATTR_LINK_STATUS,
			      fw_link_status->linkStatus)) ||
	    (err = nla_put_u32(msg->tgd_skb, TGD_NLSDN_ATTR_LINK_DOWN_CAUSE,
			       fw_link_status->linkFailureCause)) ||
	    (err = nla_put_u8(msg->tgd_skb, TGD_NLSDN_ATTR_SELF_NODE_TYPE,
			      fw_link_status->linkStaNodeType)) ||
	    (err = nla_put_u8(msg->tgd_skb, TGD_NLSDN_ATTR_PEER_NODE_TYPE,
			      fw_link_status->peerNodeType))) {
		TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n", err);
		goto nla_put_failure;
	}
	return 0;

nla_put_failure:
	// msg clean up in caller
	return err;
}

int tgd_nlsdn_wsec_status_msg(struct tgd_nlmsg *msg, void *event_data, int len)
{
	int err = 0;
	fb_tgd_link_wsec_status_t *fw_wsec_status;

	fw_wsec_status = (fb_tgd_link_wsec_status_t *)event_data;

	if (strlen(fw_wsec_status->ifname) > 0) {
		if ((err = nla_put(msg->tgd_skb, TGD_NLSDN_ATTR_VARDATA,
				   strlen(fw_wsec_status->ifname) + 1,
				   fw_wsec_status->ifname))) {
			TGD_DBG_CTRL_DBG(
			    "failed to add attribute intf name, err %d\n", err);
			goto nla_put_failure;
		}
	}

	if ((err = nla_put_u8(msg->tgd_skb, TGD_NLSDN_ATTR_WSEC_STATUS,
			      fw_wsec_status->status))) {
		TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n", err);
		goto nla_put_failure;
	}
	return 0;

nla_put_failure:
	// msg clean up in caller
	return err;
}

int tgd_nlsdn_stats_passthrough(struct tgd_nlmsg *msg, void *event_data,
				int len_passthrough)
{
	int err = 0;
	TGD_DBG_CTRL_INFO("PasThr %d bytes givingToNetlink\n", len_passthrough);
	if (len_passthrough)
		if ((err = nla_put(msg->tgd_skb, TGD_NLSDN_ATTR_VARDATA,
				   len_passthrough, event_data))) {
			TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n",
					 err);
			goto nla_put_failure;
		}
	return 0;

nla_put_failure:
	// msg clean up in caller
	return err;
}

static int tgd_nlsdn_grantalloc(struct sk_buff *skb2, struct genl_info *info)
{
	struct tgd_terra_driver *fb_drv;

	fb_drv = tgd_nl_get_fb_drv(info);
	if (!fb_drv) {
		TGD_DBG_CTRL_ERROR("tgd_terra_driver not found\n");
		return 1;
	}
	NL_CMD_STATS_INC(fb_drv, NL_CMD_GRANTALLOC);
	/* parse grant config from user */
	/* run ioctl */
	/* return status */
	/* mark time timeout response? */
	return 0;
}

/* init module */
int tgd_nlsdn_init(void)
{
	int err;

	/* register genl netlink family */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	err = genl_register_family(&tgd_nlsdn_fam);

	if (CHECK_RET(err, "TGD: Failed to register netlink family, err %d\n",
		      err)) {
		return err;
	}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	err = genl_register_family_with_ops_groups(
	    &tgd_nlsdn_fam, tgd_nlsdn_ops, tgd_nlsdn_mc_groups);

	if (CHECK_RET(err, "TGD: Failed to register netlink family, err %d\n",
		      err)) {
		return err;
	}
#else
	err = genl_register_family_with_ops(&tgd_nlsdn_fam, tgd_nlsdn_ops,
					    ARRAY_SIZE(tgd_nlsdn_ops));
	if (CHECK_RET(err, "TGD: Failed to register netlink family, err %d\n",
		      err)) {
		return err;
	}

	err = genl_register_mc_group(&tgd_nlsdn_fam, &tgd_nlsdn_mc_groups[0]);
	if (CHECK_RET(err, "TGD: Failed to register MC family group, err %d\n",
		      err)) {
		genl_unregister_family(&tgd_nlsdn_fam);
		return err;
	}
#endif

	TGDPRINT("registered netlink family %s\n", TGD_NLSDN_GENL_NAME);

	/* init global data structure */
	tgd_nlsdn_global_config.tgd_version = TGD_NLSDN_VERSION;

	return 0;
};

/* exit module */
void tgd_nlsdn_exit(void)
{
	genl_unregister_family(&tgd_nlsdn_fam);
	pr_info("TGD: Unregistered %s genl family\n", TGD_NLSDN_GENL_NAME);
}

static int tgd_nlsdn_get_stats(struct sk_buff *skb2, struct genl_info *info)
{
	tgd_stats_t *stat_data;
	struct tgd_terra_driver *tgd_g_data_ptr;
	int link_count;
	int dat_buf_size;
	int ret_link_count = 0;
	int ret_len;
	struct tgd_nlmsg msg = TGD_NLMSG_INIT(TGD_NLSDN_CMD_GET_STATS, info);
	int err = 0;

	tgd_g_data_ptr = tgd_nl_get_fb_drv(info);
	if (!tgd_g_data_ptr) {
		TGD_DBG_CTRL_ERROR("tgd_terra_driver not found\n");
		return 1;
	}
	NL_CMD_STATS_INC(tgd_g_data_ptr, NL_CMD_GET_STATS);
	link_count = tgd_g_data_ptr->max_link_count;

	dat_buf_size = (sizeof(fb_tgd_link_stats_t) * link_count);
	stat_data = (tgd_stats_t *)kmalloc(dat_buf_size + sizeof(tgd_stats_t),
					   GFP_KERNEL);
	if (CHECK_NULL(stat_data, "failed to kmalloc\n")) {
		return -ENOMEM;
	}
	stat_data->num_links = 0;
	if (link_count > 0) {
		ret_len = tgd_get_stats(tgd_g_data_ptr, stat_data->link_stat,
					dat_buf_size, &ret_link_count);
		TGD_DBG_CTRL_INFO(
		    "(ReqLinkCount:%d) retLinkCount: %d ret_size: %d\n",
		    tgd_g_data_ptr->link_count, ret_link_count, ret_len);
		stat_data->num_links = ret_link_count;
	} else {
		TGD_DBG_CTRL_INFO(
		    "link_count: %d (Invalid or No data to return)\n",
		    stat_data->num_links);
		ret_len = 0;
	}

	// check, do we still need to send nlmsg when ret_len is 0

	if ((err = tgd_new_genl_message(&msg, NLMSG_DEFAULT_SIZE))) {
		kfree(stat_data);
		return err;
	}
	if ((err = nla_put(msg.tgd_skb, TGD_NLSDN_ATTR_STATS,
			   ret_len + sizeof(tgd_stats_t), stat_data)) ||
	    (err = nla_put_u8(msg.tgd_skb, TGD_NLSDN_ATTR_SUCCESS,
			      err))) { // check, what value of err is needed.
		kfree(stat_data);
		TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n", err);
		goto nla_put_failure;
	}
	tgd_send_genl_message(&msg, tgd_g_data_ptr);
	kfree(stat_data);

	if (ret_len > 0) {
		return 0;
	} else {
		return -1;
	}

nla_put_failure:
	nlmsg_free(msg.tgd_skb);
	return err;
}

/*****************************************************************************/
static int tgd_nlsdn_send_sb_passthrough(struct sk_buff *skb2,
					 struct genl_info *info)
{
	unsigned char *nl_var_data;
	int nl_var_length;
	int err = 0;
	int ret = 1;
	struct tgd_nlmsg msg = TGD_NLMSG_INIT(TGD_NLSDN_CMD_PASSTHRU_SB, info);
	struct nlattr *na;
	struct tgd_terra_driver *fb_drv;

	fb_drv = tgd_nl_get_fb_drv(info);
	if (!fb_drv) {
		TGD_DBG_CTRL_ERROR("tgd_terra_driver not found\n");
		return 1;
	}
	NL_CMD_STATS_INC(fb_drv, NL_CMD_PASSTHRU_SB);
	TGD_DBG_CTRL_INFO("In tgd_nlsdn_send_sb_passthrough\n");

	nl_var_data =
	    tgd_get_nl_var_data(info, &nl_var_length, SB_PASSTHROUGH_MAX);
	if ((nl_var_length) && (nl_var_data)) {
		err = tgd_send_passthrough_to_fw(fb_drv, nl_var_data,
						 nl_var_length);
		ret = (err == PASSTHRU_SUCCESS_CODE) ? 0 : 1;
	}

	/* Allow passthrough sender to suppress acks */
	na = info->attrs[TGD_NLSDN_ATTR_PASSTHRU_NOACK];
	if (na != NULL && nla_get_u8(na) != 0) {
		return 0;
	}

	if ((err = tgd_new_genl_message(&msg, NLMSG_DEFAULT_SIZE))) {
		return err;
	}
	if ((err = nla_put_u8(msg.tgd_skb, TGD_NLSDN_ATTR_SUCCESS, ret))) {
		TGD_DBG_CTRL_DBG("failed to add ret code, err %d\n", err);
		goto nla_put_failure;
	}

	/* extract the subtype field for passthru and attach in response . */
	na = info->attrs[TGD_NLSDN_ATTR_PASSTHRU_TYPE];
	if (na != NULL) {
		if ((err = nla_put_u8(msg.tgd_skb, TGD_NLSDN_ATTR_PASSTHRU_TYPE,
				      nla_get_u8(na)))) {
			TGD_DBG_CTRL_DBG("failed to attach subtype, err %d\n",
					 err);
			goto nla_put_failure;
		}
	}

	tgd_send_genl_message(&msg, fb_drv);
	return 0;

nla_put_failure:
	nlmsg_free(msg.tgd_skb);
	return err;
}

/*****************************************************************************/
static int tgd_nlsdn_handle_drvr_config(struct sk_buff *skb2,
					struct genl_info *info)
{
	int nl_var_length;
	unsigned char *nl_var_data;
	unsigned char *drv_cfg_data_ptr;
	struct tgd_terra_driver *fb_drv;

	fb_drv = tgd_nl_get_fb_drv(info);
	if (!fb_drv) {
		TGD_DBG_CTRL_ERROR("tgd_terra_driver not found\n");
		return 1;
	}
	NL_CMD_STATS_INC(fb_drv, NL_CMD_SET_DRVR_CONFIG);
	TGD_DBG_CTRL_INFO("In tgd_nlsdn_handle_drvr_config\n");

	nl_var_data =
	    tgd_get_nl_var_data(info, &nl_var_length, SB_PASSTHROUGH_MAX);
	if ((nl_var_data == NULL) || (nl_var_length <= 0)) {
		TGD_DBG_CTRL_INFO(
		    "tgd_get_nl_var_data Failed return length/dataP\n");
		return -1;
	}

	drv_cfg_data_ptr = (unsigned char *)kmalloc(nl_var_length, GFP_KERNEL);
	if (drv_cfg_data_ptr) {
		memcpy(drv_cfg_data_ptr, nl_var_data, nl_var_length);
	}

	if (drv_cfg_data_ptr) {
		tgd_handle_drv_cfg_data(fb_drv, drv_cfg_data_ptr, nl_var_length,
					info);
		kfree(drv_cfg_data_ptr);
	}

	return 0;
}

/*****************************************************************************/
static int tgd_nlsdn_dev_alloc(struct sk_buff *skb2, struct genl_info *info)
{
	struct tgd_terra_dev_priv *dev_priv;
	struct tgd_terra_driver *fb_drv;
	struct tgd_nlmsg msg =
	    TGD_NLMSG_INIT(TGD_NLSDN_CMD_DEV_ALLOC_RSP, info);
	const char *ifname;
	tgEthAddr ethaddr;
	struct nlattr *na;
	uint64_t macaddr;
	int err = 0;

	fb_drv = tgd_nl_get_fb_drv(info);
	if (!fb_drv) {
		TGD_DBG_CTRL_ERROR("tgd_terra_driver not found\n");
		return 1;
	}
	NL_CMD_STATS_INC(fb_drv, NL_CMD_DEV_ALLOC);
	TGD_DBG_CTRL_INFO("In tgd_nlsdn_handle_dev_alloc\n");

	na = info->attrs[TGD_NLSDN_ATTR_MACADDR];
	if (CHECK_NULL(na, "no mac address passed\n")) {
		return 1;
	}
	macaddr = nla_get_u64(na);
	TGD_CONVERT_LONG_TO_MACADDR(macaddr, ethaddr);

	/* Try to locate vacant device */
	dev_priv = tgd_terra_dev_reserve(fb_drv, &ethaddr);
	if (CHECK_NULL(dev_priv, "failed to reserve interface\n")) {
		return -EBUSY;
	}

	/* Prepare the response */
	if ((err = tgd_new_genl_message(&msg, NLMSG_DEFAULT_SIZE))) {
		return err;
	}

	ifname = netdev_name(dev_priv->dev);
	if ((err = tgd_nla_put_u64(msg.tgd_skb, TGD_NLSDN_ATTR_MACADDR,
				   macaddr)) ||
	    (err = nla_put_u32(msg.tgd_skb, TGD_NLSDN_ATTR_IFINDEX,
			       dev_priv->dev->ifindex)) ||
	    (err = nla_put(msg.tgd_skb, TGD_NLSDN_ATTR_VARDATA,
			   strlen(ifname) + 1, ifname)) ||
	    (err = nla_put_u8(msg.tgd_skb, TGD_NLSDN_ATTR_SUCCESS,
			      1))) { // check, why 1 is expected and not 0.
		TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n", err);
		goto nla_put_failure;
	}
	tgd_send_genl_message(&msg, fb_drv);
	return 0;

nla_put_failure:
	nlmsg_free(msg.tgd_skb);
	return err;
}

/*****************************************************************************/
static int tgd_nlsdn_set_gps_time(struct sk_buff *skb2, struct genl_info *info)
{
	struct tgd_terra_driver *fb_drv;
	struct nlattr *na;
	struct timespec ts;

	fb_drv = tgd_nl_get_fb_drv(info);
	if (!fb_drv) {
		TGD_DBG_CTRL_ERROR("tgd_terra_driver not found\n");
		return 1;
	}

	memset(&ts, 0x0, sizeof(ts));

	// Extract the received gps time
	na = info->attrs[TGD_NLSDN_ATTR_GPS_TIME_S];
	if (CHECK_NULL(na, "gps time (s) is missing\n")) {
		return 1;
	}
	ts.tv_sec = nla_get_u64(na);
	na = info->attrs[TGD_NLSDN_ATTR_GPS_TIME_NS];
	if (CHECK_NULL(na, "gps time (ns) is missing\n")) {
		return 1;
	}
	ts.tv_nsec = nla_get_u64(na);
	TGD_DBG_CTRL_INFO("set_gps_time %ld.%09ld\n", ts.tv_sec, ts.tv_nsec);

	// Forward the gps time to the f/w via an MS9/M33 compatible api/ioctl
	//
	// FIXME: Forward the gps time only when gps is enabled.
	// Ideas: (1) introduce another netlink message similar to
	// TGF_PT_SB_GPS_ENABLE (2) tgd_gps_time_update() may work if its
	// private 'send_to_fw' flag gets updated properly when the gps driver
	// implementation is missing.
	//
	tgd_send_gps_time(fb_drv, &ts);

	return 0;
}

/*****************************************************************************/
static int tgd_nlsdn_set_gps_pos(struct sk_buff *skb2, struct genl_info *info)
{
	int nl_var_length;
	unsigned char *nl_var_data;
	struct tgd_terra_driver *fb_drv;
	struct t_gps_self_pos *gps_self_pos;

	fb_drv = tgd_nl_get_fb_drv(info);
	if (!fb_drv) {
		TGD_DBG_CTRL_ERROR("tgd_terra_driver not found\n");
		return 1;
	}
	TGD_DBG_CTRL_INFO("In tgd_nlsdn_set_gps_pos\n");

	nl_var_data =
	    tgd_get_nl_var_data(info, &nl_var_length, SB_PASSTHROUGH_MAX);
	if ((nl_var_data == NULL) || (nl_var_length <= 0)) {
		TGD_DBG_CTRL_INFO(
		    "tgd_get_nl_var_data Failed return length/dataP\n");
		return -1;
	}

	gps_self_pos = (struct t_gps_self_pos *)nl_var_data;
	TGD_DBG_CTRL_INFO("set_gps_pos lat=%d lon=%d alt=%d acc=%d\n",
			  gps_self_pos->latitude, gps_self_pos->longitude,
			  gps_self_pos->height, gps_self_pos->accuracy);
	tgd_send_gps_pos(fb_drv, gps_self_pos->latitude,
			 gps_self_pos->longitude, gps_self_pos->height,
			 gps_self_pos->accuracy);

	return 0;
}

/*************************************************************************/
#define GPS_RSP_MAX_SIZE 512

static int tgd_handle_drv_cfg_data(struct tgd_terra_driver *fb_drv,
				   unsigned char *data_ptr, int data_len,
				   struct genl_info *info)
{
	int err = 0;
	int ret = 0;
	int type = 0;
	int version_info[2];
	struct tgd_nlmsg msg =
	    TGD_NLMSG_INIT(TGD_NLSDN_CMD_NOTIFY_DRVR_RSP, NULL);
	const char err_msg[] = "Unknown Driver Config Command";
	unsigned char *gps_rsp_buf;
	int ret_len;

	TGD_DBG_CTRL_INFO(KERN_WARNING "In handle_drv_cfg_data\n");
	if ((err = tgd_new_genl_message(&msg, NLMSG_DEFAULT_SIZE))) {
		return err;
	}

	if (data_len < DRIVER_CFG_HDR_SIZE) {
		ret = -1;
		type = 0;
	} else { // First 2 bytes -> dataType, used only for driver/gps test
		type = data_ptr[0];
		type = (type & 0xFF) + ((data_ptr[1] << 8) & 0xFF00);
	}
	if (type == DRVR_CFG_CMD_ECHO) { // Just echo back
		if ((err = nla_put(msg.tgd_skb, TGD_NLSDN_ATTR_VARDATA,
				   data_len, data_ptr))) {
			TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n",
					 err);
			goto nla_put_failure;
		}
	} else if (type == DRVR_CFG_CMD_VER) { // send version info
		fb_tgd_bh_api_version(fb_drv, &version_info[0],
				      &version_info[1]);
		if ((err = nla_put(msg.tgd_skb, TGD_NLSDN_ATTR_VARDATA,
				   sizeof(version_info), version_info))) {
			TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n",
					 err);
			goto nla_put_failure;
		}
	} else if (type == DRVR_CFG_CMD_GPS) {
		gps_rsp_buf =
		    (unsigned char *)kmalloc(GPS_RSP_MAX_SIZE, GFP_KERNEL);
		if (!gps_rsp_buf) {
			printk(KERN_WARNING
			       "failed to allocate memory for GPS response\n");
			// check, ret should be set, do we still need to send
			// nlmsg?
			goto fn_exit;
		}
		ret_len = tgd_gps_get_nl_rsp(fb_drv, data_ptr, data_len,
					     gps_rsp_buf, GPS_RSP_MAX_SIZE);
		if (ret_len <= 0) {
			kfree(gps_rsp_buf);
			printk(KERN_WARNING "get_gps_nl_rsp Return: %d\n",
			       ret_len);
			ret = -1;
			goto fn_exit;
		}
		if ((err = nla_put(msg.tgd_skb, TGD_NLSDN_ATTR_VARDATA, ret_len,
				   gps_rsp_buf))) {
			kfree(gps_rsp_buf);
			TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n",
					 err);
			goto nla_put_failure;
		}
		kfree(gps_rsp_buf);
	} else {
		if ((err = nla_put(msg.tgd_skb, TGD_NLSDN_ATTR_VARDATA,
				   sizeof(err_msg), err_msg))) {
			TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n",
					 err);
			goto nla_put_failure;
		}
	}

fn_exit:
	if ((err = nla_put_u8(msg.tgd_skb, TGD_NLSDN_ATTR_SUCCESS, ret))) {
		TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n", err);
		goto nla_put_failure;
	}
	tgd_send_genl_message(&msg, fb_drv);
	return 0;

nla_put_failure:
	nlmsg_free(msg.tgd_skb);
	return err;
}

/******************************************************************
 * push the GPS related stats to the NB
 * TGD_NLSDN_CMD_DRVRSTAT_NB
 ******************************************************************/
int tgd_nlsdn_push_gps_stat_nb(struct tgd_terra_driver *fb_drv,
			       unsigned char *gps_rsp_buf, int len)
{
	struct tgd_nlmsg msg = TGD_NLMSG_INIT(TGD_NLSDN_CMD_DRVRSTAT_NB, NULL);
	int err = 0;

	if ((len <= 0) || (!gps_rsp_buf)) {
		printk("Error: tgd_nlsdn_push_gps_stat_nb: len: %d\n", len);
		return -1;
	}
	if ((err = tgd_new_genl_message(&msg, 0))) {
		return err;
	}

	if ((err = nla_put(msg.tgd_skb, TGD_NLSDN_ATTR_VARDATA, len,
			   gps_rsp_buf))) {
		TGD_DBG_CTRL_DBG("failed to add attribute, err %d\n", err);
		goto nla_put_failure;
	}

	tgd_send_genl_message(&msg, fb_drv);
	return 0;

nla_put_failure:
	nlmsg_free(msg.tgd_skb);
	return err;
}

int tgd_nlsdn_send_wsec_linkup_status(struct tgd_terra_driver *fb_drv,
				      unsigned char *wsec_linkup_status_buf,
				      int len)
{
	struct tgd_nlmsg msg =
	    TGD_NLMSG_INIT(TGD_NLSDN_CMD_NOTIFY_WSEC_LINKUP_STATUS, NULL);
	int err = 0;

	if ((len <= 0) || (!wsec_linkup_status_buf)) {
		printk("Error: tgd_nlsdn_send_wsec_linkup_status: len: %d "
		       "buf=%p\n",
		       len, wsec_linkup_status_buf);
		return -1;
	}

	if ((err = tgd_new_genl_message(&msg, 0))) {
		return err;
	}

	if ((err = nla_put(msg.tgd_skb, TGD_NLSDN_ATTR_VARDATA, len,
			   wsec_linkup_status_buf))) {
		TGD_DBG_CTRL_DBG(
		    "failed to add attribute wsec_linkup_status, err %d\n",
		    err);
		goto nla_put_failure;
	}

	tgd_send_genl_message(&msg, fb_drv);
	return 0;

nla_put_failure:
	nlmsg_free(msg.tgd_skb);
	return err;
}

/* Send the Backhaul baseband device's UP/DOWN status to the subscribers of
 * the netlink socket. We can have up to 4 baseband devices, and their
 * associated backhaul drivers (tgd_terra_driver *), and each of these
 * can be independently taken up and down.
 */
int tgd_nlsdn_send_device_updown_status(struct tgd_terra_driver *fb_drv,
					fb_tgd_device_status_t updown_status)
{
	struct tgd_nlmsg msg =
	    TGD_NLMSG_INIT(TGD_NLSDN_CMD_NOTIFY_DEV_UPDOWN_STATUS, NULL);
	int err = 0;

	if ((err = tgd_new_genl_message(&msg, 0))) {
		return err;
	}

	TGD_DBG_CTRL_INFO(
	    "tgd_nlsdn_send_device_updown_status: %d  msg_cmd=%d\n ",
	    updown_status, msg.tgd_cmd);

	if ((err = nla_put_u8(msg.tgd_skb, TGD_NLSDN_ATTR_UPDOWN_STATUS,
			      updown_status))) {
		TGD_DBG_CTRL_DBG(
		    "failed to add attribute device_updown_status=%d, err %d\n",
		    updown_status, err);
		goto nla_put_failure;
	}

	tgd_send_genl_message(&msg, fb_drv);
	return 0;

nla_put_failure:
	nlmsg_free(msg.tgd_skb);
	return err;
}
