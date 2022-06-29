/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef __FB_TGD_UBLOX_GPS_MISC_H__
#define __FB_TGD_UBLOX_GPS_MISC_H__

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/slab.h>

#define TGD_UBLOX_GPS_DEV_NAME "ublox-gps"
#define TGD_UBLOX_GPS_I2C_SLAVE_ADDR 0x42
#define TGD_UBLOX_MSG_END 0xFF

typedef void *t_ublox_hndlr;

struct tgd_i2c_if_stat {
	unsigned int rx_fifo_empty_count;
	unsigned int rx_len_zero_count;
	unsigned int rx_len_truncated_count;
	unsigned int rx_poll_count;
	unsigned int rx_pkt_count;
	unsigned int rx_loop_break_count;
	unsigned int rx_error_count;
	unsigned int tx_byte_count;
	unsigned int tx_pkt_count;
	unsigned int tx_error_count;
};

struct tgd_ublox_gps_prv_data {
	struct i2c_client *client;
	void *msg_handler;
	struct tgd_i2c_if_stat stats;
};

int tgd_ublox_gps_init(void);
void tgd_ublox_gps_exit(void);
int tgd_get_gps_time_from_string(const char *gps_string,
				 struct timespec *read_time);
void *tgd_ublox_msg_handler_init(t_ublox_hndlr ublox_dev_handler);
int tgd_ublox_msg_handler_deinit(void *);

int tgd_get_i2c_stat(t_ublox_hndlr dev_hndl, char *buf, int buf_size);
int ublox_i2c_receive(t_ublox_hndlr dev_hndl, unsigned char *buf, int buf_size);
int ublox_i2c_send(t_ublox_hndlr dev_hndl, unsigned char *tx_msg, int len);

#endif
