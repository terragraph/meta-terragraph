/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Terragraph main source file */

#define pr_fmt(fmt) "fb_tgd_terragraph: " fmt

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>

#include <linux/cpumask.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <asm/checksum.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <linux/u64_stats_sync.h>
#include <linux/reboot.h>
#include <linux/platform_device.h>
#include <linux/genetlink.h>
#include <linux/mod_devicetable.h>

#include <net/dsfield.h>

#include <net/sch_generic.h>
#include <net/pkt_sched.h>
#include <fb_tg_qdisc_pfifofc_if.h>

#include <fb_tg_fw_driver_if.h>
#include "fb_tgd_backhaul.h"
#include "fb_tgd_terragraph.h"
#include "fb_tgd_fw_if.h"
#include "fb_tgd_gps_if.h"
#include "fb_tgd_debug.h"
#include "fb_tgd_route.h"
#include "fb_tgd_queue_stats.h"
#include "fb_tgd_cfg80211.h"
#include "fb_tgd_nlsdn.h"

/* Compatibility with earlier kernels */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)

#if BITS_PER_LONG == 32 && defined(CONFIG_SMP)
#define u64_stats_init(syncp) seqcount_init(syncp.seq)
#else
#define u64_stats_init(syncp)                                                  \
	do {                                                                   \
	} while (0)
#endif

#define u64_stats_fetch_begin_irq u64_stats_fetch_begin_bh
#define u64_stats_fetch_retry_irq u64_stats_fetch_retry_bh

#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
#define tg_min_mtu(ndev) 68
#define tg_max_mtu(ndev) TGD_WLAN_MTU_SIZE
#else
#define tg_min_mtu(ndev) (ndev)->min_mtu
#define tg_max_mtu(ndev) (ndev)->max_mtu
#endif

static int tgd_num_of_virt_links = TERRAGPH_NUM_OF_VIRTUAL_LINKS;
module_param(tgd_num_of_virt_links, int, 0444);

static bool tgd_auto_up = false;
module_param(tgd_auto_up, bool, 0444);

static int tgd_def_mtu = -1;
module_param(tgd_def_mtu, int, 0444);

bool module_has_dvpp;
module_param(module_has_dvpp, bool, 0444);

// Should pass bit mask value, 0000_0000 <ctrl16bit_data16bit> disable all
// messages
// B0-ErrorLvl, B1-DebugLvl, B2-InfoLvl
u32 tgd_dbg_enable_level =
    DBG_LVL_CTRL_ERROR | DBG_LVL_DATA_ERROR | DBG_LVL_CFG80211_DBG;
module_param_named(dbg_mask, tgd_dbg_enable_level, uint, 0644);

int tgd_enable_nss = 0;
module_param(tgd_enable_nss, int, 0444);

#ifdef TG_ENABLE_PFIFOFC
/*
 * Default values for the maximum queue length, and the
 * queue lengths for flow control signaling for the different
 * flow control levels. The current implementation supports
 * two levels of flow control signaling via a callback.
 */
// Default value for max queue length for each priority band
#define FB_TGD_PFIFOFC_QLEN (640)
// Default value for hysteresis between turning Flow Control OFF from ON
// for each Flow Control level.
#define FB_TGD_PFIFOFC_FC_HYST (80)
// Default value for signaling flow control ON for RED colored packets
#define FB_TGD_PFIFOFC_QLEN_RED_ON (320)
// Default value for signaling flow control OFF for RED colored packets
#define FB_TGD_PFIFOFC_QLEN_RED_OFF                                            \
	(FB_TGD_PFIFOFC_QLEN_RED_ON - FB_TGD_PFIFOFC_FC_HYST)
// Default value for signaling flow control ON for ALL packets
#define FB_TGD_PFIFOFC_QLEN_ALL_ON (520)
// Default value for signaling flow control OFF for ALL packets
#define FB_TGD_PFIFOFC_QLEN_ALL_OFF                                            \
	(FB_TGD_PFIFOFC_QLEN_ALL_ON - FB_TGD_PFIFOFC_FC_HYST)

// Module params for pfifofc qdisc
int tgd_enable_pfifofc = 1;
module_param(tgd_enable_pfifofc, int, 0444);

static int tgd_qdisc_maxqueue_len = FB_TGD_PFIFOFC_QLEN;
module_param(tgd_qdisc_maxqueue_len, int, 0444);
static int tgd_qdisc_red_on = FB_TGD_PFIFOFC_QLEN_RED_ON;
module_param(tgd_qdisc_red_on, int, 0444);
static int tgd_qdisc_red_off = FB_TGD_PFIFOFC_QLEN_RED_OFF;
module_param(tgd_qdisc_red_off, int, 0444);
static int tgd_qdisc_all_on = FB_TGD_PFIFOFC_QLEN_ALL_ON;
module_param(tgd_qdisc_all_on, int, 0444);
static int tgd_qdisc_all_off = FB_TGD_PFIFOFC_QLEN_ALL_OFF;
module_param(tgd_qdisc_all_off, int, 0444);
#endif /* TG_ENABLE_PFIFOFC */

/* There is one driver per device.  They are all chained up here */
struct klist tgd_drivers_list;

struct dentry *fb_tgd_debug_root_dir;

#ifdef TG_ENABLE_PFE
int tgd_enable_pfe = 1;
module_param(tgd_enable_pfe, int, 0444);
#endif

#ifdef TG_ENABLE_DPAA2
int tgd_enable_dpaa2 = 1;
module_param(tgd_enable_dpaa2, int, 0444);
#endif

/*
 * Do not bind event handling to any cpu by default, but allow one to be
 * overridden through compile time definition  and at runtime.
 */
#ifndef TGD_RX_EVENT_CPU
#define TGD_RX_EVENT_CPU WORK_CPU_UNBOUND
#endif
static int tgd_rx_event_cpu = TGD_RX_EVENT_CPU;

static int tgd_param_set_rx_event_cpu(const char *val,
				      const struct kernel_param *kp)
{
	int cpu, ret;

	ret = kstrtoint(val, 0, &cpu);
	if (ret != 0)
		return ret;

	/* Clear the binding if requested */
	if (cpu == -1)
		cpu = WORK_CPU_UNBOUND;
	else { /* Validate the CPU id */

		if (cpu >= num_possible_cpus() || !cpu_online(cpu))
			return -EINVAL;
	}

	tgd_rx_event_cpu = cpu;
	return 0;
}

static int tgd_param_get_rx_event_cpu(char *val, const struct kernel_param *kp)
{
	int cpu;

	/* Convert WORK_CPU_UNBOUND to -1 */
	cpu = tgd_rx_event_cpu;
	if (cpu == WORK_CPU_UNBOUND)
		cpu = -1;

	return snprintf(val, PAGE_SIZE, "%d", cpu);
}

static const struct kernel_param_ops tgd_rx_evt_cpu_ops = {
    .set = tgd_param_set_rx_event_cpu,
    .get = tgd_param_get_rx_event_cpu,
};

module_param_cb(tgd_rx_event_cpu, &tgd_rx_evt_cpu_ops, &tgd_rx_event_cpu, 0644);

/*
 * For platforms that support more than one A-MSDU format, allow one to be
 * used specified by the kernel module parameter, in case corresponding
 * routing backend needs to know about it.
 */
tgd_amsdu_frame_format_t tgd_bh_amsdu_ff = TG_SHORT;

static int tgd_param_set_bh_amsdu_ff(const char *val,
				     const struct kernel_param *kp)
{
	int rv = 0;
	char valcp[16];
	char *s;

	strncpy(valcp, val, 16);
	valcp[15] = '\0';

	s = strstrip(valcp);

	/* Treat empty parameter as request to maintain status-quo */
	if (strcmp(s, "tg-short") == 0)
		tgd_bh_amsdu_ff = TG_SHORT;
	else if (strcmp(s, "std-short") == 0)
		tgd_bh_amsdu_ff = STD_SHORT;
	else if (*s != '\0')
		rv = -EINVAL;
	return rv;
}

static int tgd_param_get_bh_amsdu_ff(char *val, const struct kernel_param *kp)
{
	if (tgd_bh_amsdu_ff == TG_SHORT)
		return snprintf(val, PAGE_SIZE, "%s", "tg-short");
	if (tgd_bh_amsdu_ff == STD_SHORT)
		return snprintf(val, PAGE_SIZE, "%s", "std-short");
	return snprintf(val, PAGE_SIZE, "%s", "unknown");
}

static const struct kernel_param_ops tgd_bh_amsdu_ff_ops = {
    .set = tgd_param_set_bh_amsdu_ff,
    .get = tgd_param_get_bh_amsdu_ff,
};
module_param_cb(tgd_bh_amsdu_frame_format, &tgd_bh_amsdu_ff_ops,
		&tgd_bh_amsdu_ff, 0644);

