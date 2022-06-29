/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Terragraph NSS routing backend */

#define pr_fmt(fmt) "fb_tgd_terragraph: " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/version.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>

// TODO:
// We should include API headers but their API uses reserved keywords
// For now we use their kernel source directly as workaround
//#include <mv_nss_ops.h>
//#include <metadata.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 8)
#include <net/gnss/mv_nss_defs.h>
#include <net/gnss/mv_nss_ops.h>
#include <net/gnss/mv_nss_metadata.h>
#else
#include <net/mvebu/gnss/mv_nss_ops.h>
#include <net/mvebu/gnss/mv_nss_metadata.h>
#endif
#include <mv_sfp_vp.h>
#include <mv_sfp_types.h>
#include <mv_nss.h>

#include <fb_tg_fw_driver_if.h>
#include "fb_tgd_terragraph.h"
#include "fb_tgd_fw_if.h"
#include "fb_tgd_debug.h"
#include "fb_tgd_route.h"
#include "fb_tgd_amsdu.h"
#include "fb_tgd_backhaul.h"

#define TGD_NSS_WLAN_PORT_START MV_NSS_PORT_FWD_MIN     // PortID: 16
#define TGD_NSS_CPU_PORT_START MV_NSS_PORT_CPU_MIN      // PortID: 10
#define TGD_NSS_NH_PORT_START MV_NSS_PORT_APP_MIN       // PortID: 32
#define TGD_NSS_WLAN_PORT_MAX_COUNT MV_NSS_PORT_FWD_NUM // 8
// Names of WLAN ports WLAN16 -> WLAN23
// Name of Virtual ports nss16 -> nss23
// Names of NextHops nh-terra16 -> nhterra-23
#define TGD_NSS_WLAN_PORT_NAME_FROM_ID(str, len, id)                           \
	TGD_NAME_FROM_ID(str, len, id, "terra%d")
#define TGD_NSS_NH_NAME_FROM_ID(str, len, id)                                  \
	TGD_NAME_FROM_ID(str, len, id, "nh-terra%d")
#define TGD_NSS_VP_PORT_NAME_FROM_ID(str, len, id)                             \
	TGD_NAME_FROM_ID(str, len, id, "nss%d")
#define TGD_NAME_FROM_ID(str, len, id, fmt)                                    \
	do {                                                                   \
		snprintf(str, len, fmt, id);                                   \
	} while (0)
#define TGD_NSS_INVALID_PORT -1
#define TGD_NSS_INVALID_PORT -1
#define TGD_NSS_PMDATA_TYPE (0x00000003) // Short AMSDU type indication

/*
 * Cos values used for data and ctrl traffic
 * Note values should match the values assigned in
 * fib_nss agent
 */
#define NSS_COS_LO 2
#define NSS_COS_HI 0

/* Map queue id's to corresponding COS values */
static uint8_t qid_to_cos[FB_TGD_BH_MQ_QUEUE_NUM] = {
    [FB_TGD_MQ_BK] = NSS_COS_LO,
    [FB_TGD_MQ_BE] = NSS_COS_LO,
    [FB_TGD_MQ_VI] = NSS_COS_HI,
    [FB_TGD_MQ_VO] = NSS_COS_HI};

struct fb_tgd_nss_rt_backend {
	struct fb_tgd_routing_backend rt_base;
	struct mv_nss_ops *nss_ops;
};

struct fb_tgd_nss_rt_devpriv {
	struct net_device *nss_dev;
	int nss_wlan_port_id;
	int nss_nh_port_id;
};

static inline struct fb_tgd_nss_rt_devpriv *
tgd_mvl_nss_get_priv(struct tgd_terra_dev_priv *priv)
{
	return priv->rt_data;
}

static inline void tgd_mvl_nss_set_priv(struct tgd_terra_dev_priv *priv,
					struct fb_tgd_nss_rt_devpriv *nss_priv)
{
	priv->rt_data = nss_priv;
}

static inline struct fb_tgd_nss_rt_backend *
tgd_mvl_nss_get_backend(struct tgd_terra_dev_priv *dev_priv)
{
	struct tgd_terra_driver *fb_drv_data;

	fb_drv_data = dev_priv->fb_drv_data;
	return container_of(fb_drv_data->rt_backend,
			    struct fb_tgd_nss_rt_backend, rt_base);
}

