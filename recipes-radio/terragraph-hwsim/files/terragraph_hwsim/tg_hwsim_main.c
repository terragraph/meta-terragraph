/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
/* @lint-ignore-every TXT2 Tab Literal */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>

#include "tg_hwsim.h"
#include "tg_hwsim_nl.h"

static LIST_HEAD(basebands);

static int num_of_basebands = 4;
module_param(num_of_basebands, int, 0444);
MODULE_PARM_DESC(num_of_basebands, "Number of simulated Terragraph "
				   "baseband devices");

static int tgd_num_of_virt_links = 16;
module_param(tgd_num_of_virt_links, int, 0444);
MODULE_PARM_DESC(tgd_num_of_virt_links,
		 "Number of simulated terradev's (terraX interfaces) "
		 "per Terragraph device");

static bool tgd_auto_up = false;
module_param(tgd_auto_up, bool, 0444);
MODULE_PARM_DESC(tgd_auto_up,
		 "Sets the IFF_UP flag on every terraX interface");


int tg_hwsim_assoc_on_baseband(struct baseband_data *bb, u64 link_addr)
{
	int err = 0;
	struct terradev_priv_data *terradev;

	terradev = get_terradev_from_link_addr(bb, link_addr);
	if (terradev == NULL) {
		printk(KERN_DEBUG
		       "tg_hwsim: couldn't associate unallocated link, "
		       "allocating terradev first");
		terradev = tg_hwsim_dev_alloc(bb, link_addr);
		if (terradev == NULL) {
			printk(KERN_DEBUG
			       "tg_hwsim: couldn't allocate link for assoc "
			       "request");
			err = -EBUSY;
			goto out;
		}
	}
	terradev->link_status = TG_LINKUP;

	printk(KERN_DEBUG "tg_hwsim: associating with peer %llx on %s",
	       link_addr, terradev->netdev->name);
		
	netif_carrier_on(terradev->netdev);
	netif_tx_wake_all_queues(terradev->netdev);

	tg_hwsim_notify_link_status_from_dev(terradev, TG_NOT_APPLICABLE);

	tg_hwsim_notify_wsec_linkup_status(terradev);
out:
	return err;
}

int tg_hwsim_dissoc_on_baseband(struct baseband_data *bb, u64 link_addr)
{
	struct terradev_priv_data *terradev;
	int err = 0;

	terradev = get_terradev_from_link_addr(bb, link_addr);
	if (terradev == NULL) {
		printk(KERN_DEBUG
		       "tg_hwsim: couldn't disassociate unallocated link");
		err = -ENOENT;
		goto out;
	}
	terradev->link_status = TG_LINKDOWN;

	printk(KERN_DEBUG "tg_hwsim: disassociating with peer %llx on %s",
	       link_addr, terradev->netdev->name);

	netif_carrier_off(terradev->netdev);
	netif_tx_disable(terradev->netdev);

	tg_hwsim_notify_link_status_from_dev(terradev, TG_NOT_APPLICABLE);
out:
	return err;
}

struct terradev_priv_data *get_terradev_from_link_addr(struct baseband_data *bb,
						       u64 link_addr)
{
	struct terradev_priv_data *terradev;

	list_for_each_entry(terradev, &bb->terradevs, terradevs) {
		if (terradev->link_sta_addr == link_addr) {
			return terradev;
		}
	}
	printk(KERN_DEBUG
	       "tg_hwsim: No terradev allocated to MAC address %llx found",
	       link_addr);

	return NULL;
}

struct baseband_data *get_baseband_from_addr(u64 mac_addr)
{
	struct baseband_data *bb_data;

	list_for_each_entry(bb_data, &basebands, basebands) {
		if (bb_data->mac_addr == mac_addr) {
			return bb_data;
		}
		
		/* when address is unset return first baseband */
		if (mac_addr == 0) {
			return bb_data;
		}
	}

	printk(KERN_DEBUG "tg_hwsim: No baseband with MAC address %llx found",
	       mac_addr);
	return NULL;
}

void set_baseband_mac(struct baseband_data *bb, u64 mac_addr)
{
	struct terradev_priv_data *terradev;
	bb->mac_addr = mac_addr;

	list_for_each_entry(terradev, &bb->terradevs, terradevs) {
		u64_to_ether_addr(mac_addr, terradev->netdev->dev_addr);
	}
}