extern int tgd_nlsdn_init(void);
extern void tgd_nlsdn_exit(void);

void tgd_process_fb_events(struct work_struct *work);

/*
 * We can have four instances of terra_driver each controlling one
 * baseband device.  The key to identify it is the mac stored as
 * a u64
 */
struct tgd_terra_driver *tgd_find_fb_drv(u64 key)
{
	struct tgd_terra_driver *fb_drv = NULL;
	struct klist_iter i;
	struct klist_node *n;

	klist_iter_init_node(&tgd_drivers_list, &i, NULL);
	while ((n = klist_next(&i)) != NULL) {
		fb_drv =
		    container_of(n, struct tgd_terra_driver, driver_list_node);
		if (key)
			TGD_DBG_DATA_INFO(
			    "Trying to find fb_drv for %llx key %llx\n", key,
			    fb_drv->macaddr);
		if (!key)
			break; // return the first fb_drv for now;
		/* key is macaddr stored as u64, so == for comparison */
		if (fb_drv->macaddr == key)
			break;
		fb_drv = NULL;
	}
	klist_iter_exit(&i);
	return (fb_drv);
}

// Find the Virtual interface based on the Src Address of the packet
struct net_device *
tgd_terra_find_net_device_by_mac(struct tgd_terra_driver *fb_drv_data,
				 tgEthAddr *link_mac_addr)
{
	struct tgd_terra_dev_priv *priv;
	int i = 0;

	if (link_mac_addr == NULL) {
		TGD_DBG_DATA_ERROR("Error = link_mac_addr = NULL\n");
		return NULL;
	}

	list_for_each_entry(priv, &fb_drv_data->dev_q_head, list_entry)
	{
		if (memcmp(&priv->link_sta_addr, link_mac_addr, ETH_ALEN) ==
		    0) {
			TGD_DBG_DATA_INFO("DevFound %p %pM INDEX = %d "
					  "Ltx:%d Lrx:%d\n",
					  priv, link_mac_addr->addr, i,
					  priv->tx_link, priv->rx_link);
			return priv->dev;
		}
		i++;
	}

	return NULL;
}

// Find the Virtual interface based on the Src Address of the packet
struct net_device *
tgd_terra_find_net_device_by_link(struct tgd_terra_driver *fb_drv_data,
				  int link_id)
{
	struct tgd_terra_dev_priv *priv;

	list_for_each_entry(priv, &fb_drv_data->dev_q_head, list_entry)
	{
		if (link_id == priv->tx_link || link_id == priv->rx_link)
			return priv->dev;
	}
	return NULL;
}

int tgd_terra_del_link_info(struct tgd_terra_driver *fb_drv_data,
			    tgEthAddr *link_mac_addr)
{
	struct tgd_terra_dev_priv *priv;
	struct net_device *dev;

	dev = tgd_terra_find_net_device_by_mac(fb_drv_data, link_mac_addr);
	if (dev == NULL) {
		TGD_DBG_CTRL_INFO("linkStaAddr %pM not found\n", link_mac_addr);
		return -1;
	}
	priv = netdev_priv(dev);
	TGD_DBG_CTRL_INFO("Event DEL_LINK sa %pM, da %pM\n", dev->dev_addr,
			  link_mac_addr);
	fb_tgd_bh_del_links_info(priv);
	return 0;
}

int tgd_terra_set_link_status(struct tgd_terra_driver *fb_drv_data,
			      tgEthAddr *link_mac_addr, tgLinkStatus link_state)
{
	struct net_device *dev;
	struct tgd_terra_dev_priv *priv;

	dev = tgd_terra_find_net_device_by_mac(fb_drv_data, link_mac_addr);
	if (dev == NULL) {
		TGD_DBG_CTRL_INFO("linkStaAddr %pM not found\n", link_mac_addr);
		return -1;
	}

	priv = netdev_priv(dev);
	mutex_lock(&priv->link_lock);

	TGD_DBG_CTRL_INFO("Setting Link Status %d\n", link_state);

	switch (link_state) {
	case TG_LINKUP:
		netif_carrier_on(dev);
		netif_tx_wake_all_queues(dev);
		if (priv->link_state != TG_LINKPAUSE)
			fb_drv_data->link_count++;
		priv->link_state = link_state;
		break;
	case TG_LINKPAUSE:
		if (priv->link_state != TG_LINKINIT) {
			priv->link_state = link_state;
		}
		netif_carrier_off(dev);
		netif_tx_disable(dev);
		break;
	case TG_LINKDOWN:
		// TBD: Change it to netif_stop_queue
		netif_carrier_off(dev);
		netif_tx_disable(dev);

		if (priv->link_state != TG_LINKINIT) {
			priv->link_state = link_state;
			fb_drv_data->link_count--;
		}
		break;
	default:
		break;
	}
	link_state = priv->link_state;
	mutex_unlock(&priv->link_lock);
	tgd_rt_set_link_state(priv, link_state);

	return 0;
}

// Set All the interface MAC address to the same when we get Fw Init Response
void tgd_set_if_mac_addr(struct tgd_terra_driver *fb_drv_data, u8 *mac_addr)
{
	struct tgd_terra_dev_priv *priv;
	u64 mac;

	memcpy(fb_drv_data->mac_addr.addr, mac_addr, ETH_ALEN);
	/* We store mac addr also as u64. We identify the context based on
	 * this */
	mac = TGD_CONVERT_MACADDR_TO_LONG(fb_drv_data->mac_addr);
	TGD_DBG_DATA_DBG("Setting MAC Addr to %pM (%llx) for dev %d \n",
			 mac_addr, mac, fb_drv_data->idx);
	fb_drv_data->macaddr = mac;
	/* As for yet we don't know macaddress for this baseband instance.
	 * Now that we have the macaddress and we can fill it in.
	 */
	list_for_each_entry(priv, &fb_drv_data->dev_q_head, list_entry)
	{
		memcpy(priv->dev->dev_addr, fb_drv_data->mac_addr.addr,
		       ETH_ALEN);
	}
}

void tgd_flow_control_common(struct tgd_terra_driver *fb_dvr_data,
			     struct tgd_terra_dev_priv *priv, int link,
			     unsigned char qid, bool stop_tx)
{

	if ((fb_dvr_data->fc_enable) && (priv->tx_link == link)) {
		struct net_device *dev = priv->dev;
		struct netdev_queue *dev_queue = netdev_get_tx_queue(dev, qid);
		if (stop_tx) {
			// Flow Control ON
			TGD_DBG_DATA_DBG("FC_ON\n");
			if (!netif_tx_queue_stopped(dev_queue)) {
				netif_tx_stop_queue(dev_queue);
				terra_dev_stats_inc(priv, LINK_SUSPEND, 1);
			}
#ifdef TG_ENABLE_PFIFOFC
			if (unlikely(tgd_enable_pfifofc == 0))
#endif
			{
				tgd_rt_flow_control(priv, qid, stop_tx);
			}
		} else if (priv->link_state == TG_LINKUP) {
			// Flow Control OFF and Link is UP
			if (netif_tx_queue_stopped(dev_queue)) {
				terra_dev_stats_inc(priv, LINK_RESUME, 1);
				netif_tx_wake_queue(dev_queue);
			}
#ifdef TG_ENABLE_PFIFOFC
			if (unlikely(tgd_enable_pfifofc == 0))
#endif
			{
				tgd_rt_flow_control(priv, qid, stop_tx);
			}
		} else
			TGD_DBG_DATA_DBG("Suppress flow off\n");
	}
}

/*
 * The flow control callback function registered with the pfifofc
 * qdisc. It calls the flow control function of the routing module.
 */
void netdev_tx_flow_control(struct net_device *dev, int color, int prob,
			    int priority)
{
	struct tgd_terra_dev_priv *dev_priv = netdev_priv(dev);

	if (likely(dev_priv)) {
		uint16_t qid = fb_tgd_bh_select_queue(dev_priv, priority);
		TGD_DBG_DATA_DBG(
		    "netdev_tx_flow_control: color=%d prob=%d prio=%d"
		    "qid=%d dev_priv=%p\n",
		    color, prob, priority, qid, dev_priv);

		switch (color) {
			/* Current routing module does not support multi-level
			**  QOS with multiple drop probabilities.
			** Ignore RED_ON, and stop and restart for
			** ALL_ON/ALL_OFF
			*/
		case ALL_ON:
			terra_dev_stats_inc(dev_priv, TX_TGD_FLOW_ON, 1);
			tgd_rt_flow_control(dev_priv, (unsigned char)qid, true);
			break;
		case ALL_OFF:
			terra_dev_stats_inc(dev_priv, TX_TGD_FLOW_OFF, 1);
			tgd_rt_flow_control(dev_priv, (unsigned char)qid,
					    false);
			break;
		case RED_ON:
		default:
			break;
		}
	}
}