static int tgd_mvl_nss_if_bind(struct tgd_terra_dev_priv *priv)
{
	struct fb_tgd_nss_rt_devpriv *nss_priv;
	struct net_device *nss_dev = NULL;
	char nss_dev_name[MV_NSS_PORT_NAME_LEN] = {};
	enum mv_sfp_rc ret;
	u16 vpid;

	nss_priv = tgd_mvl_nss_get_priv(priv);

	/* Only do the binding once */
	if (nss_priv->nss_dev != NULL)
		return (0);

	vpid = (u16)nss_priv->nss_wlan_port_id;

	ret = mv_sfp_vp_set_type(vpid, MV_SFP_VP_TYPE_WLAN);
	if (ret != MV_SFP_RC_OK) {
		TGD_DBG_CTRL_ERROR(
		    "%s: NSS SFP WLAN port: %d set type failed : "
		    "Ret Status %d\n",
		    __FUNCTION__, vpid, ret);
		return -1;
	}
	// nss16 - nss23 corresponding to WLAN16 -> WLAN23
	TGD_NSS_VP_PORT_NAME_FROM_ID(nss_dev_name, MV_NSS_PORT_NAME_LEN, vpid);
	nss_dev = dev_get_by_name(&init_net, nss_dev_name);
	if (nss_dev == NULL) {
		TGD_DBG_CTRL_ERROR(
		    "%s: NSS SFP WLAN port: %d cannot find nss vp: %s\n",
		    __FUNCTION__, vpid, nss_dev_name);
		return -1;
	}
	ret = mv_sfp_vp_set_dest(vpid, nss_dev);
	if (ret != MV_SFP_RC_OK) {
		TGD_DBG_CTRL_ERROR(
		    "%s: NSS SFP WLAN port: %d set dest failed : "
		    "Ret Status %d\n",
		    __FUNCTION__, vpid, ret);
		dev_put(nss_dev);
		return -1;
	}
	ret = mv_sfp_vp_set_parent(vpid, priv->dev);
	if (ret != MV_SFP_RC_OK) {
		TGD_DBG_CTRL_ERROR(
		    "%s: NSS SFP WLAN port: %d set parent failed : "
		    "Ret Status %d\n",
		    __FUNCTION__, vpid, ret);
		(void)mv_sfp_vp_delete_dest(vpid);
		dev_put(nss_dev);
		return -1;
	}
	ret = mv_sfp_fc_set_dest_dev(priv->dev, nss_dev);
	if (ret != MV_SFP_RC_OK) {
		TGD_DBG_CTRL_ERROR(
		    "%s: NSS SFP WLAN port: %d set parent failed : "
		    "Ret Status %d\n",
		    __FUNCTION__, vpid, ret);
		(void)mv_sfp_vp_delete_parent(vpid);
		(void)mv_sfp_vp_delete_dest(vpid);
		dev_put(nss_dev);
		return -1;
	}

	nss_priv->nss_dev = nss_dev;
	return 0;
}

static void tgd_mvl_nss_if_del(struct fb_tgd_nss_rt_devpriv *nss_priv)
{
	mv_nss_result_spec_t nss_spec = {};
	mv_nss_result_t nss_res = {};
	nss_spec.cb = NULL;
	nss_spec.res = &nss_res;

	if (nss_priv->nss_nh_port_id != TGD_NSS_INVALID_PORT) {
		mv_nss_port_delete(nss_priv->nss_nh_port_id, &nss_spec);
		nss_priv->nss_nh_port_id = TGD_NSS_INVALID_PORT;
	}
	if (nss_priv->nss_wlan_port_id != TGD_NSS_INVALID_PORT) {
		mv_nss_port_delete(nss_priv->nss_wlan_port_id, &nss_spec);
		nss_priv->nss_wlan_port_id = TGD_NSS_INVALID_PORT;
	}
}

