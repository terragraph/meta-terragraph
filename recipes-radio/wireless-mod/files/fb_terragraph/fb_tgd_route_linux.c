/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Terragraph Linux routing backend */

#define pr_fmt(fmt) "fb_tgd_terragraph: " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>

#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/skbuff.h>

#include <fb_tg_fw_driver_if.h>
#include "fb_tgd_terragraph.h"
#include "fb_tgd_fw_if.h"
#include "fb_tgd_debug.h"
#include "fb_tgd_route.h"
#include "fb_tgd_amsdu.h"

static int fb_tgd_rt_linux_add_device(struct tgd_terra_dev_priv *dev_priv)
{
	return 0;
}

static void fb_tgd_rt_linux_del_device(struct tgd_terra_dev_priv *dev_priv)
{
}

static void fb_tgd_rt_linux_set_link_state(struct tgd_terra_dev_priv *dev_priv,
					   tgLinkStatus state)
{
}

static inline void fb_tgd_rt_linux_rx_pkt(struct tgd_terra_dev_priv *dev_priv,
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

static void fb_tgd_rt_linux_rx(struct tgd_terra_dev_priv *dev_priv,
			       struct sk_buff *skb)
{
	struct ethhdr *ehdr;
	int ret;

	ehdr = (struct ethhdr *)skb->data;

	if (ehdr->h_proto != htons(ETH_P_TGAMSDU)) {
		fb_tgd_rt_linux_rx_pkt(dev_priv, skb);
	} else {
		struct sk_buff_head list;

		__skb_queue_head_init(&list);

		ret = tgd_amsdu_decapsulate(skb, &list);
		if (ret != 0)
			return;

		while (!skb_queue_empty(&list)) {
			skb = __skb_dequeue(&list);
			fb_tgd_rt_linux_rx_pkt(dev_priv, skb);
		}
	}
}

static void fb_tgd_rt_linux_flow_control(struct tgd_terra_dev_priv *dev_priv,
					 unsigned char qid, bool state)
{
	// Linux flow control is part of tgd_flow_control_common()
}

static void fb_tgd_rt_linux_module_fini(struct tgd_terra_driver *tgd_data)
{
	struct fb_tgd_routing_backend *rtb;

	rtb = tgd_data->rt_backend;
	if (rtb != NULL)
		kfree(rtb);
	tgd_data->rt_backend = NULL;
}

int fb_tgd_rt_linux_module_init(struct tgd_terra_driver *tgd_data)
{
	struct fb_tgd_routing_backend *rtb;

	rtb = kzalloc(sizeof(struct fb_tgd_routing_backend), GFP_KERNEL);
	if (rtb == NULL)
		return -ENOMEM;

	rtb->rt_mod_fini = fb_tgd_rt_linux_module_fini;
	rtb->rt_add_dev = fb_tgd_rt_linux_add_device;
	rtb->rt_del_dev = fb_tgd_rt_linux_del_device;
	rtb->rt_set_link_state = fb_tgd_rt_linux_set_link_state;
	rtb->rt_flow_control = fb_tgd_rt_linux_flow_control;
	rtb->rt_tx = tgd_terra_bh_tx_common;
	rtb->rt_rx = fb_tgd_rt_linux_rx;
	tgd_data->rt_backend = rtb;
	return 0;
}