struct tgd_terra_dev_priv *
tgd_terra_dev_reserve(struct tgd_terra_driver *fb_drv_data,
		      const tgEthAddr *link_mac_addr)
{
	struct tgd_terra_dev_priv *priv, *avail;
	tgEthAddr zero_mac;

	avail = NULL;
	memset(&zero_mac, 0, sizeof(zero_mac));

	list_for_each_entry(priv, &fb_drv_data->dev_q_head, list_entry)
	{
		if (priv->link_state != TG_LINKINIT)
			continue;

		/* Prefer devices that were used for this peer in the past */
		if (memcmp(&priv->link_sta_addr, link_mac_addr,
			   sizeof(priv->link_sta_addr)) == 0) {
			avail = priv;
			break;
		}

		/* .. then grab any previously unused device */
		if (memcmp(&priv->link_sta_addr, &zero_mac,
			   sizeof(priv->link_sta_addr)) == 0) {
			avail = priv;
			break;
		}

		/* .. then grab any inactive device in the order of appearance
		 */
		if (avail == NULL)
			avail = priv;
	}

	if (avail != NULL) {
		/* Found the unused device */
		TGD_DBG_CTRL_INFO("%s: Dev %s reserved for %pM\n", __FUNCTION__,
				  netdev_name(avail->dev), link_mac_addr);
		memcpy(&avail->link_sta_addr, link_mac_addr, ETH_ALEN);
		return avail;
	}

	return NULL;
}

void tgd_terra_set_link_mac_addr(struct tgd_terra_driver *fb_drv_data,
				 tgEthAddr *link_mac_addr, uint8_t rxLink,
				 uint8_t txLink)
{
	struct tgd_terra_dev_priv *priv;

	priv = tgd_terra_dev_reserve(fb_drv_data, link_mac_addr);
	if (priv != NULL) {
		/* Found the unused device */
		TGD_DBG_CTRL_DBG("%s: Dev %s add link [old rx %d, tx %d]\n",
				 __FUNCTION__, netdev_name(priv->dev),
				 priv->rx_link, priv->tx_link);
		priv->link_state = TG_LINKDOWN;
		if (fb_tgd_bh_add_links_info(priv, link_mac_addr->addr, rxLink,
					     txLink) != 0) {
			priv->link_state = TG_LINKINIT;
		}
	} else {
		TGD_DBG_CTRL_DBG("No Device Found %pM\n", link_mac_addr->addr);
	}
	return;
}

struct tgd_terra_dev_priv *
tgd_terra_lookup_link_by_mac_addr(struct tgd_terra_driver *fb_drv_data,
				  tgEthAddr *link_mac_addr)
{
	struct tgd_terra_dev_priv *priv;
	struct net_device *dev;

	dev = tgd_terra_find_net_device_by_mac(fb_drv_data, link_mac_addr);
	if (dev == NULL) {
		TGD_DBG_CTRL_INFO("linkStaAddr %pM not found\n", link_mac_addr);
		return NULL;
	}

	priv = netdev_priv(dev);
	return priv;
}

// Process received packet
void tgd_terra_rx_data_handler(struct tgd_terra_driver *fb_drv_data,
			       struct tgd_terra_dev_priv *priv,
			       struct sk_buff *skb, int link)
{
	struct ethhdr *eth_header;
	eth_header = (struct ethhdr *)skb->data;

#ifdef TGD_CFG80211_DEBUG
	if (eth_header->h_proto == htons(ETH_P_PAE)) {
		TGD_DBG_CFG80211_DBG("%s: rx eapol pkt, len %u\n", __func__,
				     skb->len);
	}
#endif

	if ((priv->pae_closed) && (eth_header->h_proto != htons(ETH_P_PAE))) {
		/* if port access is closed, drop all non 802.1x packets */
		terra_dev_stats_inc(priv, RX_TGD_RX_STOPPED, 1);
		TGD_DBG_CFG80211_DBG("%s: PAE drop pkt h_proto=%04x\n",
				     __func__, ntohs(eth_header->h_proto));
		dev_kfree_skb(skb);
		return;
	}

	/* Write metadata, and then pass to the receive level */
	skb->dev = priv->dev;
	tgd_rt_rx(priv, skb);
}

void tgd_terra_rx_event_handler(struct tgd_terra_driver *fb_drv_data,
				const uint8_t *event_data, unsigned long size)
{
	unsigned long flags;
	fbTgIfEvent *fw_event;
	tgd_terra_rx_event_t *event;

	if (size > TGD_MAX_EVENT_SIZE) {
		TGD_DBG_CTRL_ERROR("Rx event size %lu too big\n", size);
		return;
	}
	if (fb_drv_data == NULL || event_data == NULL || size == 0) {
		TGD_DBG_CTRL_ERROR("Rx event ERROR ctxt %p event %p size %lu\n",
				   fb_drv_data, event_data, size);
		return;
	}
	if (!fb_drv_data->rx_event_enable) {
		TGD_DBG_CTRL_INFO("Dropping event ctxt %p event %p\n",
				  fb_drv_data, event_data);
		return;
	}

	event = (tgd_terra_rx_event_t *)kzalloc(
	    sizeof(tgd_terra_rx_event_t) + size, GFP_ATOMIC);
	if (!event) {
		TGD_DBG_CTRL_ERROR("Alloc fail size %zu\n",
				   sizeof(tgd_terra_rx_event_t));
		return;
	}
	memcpy(event->data, event_data, size);
	event->size = size;
	event->stamp = jiffies;

	fw_event = (fbTgIfEvent *)event_data;
	TGD_DBG_CTRL_INFO("Adding event %u(%p) size %lu\n", fw_event->type,
			  event, size);

	spin_lock_irqsave(&fb_drv_data->rx_event_q_lock, flags);
	list_add_tail(&event->entry, &fb_drv_data->rx_event_q_head);
	spin_unlock_irqrestore(&fb_drv_data->rx_event_q_lock, flags);
	queue_work_on(tgd_rx_event_cpu, fb_drv_data->rx_event_wq,
		      &fb_drv_data->rx_event_work);
}

void tgd_fb_flush_event_q(struct tgd_terra_driver *fb_drv_data)
{
	unsigned long flags;
	tgd_terra_rx_event_t *event;

	spin_lock_irqsave(&fb_drv_data->rx_event_q_lock, flags);
	while (!list_empty(&fb_drv_data->rx_event_q_head)) {
		event = list_first_entry(&fb_drv_data->rx_event_q_head,
					 tgd_terra_rx_event_t, entry);
		list_del(&event->entry);
		kfree(event);
	}
	spin_unlock_irqrestore(&fb_drv_data->rx_event_q_lock, flags);
}

void tgd_process_fb_events(struct work_struct *work)
{
	struct tgd_terra_driver *fb_drv_data;
	unsigned long flags, run_beg, run_end;
	tgd_terra_rx_event_t *event;
	fbTgIfEvent *fw_event;

	fb_drv_data =
	    container_of(work, struct tgd_terra_driver, rx_event_work);

	spin_lock_irqsave(&fb_drv_data->rx_event_q_lock, flags);
	while (!list_empty(&fb_drv_data->rx_event_q_head)) {
		event = list_first_entry(&fb_drv_data->rx_event_q_head,
					 tgd_terra_rx_event_t, entry);
		list_del(&event->entry);
		spin_unlock_irqrestore(&fb_drv_data->rx_event_q_lock, flags);
		run_beg = jiffies;
		fw_event = (fbTgIfEvent *)event->data;
		TGD_DBG_CTRL_INFO("Processing event %u(%p) size %u\n",
				  fw_event->type, event, event->size);
		tgd_fw_msg_handler(fb_drv_data, event->data, event->size);
		run_end = jiffies;

		/* Log all events that took longer than 1 sec to be handled */
		if (jiffies_to_msecs(run_end - event->stamp) >= 1000)
			TGD_DBG_CTRL_ERROR(
			    "Event %u took too long to be processed: "
			    "received %lu started %lu done %lu\n",
			    fw_event->type, event->stamp, run_beg, run_end);
		kfree(event);
		spin_lock_irqsave(&fb_drv_data->rx_event_q_lock, flags);
	}
	spin_unlock_irqrestore(&fb_drv_data->rx_event_q_lock, flags);
}

/*
 * Standard processing before frame is forwarded to BH for transmission
 */
