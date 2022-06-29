/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Terragraph NSS routing backend */

#define pr_fmt(fmt) "fb_tgd_route_pfe: " fmt

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

#include <linux/pfe_tg_api.h>

#include <fb_tg_fw_driver_if.h>
#include "fb_tgd_terragraph.h"
#include "fb_tgd_fw_if.h"
#include "fb_tgd_debug.h"
#include "fb_tgd_route.h"
#include "fb_tgd_amsdu.h"
#include "fb_tgd_backhaul.h"

/* Map queue id's to corresponding COS values */
static tg_pktprio_t qid_to_cos[FB_TGD_BH_MQ_QUEUE_NUM] = {
    [FB_TGD_MQ_BK] = TG_PRIO_LO,
    [FB_TGD_MQ_BE] = TG_PRIO_LO,
    [FB_TGD_MQ_VI] = TG_PRIO_HI,
    [FB_TGD_MQ_VO] = TG_PRIO_HI,
};

struct fb_tgd_pfe_rt_backend {
	struct fb_tgd_routing_backend rt_base;
	struct tg_provider_ops *pfe_ops;
	tg_api_ctx_t *pfe_ctx;
};

struct fb_tgd_pfe_rt_devpriv {
	struct tg_provider_ops *pfe_ops;
	tg_api_port_t *pfe_port;
};

static inline struct fb_tgd_pfe_rt_devpriv *
tgd_pfe_get_priv(struct tgd_terra_dev_priv *priv)
{
	return priv->rt_data;
}

static inline void tgd_pfe_set_priv(struct tgd_terra_dev_priv *priv,
				    struct fb_tgd_pfe_rt_devpriv *pfe_priv)
{
	priv->rt_data = pfe_priv;
}

static inline struct fb_tgd_pfe_rt_backend *
tgd_pfe_get_backend(struct tgd_terra_dev_priv *dev_priv)
{
	struct tgd_terra_driver *fb_drv_data;

	fb_drv_data = dev_priv->fb_drv_data;
	return container_of(fb_drv_data->rt_backend,
			    struct fb_tgd_pfe_rt_backend, rt_base);
}

static void tgd_pfe_tx_packet(tg_consumer_dev_t *dev, tg_packet_t *pkt,
			      const tg_tx_packet_mdata_t *mdata)
{
	dev->netdev_ops->ndo_start_xmit(pkt, dev);
}

struct tg_consumer_ops pfe_consumer_ops = {
    .tgapi_tx_packet = tgd_pfe_tx_packet,
};

static void tgd_pfe_if_del(struct fb_tgd_pfe_rt_devpriv *pfe_priv)
{
	if (pfe_priv->pfe_ops != NULL)
		pfe_priv->pfe_ops->tgapi_release_port(pfe_priv->pfe_port);
	pfe_priv->pfe_port = NULL;
	pfe_priv->pfe_ops = NULL;
}

static int tgd_pfe_if_unbind(struct tgd_terra_dev_priv *dev_priv,
			     struct fb_tgd_pfe_rt_devpriv *pfe_priv)
{
	return 0;
}

static int fb_tgd_rt_pfe_add_device(struct tgd_terra_dev_priv *dev_priv)
{
	struct fb_tgd_pfe_rt_backend *pfe_backend;
	struct fb_tgd_pfe_rt_devpriv *pfe_priv;
	int ret;

	pfe_priv = kzalloc(sizeof(*pfe_priv), GFP_KERNEL);
	if (pfe_priv == NULL)
		return -ENOMEM;

	pfe_backend = tgd_pfe_get_backend(dev_priv);

	/*
	 * Allocate the port instance
	 */
	ret = pfe_backend->pfe_ops->tgapi_alloc_port(
	    pfe_backend->pfe_ctx, dev_priv->dev, &pfe_consumer_ops,
	    &pfe_priv->pfe_port);
	if (ret != 0) {
		kfree(pfe_priv);
		return ret;
	}
	pfe_priv->pfe_ops = pfe_backend->pfe_ops;
	tgd_pfe_set_priv(dev_priv, pfe_priv);

	return 0;
}

