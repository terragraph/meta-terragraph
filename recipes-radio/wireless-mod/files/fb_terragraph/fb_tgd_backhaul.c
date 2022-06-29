/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Terragraph driver-specific backhaul API wrapper */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

#include <fb_tg_fw_driver_if.h>
#include <fb_tg_backhaul_if.h>

#include "fb_tgd_backhaul.h"
#include "fb_tgd_terragraph.h"
#include "fb_tgd_fw_if.h"
#include "fb_tgd_debug.h"
#include "fb_tgd_route.h"
#include "fb_tgd_nlsdn.h"

#define FB_TGD_BH_API_VERSION TGD_BH_API_VERSION
#define FB_TGD_BH_IOCTL_BUF_SZ TGD_BH_IOCTL_BUF_SZ

/**
 * Link direction enum values
 */
#define FB_TGD_BH_LINK_DIR_RX TGD_BH_LINK_DIR_RX
#define FB_TGD_BH_LINK_DIR_TX TGD_BH_LINK_DIR_TX

/*
 * Ops access macro
 */
#define BH_OPS(fb_drv) ((struct tgd_bh_ops *)((fb_drv)->drv_bh_ops))

/* Define common structures */
typedef struct tgd_bh_data_rxd fb_tgd_data_rxd_t;
typedef struct tgd_bh_data_txd fb_tgd_data_txd_t;

static bool tgd_bh_enable_flow_control = true;
module_param(tgd_bh_enable_flow_control, bool, 0644);

static void fb_tgd_bh_flow_control_off(void *ctxt, void *link_ptr, int link,
				       unsigned char qid)
{
	struct tgd_terra_driver *fb_dvr_data = (struct tgd_terra_driver *)ctxt;
	struct tgd_terra_dev_priv *priv = (struct tgd_terra_dev_priv *)link_ptr;

	if (tgd_bh_enable_flow_control)
		tgd_flow_control_common(fb_dvr_data, priv, link, qid, false);
}

static void fb_tgd_bh_flow_control_on(void *ctxt, void *link_ptr, int link,
				      unsigned char qid)
{
	struct tgd_terra_driver *fb_dvr_data = (struct tgd_terra_driver *)ctxt;
	struct tgd_terra_dev_priv *priv = (struct tgd_terra_dev_priv *)link_ptr;

	if (tgd_bh_enable_flow_control)
		tgd_flow_control_common(fb_dvr_data, priv, link, qid, true);
}

static void fb_tgd_bh_set_mac_addr(void *ctxt, uint8_t *mac_addr)
{
	struct tgd_terra_driver *fb_drv_data = (struct tgd_terra_driver *)ctxt;

	tgd_set_if_mac_addr(fb_drv_data, mac_addr);

	/* Send the netlink message to the subscribers that the device is up
	 * with the new MAC. The vendor driver can update the MAC as part of
	 * init process, after the driver is registered.
	 */
	TGD_DBG_CTRL_ERROR("fb_tgd_bh_set_mac_addr: Send UP with MAC 0x%llx \n",
			   fb_drv_data->macaddr);
	tgd_nlsdn_send_device_updown_status(fb_drv_data, DEVICE_UP);
}

static void fb_tgd_bh_rx_data(void *ctxt, struct sk_buff *skb,
			      fb_tgd_data_rxd_t *rxd);
static void fb_tgd_bh_rx_event(void *ctxt, const uint8_t *event, uint32_t size);

static struct tgd_bh_callback_ops fb_tgd_bh_dev_ops = {
    .api_version = FB_TGD_BH_API_VERSION,
    .rx_data = fb_tgd_bh_rx_data,
    .rx_event = fb_tgd_bh_rx_event,
    .link_resume = fb_tgd_bh_flow_control_off,
    .link_suspend = fb_tgd_bh_flow_control_on,
    .set_mac_addr = fb_tgd_bh_set_mac_addr,
};

// Callback function to receive data from the WLAN Driver
static void fb_tgd_bh_rx_data(void *ctxt, struct sk_buff *skb,
			      fb_tgd_data_rxd_t *rxd)
{
	struct tgd_terra_driver *fb_drv_data;
	struct tgd_terra_dev_priv *priv;

	fb_drv_data = (struct tgd_terra_driver *)ctxt;
	priv = (struct tgd_terra_dev_priv *)rxd->linkCtx;

	TGD_DBG_DATA_DBG("Rx_pkt = %p, len = %d\n", skb->data, skb->len);

	/* Process the skb */
	tgd_terra_rx_data_handler(fb_drv_data, priv, skb, rxd->rxLinkId);
}

