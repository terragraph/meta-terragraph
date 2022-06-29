/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Firmware interface related API's */

#ifndef TGD_FW_IF_H
#define TGD_FW_IF_H

int tgd_send_fw_init(struct tgd_terra_driver *fb_drv_data,
		     unsigned int var_data_len, unsigned char *var_data_ptr);
int tgd_send_bmfm_cfg_req(struct tgd_terra_driver *fb_drv_data,
			  tgEthAddr *link_sta_mac_addr, tgBfRole bf_role,
			  unsigned int var_data_len,
			  unsigned char *var_data_ptr);
int tgd_send_start_assoc_req(tgEthAddr *link_sta_mac_addr,
			     unsigned int var_data_len,
			     unsigned char *var_data_ptr);
int tgd_send_disassoc_req(struct tgd_terra_driver *fb_drv_data,
			  tgEthAddr *link_sta_mac_add);
void tgd_fw_msg_handler(void *fb_drv_data, uint8_t *event, unsigned long size);
int tgd_send_gps_time(struct tgd_terra_driver *fb_drv_data,
		      struct timespec *time);
int tgd_send_gps_pos(struct tgd_terra_driver *fb_drv_data, int latitude,
		     int longitude, int height, int accuracy);
int tgd_send_queue_stats(struct tgd_terra_driver *fb_drv_data,
			 const tgSbQueueStats *stats, int numLinks);
int tgd_send_passthrough_to_fw(struct tgd_terra_driver *fb_drv_data,
			       char *src_data_ptr, int len);
void tgd_send_fw_shutdown(struct tgd_terra_driver *fb_drv_data);

#endif
