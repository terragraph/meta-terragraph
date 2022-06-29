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
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

#include <fb_tg_fw_driver_if.h>

#include "fb_tgd_backhaul.h"
#include "fb_tgd_debug.h"
#include "fb_tgd_terragraph.h"
#include "fb_tgd_fw_if.h"
#include "fb_tgd_debug.h"
#include "fb_tgd_route.h"
#include "fb_tgd_nlsdn.h"

#include "wil6210/slave.h"

extern bool module_has_dvpp;

struct tgd_ql_link_info {
	struct tgd_terra_dev_priv *dev_priv;
	u8 mac[ETH_ALEN];
	bool is_rx;
	u8 cid;
	/* amount of bytes/packets that were successfully enqueued
	 * to HW TX queue on slave, or failed (dropped or failed
	 * for other reasons)
	 */
	u64 tx_enq_bytes, tx_enq_packets;
	u64 tx_enq_fail_bytes, tx_enq_fail_packets;
};

struct tgd_ql_priv {
	struct mutex mutex;
	struct tgd_terra_driver *drv_data;
	struct wil_slave_ops slave_ops;
	void *slave_dev;
	struct tgd_ql_link_info links[WIL_SLAVE_MAX_LINKS];
	u8 tx_cids[WIL_SLAVE_MAX_CID]; /* map cid to tx link id */
	u8 rx_cids[WIL_SLAVE_MAX_CID]; /* map cid to rx link id */

	struct completion *disconnected[WIL_SLAVE_MAX_CID];
};

int fb_tgd_bh_tx_data(struct tgd_terra_dev_priv *priv, struct sk_buff *skb)
{
	struct tgd_terra_driver *fb_drv_data;
	struct tgd_ql_priv *ql_priv;
	struct tgd_ql_link_info *link;
	int ret;

	fb_drv_data = priv->fb_drv_data;
	ql_priv = fb_drv_data->bh_ctx;
	if (unlikely(ql_priv == NULL || priv->tx_link == TGD_LINK_INVALID))
		goto free_skb;

	link = &ql_priv->links[priv->tx_link];
	ret =
	    ql_priv->slave_ops.tx_data(ql_priv->slave_dev, priv->tx_link, skb);

	switch (ret) {
	case NETDEV_TX_OK:
		link->tx_enq_packets++;
		link->tx_enq_bytes += skb->len;
		break;
	case NET_XMIT_DROP:
		link->tx_enq_fail_packets++;
		link->tx_enq_fail_bytes += skb->len;
		break;
	case NETDEV_TX_BUSY:
		/* the slave did not free the skb. We must free it here
		 * because caller assumes we consumed the skb.
		 * Currently the busy error is not reported back but fixing
		 * this requires a change in the backhaul API.
		 */
		link->tx_enq_fail_packets++;
		link->tx_enq_fail_bytes += skb->len;
		goto free_skb;
	}

	return 0;
free_skb:
	dev_kfree_skb_any(skb);
	return -1;
}

int fb_tgd_bh_ioctl(struct tgd_terra_driver *fb_drv_data, uint8_t *req_buf,
		    uint req_len, uint8_t *resp_buf, uint resp_len)
{
	struct tgd_ql_priv *ql_priv;
	u16 out_resp_len = resp_len;
	int ret;

	ql_priv = fb_drv_data->bh_ctx;
	if (likely(ql_priv != NULL)) {
		ret =
		    ql_priv->slave_ops.ioctl(ql_priv->slave_dev, 0, req_buf,
					     req_len, resp_buf, &out_resp_len);
		if (ret < 0)
			return ret;
		return out_resp_len;
	} else {
		TGD_DBG_CTRL_INFO("%s:Invalid bh_ctx\n", __FUNCTION__);
		return -EINVAL;
	}
}