static int tgd_mvl_nss_if_set(struct tgd_terra_dev_priv *dev_priv)
{
	struct fb_tgd_nss_rt_devpriv *nss_priv;

	mv_nss_port_t nss_port = {};
	mv_nss_port_wlan_t wlan_port = {};
	mv_nss_port_nexthop_t next_hop_port = {};
	mv_nss_result_spec_t nss_spec = {};
	mv_nss_result_t nss_res = {};
	mv_nss_cos_queue_map_t cos_queue_map = {};

	nss_priv = tgd_mvl_nss_get_priv(dev_priv);

	nss_spec.cb = NULL;
	nss_spec.res = &nss_res;

	// Add NSS Wlan Port
	nss_port.port_id = TGD_NSS_WLAN_PORT_START + dev_priv->dev_index;
	TGD_NSS_WLAN_PORT_NAME_FROM_ID(nss_port.name, MV_NSS_PORT_NAME_LEN,
				       nss_port.port_id);
	nss_port.port_dst_id = TGD_NSS_CPU_PORT_START;
	nss_port.type = MV_NSS_PORT_WLAN;
	nss_port.state = MV_NSS_PORT_STATE_UP;
	nss_port.cos = 0;

	wlan_port.port_id = TGD_NSS_CPU_PORT_START;
	memcpy(&wlan_port.l2addr, dev_priv->dev->dev_addr,
	       sizeof(wlan_port.l2addr));
	nss_port.cfg = &wlan_port;

	if (mv_nss_port_set(&nss_port, &nss_spec) != 0 ||
	    nss_res.status != MV_NSS_OK) {
		TGD_DBG_CTRL_ERROR(
		    "%s: NSS WLAN port: %d add failed : Res Status %d\n",
		    __FUNCTION__,
		    (TGD_NSS_WLAN_PORT_START + dev_priv->dev_index),
		    nss_res.status);
		return -1;
	}
	// port default cos
	if (mv_nss_port_cos_set(nss_port.port_id, NSS_COS_LO, &nss_spec) != 0 ||
	    nss_res.status != MV_NSS_OK) {
		TGD_DBG_CTRL_ERROR("%s: NSS WLAN port: %d port_cos Set  failed "
				   ": Res Status %d\n",
				   __FUNCTION__, nss_port.port_id,
				   nss_res.status);
		return -1;
	}
	nss_priv->nss_wlan_port_id = nss_port.port_id;

	// Create a cos 0 to queue 1 mapping used for ctrl traffic and
	// cos 2 to queue 0 mapping for data traffic
	cos_queue_map.cos = NSS_COS_HI;
	cos_queue_map.spec.port_id = nss_priv->nss_wlan_port_id;
	cos_queue_map.spec.type = MV_NSS_QUEUE_EGRESS;
	cos_queue_map.spec.queue_id = 1;
	if (mv_nss_cos_queue_set(&cos_queue_map, 1, &nss_spec) != 0) {
		TGD_DBG_CTRL_ERROR(
		    "%s: WLAN port: %d Cos: %d mapping to queue 1 failed : "
		    "Res Status %d\n",
		    __FUNCTION__, nss_priv->nss_wlan_port_id, cos_queue_map.cos,
		    nss_res.status);
		// continue on, should not affect basic funtionality
	}
	cos_queue_map.cos = NSS_COS_LO;
	cos_queue_map.spec.port_id = nss_priv->nss_wlan_port_id;
	cos_queue_map.spec.type = MV_NSS_QUEUE_EGRESS;
	cos_queue_map.spec.queue_id = 0;
	if (mv_nss_cos_queue_set(&cos_queue_map, 1, &nss_spec) != 0) {
		TGD_DBG_CTRL_ERROR(
		    "%s: WLAN port: %d Cos: %d mapping to queue 0 failed : "
		    "Res Status %d\n",
		    __FUNCTION__, nss_priv->nss_wlan_port_id, cos_queue_map.cos,
		    nss_res.status);
		// continue on, should not affect basic funtionality
	}

	// Add NSS Wlan Next Hop Port
	nss_port.port_id = TGD_NSS_NH_PORT_START + dev_priv->dev_index;
	TGD_NSS_NH_NAME_FROM_ID(nss_port.name, MV_NSS_PORT_NAME_LEN,
				nss_port.port_id);
	nss_port.port_dst_id = 0;
	nss_port.type = MV_NSS_PORT_NEXTHOP;
	nss_port.state = MV_NSS_PORT_STATE_UP;
	nss_port.cos = 0;

	next_hop_port.port_id = nss_priv->nss_wlan_port_id;
	memcpy(&next_hop_port.l2addr, dev_priv->link_sta_addr.addr,
	       sizeof(dev_priv->link_sta_addr.addr));
	nss_port.cfg = &next_hop_port;

	if (mv_nss_port_set(&nss_port, &nss_spec) != 0 ||
	    nss_res.status != MV_NSS_OK) {
		TGD_DBG_CTRL_ERROR(
		    "%s: NSS NEXT HOP port: %d add failed : Status %d\n",
		    __FUNCTION__, (TGD_NSS_NH_PORT_START + dev_priv->dev_index),
		    nss_res.status);
		tgd_mvl_nss_if_del(nss_priv);
		return -1;
	}
	nss_priv->nss_nh_port_id = nss_port.port_id;
	return 0;
}