// Callback function to receive firmware events from the WLAN driver
static void fb_tgd_bh_rx_event(void *ctxt, const uint8_t *event, uint32_t size)
{
	struct tgd_terra_driver *fb_drv_data = (struct tgd_terra_driver *)ctxt;

	TGD_DBG_DATA_INFO("Rx Event %p size %d %x:%x:%x:%x:%x:%x:%x:%x:%x:%x\n",
			  event, size, event[0], event[1], event[2], event[3],
			  event[4], event[5], event[6], event[7], event[8],
			  event[9]);

	tgd_terra_rx_event_handler(fb_drv_data, event, size);
}

static int fb_tgd_bh_add_link_info(struct tgd_terra_driver *fb_drv_data,
				   struct tgd_terra_dev_priv *priv,
				   struct tgd_bh_link_info_desc *ldesc)
{
	if (fb_drv_data->bh_ctx == NULL)
		return -EINVAL;

	return BH_OPS(fb_drv_data)->add_link_info(fb_drv_data->bh_ctx, ldesc);
}

static int fb_tgd_bh_delete_link_info(struct tgd_terra_driver *fb_drv_data,
				      struct tgd_bh_link_info_desc *ldesc)
{
	if (fb_drv_data->bh_ctx)
		return BH_OPS(fb_drv_data)
		    ->delete_link_info(fb_drv_data->bh_ctx, ldesc);
	else {
		TGD_DBG_CTRL_INFO("%s: Invalid bh ctx", __FUNCTION__);
		return -EINVAL;
	}
}

int fb_tgd_bh_tx_data(struct tgd_terra_dev_priv *priv, struct sk_buff *skb)
{
	struct tgd_terra_driver *fb_drv_data;
	fb_tgd_data_txd_t txd;

	fb_drv_data = priv->fb_drv_data;

	txd.lifetime = TGD_TX_DATA_LIFETIME;
	txd.peerIndex = priv->peer_index;
	txd.txLinkId = priv->tx_link;

	BH_OPS(fb_drv_data)->tx_data(fb_drv_data->bh_ctx, skb, &txd);
	return 0;
}

int fb_tgd_bh_ioctl(struct tgd_terra_driver *fb_drv_data, uint8_t *req_buf,
		    uint req_len, uint8_t *resp_buf, uint resp_len)
{
	if (fb_drv_data->bh_ctx)
		return BH_OPS(fb_drv_data)
		    ->ioctl(fb_drv_data->bh_ctx, req_buf, req_len, resp_buf,
			    resp_len);
	else {
		TGD_DBG_CTRL_INFO("%s:Invalid bh_ctx\n", __FUNCTION__);
		return -EINVAL;
	}
}

int fb_tgd_bh_set_key(struct tgd_terra_dev_priv *priv, const uint8_t *dest_mac,
		      const uint8_t *key_data, uint key_len)
{
	struct tgd_terra_driver *fb_drv_data;

	fb_drv_data = priv->fb_drv_data;
	if (fb_drv_data->bh_ctx)
		return BH_OPS(fb_drv_data)
		    ->set_key(fb_drv_data->bh_ctx, priv->peer_index, dest_mac,
			      key_data, key_len);
	else {
		TGD_DBG_CTRL_INFO("%s:Invalid bh ctx", __FUNCTION__);
		return -EINVAL;
	}
}

// Register with baseband driver
int fb_tgd_bh_register_client(struct tgd_terra_driver *fb_drv_data)
{
	struct tgd_bh_client_info ci;
	int ret;

	memset(&ci, 0, sizeof(ci));
	ci.client_ops = &fb_tgd_bh_dev_ops;
	ci.client_ctx = fb_drv_data;
	ci.client_max_peers = fb_drv_data->max_link_count;

	ret = BH_OPS(fb_drv_data)
		  ->register_client(fb_drv_data->drv_bh_ctx, &ci,
				    &fb_drv_data->bh_ctx);
	if (ret < 0) {
		TGD_DBG_CTRL_ERROR(
		    "Registration with BH driver failed, error: %d\n", ret);
	} else {
		TGD_DBG_CTRL_INFO("Restration with BH driver successful drv "
				  "data %p bh handle %p\n",
				  fb_drv_data, fb_drv_data->bh_ctx);
	}
	return ret;
}