int fb_tgd_bh_set_key(struct tgd_terra_dev_priv *priv, const uint8_t *dest_mac,
		      const uint8_t *key_data, uint key_len)
{
	struct tgd_terra_driver *fb_drv_data;
	struct tgd_ql_priv *ql_priv;

	fb_drv_data = priv->fb_drv_data;
	ql_priv = fb_drv_data->bh_ctx;
	if (likely(ql_priv != NULL))
		return ql_priv->slave_ops.set_key(ql_priv->slave_dev, dest_mac,
						  key_data, key_len);
	else {
		TGD_DBG_CTRL_INFO("%s:Invalid bh ctx", __FUNCTION__);
		return -EINVAL;
	}
}

void tgd_terra_update_link_stats(struct tgd_terra_dev_priv *priv)
{
	struct wil_slave_link_stats cur_stats;
	struct tgd_terra_driver *fb_drv_data;
	struct tgd_ql_link_info *link;
	struct tgd_ql_priv *ql_priv;
	int ret;

	fb_drv_data = priv->fb_drv_data;
	if (priv->tx_link == TGD_LINK_INVALID &&
	    priv->rx_link == TGD_LINK_INVALID) {
		return;
	}

	if (fb_drv_data->bh_ctx == NULL) {
		TGD_DBG_CTRL_ERROR("%s: Invalid bh_ctx\n", __FUNCTION__);
		return;
	}

	ql_priv = fb_drv_data->bh_ctx;
	ret = ql_priv->slave_ops.link_stats(ql_priv->slave_dev, priv->tx_link,
					    &cur_stats);
	if (ret != 0)
		return;

	link = &ql_priv->links[priv->tx_link];
	/* Account for ever-increasing stats */
	priv->link_stats.bytes_sent = cur_stats.tx_bytes;
	priv->link_stats.pkts_sent = cur_stats.tx_packets;
	priv->link_stats.tx_err = cur_stats.tx_errors;
	priv->link_stats.pkts_enqueued = link->tx_enq_packets;
	priv->link_stats.bytes_enqueued = link->tx_enq_bytes;
	/* Momentary snapshot stats */
	priv->link_stats.pkts_pending = cur_stats.tx_pend_packets;
	priv->link_stats.bytes_pending = cur_stats.tx_pend_bytes;
	priv->link_stats.bytes_sent_failed = 0;
	priv->link_stats.bytes_enqueue_failed = link->tx_enq_fail_bytes;
	priv->link_stats.bytes_sent_pad = 0;
	priv->link_stats.bytes_sent_failed_pad = 0;
	priv->link_stats.bytes_enqueued_pad = 0;
	priv->link_stats.bytes_enqueue_fail_pad = 0;
	priv->link_stats.bytes_recved = cur_stats.rx_bytes;
	priv->link_stats.pkts_recved = cur_stats.rx_packets;
}

/* this is usually called in atomic context, cannot use mutex, assume bh has
 * proper locking */
int tgd_link_pkts_pending(struct tgd_terra_dev_priv *priv)
{
	struct wil_slave_link_stats cur_stats;
	struct tgd_terra_driver *fb_drv_data;
	struct tgd_ql_priv *ql_priv;
	int ret;

	fb_drv_data = priv->fb_drv_data;
	ql_priv = fb_drv_data->bh_ctx;
	if (unlikely(ql_priv == NULL)) {
		TGD_DBG_CTRL_ERROR("%s: Invalid bh_ctx\n", __FUNCTION__);
		return -1;
	}

	if (priv->tx_link >= 0) {
		ret = ql_priv->slave_ops.link_stats(ql_priv->slave_dev,
						    priv->tx_link, &cur_stats);
		if (ret == 0)
			return cur_stats.tx_pend_packets;
	}

	return -1;
}