struct terradev_priv_data *tg_hwsim_dev_alloc(struct baseband_data *bb,
					      u64 link_addr)
{
	struct terradev_priv_data *terradev = NULL;
	bool avail = false;

	list_for_each_entry(terradev, &bb->terradevs, terradevs) {
		/* only select initialized links */
		if (terradev->link_status != TG_LINKINIT) {
			continue;
		}

		avail = true;

		/* prefer devices that were used for this peer in the past or
		 * are previously unused */
		if (terradev->link_sta_addr == link_addr
		 || terradev->link_sta_addr == 0) {
			break;
		}
	}

	if (avail) {
		terradev->link_sta_addr = link_addr;
		return terradev;
	}
	return NULL;
}

static struct net_device_stats *get_terradev_stats(struct net_device *netdev)
{
	struct terradev_priv_data *data;
	data = netdev_priv(netdev);
	return &data->stats;
}

static inline bool is_terradev_mac(u64 mac_addr)
{
	return (mac_addr >> MAC_PREFIX_SHIFT) == TERRADEV_MAC_PREFIX;
}

static inline bool is_qemudev_mac(u64 mac_addr)
{
	return (mac_addr >> MAC_PREFIX_SHIFT) == QEMUDEV_MAC_PREFIX;
}

static inline u64 terradev_mac_to_qemudev_mac(u64 mac_addr)
{
	return ((mac_addr & (u64) MAC_PREFIX_MASK) | (u64) QEMUDEV_MAC_PREFIX_MASK);
}

static inline u64 qemudev_mac_to_terradev_mac(u64 mac_addr)
{
	return ((mac_addr & (u64) MAC_PREFIX_MASK) | (u64) TERRADEV_MAC_PREFIX_MASK);
}

static netdev_tx_t terradev_start_xmit(struct sk_buff *skb,
				       struct net_device *netdev)
{
	struct terradev_priv_data *data;
	struct net_device_stats *stats;

	data = netdev_priv(netdev);

	stats = &data->stats;
	stats->tx_packets++;
	stats->tx_bytes += skb->len;

	if (data->baseband->transmit_netdev != NULL
	 && data->link_status == TG_LINKUP) {
		struct ethhdr *skb_eth_header;
		u64 dest_addr;
		skb_eth_header = eth_hdr(skb);
		dest_addr = ether_addr_to_u64(skb_eth_header->h_dest);
		if (is_terradev_mac(dest_addr)) {
			/* convert terradev MAC to QEMU dev MAC so that QEMU
			 * dev on other side doesn't drop the frame */
			dest_addr = terradev_mac_to_qemudev_mac(dest_addr);
			u64_to_ether_addr(dest_addr, skb_eth_header->h_dest);
		} else if (!is_multicast_ether_addr(skb_eth_header->h_dest)) {
			/* drop any frames not destined for a terradev */
			dev_kfree_skb(skb);
			goto out;
		}
		/* forward from a terradev to its corresponding QEMU virtual
		 * ethernet device */
		skb->dev = data->baseband->transmit_netdev;
		dev_queue_xmit(skb);
	} else {
		dev_kfree_skb(skb);
	}

out:
	return NETDEV_TX_OK;
}

static rx_handler_result_t terradev_handle_rx(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct baseband_data *bb = rcu_dereference(skb->dev->rx_handler_data);
	struct terradev_priv_data *terradev;
	struct ethhdr *skb_eth_header;
	u64 mac_addr;
	int ret = RX_HANDLER_PASS;
	
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb)) {
		printk(KERN_DEBUG
		       "tg_hwsim: failed skb share check in rx handler");
		goto out;
	}
	*pskb = skb;

	skb_eth_header = eth_hdr(skb);
	mac_addr = ether_addr_to_u64(skb_eth_header->h_source);

	if (unlikely(!is_terradev_mac(mac_addr))) {
		dev_kfree_skb(skb);
		ret = RX_HANDLER_CONSUMED;
		goto out;
	}

	terradev = get_terradev_from_link_addr(bb, mac_addr);
	if (unlikely(terradev == NULL)) {
		if (mac_addr == bb->mac_addr) {
			goto out;
		}
		/* HACK: for now, bring a link up on receipt of any packet. this
		 * will only work properly for a setup with 2 nodes on one eth
		 * device, since broadcast/multicast packets will go to multiple
		 * nodes. */
		tg_hwsim_assoc_on_baseband(bb, mac_addr);
		terradev = get_terradev_from_link_addr(bb, mac_addr);
		if (terradev == NULL) {
			printk(KERN_DEBUG
			       "tg_hwsim: failed to associate link for "
			       "new receiving mac addr %llx", mac_addr);
			dev_kfree_skb(skb);
			ret = RX_HANDLER_CONSUMED;
			goto out;
		}
	}

	mac_addr = ether_addr_to_u64(skb_eth_header->h_dest);
	if (likely(is_qemudev_mac(mac_addr))) {
		/* convert QEMU dev MAC to terradev MAC so that the QEMU
		 * netdev is transparent to the terraX netdevs*/
		mac_addr = qemudev_mac_to_terradev_mac(mac_addr);
		u64_to_ether_addr(mac_addr, skb_eth_header->h_dest);
	}

	/* forwards packets from the QEMU virtual ethernet device to an
	 * associated terradev */
	skb->dev = terradev->netdev;
	netif_rx(skb);
	ret = RX_HANDLER_CONSUMED;

