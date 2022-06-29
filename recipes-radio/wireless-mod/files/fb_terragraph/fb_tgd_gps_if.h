/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Generic GPS interface or Terragraph driver */

#ifndef TGD_GPS_IF_H
#define TGD_GPS_IF_H

struct tgd_terra_driver;

int tgd_gps_init(void);
void tgd_gps_exit(void);

int tgd_gps_dev_init(struct tgd_terra_driver *drv_data);
void tgd_gps_dev_exit(struct tgd_terra_driver *drv_data);

int tgd_gps_start_fw_sync(struct tgd_terra_driver *drv_data);
int tgd_gps_stop_fw_sync(struct tgd_terra_driver *drv_data);

void tgd_gps_send_to_fw(struct tgd_terra_driver *drv_data, bool enable);

int tgd_gps_get_nl_rsp(struct tgd_terra_driver *drv_data,
		       unsigned char *cmd_ptr, int cmd_len,
		       unsigned char *rsp_buf, int rsp_buf_len);

#endif /* TGD_GPS_IF_H */