static void fb_tgd_rt_pfe_del_device(struct tgd_terra_dev_priv *dev_priv)
{
	struct fb_tgd_pfe_rt_devpriv *pfe_priv;

	pfe_priv = tgd_pfe_get_priv(dev_priv);
	if (pfe_priv == NULL)
		return;

	/* Stop handling of traffic on wlan port */
	tgd_pfe_if_unbind(dev_priv, pfe_priv);

	/* Disassociate from virtual wlan port */
	tgd_pfe_if_del(pfe_priv);

	tgd_pfe_set_priv(dev_priv, NULL);
	kfree(pfe_priv);
}

static int amsdu_data_timeout_us = 1000;
module_param(amsdu_data_timeout_us, int, 0644);
MODULE_PARM_DESC(amsdu_data_timeout_us,
		 "Timeout for low priority A-MSDU context");

static int amsdu_data_size = 6000;
module_param(amsdu_data_size, int, 0644);
MODULE_PARM_DESC(amsdu_data_size,
		 "Maximum size for low-priority A-MSDU frames");

static void fb_tgd_rt_pfe_configure_amsdu(struct tgd_terra_dev_priv *dev_priv)
{
	struct fb_tgd_pfe_rt_devpriv *pfe_priv;
	tg_amsdu_config_t cfg;

	memset(&cfg, 0, sizeof(cfg));

	/* Hardcoded configuration */
	cfg.max_size = amsdu_data_size;
	cfg.timeout_us = amsdu_data_timeout_us;
	cfg.flags = (1 << AMSDU_CFG_FLAG_PROPRIETARY_FORMAT_BIT);

	/* Tell PFE what addresses to use */
	memcpy(cfg.src_mac, dev_priv->dev->dev_addr, sizeof(cfg.src_mac));
	memcpy(cfg.dst_mac, dev_priv->link_sta_addr.addr, sizeof(cfg.dst_mac));

	pfe_priv = tgd_pfe_get_priv(dev_priv);
	pfe_priv->pfe_ops->tgapi_amsdu_configure(pfe_priv->pfe_port, TG_PRIO_LO,
						 &cfg);
}

static void fb_tgd_rt_pfe_set_link_state(struct tgd_terra_dev_priv *dev_priv,
					 tgLinkStatus state)
{
	struct fb_tgd_pfe_rt_devpriv *pfe_priv;

	pfe_priv = tgd_pfe_get_priv(dev_priv);
	if (pfe_priv == NULL || pfe_priv->pfe_ops == NULL)
		return;

	if (state == TG_LINKUP) {
		/* Configure A-MSDU context here */
		fb_tgd_rt_pfe_configure_amsdu(dev_priv);
		pfe_priv->pfe_ops->tgapi_open_port(pfe_priv->pfe_port);
	} else {
		pfe_priv->pfe_ops->tgapi_close_port(pfe_priv->pfe_port);
	}
}

static inline void fb_tgd_rt_pfe_rx_pkt(struct tgd_terra_dev_priv *dev_priv,
					struct sk_buff *skb)
{
	size_t len;
	int ret;

	len = skb->len;
	skb->protocol = eth_type_trans(skb, skb->dev);
	ret = netif_rx(skb);

	TGD_DBG_DATA_INFO("Receive %s len: %zu, netif_rx: %d\n",
			  netdev_name(dev_priv->dev), len, ret);
}

static void fb_tgd_rt_pfe_rx(struct tgd_terra_dev_priv *dev_priv,
			     struct sk_buff *skb)
{
	tg_rx_packet_mdata_t mdata;
	struct fb_tgd_pfe_rt_devpriv *pfe_priv;
	struct ethhdr *ehdr;
	int ret;

	ehdr = (struct ethhdr *)skb->data;

	/* Feed not aggregated frames directly to host */
	if (ehdr->h_proto != htons(ETH_P_TGAMSDU)) {
		fb_tgd_rt_pfe_rx_pkt(dev_priv, skb);
		return;
	}

	pfe_priv = tgd_pfe_get_priv(dev_priv);
	mdata.flags = (1 << RX_MDATA_FLAG_AMSDU_BIT) |
		      (1 << RX_MDATA_FLAG_PROPRIETARY_AMSDU_BIT);