out:
	return ret;
}

static int terradev_open(struct net_device *netdev)
{
	netif_start_queue(netdev);
	return 0;
}

static int terradev_close(struct net_device *netdev)
{
	netif_stop_queue(netdev);
	return 0;
}

static const struct net_device_ops terradev_ops = {
	.ndo_get_stats		= get_terradev_stats,
	.ndo_start_xmit		= terradev_start_xmit,
	.ndo_open		= terradev_open,
	.ndo_stop		= terradev_close,
};

static void delete_terradev(struct terradev_priv_data *data)
{
	struct net_device *netdev;

	netdev = data->netdev;

	if (netdev->reg_state == NETREG_REGISTERED)
		unregister_netdev(data->netdev);

	free_netdev(data->netdev);
}

static void cleanup_terradevs(struct baseband_data *bb_data)
{
	struct terradev_priv_data *data;
	while ((data = list_first_entry_or_null(&bb_data->terradevs,
						struct terradev_priv_data,
						terradevs))) {
		list_del(&data->terradevs);
		delete_terradev(data);
	}
}

static void delete_baseband(struct baseband_data *data)
{
	struct net_device *netdev;

	cleanup_terradevs(data);

	netdev = data->netdev;
	if (netdev->reg_state == NETREG_REGISTERED)
		unregister_netdev(data->netdev);
	
	if (data->transmit_netdev) {
		rtnl_lock();
		netdev_rx_handler_unregister(data->transmit_netdev);
		rtnl_unlock();
	}

	free_netdev(data->netdev);
}

static void cleanup_basebands(void)
{
	struct baseband_data *data;
	while ((data = list_first_entry_or_null(&basebands,
						struct baseband_data,
						basebands))) {
		list_del(&data->basebands);
		delete_baseband(data);
	}
}

static void setup_terradev(struct net_device *netdev)
{
	netdev->netdev_ops = &terradev_ops;
	ether_setup(netdev);

	netif_carrier_off(netdev);
	netif_tx_disable(netdev);
}

static int add_terradev(struct baseband_data *baseband)
{
	struct net_device *netdev;
	struct terradev_priv_data *data;
	int err = 0;

	netdev = alloc_netdev(sizeof(*data), "terra%d", NET_NAME_ENUM,
			      setup_terradev);
	if (netdev == NULL) {
		printk(KERN_DEBUG
		       "tg_hwsim: Failed to allocate netdev for terraX device");
		err = -ENOMEM;
		goto out;
	}
	u64_to_ether_addr(baseband->mac_addr, netdev->dev_addr);

	data = netdev_priv(netdev);
	data->netdev = netdev;
	list_add_tail(&data->terradevs, &baseband->terradevs);
	data->baseband = baseband;

	data->link_status = TG_LINKINIT;

	err = register_netdev(netdev);
	if (err) {
		printk(KERN_DEBUG
		       "tg_hwsim: Failed to register %s with error %i",
		       netdev->name, err);
		goto out_free;
	}
out:
	return err;
out_free:
	free_netdev(netdev);
	return err;
}

static struct net_device *find_eth_netdev_with_addr(u64 mac_addr)
{
	struct net_device *netdev = NULL;

	/* search through all netdevs in the init namespace to find one that
	 * matches the given MAC address */
	printk(KERN_DEBUG
	       "tg_hwsim: attempting to find eth netdev with MAC address %llx",
	       mac_addr);
	read_lock(&dev_base_lock);
	netdev = first_net_device(&init_net);
	while (netdev) {
		if (ether_addr_to_u64(netdev->dev_addr) == mac_addr
		 && strncmp(netdev->name, "eth", 3) == 0) {
			break;
		}
		netdev = next_net_device(netdev);
	}
	read_unlock(&dev_base_lock);