int tgd_terra_bh_tx_pre(struct tgd_terra_dev_priv *priv, struct sk_buff *skb)
{
	struct net_device *dev;
	struct ethhdr *ehdr;
	int len;
	u16 qid;

	dev = priv->dev;
	ehdr = (struct ethhdr *)skb->data;
	len = skb->len;

#ifdef TGD_CFG80211_DEBUG
	if (ehdr->h_proto == htons(ETH_P_PAE))
		TGD_DBG_CFG80211_DBG("%s: tx eapol pkt, len %u\n",
				     netdev_name(dev), skb->len);
#endif

	if (len < sizeof(struct ethhdr)) {
		/* Validate the ethernet packet length. */
		TGD_DBG_DATA_DBG("%s: Packet too short (%i octets)\n",
				 netdev_name(dev), len);
		terra_dev_stats_inc(priv, TX_TGD_ERR, 1);
		dev_kfree_skb(skb);
		return -1;
	}

	if (!netif_carrier_ok(dev) || priv->link_state != TG_LINKUP ||
	    (priv->tx_link < 0)) {
		terra_dev_stats_inc(priv, TX_TGD_ERR, 1);
		TGD_DBG_DATA_DBG(
		    "%s: Device not ready to tx, freeing pkt len - %d "
		    "tx_link %d link_state %d\n",
		    netdev_name(dev), len, priv->tx_link, priv->link_state);
		dev_kfree_skb(skb);
		return -1;
	}

	/* See if particular queue is stopped */
	qid = skb_get_queue_mapping(skb);
	if (qid >= dev->num_tx_queues) {
		dev_err(&dev->dev,
			"ERROR: Wrong queue_mapping %d in skb. "
			"Resetting to 0\n",
			qid);
		qid = 0;
	}

	if (netif_tx_queue_stopped(netdev_get_tx_queue(dev, qid))) {
		terra_dev_stats_inc(priv, TX_TGD_TX_STOPPED, 1);
		dev_kfree_skb(skb);
		return -1;
	}

	/*
	 * Tricky thing is we need support rekey where we allow current traffic
	 * before setting the new keys
	 */
	if (priv->m4_pending && (ehdr->h_proto == htons(ETH_P_PAE))) {
		if (tgd_cfg80211_is_4way_m4(priv, skb))
			tgd_cfg80211_evt_m4_sent(priv);
	}

	/*
	 * Check if only PAE packets are allowed in at this time
	 */
	if (priv->pae_closed) {
		/* if port access is closed, drop all non 802.1x packets */
		if (ehdr->h_proto != htons(ETH_P_PAE)) {
			terra_dev_stats_inc(priv, TX_TGD_TX_STOPPED, 1);
			TGD_DBG_CFG80211_DBG("%s: PAE drop pkt h_proto=%04x\n",
					     netdev_name(dev),
					     ntohs(ehdr->h_proto));
			dev_kfree_skb(skb);
			return -1;
		} else
			TGD_DBG_CFG80211_DBG("%s: PAE pass EAPOL pkt\n",
					     netdev_name(dev));
	}

	/* Increment per queue packet count */
	terra_dev_stats_inc(priv, TX_PACKETS_COS0 + qid, 1);

	/*
	 * This is for multicast data, set the dest addr to link mac addr
	 */
	memcpy((u8 *)ehdr->h_dest, &priv->link_sta_addr, ETH_ALEN);

	/*
	 * At this time packet is ready to be given to backhaul
	 */
	return 0;
}

/*
 * Function that does the actual forwarding of the frame to BH layer
 */
int tgd_terra_bh_tx_post(struct tgd_terra_dev_priv *priv, struct sk_buff *skb)
{
	int ret;

	/* Call underlying backhaul transport */
	ret = fb_tgd_bh_tx_data(priv, skb);
	if (ret < 0) {
		/*
		 * Failed for any other reason, account as generic error and
		 * trust BH driver to free the buffer.
		 */
		terra_dev_stats_inc(priv, TX_TGD_ERR, 1);
	}

	TGD_DBG_DATA_INFO("%s: sent packet to bh driver: link %d\n",
			  netdev_name(priv->dev), priv->tx_link);
	return ret;
}

/*
 * Function that ties together pre and post steps of transmission together,
 * for convenience of routing backends that do not do any special processing
 * in TX path.
 */
void tgd_terra_bh_tx_common(struct tgd_terra_dev_priv *priv,
			    struct sk_buff *skb)
{
	int ret;

	ret = tgd_terra_bh_tx_pre(priv, skb);
	if (ret == 0)
		tgd_terra_bh_tx_post(priv, skb);
}

/*
 * Transmit a packet (called by the kernel)
 */
static netdev_tx_t tgd_terra_tx(struct sk_buff *skb, struct net_device *dev)
{
	tgd_rt_tx(netdev_priv(dev), skb);
	return NETDEV_TX_OK;
}

static inline void tgd_terra_set_skb_priority(struct sk_buff *skb)
{
	u8 tos;

	/* Depend on legacy IP classification for any non-IPv6 packets */
	if (skb->protocol != htons(ETH_P_IPV6))
		return;

	/* Calculate 802.1p PCP from TOS field */
	tos = ipv6_get_dsfield(ipv6_hdr(skb)) >> 2;
	skb->priority =
	    (tos == 0x30) ? FB_TGD_BH_SKB_PRIO_VI : FB_TGD_BH_SKB_PRIO_BE;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 2, 0)
static u16 tgd_terra_select_queue(struct net_device *dev, struct sk_buff *skb,
				  struct net_device *sb_dev)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
static u16 tgd_terra_select_queue(struct net_device *dev, struct sk_buff *skb,
				  struct net_device *sb_dev,
				  select_queue_fallback_t fallback)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
static u16 tgd_terra_select_queue(struct net_device *dev, struct sk_buff *skb,
				  void *accel_priv,
				  select_queue_fallback_t fallback)
#else
static u16 tgd_terra_select_queue(struct net_device *dev, struct sk_buff *skb)
#endif
{
	/* Classify the packet if necessary */
	tgd_terra_set_skb_priority(skb);

	/* FIXME: only use priority */
	return fb_tgd_bh_select_queue(netdev_priv(dev), skb->priority);
}

/*
 * Terragraph-specific attribute group
 */
static ssize_t tgd_terra_show_peer_mac(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct tgd_terra_dev_priv *priv = netdev_priv(to_net_dev(dev));
	return snprintf(buf, PAGE_SIZE, "%pM\n", priv->link_sta_addr.addr);
}

static DEVICE_ATTR(peer_mac, 0444, tgd_terra_show_peer_mac, NULL);

static struct attribute *tgd_terra_dev_attrs[] = {&dev_attr_peer_mac.attr,
						  NULL};

static const struct attribute_group tgd_terra_attr_group = {
    .attrs = tgd_terra_dev_attrs};

/*
 * Ioctl commands
 */
int tgd_terra_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	TGD_DBG_CTRL_INFO("ioctl\n");
	return 0;
}

/*
 * Fetch link stats
 */
void tgd_terra_get_net_link_stat(struct net_device *dev,
				 struct fb_tgd_bh_link_stats *link_stat_ptr)
{
	struct tgd_terra_dev_priv *priv = netdev_priv(dev);

	tgd_terra_link_stats(priv, link_stat_ptr);
}

void tgd_terra_get_net_if_stat(struct net_device *dev,
			       struct fb_tgd_bh_link_stats *if_stat_ptr)
{
	struct tgd_terra_dev_priv *priv = netdev_priv(dev);
	int i;

	/* Get active link stats */
	tgd_terra_get_net_link_stat(dev, if_stat_ptr);

	/* Add stats collected from the past */
	for_each_possible_cpu(i)
	{
		const struct terra_dev_pcpu_stats *pstats;
		unsigned int start;
		u64 t_tx_errors, t_tx_packets, t_tx_bytes;
		u64 t_rx_packets, t_rx_bytes;

		pstats = per_cpu_ptr(priv->pcpu_stats, i);
		do {
			start = u64_stats_fetch_begin_irq(&pstats->syncp);
			t_tx_errors = pstats->stats[TX_ERR];
			t_tx_packets = pstats->stats[TX_PACKETS];
			t_tx_bytes = pstats->stats[TX_BYTES];
			t_rx_packets = pstats->stats[RX_PACKETS];
			t_rx_bytes = pstats->stats[RX_BYTES];
		} while (u64_stats_fetch_retry_irq(&pstats->syncp, start));

		if_stat_ptr->pkts_sent += t_tx_packets;
		if_stat_ptr->bytes_sent += t_tx_bytes;
		if_stat_ptr->pkts_recved += t_rx_packets;
		if_stat_ptr->bytes_recved += t_rx_bytes;
		if_stat_ptr->tx_err += t_tx_errors;
	}
}

/*
 * Return statistics to the caller
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
void tgd_terra_stats64(struct net_device *dev,
		       struct rtnl_link_stats64 *net_stats)
#else
struct rtnl_link_stats64 *tgd_terra_stats64(struct net_device *dev,
					    struct rtnl_link_stats64 *net_stats)
#endif
{
	struct fb_tgd_bh_link_stats lstats;

	if (net_stats != NULL) {
		/* Get active link stats */
		tgd_terra_get_net_if_stat(dev, &lstats);

		memset(net_stats, 0, sizeof(*net_stats));
		net_stats->rx_packets = lstats.pkts_recved;
		net_stats->tx_packets = lstats.pkts_sent;
		net_stats->rx_bytes = lstats.bytes_recved;
		net_stats->tx_bytes = lstats.bytes_sent;
		net_stats->tx_errors = lstats.tx_err;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	return net_stats;
