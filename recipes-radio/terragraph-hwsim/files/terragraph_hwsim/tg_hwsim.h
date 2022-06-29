/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
/* @lint-ignore-every TXT2 Tab Literal */

#ifndef TG_HWSIM_H
#define TG_HWSIM_H

#include <linux/netdevice.h>
#include <fb_tgd_fw_common.h>

#define TERRADEV_MAC_PREFIX 0x5255
#define QEMUDEV_MAC_PREFIX 0x5256
#define MAC_PREFIX_SHIFT 32
#define MAC_PREFIX_MASK 0x0000FFFFFFFF
#define TERRADEV_MAC_PREFIX_MASK TERRADEV_MAC_PREFIX << MAC_PREFIX_SHIFT
#define QEMUDEV_MAC_PREFIX_MASK QEMUDEV_MAC_PREFIX << MAC_PREFIX_SHIFT

struct baseband_data {
	struct list_head basebands;
	struct list_head terradevs;
	u64 mac_addr;
	struct net_device *netdev;
	struct net_device *transmit_netdev;
};

struct terradev_priv_data {
	struct list_head terradevs;
	struct net_device *netdev;
	struct net_device_stats stats;
	struct baseband_data *baseband;
	u64 link_sta_addr;
	tgLinkStatus link_status;
};

int tg_hwsim_assoc_on_baseband(struct baseband_data *bb, u64 link_addr);

int tg_hwsim_dissoc_on_baseband(struct baseband_data *bb, u64 link_addr);

struct terradev_priv_data *get_terradev_from_link_addr(struct baseband_data *bb,
						       u64 link_addr);

struct baseband_data *get_baseband_from_addr(u64 mac_addr);

struct terradev_priv_data *tg_hwsim_dev_alloc(struct baseband_data *bb,
					      u64 link_addr);

void set_baseband_mac(struct baseband_data *bb, u64 mac_addr);

#endif
