/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
/* @lint-ignore-every TXT2 Tab Literal */

#include <net/genetlink.h>

#include <fb_tgd_nlsdn_common.h>

#include "tg_hwsim.h"
#include "tg_hwsim_nl.h"
#include "tg_hwsim_fw.h"

static struct baseband_data *get_baseband_from_nl(struct genl_info *info)
{
	struct nlattr *attr;
	u64 mac_addr;

	attr = info->attrs[TGD_NLSDN_ATTR_RADIO_MACADDR];
	if (attr == NULL) {
		printk(KERN_DEBUG
		       "tg_hwsim: nl msg didn't contain a MAC address");
		return NULL;
	}

	mac_addr = nla_get_u64(attr);

	return get_baseband_from_addr(mac_addr);
}

static struct genl_family tg_hwsim_fam;
static TGD_NLSDN_POLICY_DEFN();

static int nl_get_vardata(struct genl_info *info, unsigned char **var_data,
			  int *var_data_len)
{
	struct nlattr* na;
	int err = 0;

	na = info->attrs[TGD_NLSDN_ATTR_VARDATA];
	if (na == NULL) {
		err = -EINVAL;
		printk(KERN_DEBUG "tg_hwsim: no VARDATA in passthru sb");
		goto out;
 	}

	*var_data_len = nla_len(na);

	*var_data = (unsigned char*)nla_data(na);
	if (*var_data == NULL) {
		err = -EINVAL;
		printk(KERN_DEBUG "tg_hwsim: VARDATA was null");
		goto out;
	}

out:
	return err;
}

int tg_hwsim_notify_wsec_linkup_status(struct terradev_priv_data *terradev)
{
	struct sk_buff *skb;
	void *msg_header;
	int err = 0;

	skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb == NULL) {
		err = -ENOMEM;
		printk(KERN_DEBUG "tg_hwsim: failed to allocate buffer for nl "
		       "wsec linkup status");
		goto out;
	}

	msg_header = genlmsg_put(skb, 0, 0, &tg_hwsim_fam, 0,
				 TGD_NLSDN_CMD_NOTIFY_WSEC_LINKUP_STATUS);
	if (msg_header == NULL) {
		err = -ENOMEM;
		printk(KERN_DEBUG "tg_hwsim: failed to put genl msg header for "
		       "nl wsec linkup status");
		goto out_free;
	}

	if ((err = nla_put(skb, TGD_NLSDN_ATTR_VARDATA,
			   strlen(terradev->netdev->name) + 1,
			   terradev->netdev->name))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to put attribute for nl wsec linkup "
		       "status");
		goto out_free;
	}

	genlmsg_end(skb, msg_header);

	if ((err = genlmsg_multicast_allns(&tg_hwsim_fam, skb, 0,
					   TGD_NLSDN_GROUP, GFP_ATOMIC))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to send nl wsec linkup status notify");
		goto out;
	}
out:
	return err;
out_free:
	nlmsg_free(skb);
	return err;
}

void tg_hwsim_notify_link_status_from_dev(struct terradev_priv_data *terradev,
					  tgLinkFailureCause failure_cause)
{
	struct tg_hwsim_link_status link_status_info;
	link_status_info.ifname = terradev->netdev->name;
	link_status_info.link_addr = terradev->link_sta_addr;
	link_status_info.link_status = terradev->link_status;
	link_status_info.failure_cause = failure_cause;
	/* only support DN */
	link_status_info.node_type = 2;
	link_status_info.peer_type = 2;

	tg_hwsim_notify_link_status(&link_status_info, terradev->baseband);
}


int tg_hwsim_notify_link_status(struct tg_hwsim_link_status *link_status_info,
				struct baseband_data *bb)
{
	struct sk_buff *skb;
	void *msg_header;
	int err = 0;

	skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb == NULL) {
		err = -ENOMEM;
		printk(KERN_DEBUG "tg_hwsim: failed to allocate buffer for nl "
		       "link status");
		goto out;
	}

	msg_header = genlmsg_put(skb, 0, 0, &tg_hwsim_fam, 0,
				 TGD_NLSDN_CMD_NOTIFY_LINK_STATUS);
	if (msg_header == NULL) {
		err = -ENOMEM;
		printk(KERN_DEBUG "tg_hwsim: failed to put genl msg header for "
		       "nl link status");
		goto out_free;
	}

	if ((err = nla_put_u64_64bit(skb, TGD_NLSDN_ATTR_RADIO_MACADDR,
				     bb->mac_addr,
				     TGD_NLSDN_ATTR_PAD))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to put MAC address for nl link "
		       "status");
		goto out_free;
	}

	if ((err = nla_put_u64_64bit(skb, TGD_NLSDN_ATTR_MACADDR,
				     link_status_info->link_addr,
				     TGD_NLSDN_ATTR_PAD))
	 || (err = nla_put(skb, TGD_NLSDN_ATTR_VARDATA,
			   strlen(link_status_info->ifname) + 1,
			   link_status_info->ifname))
	 || (err = nla_put_u8(skb, TGD_NLSDN_ATTR_LINK_STATUS,
			      link_status_info->link_status))
	 || (err = nla_put_u32(skb, TGD_NLSDN_ATTR_LINK_DOWN_CAUSE,
			       link_status_info->failure_cause))
	 || (err = nla_put_u8(skb, TGD_NLSDN_ATTR_SELF_NODE_TYPE,
			      link_status_info->node_type))
	 || (err = nla_put_u8(skb, TGD_NLSDN_ATTR_PEER_NODE_TYPE,
			      link_status_info->peer_type))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to put attribute for nl link status");
		goto out_free;
	}

	genlmsg_end(skb, msg_header);

	if ((err = genlmsg_multicast_allns(&tg_hwsim_fam, skb, 0,
					   TGD_NLSDN_GROUP, GFP_ATOMIC))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to send nl linkstatus notify");
		goto out;
	}
out:
	return err;
out_free:
	nlmsg_free(skb);
	return err;
}

int tg_hwsim_send_nl_nb_passthru(struct baseband_data *bb,
					unsigned char *var_data,
					int var_data_len)
{
	struct sk_buff *skb;
	void *msg_header;
	int err = 0;

	skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb == NULL) {
		err = -ENOMEM;
		printk(KERN_DEBUG "tg_hwsim: failed to allocate buffer for nl "
		       "nb passthru");
		goto out;
	}

	msg_header = genlmsg_put(skb, 0, 0, &tg_hwsim_fam, 0,
				 TGD_NLSDN_CMD_PASSTHRU_NB);
	if (msg_header == NULL) {
		err = -ENOMEM;
		printk(KERN_DEBUG "tg_hwsim: failed to put genl msg header for "
		       "nl nb passthru");
		goto out_free;
	}

	if ((err = nla_put_u64_64bit(skb, TGD_NLSDN_ATTR_RADIO_MACADDR,
				     bb->mac_addr,
				     TGD_NLSDN_ATTR_PAD))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to put MAC address for nl nb "
		       "passthru");
		goto out_free;
	}

	if ((err = nla_put(skb, TGD_NLSDN_ATTR_VARDATA,
			   var_data_len,
			   var_data))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to put vardata for nl nb passthru");
		goto out_free;
	}

	genlmsg_end(skb, msg_header);

	if ((err = genlmsg_multicast_allns(&tg_hwsim_fam, skb, 0,
					   TGD_NLSDN_GROUP, GFP_ATOMIC))) {
		printk(KERN_DEBUG "tg_hwsim: failed to send nl nb passthru");
		goto out;
	}
out:
	return err;
out_free:
	nlmsg_free(skb);
	return err;
}