#else
	return;
#endif
}

static const char terra_stat_strings[][ETH_GSTRING_LEN] = {
    "rx_packets", "tx_packets", "rx_bytes",
    "tx_bytes",   "rx_errors",  "tx_errors",
};

#define TERRA_NUM_ETHTOOL_STATS                                                \
	(sizeof(terra_stat_strings) / sizeof(terra_stat_strings[0]))

static void terra_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	if (stringset != ETH_SS_STATS)
		return;
	memcpy(data, terra_stat_strings, sizeof(terra_stat_strings));
}

static int terra_get_sset_count(struct net_device *dev, int string_set)
{
	switch (string_set) {
	case ETH_SS_STATS:
		return TERRA_NUM_ETHTOOL_STATS;
	}
	return -EOPNOTSUPP;
}

void ethtool_op_get_terra_stats(struct net_device *dev,
				struct ethtool_stats *ethtool_stats, u64 *data)
{
	struct fb_tgd_bh_link_stats lstats;

	if (data == NULL)
		return;
	memset(&lstats, 0, sizeof(lstats));

	/* Get link stats from DHD. */
	tgd_terra_get_net_link_stat(dev, &lstats);
	data[0] = lstats.pkts_recved;
	data[1] = lstats.pkts_sent;
	data[2] = lstats.bytes_recved;
	data[3] = lstats.bytes_sent;
	data[4] = lstats.rx_err;
	data[5] = lstats.tx_err;
}

/*
 * The "change_mtu" method is usually not needed.
 * If you need it, it must be like this.
 */
int tgd_terra_change_mtu(struct net_device *dev, int new_mtu)
{
	/* check ranges */
	if (new_mtu < tg_min_mtu(dev) || new_mtu > tg_max_mtu(dev))
		return -EINVAL;
	/*
	 * Do anything you need, and the accept the value
	 */
	dev->mtu = new_mtu;
	return 0; /* success */
}

static const struct ethtool_ops terra_ethtool_ops = {
    .get_link = ethtool_op_get_link,
    .get_ethtool_stats = ethtool_op_get_terra_stats,
    .get_strings = terra_get_strings,
    .get_sset_count = terra_get_sset_count,
};

static const struct net_device_ops terra_dev_ops = {
    .ndo_start_xmit = tgd_terra_tx,
    .ndo_do_ioctl = tgd_terra_ioctl,
    .ndo_get_stats64 = tgd_terra_stats64,
    .ndo_change_mtu = tgd_terra_change_mtu,
    .ndo_set_mac_address = eth_mac_addr,
    .ndo_select_queue = tgd_terra_select_queue,
    .ndo_validate_addr = eth_validate_addr,
};

/**********************************************************************
 * Change the dbg mask, invoked from the sdnclient with new mask value
 **********************************************************************/
unsigned int set_debug_mask(unsigned int new_dbg_mask)
{
	unsigned int cur_mask;

	if (new_dbg_mask & 0xF0000000) // read current value if any one of the
				       // upper 4 bit is set
	{
		return tgd_dbg_enable_level;
	}
	cur_mask = tgd_dbg_enable_level;
	tgd_dbg_enable_level = new_dbg_mask;

	return cur_mask;
}

#define str(x) #x

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 8)

/* create strings out of the terra dev stats enums */
static char *terra_dev_stats_str[] = {TERRA_STATS_OP(str)};

static int get_terra_stats(struct seq_file *s, void *data)
{
	struct device *dev = s->private;
	struct net_device *ndev = to_net_dev(dev);
	struct tgd_terra_dev_priv *priv = netdev_priv(ndev);
	struct fb_tgd_bh_link_stats lstats;
	u64 cntrs[TERRA_DEV_STATS_MAX];
	int i, j;

	for (i = 0; i < TERRA_DEV_STATS_MAX; ++i) {
		cntrs[i] = 0;
		for_each_possible_cpu(j)
		{
			const struct terra_dev_pcpu_stats *pstats;
			unsigned int start;
			u64 c_cntr = 0;

			pstats = per_cpu_ptr(priv->pcpu_stats, j);
			do {
				start =
				    u64_stats_fetch_begin_irq(&pstats->syncp);
				c_cntr = pstats->stats[i];
			} while (
			    u64_stats_fetch_retry_irq(&pstats->syncp, start));
			cntrs[i] += c_cntr;
		}
	}

	/* Get link stats */
	tgd_terra_link_stats(priv, &lstats);

	/* Add current link stats to interface stats */
	cntrs[TX_ERR] += lstats.tx_err;
	cntrs[TX_PACKETS] += lstats.pkts_sent;
	cntrs[TX_BYTES] += lstats.bytes_sent;
	cntrs[RX_PACKETS] += lstats.pkts_recved;
	cntrs[RX_BYTES] += lstats.bytes_recved;

	/* Print interface stats out */
	for (i = 0; i < TERRA_DEV_STATS_MAX; ++i)
		seq_printf(s, "%-32s: %llu\n", terra_dev_stats_str[i],
			   cntrs[i]);

	/* Print links stats out */
	seq_printf(s, "%-32s: %llu\n", "LINK_RX_PACKETS", lstats.pkts_recved);
	seq_printf(s, "%-32s: %llu\n", "LINK_RX_BYTES", lstats.bytes_recved);
	seq_printf(s, "%-32s: %llu\n", "LINK_TX_PACKETS", lstats.pkts_sent);
	seq_printf(s, "%-32s: %llu\n", "LINK_TX_BYTES", lstats.bytes_sent);
	seq_printf(s, "%-32s: %llu\n", "LINK_TX_BYTES_PENDING",
		   lstats.bytes_pending);
	seq_printf(s, "%-32s: %llu\n", "LINK_TX_PACKETS_PENDING",
		   lstats.pkts_pending);
	seq_printf(s, "%-32s: %llu\n", "LINK_TX_ERR", lstats.tx_err);
	seq_printf(s, "%-32s: %llu\n", "LINK_PKTS_ENQUEUED",
		   lstats.pkts_enqueued);
	seq_printf(s, "%-32s: %llu\n", "LINK_BYTES_ENQUEUED",
		   lstats.bytes_enqueued);
	seq_printf(s, "%-32s: %llu\n", "LINK_BYTES_SENT_FAILED",
		   lstats.bytes_sent_failed);
	seq_printf(s, "%-32s: %llu\n", "LINK_BYTES_ENQ_FAILED",
		   lstats.bytes_enqueue_failed);
	seq_printf(s, "%-32s: %llu\n", "LINK_BYTES_ENQ_PAD",
		   lstats.bytes_enqueued_pad);
	seq_printf(s, "%-32s: %llu\n", "LINK_BYTES_ENQ_FAIL_PAD",
		   lstats.bytes_enqueue_fail_pad);
	seq_printf(s, "%-32s: %llu\n", "LINK_TX_BYTES_PAD",
		   lstats.bytes_sent_pad);
	seq_printf(s, "%-32s: %llu\n", "LINK_TX_BYTES_FAIL_PAD",
		   lstats.bytes_sent_failed_pad);

	// Qdisc link stats
	seq_printf(s, "%-32s: %u\n", "TX_QDISC_BYTES_PEND",
		   lstats.qdisc_cur_bytes);
	seq_printf(s, "%-32s: %u\n", "TX_QDISC_PKTS_PEND",
		   lstats.qdisc_cur_pkts);
	for (i = 0; i < PFIFOFC_BANDS; i++) {
		seq_printf(s, "%s%-9d: %llu\n", "TX_QDISC_TOTAL_PKTS_COS", i,
			   lstats.qdisc_total_pkts_enqd[i]);
		seq_printf(s, "%s%-10d: %llu\n", "TX_QDISC_PKTS_DROP_COS", i,
			   lstats.qdisc_total_pkts_dropped[i]);
		seq_printf(s, "%s%-9d: %u\n", "TX_QDISC_PKTS_BKLOG_COS", i,
			   lstats.qdisc_cur_pkts_backlog[i]);
	}
	return 0;
}