// Unregister from baseband driver
int fb_tgd_bh_unregister_client(struct tgd_terra_driver *fb_drv_data)
{
	if (fb_drv_data->bh_ctx == NULL)
		return 0;

	BH_OPS(fb_drv_data)->unregister_client(fb_drv_data->bh_ctx);
	fb_drv_data->bh_ctx = NULL;
	return 0;
}

void tgd_terra_update_link_stats(struct tgd_terra_dev_priv *priv)
{
	struct tgd_bh_link_stats cur_stats;
	struct tgd_terra_driver *fb_drv_data;
	int ret;

	fb_drv_data = priv->fb_drv_data;
	if ((priv->tx_link >= 0 || priv->rx_link >= 0) &&
	    fb_drv_data->bh_ctx == NULL) {
		TGD_DBG_CTRL_ERROR("%s: Invalid bh_ctx\n", __FUNCTION__);
		return;
	}

	if (priv->tx_link >= 0) {
		ret = BH_OPS(fb_drv_data)
			  ->link_stats(fb_drv_data->bh_ctx, priv->peer_index,
				       &cur_stats);
		if (ret == 0) {
			/* Account for ever-increasing stats */
			priv->link_stats.bytes_sent = cur_stats.bytes_sent;
			priv->link_stats.pkts_sent = cur_stats.pkts_sent;
			priv->link_stats.tx_err = cur_stats.tx_err;
			priv->link_stats.pkts_enqueued =
			    cur_stats.pkts_enqueued;
			priv->link_stats.bytes_enqueued =
			    cur_stats.bytes_enqueued;
			/* Momentary snapshot stats */
			priv->link_stats.pkts_pending = cur_stats.pkts_pending;
			priv->link_stats.bytes_pending =
			    cur_stats.bytes_pending;
			priv->link_stats.bytes_sent_failed =
			    cur_stats.bytes_sent_failed;
			priv->link_stats.bytes_enqueue_failed =
			    cur_stats.bytes_enqueue_failed;
			priv->link_stats.bytes_sent_pad =
			    cur_stats.bytes_sent_pad;
			priv->link_stats.bytes_sent_failed_pad =
			    cur_stats.bytes_sent_failed_pad;
			priv->link_stats.bytes_enqueued_pad =
			    cur_stats.bytes_enqueued_pad;
			priv->link_stats.bytes_enqueue_fail_pad =
			    cur_stats.bytes_enqueue_fail_pad;

			priv->link_stats.bytes_recved = cur_stats.bytes_recved;
			priv->link_stats.pkts_recved = cur_stats.pkts_recved;
		}
	}
}

/* this is usually called in atomic context, cannot use mutex, assume bh has
 * proper locking */
int tgd_link_pkts_pending(struct tgd_terra_dev_priv *priv)
{
	struct tgd_bh_link_stats cur_stats;
	struct tgd_terra_driver *fb_drv_data;
	int ret;

	fb_drv_data = priv->fb_drv_data;
	if (!fb_drv_data->bh_ctx) {
		TGD_DBG_CTRL_ERROR("%s: Invalid bh_ctx\n", __FUNCTION__);
		return -1;
	}

	if (priv->tx_link >= 0) {
		ret = BH_OPS(fb_drv_data)
			  ->link_stats(fb_drv_data->bh_ctx, priv->peer_index,
				       &cur_stats);
		if (ret == 0)
			return cur_stats.pkts_pending;
	}

	return -1;
}

int fb_tgd_bh_del_links_info(struct tgd_terra_dev_priv *priv)
{
	struct tgd_bh_link_info_desc ldesc;
	struct terra_dev_pcpu_stats *pcpu_stats;

	mutex_lock(&priv->link_lock);

	/* Get last snapshot of link stats */
	spin_lock(&priv->stats_lock);
	tgd_terra_update_link_stats(priv);
	spin_unlock(&priv->stats_lock);

	/* Disentangle from BH */
	if (priv->tx_link >= 0 || priv->rx_link >= 0) {
		memset(&ldesc, 0, sizeof(ldesc));
		ldesc.peerIndex = priv->peer_index;
		ldesc.rxLinkId = priv->rx_link;
		ldesc.txLinkId = priv->tx_link;

		fb_tgd_bh_delete_link_info(priv->fb_drv_data, &ldesc);
	}

	spin_lock(&priv->stats_lock);
	priv->tx_link = TGD_LINK_INVALID;
	priv->rx_link = TGD_LINK_INVALID;

	priv->link_state = TG_LINKINIT;

	/*
	 * Spill link stats out into global device stats. The difference
	 * between link stats and device stats it that link stats do
	 * reset between link drops, while device retains counts for
	 * as long it is alive.
	 */
	pcpu_stats = this_cpu_ptr(priv->pcpu_stats);
	u64_stats_update_begin(&pcpu_stats->syncp);
	pcpu_stats->stats[TX_ERR] += priv->link_stats.tx_err;
	pcpu_stats->stats[TX_PACKETS] += priv->link_stats.pkts_sent;
	pcpu_stats->stats[TX_BYTES] += priv->link_stats.bytes_sent;
	pcpu_stats->stats[RX_PACKETS] += priv->link_stats.pkts_recved;
	pcpu_stats->stats[RX_BYTES] += priv->link_stats.bytes_recved;
	u64_stats_update_end(&pcpu_stats->syncp);

	/* Reset link stats */
	memset(&priv->link_stats, 0, sizeof(priv->link_stats));
	spin_unlock(&priv->stats_lock);

	mutex_unlock(&priv->link_lock);
	return 0;
}

