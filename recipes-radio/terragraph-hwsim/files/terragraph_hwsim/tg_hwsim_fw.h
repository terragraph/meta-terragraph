/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
/* @lint-ignore-every TXT2 Tab Literal */

#ifndef TG_HWSIM_FW_H
#define TG_HWSIM_FW_H

/* HACK: prevent firmware header file from using userspace types
 * from stdint.h in kernel module */
#define TG_FIRMWARE
#include <linux/types.h>
#include <fb_tg_fw_pt_if.h>
#include <fb_tg_fw_driver_if.h>

#include "tg_hwsim.h"

struct tg_hwsim_fw_op {
	uint16_t cmd;
	int (*cb)(tgfPtMsg *, struct baseband_data *);
};

int tg_hwsim_handle_fw_msg(struct baseband_data *bb, unsigned char *var_data,
			   int var_data_len);

#endif