static int dump_terra_dev_info(struct seq_file *s, void *data)
{
	struct device *dev = s->private;
	struct net_device *ndev = to_net_dev(dev);
	struct tgd_terra_dev_priv *priv = netdev_priv(ndev);
	int i;
#ifdef TG_ENABLE_CFG80211
	struct tgd_cfg80211_info tci;
#endif

	/* Maybe not completely SMP safe to get a consistent
	 * snapshot, but we are just reading some data out
	 * so we should be ok and most of the below does not
	 * change often.
	 */
	seq_printf(s, "%-16s: %s\n", "NetDevName", priv->dev->name);
	seq_printf(s, "%-16s: %d\n", "Status", priv->status);
	seq_printf(s, "%-16s: %d\n", "Rx_link", priv->rx_link);
	seq_printf(s, "%-16s: %d\n", "Tx_link", priv->tx_link);
	seq_printf(s, "%-16s: %pM\n", "Mac addr",
		   priv->fb_drv_data->mac_addr.addr);
	seq_printf(s, "%-16s: %#llx\n", "Macaddr", priv->fb_drv_data->macaddr);
	seq_printf(s, "%-16s: %pM\n", "Sta addr", priv->link_sta_addr.addr);
	seq_printf(s, "%-16s: %d\n", "Link state", priv->link_state);
	seq_printf(s, "%-16s: %d\n", "Link Count",
		   priv->fb_drv_data->link_count);

#ifdef TG_ENABLE_CFG80211
	tgd_cfg80211_get_info(priv, &tci);

	seq_printf(s, "%-16s: %d\n", "ap_started", tci.ap_started);
	seq_printf(s, "%-16s: %d\n", "tg_connected", tci.tg_connected);
	seq_printf(s, "%-16s: %d\n", "wsec_auth", tci.wsec_auth);
	seq_printf(s, "%-16s: %d\n", "pae_closed", priv->pae_closed);
	if (!tci.ap_started && (tci.wsec_auth != TGF_WSEC_DISABLE)) {
		seq_printf(s, "%-16s: %d\n", "m4_pending", priv->m4_pending);
		seq_printf(s, "%-16s: %d\n", "m4_sent", tci.m4_sent);
	}
#endif
	for (i = 0; i < priv->dev->num_tx_queues; i++) {
		const struct netdev_queue *netq;

		netq = netdev_get_tx_queue(priv->dev, i);
		if (netq == NULL)
			continue;
		seq_printf(s, "%-15s%.1d: %d\n", "Flow Control TX", i,
			   netif_tx_queue_stopped(netq));
	}
	return 0;
}

#endif

void tgd_terra_stop_device(struct tgd_terra_dev_priv *dev_priv)
{
	struct net_device *dev;

	dev = dev_priv->dev;
	/* No more transfers */
	netif_carrier_off(dev);
	netif_tx_disable(dev);
	/* Remove links */
	fb_tgd_bh_del_links_info(dev_priv);
}

void tgd_terra_delete_device(struct tgd_terra_dev_priv *dev_priv)
{
	struct net_device *dev;

	/* Wait for all active device users to go away */
	dev = dev_priv->dev;
	tgd_rt_del_device(dev_priv);
	if (dev->reg_state == NETREG_REGISTERED)
		unregister_netdev(dev);

	TGD_DBG_CTRL_DBG("tgd_terra_delete_device:dev=%p qdisc=%p\n", dev,
			 dev->qdisc);
	list_del(&dev_priv->list_entry);

	if (dev_priv->debugfs_stats_dir != NULL)
		debugfs_remove_recursive(dev_priv->debugfs_stats_dir);
	if (dev_priv->pcpu_stats)
		free_percpu(dev_priv->pcpu_stats);
	mutex_destroy(&dev_priv->link_lock);

#ifdef TG_ENABLE_CFG80211
	if (dev_priv->wdev) {
		tgd_wdev_free(dev_priv->wdev);
		dev_priv->wdev = NULL;
	}
#endif
	free_netdev(dev);
}

/*
 * The setup function, invoked by alloc_netdev and acts as a constructor.
 * This function cannot fail and cannot have a side effects that require
 * extra cleanup if alloc_netdev fails after it invokes the setup. Namely,
 * we cannnot put this device on any global list, since it will not get
 * removed in the failure case.
 */
static void tgd_terra_init_device(struct net_device *dev)
{
	/*
	 * Then, assign other fields in dev, using ether_setup() and some
	 * hand assignments
	 */
	ether_setup(dev); /* assign some of the fields */

	// dev->watchdog_timeo = timeout;
	/* Provide enough head room in tx pkts for FB WG driver use */
	dev->needed_headroom += FB_TGD_BH_MAX_HDR_SIZE;

	dev->netdev_ops = &terra_dev_ops;
	dev->ethtool_ops = &terra_ethtool_ops;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	/*
	 * Limit our MTU to something we can handle. This is hardcoded
	 * for older kernel versions.
	 */
	dev->max_mtu = TGD_WLAN_MTU_SIZE;
#endif
	/* Honor module parameter if valid one were given */
	if (tgd_def_mtu >= tg_min_mtu(dev) && tgd_def_mtu <= tg_max_mtu(dev))
		dev->mtu = tgd_def_mtu;

	// Only enable the interface when link is established
	netif_carrier_off(dev);
	netif_tx_disable(dev);
}

int tgd_terra_create_device(struct tgd_terra_driver *tgd_drv, int peer_index)
{
	struct tgd_terra_dev_priv *priv;
#ifdef TG_ENABLE_CFG80211
	struct wireless_dev *wdev = NULL;
#endif
	struct net_device *dev;
	int dev_index;
	int cpu;
	int ret;
#ifdef TG_ENABLE_PFIFOFC
	struct netdev_queue *dev_queue;
	struct Qdisc *qdisc;
	struct tgd_pfifofc_qopt tune;
#endif
	char if_name[IFNAMSIZ + 1];

	dev_index = ((tgd_num_of_virt_links * tgd_drv->idx) + peer_index);

	snprintf(if_name, sizeof(if_name), "terra%d", dev_index);
	if_name[sizeof(if_name) - 1] = 0;

	dev = alloc_netdev_mq(sizeof(struct tgd_terra_dev_priv), if_name,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
			      NET_NAME_UNKNOWN,
#endif
			      tgd_terra_init_device, FB_TGD_BH_MQ_QUEUE_NUM);

	if (dev == NULL) {
		pr_err("Failed alloc_netdev for device %d\n", dev_index);
		return -ENOMEM;
	}

	/*
	 * Then, initialize the priv field. This encloses the statistics
	 * and a few private fields.
	 */
	priv = netdev_priv(dev);
	priv->fb_drv_data = tgd_drv;
	priv->dev = dev;
	memcpy(dev->dev_addr, tgd_drv->mac_addr.addr, ETH_ALEN);

	/*
	 * Expose terragraph-specific attributes
	 */
	dev->sysfs_groups[0] = &tgd_terra_attr_group;

	priv->tx_link = TGD_LINK_INVALID;
	priv->rx_link = TGD_LINK_INVALID;
	priv->link_state = TG_LINKINIT;
	priv->dev_index = dev_index;
	priv->peer_index = peer_index;

	mutex_init(&priv->link_lock);
	spin_lock_init(&priv->stats_lock);

	TGD_DBG_CTRL_DBG("dev %p priv %p drv_data %p\n", dev, priv,
			 priv->fb_drv_data);

	/* Add device to the device list */
	list_add_tail(&priv->list_entry, &tgd_drv->dev_q_head);

	priv->pcpu_stats = alloc_percpu(struct terra_dev_pcpu_stats);
	if (!priv->pcpu_stats) {
		TGD_DBG_CTRL_ERROR("Failed to alloc pcpu_stats\n");
		ret = -ENOMEM;
		goto out;
	}
	for_each_possible_cpu(cpu)
	{
		struct terra_dev_pcpu_stats *pcpu_stats;

		pcpu_stats = per_cpu_ptr(priv->pcpu_stats, cpu);
		u64_stats_init(&pcpu_stats->syncp);
	}

	/*
	 * Allow underlying vendor driver tweak any interface parameters
	 * that influence the efficiency of the data transfers. Namely,
	 * vendor might choose to add to remove hardware offload flags
	 * to better reflect the hardware capabilities.
	 */
	fb_tgd_bh_setup_netdev(priv);

#ifdef TG_ENABLE_CFG80211
	wdev = tgd_cfg80211_init(dev);
	if (IS_ERR(wdev)) {
		pr_err("Failed to init cfg80211 wdev\n");
		ret = PTR_ERR(wdev);
		goto out;
	}
	priv->wdev = wdev;
#endif

	ret = register_netdev(dev);
	if (ret != 0) {
		pr_err("error %d registering device \"%s\"\n", ret, dev->name);
		goto out;
	}

#ifdef TG_ENABLE_PFIFOFC
	if (likely(tgd_enable_pfifofc)) {
		// dev->qdisc assignment has to be done after call
		// register_netdev(); register_netdev() sets default noop_qdisc
		// as dev->qdisc.

		// Only 1 netdev queue (qid = 0) is active with pfifofc qdisc.
		dev_queue = netdev_get_tx_queue(dev, 0);
		qdisc =
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 16, 0)
		    qdisc_create_dflt(dev_queue, &pfifofc_qdisc_ops, TC_H_ROOT,
				      NULL);
#else
		    qdisc_create_dflt(dev_queue, &pfifofc_qdisc_ops, TC_H_ROOT);
#endif
		if (qdisc == NULL) {
			TGD_DBG_CTRL_ERROR(
			    "Failed qdisc_create_dflt for device %d\n",
			    dev_index);
			ret = -ENOMEM;
			goto out;
		}
		qdisc->flags |= TCQ_F_NOPARENT;
		dev_queue->qdisc_sleeping = qdisc;
		dev_queue->qdisc = qdisc;
		dev->qdisc = qdisc;

		tune.max_queue_len = (u16)tgd_qdisc_maxqueue_len;
		tune.qlen_red_on = (u16)tgd_qdisc_red_on;
		tune.qlen_red_off = (u16)tgd_qdisc_red_off;
		tune.qlen_all_on = (u16)tgd_qdisc_all_on;
		tune.qlen_all_off = (u16)tgd_qdisc_all_off;
		qdisc_dev_register_flow_control_cb(
		    dev->qdisc, (void *)netdev_tx_flow_control, &tune);
		TGD_DBG_CTRL_DBG("Registered pfifofc qdisc=%p. cb=%p. dev=%p\n",
				 dev->qdisc, (void *)netdev_tx_flow_control,
				 dev);
	}