static int tgd_mvl_nss_if_unbind(struct tgd_terra_dev_priv *dev_priv,
				 struct fb_tgd_nss_rt_devpriv *nss_priv)
{
	enum mv_sfp_rc ret;
	u16 vpid;

	/* Unbinding previously not bound instance is always OK */
	if (nss_priv->nss_dev == NULL)
		return (0);

	dev_put(nss_priv->nss_dev);
	nss_priv->nss_dev = NULL;

	vpid = (u16)nss_priv->nss_wlan_port_id;

	ret = mv_sfp_fc_delete_dest_dev(dev_priv->dev);
	if (ret != MV_SFP_RC_OK) {
		TGD_DBG_CTRL_ERROR(
		    "%s: NSS SFP WLAN port: %d delete parent failed: "
		    "Ret Status %d\n",
		    __FUNCTION__, vpid, ret);
		return -1;
	}
	ret = mv_sfp_vp_delete_parent(vpid);
	if (ret != MV_SFP_RC_OK) {
		TGD_DBG_CTRL_ERROR(
		    "%s: NSS SFP WLAN port: %d delete parent failed: "
		    "Ret Status %d\n",
		    __FUNCTION__, vpid, ret);
		return -1;
	}
	ret = mv_sfp_vp_delete_dest(vpid);
	if (ret != MV_SFP_RC_OK) {
		TGD_DBG_CTRL_ERROR(
		    "%s: NSS SFP WLAN port: %d delete dest failed: "
		    "Ret Status %d\n",
		    __FUNCTION__, vpid, ret);
		return -1;
	}
	return 0;
}

static int fb_tgd_rt_nss_add_device(struct tgd_terra_dev_priv *dev_priv)
{
	struct fb_tgd_nss_rt_devpriv *nss_priv;

	nss_priv = kzalloc(sizeof(*nss_priv), GFP_KERNEL);
	if (nss_priv == NULL)
		return -ENOMEM;

	nss_priv->nss_wlan_port_id = TGD_NSS_INVALID_PORT;
	nss_priv->nss_nh_port_id = TGD_NSS_INVALID_PORT;
	tgd_mvl_nss_set_priv(dev_priv, nss_priv);
	return 0;
}

static void fb_tgd_rt_nss_del_device(struct tgd_terra_dev_priv *dev_priv)
{
	struct fb_tgd_nss_rt_devpriv *nss_priv;

	nss_priv = tgd_mvl_nss_get_priv(dev_priv);

	if (nss_priv == NULL)
		return;

	tgd_mvl_nss_if_unbind(dev_priv, nss_priv);
	tgd_mvl_nss_if_del(nss_priv);

	tgd_mvl_nss_set_priv(dev_priv, NULL);
	kfree(nss_priv);
}