static void tgd_ql_delete_link_info(struct tgd_terra_dev_priv *priv,
				    int link_id)
{
	struct tgd_terra_driver *fb_drv_data;
	struct tgd_ql_priv *ql_priv;
	struct tgd_ql_link_info *link;

	if (link_id < 0 || link_id >= WIL_SLAVE_MAX_LINKS)
		return;

	fb_drv_data = priv->fb_drv_data;
	ql_priv = fb_drv_data->bh_ctx;
	if (unlikely(ql_priv == NULL))
		return;

	link = &ql_priv->links[link_id];
	if (link->dev_priv != priv)
		return;

	link->dev_priv = NULL;

	/* make sure other threads will see NULL link->dev_priv */
	wmb();

	if (ql_priv->slave_dev)
		ql_priv->slave_ops.sync_rx(ql_priv->slave_dev);
}

int fb_tgd_bh_del_links_info(struct tgd_terra_dev_priv *priv)
{
	struct terra_dev_pcpu_stats *pcpu_stats;

	mutex_lock(&priv->link_lock);

	/* Get last snapshot of link stats */
	spin_lock(&priv->stats_lock);
	tgd_terra_update_link_stats(priv);
	spin_unlock(&priv->stats_lock);

	/* Delete TX link if it is valid */
	if (priv->tx_link >= 0) {
		tgd_ql_delete_link_info(priv, priv->tx_link);
	}

	/* Delete RX link if it is valid and is not same as TX link removed
	 * above */
	if (priv->rx_link >= 0 && priv->rx_link != priv->tx_link) {
		tgd_ql_delete_link_info(priv, priv->rx_link);
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

static void tgq_ql_add_link_info(struct tgd_terra_dev_priv *priv, int link_id,
				 bool is_rx)
{
	struct tgd_terra_driver *fb_drv_data;
	struct tgd_ql_priv *ql_priv;
	struct tgd_ql_link_info *link;

	if (link_id < 0 || link_id >= WIL_SLAVE_MAX_LINKS)
		return;

	fb_drv_data = priv->fb_drv_data;
	ql_priv = fb_drv_data->bh_ctx;
	if (unlikely(ql_priv == NULL))
		return;

	link = &ql_priv->links[link_id];
	link->is_rx = is_rx;
	link->dev_priv = priv;
	link->tx_enq_bytes = 0;
	link->tx_enq_packets = 0;
	link->tx_enq_fail_bytes = 0;
	link->tx_enq_fail_packets = 0;
}

int fb_tgd_bh_add_links_info(struct tgd_terra_dev_priv *priv,
			     uint8_t *link_mac_addr, uint8_t rxLink,
			     uint8_t txLink)
{
	mutex_lock(&priv->link_lock);
	if (priv->rx_link == TGD_LINK_INVALID) {
		spin_lock(&priv->stats_lock);
		priv->rx_link = rxLink;
		spin_unlock(&priv->stats_lock);
		tgq_ql_add_link_info(priv, rxLink, true);
	}
	if (priv->tx_link == TGD_LINK_INVALID) {
		spin_lock(&priv->stats_lock);
		priv->tx_link = txLink;
		spin_unlock(&priv->stats_lock);
		tgq_ql_add_link_info(priv, txLink, false);
	}
	mutex_unlock(&priv->link_lock);
	return 0;
}

uint16_t fb_tgd_bh_select_queue(struct tgd_terra_dev_priv *priv, int priority)
{
	/* Only one queue */
	return 0;
}

static int fb_tgd_ql_rx_data(void *ctx, u8 cid, struct sk_buff *skb)
{
	struct tgd_ql_priv *ql_priv;
	struct tgd_terra_dev_priv *priv;
	int link_id;

	ql_priv = ctx;

	link_id = ql_priv->rx_cids[cid];
	if (unlikely(link_id >= WIL_SLAVE_MAX_LINKS))
		goto drop;

	priv = ql_priv->links[link_id].dev_priv;
	if (unlikely(priv == NULL || priv->rx_link != link_id))
		goto drop;

	tgd_terra_rx_data_handler(ql_priv->drv_data, priv, skb, link_id);
	return GRO_NORMAL;
drop:
	dev_kfree_skb(skb);
	return GRO_DROP;
}

static void fb_tgd_ql_set_mac_addr(struct tgd_terra_driver *fb_drv_data,
				   uint8_t *mac_addr)
{
	tgd_set_if_mac_addr(fb_drv_data, mac_addr);
	/* Send the netlink message to the subscribers that the device is up
	 * with the new MAC. The vendor driver can update the MAC as part of
	 * init process, after the driver is registered.
	 */
	TGD_DBG_CTRL_ERROR("fb_tgd_bh_set_mac_addr: Send UP with MAC 0x%llx \n",
			   fb_drv_data->macaddr);
	tgd_nlsdn_send_device_updown_status(fb_drv_data, DEVICE_UP);
}

static void fb_tgd_ql_rx_event(void *ctx, u16 id, u8 *event, u32 size)
{
	struct tgd_ql_priv *ql_priv;

	if (id != 0)
		return;

	ql_priv = ctx;
	TGD_DBG_DATA_INFO("Rx Event %p size %d %x:%x:%x:%x:%x:%x:%x:%x:%x:%x\n",
			  event, size, event[0], event[1], event[2], event[3],
			  event[4], event[5], event[6], event[7], event[8],
			  event[9]);

	tgd_terra_rx_event_handler(ql_priv->drv_data, event, size);
}

static void fb_tgd_ql_flow_control(void *ctx, u8 cid, bool stop_tx)
{
	struct tgd_ql_priv *ql_priv;
	struct tgd_terra_dev_priv *priv;
	int link_id;

	ql_priv = ctx;

	link_id = ql_priv->tx_cids[cid];
	if (link_id >= WIL_SLAVE_MAX_LINKS)
		return;

	priv = ql_priv->links[link_id].dev_priv;
	if (priv == NULL)
		return;

	tgd_flow_control_common(ql_priv->drv_data, priv, link_id, 0, stop_tx);
}

static void fb_tgd_ql_connected(void *ctx, int tx_link_id, int rx_link_id,
				const u8 *mac, u8 cid)
{
	struct tgd_ql_priv *ql_priv;

	ql_priv = ctx;

	TGD_DBG_CTRL_DBG("Connected: %pM, link_id tx %d rx %d CID %u\n", mac,
			 tx_link_id, rx_link_id, cid);

	if (cid >= WIL_SLAVE_MAX_CID) {
		TGD_DBG_CTRL_ERROR("Invalid cid: %u\n", cid);
		return;
	}
	if (tx_link_id < 0 || tx_link_id >= WIL_SLAVE_MAX_LINKS) {
		TGD_DBG_CTRL_ERROR("Invalid tx link id: %d\n", tx_link_id);
		return;
	}
	/* rx_link_id will be <0 when not specified */
	if (rx_link_id >= WIL_SLAVE_MAX_LINKS) {
		TGD_DBG_CTRL_ERROR("Invalid rx link id: %d\n", rx_link_id);
		return;
	}

	ether_addr_copy(ql_priv->links[tx_link_id].mac, mac);
	ql_priv->links[tx_link_id].cid = cid;
	ql_priv->links[tx_link_id].is_rx = false;
	ql_priv->tx_cids[cid] = tx_link_id;
	if (rx_link_id >= 0 && rx_link_id != tx_link_id) {
		ether_addr_copy(ql_priv->links[rx_link_id].mac, mac);
		ql_priv->links[rx_link_id].cid = cid;
		ql_priv->links[rx_link_id].is_rx = true;
		ql_priv->rx_cids[cid] = rx_link_id;
	}
}

static void fb_tgd_ql_disconnected(void *ctx, u8 cid)
{
	struct tgd_ql_priv *ql_priv;
	int tx_link_id, rx_link_id;

	TGD_DBG_CTRL_DBG("Disconnected: CID %u\n", cid);
	ql_priv = ctx;

	if (cid >= WIL_SLAVE_MAX_CID) {
		TGD_DBG_CTRL_ERROR("Invalid cid: %u\n", cid);
		return;
	}

	tx_link_id = ql_priv->tx_cids[cid];
	if (tx_link_id < WIL_SLAVE_MAX_LINKS) {
		ql_priv->links[tx_link_id].cid = WIL_SLAVE_MAX_CID;
		ql_priv->tx_cids[cid] = WIL_SLAVE_MAX_LINKS;
	}
	rx_link_id = ql_priv->rx_cids[cid];
	if (rx_link_id < WIL_SLAVE_MAX_LINKS) {
		ql_priv->links[rx_link_id].cid = WIL_SLAVE_MAX_CID;
		ql_priv->rx_cids[cid] = WIL_SLAVE_MAX_LINKS;
	}

	mutex_lock(&ql_priv->mutex);
	if (ql_priv->disconnected[cid]) {
		complete(ql_priv->disconnected[cid]);
	}
	mutex_unlock(&ql_priv->mutex);
}

static void fb_tgd_ql_set_channel_evt(void *ctx, u8 channel)
{
}

static void fb_tgd_ql_dissoc_links(struct tgd_ql_priv *ql_priv)
{
	long timeout_ms = 1000;
	int i;
	u8 cid;
	struct tgd_ql_link_info *link;
	struct tgd_terra_dev_priv *dev_priv;
	struct tgd_terra_driver *fb_drv_data;

	if (ql_priv == NULL) {
		TGD_DBG_CTRL_INFO("%s: Invalid bh ctx\n", __FUNCTION__);
		return;
	}

	fb_drv_data = ql_priv->drv_data;

	/*
	 * Send disassoc request to firmware for terra devices that are in the
	 * TG_LINKUP state. Wait for the driver to finish processing respective
	 * disconnect events before proceeding and allowing a firmware shutdown
	 * request to be sent.
	 */
	list_for_each_entry(dev_priv, &fb_drv_data->dev_q_head, list_entry)
	{
		if (dev_priv->link_state == TG_LINKUP) {
			link = &ql_priv->links[dev_priv->tx_link];
			cid = link->cid;

			mutex_lock(&ql_priv->mutex);
			ql_priv->disconnected[cid] =
			    (struct completion *)kmalloc(
				sizeof(struct completion), GFP_KERNEL);
			init_completion(ql_priv->disconnected[cid]);

			tgd_send_disassoc_req(fb_drv_data,
					      &dev_priv->link_sta_addr);
			mutex_unlock(&ql_priv->mutex);
		}
	}

	for (i = 0; i < WIL_SLAVE_MAX_CID; i++) {
		if (ql_priv->disconnected[i]) {
			wait_for_completion_timeout(
			    ql_priv->disconnected[i],
			    msecs_to_jiffies(timeout_ms));
			mutex_lock(&ql_priv->mutex);
			kfree(ql_priv->disconnected[i]);
			ql_priv->disconnected[i] = NULL;
			mutex_unlock(&ql_priv->mutex);
		}
	}
}

static void fb_tgd_ql_slave_going_down(void *ctx)
{
	fb_tgd_ql_dissoc_links((struct tgd_ql_priv *)ctx);
}

static struct wil_slave_rops fb_tgd_slave_rops = {
    .api_version = WIL_SLAVE_API_VERSION,

    .rx_event = fb_tgd_ql_rx_event,
    .rx_data = fb_tgd_ql_rx_data,
    .flow_control = fb_tgd_ql_flow_control,
    .connected = fb_tgd_ql_connected,
    .disconnected = fb_tgd_ql_disconnected,
    .set_channel = fb_tgd_ql_set_channel_evt,
    .slave_going_down = fb_tgd_ql_slave_going_down,
};

void fb_tgd_bh_setup_netdev(struct tgd_terra_dev_priv *priv)
{
	struct net_device *dev;

	if (module_has_dvpp)
		return;

	dev = priv->dev;

	/* DEBUGGING, QTI: initialize HW offloads according to
	 * the offloads supported by the QCA6436 chip.
	 * Do not enable TSO it is not supported by FW
	 */
	dev->hw_features =
	    NETIF_F_HW_CSUM | NETIF_F_RXCSUM | NETIF_F_SG | NETIF_F_GRO;

	dev->features |= dev->hw_features;
}

int fb_tgd_bh_api_version(struct tgd_terra_driver *fb_drv_data,
			  int *drv_version, int *vendor_ver)
{
	struct tgd_ql_priv *ql_priv;

	ql_priv = fb_drv_data->bh_ctx;

	*drv_version = WIL_SLAVE_API_VERSION;
	*vendor_ver = ql_priv->slave_ops.api_version;
	return 0;
}

int fb_tgd_bh_register_client(struct tgd_terra_driver *fb_drv_data)
{
	struct tgd_ql_priv *ql_priv;
	int ret;

	ql_priv = fb_drv_data->bh_ctx;

	ret = ql_priv->slave_ops.register_master(ql_priv->slave_dev, ql_priv,
						 &fb_tgd_slave_rops);
	if (ret != 0) {
		TGD_DBG_CTRL_ERROR(
		    "Registration with BH driver failed, error: %d\n", ret);
	} else {
		TGD_DBG_CTRL_INFO("Restration with BH driver successful drv "
				  "data %p\n",
				  fb_drv_data);
	}
	return ret;
}

// Unregister Callbacks with WLAN Driver
int fb_tgd_bh_unregister_client(struct tgd_terra_driver *fb_drv_data)
{
	struct tgd_ql_priv *ql_priv;

	ql_priv = fb_drv_data->bh_ctx;
	if (ql_priv == NULL)
		return 0;

	ql_priv->slave_ops.unregister_master(ql_priv->slave_dev);
	/*
	 * There should be no links and active senders and receivers
	 * at this point, there should be no need for extra fancy
	 * draining - upper level code takes care of that.
	 */
	mutex_destroy(&ql_priv->mutex);
	kfree(ql_priv);

	fb_drv_data->bh_ctx = NULL;
	return 0;
}

int fb_tgd_bh_api_init(struct device *dev, struct tgd_terra_driver *fb_drv_data)
{
	struct tgd_ql_priv *ql_priv;
	struct wil_slave_platdata *pdata;
	u8 mac_addr[ETH_ALEN];

	pdata = dev_get_platdata(dev);
	if (pdata == NULL)
		return -ENODEV;

	if (pdata->ops->api_version != WIL_SLAVE_API_VERSION) {
		TGD_DBG_CTRL_ERROR("ERROR: bhVer: 0x%x != fbVer: 0x%x\n",
				   pdata->ops->api_version,
				   WIL_SLAVE_API_VERSION);
		return -EPERM;
	}

	ql_priv = kzalloc(sizeof(*ql_priv), GFP_KERNEL);
	if (ql_priv == NULL)
		return -ENOMEM;

	mutex_init(&ql_priv->mutex);
	ql_priv->drv_data = fb_drv_data;
	ql_priv->slave_dev = pdata->dev_ctx;
	ql_priv->slave_ops = *pdata->ops;

	fb_drv_data->bh_ctx = ql_priv;

	pdata->ops->get_mac(pdata->dev_ctx, mac_addr);
	fb_tgd_ql_set_mac_addr(fb_drv_data, mac_addr);
	return 0;
}

void fb_tgd_bh_cleanup_links(struct tgd_terra_driver *fb_drv_data)
{
	fb_tgd_ql_dissoc_links(fb_drv_data->bh_ctx);
}

const struct platform_device_id tg_bh_id_table[] = {
    {"qwilvendor", 0},
    {},
};