	return netdev;
}

static int baseband_set_addr(struct net_device *dev, void *p)
{
	struct baseband_data *baseband;
	struct sockaddr *addr = p;
	u64 mac_addr;

	baseband = netdev_priv(dev);
	mac_addr = ether_addr_to_u64(addr->sa_data);
	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
	set_baseband_mac(baseband, mac_addr);

	/* attempts to find a virtual QEMU ethernet device with a matching MAC
	 * address to forward packets received on terradevs to */
	baseband->transmit_netdev = find_eth_netdev_with_addr(
					terradev_mac_to_qemudev_mac(mac_addr)
				    );
	if (baseband->transmit_netdev) {
		int err;
		printk(KERN_DEBUG
		       "tg_hwsim: bound QEMU netdev %s to baseband %s",
		       baseband->transmit_netdev->name, dev->name);
		if (netdev_is_rx_handler_busy(baseband->transmit_netdev)) {
			printk(KERN_DEBUG
			       "tg_hwsim: RX handler is busy on QEMU netdev %s."
			       " the handler was probably already registered by"
			       " hwsim",
			       baseband->transmit_netdev->name);
			return 0;
		}
		if ((err = netdev_rx_handler_register(baseband->transmit_netdev,
						      terradev_handle_rx,
						      baseband))) {
			printk(KERN_DEBUG "tg_hwsim: error rx handler: %d",
			       err);
			baseband->transmit_netdev = NULL;
		}
	}

	return 0;
}

static netdev_tx_t noop_start_xmit(struct sk_buff *skb,
				   struct net_device *netdev)
{
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static const struct net_device_ops baseband_ops = {
	.ndo_set_mac_address	= baseband_set_addr,
	.ndo_start_xmit		= noop_start_xmit,
};

static void setup_baseband(struct net_device *netdev)
{
	netdev->netdev_ops = &baseband_ops;
	ether_setup(netdev);
}

static int add_baseband(void)
{
	struct baseband_data *baseband;
	struct net_device *netdev;
	int err = 0;
	int i;

	netdev = alloc_netdev(sizeof(struct baseband_data), "wlan%d",
			      NET_NAME_ENUM, setup_baseband);
	if (netdev == NULL) {
		printk(KERN_DEBUG
		       "tg_hwsim: Failed to allocate netdev for wlanX device");
		err = -ENOMEM;
		goto out;
	}

	baseband = netdev_priv(netdev);
	baseband->netdev = netdev;

	list_add_tail(&baseband->basebands, &basebands);
	INIT_LIST_HEAD(&baseband->terradevs);

	for (i = 0; i < tgd_num_of_virt_links; i++) {
		err = add_terradev(baseband);
		if (err)
			goto out;
	}

	err = register_netdev(netdev);
	if (err) {
		printk(KERN_DEBUG
		       "tg_hwsim: Failed to register %s with error %i",
		       netdev->name, err);
		goto out_free;
	}
out:
	return err;
out_free:
	free_netdev(netdev);
	return err;
}

static void set_all_terradevs_up(void)
{
	struct baseband_data *bb;
	struct terradev_priv_data *terradev;

	rtnl_lock();
	list_for_each_entry(bb, &basebands, basebands) {
		list_for_each_entry(terradev, &bb->terradevs, terradevs) {
			int flags;

			flags = terradev->netdev->flags;
			if (flags & IFF_UP)
				continue;

			dev_change_flags(terradev->netdev, flags | IFF_UP);
		}
	}
	rtnl_unlock();
}

static __init int init_tg_hwsim_module(void)
{
	int i, err;

	err = init_tg_hwsim_netlink();
	if (err)
		goto out;

	for (i = 0; i < num_of_basebands; i++) {
		err = add_baseband();
		if (err)
			goto out_cleanup;
	}

	if (tgd_auto_up)
		set_all_terradevs_up();	
	return 0;
out_cleanup:
	cleanup_basebands();
out:
	return err;
}

static __exit void exit_tg_hwsim_module(void)
{
	exit_tg_hwsim_netlink();
	cleanup_basebands();
}

module_init(init_tg_hwsim_module);
module_exit(exit_tg_hwsim_module);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("Software simulator of Terragraph hardware");