static int tg_hwsim_send_nl_ack(struct genl_info *info, u8 success)
{
	struct sk_buff *ack_skb;
	struct baseband_data *data;
	struct nlattr *na;
	void *ack_msg_header;
	int err = 0;

	/* don't send ACK if its suppressed by the sender */
	na = info->attrs[TGD_NLSDN_ATTR_PASSTHRU_NOACK];
	if (na != NULL && nla_get_u8(na)) {
		goto out;
	}

	ack_skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (ack_skb == NULL) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to allocated buffer for nl ack");
		err = -ENOMEM;
		goto out;
	}

	ack_msg_header = genlmsg_put(ack_skb, 0, info->snd_seq + 1,
				     &tg_hwsim_fam, 0,
				     info->genlhdr->cmd);
	if (ack_msg_header == NULL) {
		err = -ENOMEM;
		printk(KERN_DEBUG
		       "tg_hwsim: failed to get genl msg header for nl ack");
		goto out_free;
	}

	/* set message subtype for passthru messages */
	na = info->attrs[TGD_NLSDN_ATTR_PASSTHRU_TYPE];
	if (na != NULL) {
		if ((err = nla_put_u8(ack_skb, TGD_NLSDN_ATTR_PASSTHRU_TYPE,
				      nla_get_u8(na)))) {
			printk(KERN_DEBUG
			       "tg_hwsim: failed to put passthru subtype for "
			       "nl ack");
			goto out_free;
		}
	}

	if ((err = nla_put_u8(ack_skb, TGD_NLSDN_ATTR_SUCCESS, success))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to put success attribute for nl ack");
		goto out_free;
	}

	data = get_baseband_from_nl(info);
	if (data == NULL) {
		err = -ENOENT;
		goto out_free;
	}

	if ((err = nla_put_u64_64bit(ack_skb, TGD_NLSDN_ATTR_RADIO_MACADDR,
				     data->mac_addr,
				     TGD_NLSDN_ATTR_PAD))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to put MAC address for nl ack");
		goto out_free;
	}

	genlmsg_end(ack_skb, ack_msg_header);

	if ((err = genlmsg_reply(ack_skb, info))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to send nl ack");
		goto out;
	}
out:
	return err;
out_free:
	nlmsg_free(ack_skb);
	return err;
}

static int handle_gps_get_pos(struct sk_buff *rsp_skb)
{
	struct t_gps_self_pos *pos;
	int err = 0;

	pos = kzalloc(sizeof(struct t_gps_self_pos), GFP_KERNEL);
	if (pos == NULL) {
		err = -ENOMEM;
		goto out;
	}

	pos->cmd = DRVR_CFG_CMD_GPS;
	pos->sub_cmd = GPS_GET_CMD_POS;

	/* Garibaldi Lake, BC, Canada
	 * 49.932731, -123.016348 lat long, 1468 meter altitude */
	pos->ecef_x = -224192200;
	pos->ecef_y = -345010200;
	pos->ecef_z = 485910000;
	pos->accuracy = 1000;

	if ((err = nla_put(rsp_skb, TGD_NLSDN_ATTR_VARDATA,
			   sizeof(struct t_gps_self_pos), pos))) {
		goto out;
	}
out:
	return err;
}

static int tg_hwsim_nl_drvr_config(struct sk_buff *skb, struct genl_info *info)
{
	int err = 0;
	struct sk_buff *rsp_skb;
	void *rsp_header;
	unsigned char *var_data;
	int var_data_len;
	u16 type;

	if ((err = nl_get_vardata(info, &var_data, &var_data_len))) {
		goto out;
	}

	if (var_data_len < DRIVER_CFG_HDR_SIZE) {
		err = -EINVAL;
		goto out;
	}

	rsp_skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (rsp_skb == NULL) {
		printk(KERN_DEBUG "tg_hwsim: failed to allocated buffer for nl "
		       "drvr config response");
		err = -ENOMEM;
		goto out;
	}

	rsp_header = genlmsg_put(rsp_skb, 0, 0, &tg_hwsim_fam, 0,
				 TGD_NLSDN_CMD_NOTIFY_DRVR_RSP);
	if (rsp_header == NULL) {
		err = -ENOMEM;
		printk(KERN_DEBUG "tg_hwsim: failed to get genl msg header for "
		       "nl drvr config response");
		goto out_free;
	}

	/* type is the first two bytes of var_data */
	type = (var_data[1] << 8) | var_data[0];

	if (type == DRVR_CFG_CMD_GPS) {
		if (var_data_len < 3) {
			err = -EINVAL;
			goto out_free;
		}

		type = var_data[2];
		if (type == GPS_GET_CMD_POS) {
			err = handle_gps_get_pos(rsp_skb);
		} else if (type == GPS_SET_CMD_SING_SAT) {
			/* simply send an ACK and pretend we configured GPS */
			err = nla_put(rsp_skb, TGD_NLSDN_ATTR_VARDATA,
				      var_data_len, var_data);
		} else {
			printk(KERN_DEBUG
			       "tg_hwsim: drvr_cfg gps message of subtype %d "
			       "is not supported", type);
			goto out_free;
		}

		if (err) {
			printk(KERN_DEBUG
			       "tg_hwsim: failed to create response for drvr "
			       "config gps command");
			goto out_free;
		}
	} else {
		/* driver-if currently doesn't send any drvr_config message
		 * types other than DRVR_CFG_CMD_GPS */
		printk(KERN_DEBUG
		       "tg_hwsim: drvr_cfg message of type %d is not supported",
		       type);
		goto out_free;
	}

	if ((err = nla_put_u8(rsp_skb, TGD_NLSDN_ATTR_SUCCESS, 0))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to put success attribute for drvr "
		       "config response");
		goto out_free;
	}

	genlmsg_end(rsp_skb, rsp_header);

	if ((err = genlmsg_reply(rsp_skb, info))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to send nl drvr config rsp");
		goto out;
	}