	ret =
	    pfe_priv->pfe_ops->tgapi_rx_packet(pfe_priv->pfe_port, skb, &mdata);
#ifdef TG_PFE_AMSDU_FALLBACK
	if (ret != 0) { /* Fall back to software decode */
		struct sk_buff_head list;

		__skb_queue_head_init(&list);

		ret = tgd_amsdu_decapsulate(skb, &list);
		if (ret != 0)
			return;

		while (!skb_queue_empty(&list)) {
			skb = __skb_dequeue(&list);
			fb_tgd_rt_pfe_rx_pkt(dev_priv, skb);
		}
	}
#else
	if (ret != 0) { /* PFE is full and fat, drop the packet */
		terra_dev_stats_inc(dev_priv, RX_DROP_PACKETS, 1);
		dev_kfree_skb(skb);
	}
#endif
}

static void fb_tgd_rt_pfe_flow_control(struct tgd_terra_dev_priv *dev_priv,
				       unsigned char qid, bool state)
{
	struct fb_tgd_pfe_rt_devpriv *pfe_priv;
	struct netdev_queue *dev_queue;
	tg_pktprio_t prio;

	pfe_priv = tgd_pfe_get_priv(dev_priv);

	/* Convert queue back to cos value */

	dev_queue = netdev_get_tx_queue(dev_priv->dev, qid);
	if (state) {
		netif_tx_stop_queue(dev_queue);
	} else {
		netif_tx_wake_queue(dev_queue);
	}

	if (pfe_priv->pfe_ops != NULL) {
		prio = qid_to_cos[qid];
		pfe_priv->pfe_ops->tgapi_flow_control(pfe_priv->pfe_port, prio,
						      state);
	}
}

static void fb_tgd_rt_pfe_module_fini(struct tgd_terra_driver *tgd_data)
{
	struct fb_tgd_pfe_rt_backend *rtn;

	if (tgd_data->rt_backend == NULL)
		return;

	rtn = container_of(tgd_data->rt_backend, struct fb_tgd_pfe_rt_backend,
			   rt_base);

	/* Release the API */
	if (rtn->pfe_ops != NULL)
		rtn->pfe_ops->tgapi_fini(rtn->pfe_ctx);

	/* Free tthe backend */
	kfree(rtn);
	tgd_data->rt_backend = NULL;
}

int fb_tgd_rt_pfe_module_init(struct tgd_terra_driver *tgd_data)
{
	struct fb_tgd_pfe_rt_backend *rtn;
	struct fb_tgd_routing_backend *rtb;
	struct tg_provider_ops *pfe_ops;
	int ret;

	/* Obtain the pointer to PFE API */
	pfe_ops = nxp_get_tgops_indirect();
	if (pfe_ops == NULL)
		return -ENODEV;

	/* Allocate the backend */
	rtn = kzalloc(sizeof(*rtn), GFP_KERNEL);
	if (rtn == NULL) {
		return -ENOMEM;
	}
	tgd_data->rt_backend = rtb = &rtn->rt_base;

	/* Initialize the API */
	ret = pfe_ops->tgapi_init(&rtn->pfe_ctx);
	if (ret != 0) {
		fb_tgd_rt_pfe_module_fini(tgd_data);
		return ret;
	}
	rtn->pfe_ops = pfe_ops;

	/* Populate the method table */
	rtb->rt_mod_fini = fb_tgd_rt_pfe_module_fini;
	rtb->rt_add_dev = fb_tgd_rt_pfe_add_device;
	rtb->rt_del_dev = fb_tgd_rt_pfe_del_device;
	rtb->rt_set_link_state = fb_tgd_rt_pfe_set_link_state;
	rtb->rt_flow_control = fb_tgd_rt_pfe_flow_control;
	rtb->rt_tx = tgd_terra_bh_tx_common;
	rtb->rt_rx = fb_tgd_rt_pfe_rx;

	/* Limit of number of links we can possibly support  to one */
	if (tgd_data->max_link_count > 1)
		tgd_data->max_link_count = 1;

	return 0;
}