static void fb_tgd_rt_nss_set_link_state(struct tgd_terra_dev_priv *dev_priv,
					 tgLinkStatus state)
{
	struct fb_tgd_nss_rt_devpriv *nss_priv;
	struct fb_tgd_nss_rt_backend *nss_backend;
	mv_nss_result_spec_t nss_spec = {};
	mv_nss_result_t nss_res = {};

	TGD_DBG_CTRL_DBG("%s: Processing link %s event for %s \n", __FUNCTION__,
			 dev_priv->dev->name,
			 (state == TG_LINKUP) ? "up" : "down");
	nss_priv = tgd_mvl_nss_get_priv(dev_priv);
	if (state == TG_LINKUP) {
		int ret = tgd_mvl_nss_if_set(dev_priv);
		if (ret == 0)
			tgd_mvl_nss_if_bind(dev_priv);
		if (ret != 0)
			return;
		/*
		 * Release queues in case we has them stopped
		 * when link went down previously.
		 */
		nss_backend = tgd_mvl_nss_get_backend(dev_priv);
		if (nss_backend->nss_ops->xmit_resume) {
			nss_backend->nss_ops->xmit_resume(dev_priv->dev,
							  NSS_COS_HI);
			nss_backend->nss_ops->xmit_resume(dev_priv->dev,
							  NSS_COS_LO);
		}
	} else {
		nss_spec.cb = NULL;
		nss_spec.res = &nss_res;

		if (nss_priv->nss_wlan_port_id != TGD_NSS_INVALID_PORT)
			mv_nss_port_state_set(nss_priv->nss_wlan_port_id,
					      MV_NSS_PORT_STATE_DOWN,
					      &nss_spec);
		if (nss_priv->nss_nh_port_id != TGD_NSS_INVALID_PORT)
			mv_nss_port_state_set(nss_priv->nss_nh_port_id,
					      MV_NSS_PORT_STATE_DOWN,
					      &nss_spec);
	}
}

static void fb_tgd_rt_nss_rx(struct tgd_terra_dev_priv *dev_priv,
			     struct sk_buff *skb)
{
	struct fb_tgd_nss_rt_devpriv *nss_priv;
	struct fb_tgd_nss_rt_backend *nss_backend;
	struct mv_nss_metadata *pmdata;

	/* Feed not aggregated frames directly to host */
	if (skb->protocol != htons(ETH_P_TGAMSDU)) {
		skb->protocol = eth_type_trans(skb, skb->dev);
		netif_rx(skb);
		return;
	}

	nss_backend = tgd_mvl_nss_get_backend(dev_priv);
	pmdata =
	    (struct mv_nss_metadata *)nss_backend->nss_ops->init_metadata_skb(
		skb);
	if (pmdata == NULL) {
		TGD_DBG_CTRL_ERROR("Mvl Fast Path Meta data NULL: "
				   "Headroom %d\n",
				   skb_headroom(skb));
		terra_dev_stats_inc(dev_priv, RX_ERR_NO_MDATA, 1);
		dev_kfree_skb(skb);
		return;
	}
	nss_priv = tgd_mvl_nss_get_priv(dev_priv);
	memset(pmdata, 0, sizeof(*pmdata));
	pmdata->port_dst = MV_NSS_PORT_ID_NONE;
	pmdata->port_src = nss_priv->nss_wlan_port_id;
	pmdata->type = TGD_NSS_PMDATA_TYPE; // Short AMSDU type indication;
	pmdata->cos = MV_NSS_COS_NONE;      // Force NSS to assign cos

	nss_backend->nss_ops->receive_skb(skb);
}

static void fb_tgd_rt_nss_tx(struct tgd_terra_dev_priv *dev_priv,
			     struct sk_buff *skb)
{
	struct fb_tgd_nss_rt_backend *nss_backend;
	struct mv_nss_metadata *mdata;
	int ret;

	nss_backend = tgd_mvl_nss_get_backend(dev_priv);
	mdata = nss_backend->nss_ops->get_metadata_skb(skb);

	/*
	 * Remove Marvell metadata and setup TX queue mapping for the
	 * frame based on the valued in metadata.
	 */
	if (mdata != NULL) {
		/*
		 * Set priority to BE or VI TIDs so that BH driver knows
		 * how to map this skb
		 */
		skb->priority = (mdata->cos != 0) ? FB_TGD_BH_SKB_PRIO_BE
						  : FB_TGD_BH_SKB_PRIO_VI;
		/*
		 * Remove Meta data from the SKB before passing to the
		 * WLAN
		 */
		if (mdata->cos != 0)
			terra_dev_stats_inc(dev_priv, TX_FROM_NSS_DATA_COS, 1);
		else
			terra_dev_stats_inc(dev_priv, TX_FROM_NSS_CTRL_COS, 1);
		nss_backend->nss_ops->remove_metadata_skb(skb);
		terra_dev_stats_inc(dev_priv, TX_FROM_NSS, 1);
		TGD_DBG_CTRL_DBG("Sending packet with nss cos %d \n",
				 mdata->cos);
	} else {
		if (skb->priority != 0)
			terra_dev_stats_inc(dev_priv, TX_FROM_LNX_DATA_COS, 1);
		else
			terra_dev_stats_inc(dev_priv, TX_FROM_LNX_CTRL_COS, 1);
		terra_dev_stats_inc(dev_priv, TX_FROM_LINUX, 1);
	}