out:
	return err;
out_free:
	nlmsg_free(rsp_skb);
	return err;
}

static int tg_hwsim_nl_tginit(struct sk_buff *skb, struct genl_info *info)
{
	int err = 0;
	struct sk_buff *notify_skb;
	struct baseband_data *data;
	void *notify_msg_header;

	if ((err = tg_hwsim_send_nl_ack(info, 1))) {
		printk(KERN_DEBUG "tg_hwsim: failed to send ACK for tginit");
		goto out;
	}

	notify_skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (notify_skb == NULL) {
		printk(KERN_DEBUG
		       "tg_hwsim: "
		       "failed to allocated buffer for nl tginit notify");
		err = -ENOMEM;
		goto out;
	}

	notify_msg_header = genlmsg_put(notify_skb, 0, 0, &tg_hwsim_fam, 0,
					TGD_NLSDN_CMD_NOTIFY_TGINIT);
	if (notify_msg_header == NULL) {
		err = -ENOMEM;
		printk(KERN_DEBUG
		       "tg_hwsim: failed to get genl msg header for nl tginit "
		       "notify");
		goto out_free;
	}

	data = get_baseband_from_nl(info);
	if (data == NULL) {
		err = -ENOENT;
		goto out_free;
	}

	if ((err = nla_put_u64_64bit(notify_skb, TGD_NLSDN_ATTR_MACADDR,
				     data->mac_addr,
				     TGD_NLSDN_ATTR_PAD))
	 || (err = nla_put_u8(notify_skb, TGD_NLSDN_ATTR_SUCCESS,
			      TG_IOCTL_SUCCESS))
	 || (err = nla_put(notify_skb, TGD_NLSDN_ATTR_VARDATA,
			   sizeof(TG_HWSIM_VENDOR_STR),
			   TG_HWSIM_VENDOR_STR))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to put attribute for nl tginit "
		       "notify");
		goto out_free;
	}

	genlmsg_end(notify_skb, notify_msg_header);

	if ((err = genlmsg_multicast_allns(&tg_hwsim_fam, notify_skb, 0,
					   TGD_NLSDN_GROUP, GFP_ATOMIC))) {
		printk(KERN_DEBUG "tg_hwsim: failed to send nl tginit notify");
		goto out;
	}
out:
	return err;
out_free:
	nlmsg_free(notify_skb);
	return err;
}

static int tg_hwsim_nl_dev_alloc(struct sk_buff *skb, struct genl_info *info)
{
	struct terradev_priv_data *terradev;
	struct baseband_data *bb;
	struct nlattr *na;
	const char *ifname;
	u64 link_addr;
	int err;

	struct sk_buff *rsp_skb;
	void *rsp_header;

	bb = get_baseband_from_nl(info);
	if (bb == NULL) {
		printk(KERN_DEBUG "tg_hwsim: failed to get baseband for "
		       "nl dev_alloc");
		err = -ENOENT;
		goto out;
	}

	na = info->attrs[TGD_NLSDN_ATTR_MACADDR];
	if (na == NULL) {
		printk(KERN_DEBUG "tg_hwsim: no MAC address passed to dev "
		       "alloc");
		err = -EINVAL;
		goto out;
	}
	link_addr = nla_get_u64(na);

	terradev = tg_hwsim_dev_alloc(bb, link_addr);
	if (terradev == NULL) {
		printk(KERN_DEBUG "tg_hwsim: failed to allocate terradev");
		err = -EBUSY;
		goto out;
	}

	rsp_skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (rsp_skb == NULL) {
		printk(KERN_DEBUG
		       "tg_hwsim: "
		       "failed to allocate buffer for nl dev alloc rsp");
		err = -ENOMEM;
		goto out;
	}

	rsp_header = genlmsg_put(rsp_skb, 0, 0, &tg_hwsim_fam, 0,
				 TGD_NLSDN_CMD_DEV_ALLOC_RSP);
	if (rsp_header == NULL) {
		err = -ENOMEM;
		printk(KERN_DEBUG
		       "tg_hwsim: failed to get genl msg header for nl dev "
		       "alloc rsp");
		goto out_free;
	}

	ifname = netdev_name(terradev->netdev);
	if ((err = nla_put_u64_64bit(rsp_skb, TGD_NLSDN_ATTR_MACADDR, link_addr,
				     TGD_NLSDN_ATTR_PAD))
	 || (err = nla_put_u32(rsp_skb, TGD_NLSDN_ATTR_IFINDEX,
			       terradev->netdev->ifindex))
	 || (err = nla_put(rsp_skb, TGD_NLSDN_ATTR_VARDATA,
			   strlen(ifname) + 1, ifname))
	 || (err = nla_put_u8(rsp_skb, TGD_NLSDN_ATTR_SUCCESS, 1))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to add attrs for nl dev alloc rsp");
		goto out_free;
	}

	printk(KERN_DEBUG
	       "tg_hwsim: allocated %s for link with peer MAC address %llx",
	       ifname, link_addr);

	genlmsg_end(rsp_skb, rsp_header);

	if ((err = genlmsg_reply(rsp_skb, info))) {
		printk(KERN_DEBUG "tg_hwsim: failed to send nl dev alloc rsp");
		goto out;
	}
out:
	return err;
out_free:
	nlmsg_free(rsp_skb);
	return err;
}