int fb_tgd_bh_add_links_info(struct tgd_terra_dev_priv *priv,
			     uint8_t *link_mac_addr, uint8_t rxLink,
			     uint8_t txLink)
{
	struct tgd_terra_driver *fb_drv_data = priv->fb_drv_data;
	struct tgd_bh_link_info_desc ldesc;

	mutex_lock(&priv->link_lock);
	spin_lock(&priv->stats_lock);
	priv->rx_link = rxLink;
	priv->tx_link = txLink;
	spin_unlock(&priv->stats_lock);

	ldesc.peerIndex = priv->peer_index;
	ldesc.rxLinkId = rxLink;
	ldesc.txLinkId = txLink;
	ldesc.linkCtx = priv;
	ldesc.linkDev = priv->dev;

	fb_tgd_bh_add_link_info(fb_drv_data, priv, &ldesc);
	mutex_unlock(&priv->link_lock);
	return 0;
}

uint16_t fb_tgd_bh_select_queue(struct tgd_terra_dev_priv *priv, int priority)
{
	struct tgd_terra_driver *fb_drv_data = priv->fb_drv_data;

#ifdef TG_ENABLE_PFIFOFC
	if (likely(tgd_enable_pfifofc))
		return 0;
#endif

	return (*BH_OPS(fb_drv_data)->bh_prio_mq_map)[priority];
}

void fb_tgd_bh_setup_netdev(struct tgd_terra_dev_priv *priv)
{
	struct tgd_terra_driver *fb_drv_data = priv->fb_drv_data;
	struct tgd_bh_netdev_desc ndesc;

	if (BH_OPS(fb_drv_data)->setup_netdev == NULL)
		return;

	memset(&ndesc, 0, sizeof(ndesc));
	ndesc.devPeerIndex = priv->peer_index;
	ndesc.devNameUnit = priv->dev_index;

	BH_OPS(fb_drv_data)
	    ->setup_netdev(fb_drv_data->bh_ctx, priv->dev, &ndesc);
}

int fb_tgd_bh_api_version(struct tgd_terra_driver *fb_drv_data,
			  int *drv_version, int *vendor_ver)
{
	*drv_version = FB_TGD_BH_API_VERSION;
	*vendor_ver = BH_OPS(fb_drv_data)->api_version;
	return 0;
}

int fb_tgd_bh_api_init(struct device *dev, struct tgd_terra_driver *fb_drv_data)
{
	struct tgd_bh_platdata *pdata;

	pdata = dev_get_platdata(dev);
	if (pdata == NULL)
		return -ENODEV;

	if (pdata->drv_bh_ops->api_version != FB_TGD_BH_API_VERSION) {
		TGD_DBG_CTRL_ERROR("ERROR: bhVer: 0x%x != fbVer: 0x%x\n",
				   pdata->drv_bh_ops->api_version,
				   FB_TGD_BH_API_VERSION);
		return -EPERM;
	}

	fb_drv_data->drv_bh_ops = pdata->drv_bh_ops;
	fb_drv_data->drv_bh_ctx = pdata->drv_bh_ctx;
	fb_tgd_bh_set_mac_addr(fb_drv_data, pdata->mac_addr);
	return 0;
}

void fb_tgd_bh_cleanup_links(struct tgd_terra_driver *fb_drv_data)
{
	/* no cleanup necessary */
}

/* Provide device IDs this file handles */
const struct platform_device_id tg_bh_id_table[] = {
    {TGD_BH_COMPATIBLE_STRING, 0},
    {},
};