	/* Do common preprocessing of the frame */
	ret = tgd_terra_bh_tx_pre(dev_priv, skb);
	if (ret != 0)
		return;

	/* Marvell-specific post-processing for locally originated frames */
	if (mdata == NULL) {
		/*
		 * Pkts from the Linux stack go out directly. We encap
		 * them here
		 */
		tgd_amsdu_encapsulate(skb);
		TGD_DBG_CTRL_DBG("Sending packet with skb cos %d \n",
				 skb->priority);
	}

	/* Deliver frame to BH */
	tgd_terra_bh_tx_post(dev_priv, skb);
}

static void fb_tgd_rt_nss_flow_control(struct tgd_terra_dev_priv *dev_priv,
				       unsigned char qid, bool state)
{
	struct fb_tgd_nss_rt_backend *nss_backend;
	struct fb_tgd_nss_rt_devpriv *nss_priv;
	struct netdev_queue *dev_queue;
	uint8_t cos;

	nss_backend = tgd_mvl_nss_get_backend(dev_priv);
	nss_priv = tgd_mvl_nss_get_priv(dev_priv);

	/* Convert queue back to cos value */
	cos = qid_to_cos[qid];

	dev_queue = netdev_get_tx_queue(dev_priv->dev, qid);
	if (state) {
		netif_tx_stop_queue(dev_queue);
		if (nss_backend->nss_ops->xmit_pause)
			nss_backend->nss_ops->xmit_pause(dev_priv->dev, cos);
	} else {
		netif_tx_wake_queue(dev_queue);
		if (nss_backend->nss_ops->xmit_resume)
			nss_backend->nss_ops->xmit_resume(dev_priv->dev, cos);
	}
}

static void fb_tgd_rt_nss_module_fini(struct tgd_terra_driver *tgd_data)
{
	struct fb_tgd_routing_backend *rtb;

	rtb = tgd_data->rt_backend;
	if (rtb != NULL) {
		kfree(rtb);
	}
	tgd_data->rt_backend = NULL;
}

int fb_tgd_rt_nss_module_init(struct tgd_terra_driver *tgd_data)
{
	struct fb_tgd_nss_rt_backend *rtn;
	struct fb_tgd_routing_backend *rtb;
	enum mv_sfp_rc ret = MV_SFP_RC_OK;

	if (tgd_data->frame_format != TG_SHORT) {
		TGD_DBG_CTRL_ERROR("%s: Unsupported A-MSDU format requested, "
				   "ignored on this platform\n",
				   __FUNCTION__);
		return -ENOTSUPP;
	}

	rtn = kzalloc(sizeof(*rtn), GFP_KERNEL);
	if (rtn == NULL) {
		return -ENOMEM;
	}

	rtn->nss_ops = mv_nss_ops_get(NULL);
	if (rtn->nss_ops == NULL) {
		TGD_DBG_CTRL_ERROR("%s: Unable to obtain NSS ops vector\n",
				   __FUNCTION__);
		kfree(rtn);
		return -ENODEV;
	}

	ret = mv_sfp_vp_set_type(TGD_NSS_CPU_PORT_START, MV_SFP_VP_TYPE_CPU);
	if (ret != MV_SFP_RC_OK) {
		// Log and continue for now. This is not a significant error.
		TGD_DBG_CTRL_ERROR(
		    "%s: NSS SFP port: %d set type failed : Ret Status %d\n",
		    __FUNCTION__, TGD_NSS_CPU_PORT_START, ret);
	}

	rtb = &rtn->rt_base;
	rtb->rt_mod_fini = fb_tgd_rt_nss_module_fini;
	rtb->rt_add_dev = fb_tgd_rt_nss_add_device;
	rtb->rt_del_dev = fb_tgd_rt_nss_del_device;
	rtb->rt_set_link_state = fb_tgd_rt_nss_set_link_state;
	rtb->rt_flow_control = fb_tgd_rt_nss_flow_control;
	rtb->rt_tx = fb_tgd_rt_nss_tx;
	rtb->rt_rx = fb_tgd_rt_nss_rx;
	tgd_data->rt_backend = rtb;
	return 0;
}