static int tg_hwsim_nl_passthru_sb(struct sk_buff *skb, struct genl_info *info)
{
	unsigned char* var_data;
	int var_data_len;
	struct baseband_data *bb;
	int err = 0;

	if ((err = nl_get_vardata(info, &var_data, &var_data_len))) {
		goto out;
	}

	if (var_data_len <= 0 || var_data_len > SB_PASSTHRU_MAX) {
		err = -EINVAL;
		printk(KERN_DEBUG "tg_hwsim: invalid VARDATA length");
		goto out;
	}


	bb = get_baseband_from_nl(info);
	if (bb == NULL) {
		err = -EINVAL;
		goto out;
	}

	err = tg_hwsim_handle_fw_msg(bb, var_data, var_data_len);
	if ((err = tg_hwsim_send_nl_ack(info, err))) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to send ACK for sb passthru");
		goto out;
	}

out:
	return err;
}

static struct genl_ops tg_hwsim_nl_ops[] = {
	{
		.cmd	= TGD_NLSDN_CMD_TGINIT,
		.doit	= tg_hwsim_nl_tginit,
		.policy = tgd_nlsdn_policy,
	},
	{
		.cmd	= TGD_NLSDN_CMD_DEV_ALLOC,
		.doit	= tg_hwsim_nl_dev_alloc,
		.policy = tgd_nlsdn_policy,
	},
	{
		.cmd	= TGD_NLSDN_CMD_PASSTHRU_SB,
		.doit	= tg_hwsim_nl_passthru_sb,
		.policy = tgd_nlsdn_policy,
	},
	{
		.cmd	= TGD_NLSDN_CMD_SET_DRVR_CONFIG,
		.doit	= tg_hwsim_nl_drvr_config,
		.policy = tgd_nlsdn_policy,
	},
};

static struct genl_multicast_group tg_hwsim_nl_mcgroups[] = {
	{
		.name = TGD_NLSDN_GENL_GROUP_NAME,
	},
};

static struct genl_family tg_hwsim_fam = {
	.hdrsize	= 0,
	.name		= TGD_NLSDN_GENL_NAME,
	.version	= TGD_NLSDN_VERSION,
	.maxattr	= TGD_NLSDN_ATTR_MAX,
	.netnsok	= true,
	.parallel_ops	= false,
	.ops		= tg_hwsim_nl_ops,
	.n_ops		= ARRAY_SIZE(tg_hwsim_nl_ops),
	.mcgrps		= tg_hwsim_nl_mcgroups,
	.n_mcgrps	= ARRAY_SIZE(tg_hwsim_nl_mcgroups),
};

int __init init_tg_hwsim_netlink(void)
{
	int err;

	err = genl_register_family(&tg_hwsim_fam);
	if (err) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed to register generic netlink family");
		goto out;
	}

	return 0;
out:
	return err;
}

void __exit exit_tg_hwsim_netlink(void)
{
	genl_unregister_family(&tg_hwsim_fam);
}
