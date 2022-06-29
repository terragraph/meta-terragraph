/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * Facebook Terragraph driver - firmware message definitions
 */

#ifndef FB_TG_FB_GPS_DRIVER_IF_H
#define FB_TG_FB_GPS_DRIVER_IF_H

/*
 * Callbacks implemented by the clients of GPS driver.
 */
struct fb_tgd_gps_clnt {
	void (*time_update)(void *clnt_data, struct timespec *ts);
	void (*stat_update)(void *clnt_data, void *buf, int buflen);
};

/*
 * Functions implemented by the GPS driver.
 */
struct fb_tgd_gps_impl {
	int (*init_client)(struct fb_tgd_gps_clnt *clnt, void *clnt_data,
	    void **gps_data);
	void (*fini_client)(struct fb_tgd_gps_clnt *clnt, void *gps_data);

	int (*start_sync)(void *gps_data);
	void (*stop_sync)(void *gps_data);
	int (*handle_nl_msg)(void *gps_data, unsigned char *cmd_ptr,
	    int cmd_len, void *rsp_buf, int rsp_buf_len);
};

/*
 * Platform device data for Tarragraph-comptible GPS interface
 */
struct tgd_gps_platdata {
	int drv_api_version;
	const struct fb_tgd_gps_impl *drv_gps_ops;
};

/*
 * GPS API version implemented by the GPS driver module
 */
#define TGD_GPS_API_VERSION 0x0001

#define TGD_GPS_COMPATIBLE_STRING "terragraph,gps"

#endif /* FB_TG_FB_GPS_DRIVER_IF_H */