#endif /* TG_ENABLE_PFIFOFC */

	/*
	 * Ponderance
	 */
	ret = tgd_rt_add_device(tgd_drv, priv);
	if (ret != 0) {
		pr_err("error %d registering device with routing \"%s\"\n", ret,
		       dev->name);
		goto out;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 8)
	priv->debugfs_stats_dir =
	    debugfs_create_dir(dev->name, tgd_drv->debugfs_root_dir);
	if (priv->debugfs_stats_dir == NULL) {
		pr_err("Could not create debugfs dir \"%s\"\n", dev->name);
		ret = -ENOMEM;
		goto out;
	} else {
		struct dentry *entry;

		entry = debugfs_create_devm_seqfile(&dev->dev, "stats",
						    priv->debugfs_stats_dir,
						    get_terra_stats);
		if (entry == NULL) {
			pr_err("Could not create debugfs file \"stats\"\n");
			ret = -ENOMEM;
			goto out;
		}

		entry = debugfs_create_devm_seqfile(&dev->dev, "info",
						    priv->debugfs_stats_dir,
						    dump_terra_dev_info);
		if (!entry) {
			pr_err("Could not create debugfs file \"info\"\n");
			ret = -ENOMEM;
			goto out;
		}
	}
#endif

out:
	if (ret != 0) {
		tgd_terra_delete_device(priv);
	}
	return ret;
}

/* create strings out of the nlsdn stats enums */
static char *terra_nl_stats_str[] = {NLSDN_STATS_OP(str)};

static int show_nl_stats(struct seq_file *m, void *v)
{
	struct tgd_terra_driver *fb_drv_data;
	int i;

	fb_drv_data = (struct tgd_terra_driver *)m->private;
	for (i = 0; i < NL_STATS_MAX; ++i) {
		seq_printf(m, "%-32s: %d\n", terra_nl_stats_str[i],
			   atomic_read(&fb_drv_data->nl_stats.stats[i]));
	}
	return 0;
}

static int nl_stats_open(struct inode *inode, struct file *file)
{
	struct tgd_terra_driver *fb_drv_data = inode->i_private;

	return single_open(file, show_nl_stats, fb_drv_data);
}

