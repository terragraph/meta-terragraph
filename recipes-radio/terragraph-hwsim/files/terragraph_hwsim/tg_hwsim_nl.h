/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
/* @lint-ignore-every TXT2 Tab Literal */

#ifndef TG_HWSIM_NL_H
#define TG_HWSIM_NL_H

#define TG_HWSIM_VENDOR_STR "qualcomm"

#define SB_PASSTHRU_MAX 1024

#define DRIVER_CFG_HDR_SIZE 2

struct tg_hwsim_link_status {
	char *ifname;
	u64 link_addr;
	tgLinkStatus link_status;
	tgLinkFailureCause failure_cause;
	u8 node_type;
	u8 peer_type;
};

void tg_hwsim_notify_link_status_from_dev(struct terradev_priv_data *terradev,
					  tgLinkFailureCause failure_cause);

int tg_hwsim_notify_link_status(struct tg_hwsim_link_status *link_status_info,
				struct baseband_data *bb);

int tg_hwsim_notify_wsec_linkup_status(struct terradev_priv_data *terradev);

int tg_hwsim_send_nl_nb_passthru(struct baseband_data *bb,
					unsigned char *var_data,
					int var_data_len);

int init_tg_hwsim_netlink(void) __init;
void exit_tg_hwsim_netlink(void) __exit;

#endif