static const struct file_operations nl_stats_fops = {
    .open = nl_stats_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

void tgd_terra_link_stats(struct tgd_terra_dev_priv *priv,
			  struct fb_tgd_bh_link_stats *stats)
{
	spin_lock(&priv->stats_lock);
	tgd_terra_update_link_stats(priv);
	memcpy(stats, &priv->link_stats, sizeof(*stats));
	memcpy(stats->dst_mac_addr, &priv->link_sta_addr.addr, ETH_ALEN);
	memcpy(stats->src_mac_addr, priv->dev->dev_addr, ETH_ALEN);
	stats->dev_index = priv->dev_index;
	stats->link_state = priv->link_state;
	stats->link = priv->tx_link;

#ifdef TG_ENABLE_PFIFOFC
	if (likely(tgd_enable_pfifofc)) {
		struct net_device *dev = priv->dev;
		struct tgd_pfifofc_stats qdisc_st;
		int i;

		pfifofc_dump_stats(dev->qdisc, &qdisc_st);
		for (i = 0; i < PFIFOFC_BANDS; i++) {
			stats->qdisc_total_pkts_enqd[i] =
			    qdisc_st.bstats[i].total_pkts;
			stats->qdisc_cur_pkts_backlog[i] =
			    qdisc_st.bstats[i].cur_pkts;
			stats->qdisc_total_pkts_dropped[i] =
			    qdisc_st.bstats[i].dropped_pkts;
		}
		stats->qdisc_cur_bytes = qdisc_st.total_cur_bytes;
		stats->qdisc_cur_pkts = qdisc_st.total_cur_packets;
	}
#endif
	spin_unlock(&priv->stats_lock);
}

/* Driver for backhaul devices  */
static void tgd_terra_cleanup(struct tgd_terra_driver *fb_drv_data)
{
	struct tgd_terra_dev_priv *dev_priv;

	TGD_DBG_CTRL_INFO("Doing tgd_terra_cleanup\n");

#ifdef TG_ENABLE_QUEUE_STATS
	/* Stop queue stats collection and pushing to firmware */
	fb_tgd_queue_stats_exit(fb_drv_data);
#endif

	/* Disassociate links if necessary */
	fb_tgd_bh_cleanup_links(fb_drv_data);

	/* Stop processing of incoming events */
	fb_drv_data->rx_event_enable = false;
	cancel_work_sync(&fb_drv_data->rx_event_work);
	tgd_fb_flush_event_q(fb_drv_data);

	/* Disable flow control on all devices */
	fb_drv_data->fc_enable = false;
	synchronize_rcu();

	/* Tell firmware we are going down */
	if (!list_empty(&fb_drv_data->dev_q_head))
		tgd_send_fw_shutdown(fb_drv_data);

	list_for_each_entry(dev_priv, &fb_drv_data->dev_q_head, list_entry)
	{
		tgd_terra_stop_device(dev_priv);
	}
	synchronize_rcu();

	tgd_gps_dev_exit(fb_drv_data);

	/* Unregister RX callbacks with WLAN driver */
	fb_tgd_bh_unregister_client(fb_drv_data);
	synchronize_rcu();

	/* Free the network devices */
	while ((dev_priv = list_first_entry_or_null(&fb_drv_data->dev_q_head,
						    struct tgd_terra_dev_priv,
						    list_entry)) != NULL) {
		tgd_terra_delete_device(dev_priv);
	}
	tgd_rt_fini(fb_drv_data);

	if (fb_drv_data->debugfs_symlink) {
		debugfs_remove(fb_drv_data->debugfs_symlink);
		fb_drv_data->debugfs_symlink = NULL;
	}
	if (fb_drv_data->debugfs_root_dir)
		debugfs_remove_recursive(fb_drv_data->debugfs_root_dir);
	fb_drv_data->debugfs_root_dir = NULL;

	if (fb_drv_data->rx_event_wq != NULL) {
		destroy_workqueue(fb_drv_data->rx_event_wq);
		fb_drv_data->rx_event_wq = NULL;
	}
}

static void tg_bh_shutdown(struct platform_device *pdev)
{
	struct tgd_terra_driver *fb_drv_data;

	fb_drv_data = platform_get_drvdata(pdev);
	if (fb_drv_data == NULL)
		return;

	tgd_terra_cleanup(fb_drv_data);
}

static int tg_bh_remove(struct platform_device *pdev)
{
	struct tgd_terra_driver *fb_drv_data;

	fb_drv_data = platform_get_drvdata(pdev);
	if (fb_drv_data == NULL)
		return 0;

	/* Send the netlink message to the subscribers that the device is down.
	 */
	tgd_nlsdn_send_device_updown_status(fb_drv_data, DEVICE_DOWN);

	/*
	 * Need to synchronize between fb_drv_data going
	 * away and processing netlink pkts/data pkts/events.
	 * By the time tg_bg_shutdown returns we would have
	 * unregistered from backhaul, flushed rx_event queue etc.
	 *
	 * In order to prevent processing south bound messages while tearing
	 * down this device, remove its fb_drv_data from driver_list and
	 * then call shutdown.  However we might be in the middle of
	 * processing gennl messages.  Don't want to race tearing down
	 * the device while some messages for the device are being processed.
	 * So we do the below
	 *
	 * Before genl messages processing callbacks are invoked genl_mutex
	 * is held to serialize messages.  So remove fb_drv_data from
	 * the driver_list under this lock, which guarantees
	 * no genl message is currently being processed.
	 */
	genl_lock();
	if (klist_node_attached(&fb_drv_data->driver_list_node))
		klist_del(&fb_drv_data->driver_list_node);
	genl_unlock();

	/* Do the shutdown */
	tg_bh_shutdown(pdev);

	platform_set_drvdata(pdev, NULL);
	kfree(fb_drv_data);
	return 0;
}

static int tg_bh_probe(struct platform_device *pdev)
{
	struct tgd_terra_driver *fb_drv_data;
	struct dentry *fb_tgd_debug_dir = NULL;
	struct dentry *entry;
	char name[32];
	int i, ret;

	fb_drv_data = kzalloc(sizeof(*fb_drv_data), GFP_KERNEL);
	if (fb_drv_data == NULL) {
		dev_err(&pdev->dev, "unable to allocate driver state");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, fb_drv_data);

	TGD_DBG_CTRL_INFO("FB Driver Data %p\n", fb_drv_data);
	INIT_LIST_HEAD(&fb_drv_data->dev_q_head);
	INIT_LIST_HEAD(&fb_drv_data->rx_event_q_head);
	spin_lock_init(&fb_drv_data->rx_event_q_lock);
	INIT_WORK(&fb_drv_data->rx_event_work, tgd_process_fb_events);
	fb_drv_data->link_count = 0;
	fb_drv_data->fc_enable = true;
	fb_drv_data->rx_event_enable = false;
	fb_drv_data->max_link_count = tgd_num_of_virt_links;
	fb_drv_data->idx = pdev->id;
	fb_drv_data->frame_format = tgd_bh_amsdu_ff;

	/* Initialize backhaul API wrapper */
	ret = fb_tgd_bh_api_init(&pdev->dev, fb_drv_data);
	if (ret != 0) {
		dev_err(&pdev->dev, "fb_tgd_bh_api_init failed: ret=%d", ret);
		goto out;
	}

	ret = tgd_rt_init(fb_drv_data);
	if (ret) {
		dev_err(&pdev->dev, "unable to initialize routing: ret=%d",
			ret);
		goto out;
	}

	// Register Rx callbacks with WLAN driver
	ret = fb_tgd_bh_register_client(fb_drv_data);
	if (ret < 0) {
		dev_err(&pdev->dev, "Register with wlan driver, error %d\n",
			ret);
		goto out;
	}

	/* Create debugfs root */
	snprintf(name, sizeof(name), "%s.%d", pdev->name, pdev->id);
	fb_tgd_debug_dir = debugfs_create_dir(name, fb_tgd_debug_root_dir);
	if (fb_tgd_debug_dir == NULL) {
		dev_err(&pdev->dev,
			"Could not create root debugfs dir \"%s\"\n", name);
		ret = -ENOMEM;
		goto out;
	}
	if (fb_drv_data->idx == 0) {
		/*
		 * Temp symlink hack since tacit relies on it.  Will remove it
		 * soon once the code lands and tacit check the new path
		 */
		char target[128];

		snprintf(target, sizeof(target), "%s/%s", "terragraph-baseband",
			 name);
		entry = debugfs_create_symlink("terra", NULL, target);
		if (!entry)
			dev_err(&pdev->dev,
				"debugfs symbolic link creation failed\n");
		fb_drv_data->debugfs_symlink = entry;
	}

	dev_printk(KERN_INFO, &pdev->dev, "Created device tgd debug dir\n");
	fb_drv_data->debugfs_root_dir = fb_tgd_debug_dir;

	entry = debugfs_create_file("nl_stats", 0444, fb_tgd_debug_dir,
				    fb_drv_data, &nl_stats_fops);
	if (entry == NULL) {
		dev_err(&pdev->dev, "Could not create debugfs file \"%s\"\n",
			"nl_stats");
		ret = -ENOMEM;
		goto out;
	}

	/* Create dedicated work queue to process FW events */
	snprintf(name, sizeof(name), "tgrxevt.%d", pdev->id);
	fb_drv_data->rx_event_wq = create_singlethread_workqueue(name);
	if (fb_drv_data->rx_event_wq == NULL) {
		dev_err(&pdev->dev,
			"Unable to alocate work queue for RX events\n");
		ret = -ENOMEM;
		goto out;
	}

	/* Allocate the devices */
	for (i = 0; i < fb_drv_data->max_link_count; i++) {
		/* Create and initialize virtual link device */
		ret = tgd_terra_create_device(fb_drv_data, i);
		if (ret != 0) {
			goto out;
		}
	}

	/* Initialize GPS subsystem */
	ret = tgd_gps_dev_init(fb_drv_data);
	if (ret != 0) {
		dev_err(&pdev->dev,
			"Unable to initialize GPS interface: ret=%d\n", ret);
		goto out;
	}

#ifdef TG_ENABLE_QUEUE_STATS
	/* Initialize queue stats collection and pushing to firmware */
	ret = fb_tgd_queue_stats_init(fb_drv_data);
	if (ret != 0) {
		dev_err(&pdev->dev, "fb_tgd_queue_stats_init failed: ret=%d",
			ret);
		goto out;
	}
#endif

	/* We are ready to handle FW events now */
	fb_drv_data->rx_event_enable = true;

	/* Bring interfaces up if requested by module parameters */
	if (tgd_auto_up) {
		struct tgd_terra_dev_priv *dev_priv;

		rtnl_lock();
		list_for_each_entry(dev_priv, &fb_drv_data->dev_q_head,
				    list_entry)
		{
			int flags;

			flags = dev_priv->dev->flags;
			if (flags & IFF_UP)
				continue;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
			dev_change_flags(dev_priv->dev, flags | IFF_UP, NULL);
#else
			dev_change_flags(dev_priv->dev, flags | IFF_UP);
#endif
		}
		rtnl_unlock();
	}

	/* put it in list of tgd_drivers */
	klist_add_tail(&fb_drv_data->driver_list_node, &tgd_drivers_list);

	/* Send the netlink message to the subscribers that the device is up. */
	tgd_nlsdn_send_device_updown_status(fb_drv_data, DEVICE_UP);
out:
	if (ret) {
		tg_bh_remove(pdev);
	}
	return ret;
}

extern const struct platform_device_id tg_bh_id_table[];

/* We are not ready to be auto-loaded yet. */
/* MODULE_DEVICE_TABLE(platform, tg_bh_id_table); */

static struct platform_driver tg_bh_driver = {
    .probe = tg_bh_probe,
    .remove = tg_bh_remove,
    .shutdown = tg_bh_shutdown,
    .id_table = tg_bh_id_table,
    .driver =
	{
	    .name = "terragraph",
	},
};

static void __exit tgd_terra_exit_module(void)
{
	platform_driver_unregister(&tg_bh_driver);

	tgd_gps_exit();

	if (fb_tgd_debug_root_dir)
		debugfs_remove_recursive(fb_tgd_debug_root_dir);
	fb_tgd_debug_root_dir = NULL;

	tgd_nlsdn_exit();
}

static int __init tgd_terra_init_module(void)
{
	int ret;

	/* One driver for each baseband card */
	klist_init(&tgd_drivers_list, NULL, NULL);

	/* Initial NL interface */
	ret = tgd_nlsdn_init();
	if (ret) {
		pr_err("terra: nl_init failed: ret=%d", ret);
		goto out;
	}

	/* Create debugfs root */
	fb_tgd_debug_root_dir = debugfs_create_dir("terragraph-baseband", NULL);
	if (fb_tgd_debug_root_dir == NULL) {
		pr_err("Could not create root debugfs dir \"%s\"\n",
		       "terragraph-baseband");
		ret = -ENOMEM;
		goto out;
	}

	if (!debugfs_create_u32("debug_lvl", 0644, fb_tgd_debug_root_dir,
				&tgd_dbg_enable_level)) {
		pr_err("Could not create debugfs file \"%s\"\n", "debug_lvl");
		ret = -ENOMEM;
		goto out;
	}

	/* Attach to GPS device */
	ret = tgd_gps_init();
	if (ret != 0) {
		pr_err("terra: Unable to init GPS interface: ret=%d", ret);
		goto out;
	}

	/* Attach to backhaul devices */
	ret = platform_driver_register(&tg_bh_driver);
	if (ret != 0) {
		pr_err("terra: BH platform_driver_register failed: ret=%d",
		       ret);
		goto out;
	}

	return 0;
out:
	tgd_terra_exit_module();
	return ret;
}

module_init(tgd_terra_init_module);
module_exit(tgd_terra_exit_module);

MODULE_DESCRIPTION("Facebook Wireless Terragraph Driver");
MODULE_AUTHOR("Roy Jose");
MODULE_LICENSE("Dual MIT/GPL");
