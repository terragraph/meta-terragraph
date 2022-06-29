/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

#include <fb_tg_fw_driver_if.h>
#include <fb_tg_gps_driver_if.h>

#include "fb_tgd_terragraph.h"
#include "fb_tgd_fw_if.h"
#include "fb_tgd_gps_if.h"
#include "fb_tgd_nlsdn.h"
#include "fb_tgd_debug.h"

/* Module parameter */
#ifdef TG_ENABLE_GPS
static int tgd_enable_gps = 1;
#else
static int tgd_enable_gps = 0;
#endif
module_param(tgd_enable_gps, int, 0444);

static const struct fb_tgd_gps_impl *tgd_gps_ops;

enum tgd_gps_state {
	eTGD_GPS_INIT,
	eTGD_GPS_ACQUIRING,
	eTGD_GPS_ACQUIRED,
	eTGD_GPS_FW_SYNCED,
} tgd_gps_state_t;

/* Private state structure */
struct tgd_terra_gps_state {
	struct fb_tgd_gps_clnt gps_clnt;
	const struct fb_tgd_gps_impl *gps_impl;
	void *gps_data;
	struct tgd_terra_driver *drv_priv;
	enum tgd_gps_state gps_state;
	bool send_to_fw;
};

int tgd_gps_start_fw_sync(struct tgd_terra_driver *drv_priv)
{
	struct tgd_terra_gps_state *sc;
	int ret;

	sc = drv_priv->gps_state;
	if (sc == NULL || sc->gps_state < eTGD_GPS_ACQUIRED)
		return (-1);

	/* Do not do anything if already registered */
	if (sc->gps_state >= eTGD_GPS_FW_SYNCED)
		return (0);

	/* Tell the driver we expect time updates from now on */
	ret = sc->gps_impl->start_sync(sc->gps_data);
	if (ret == -1) {
		TGD_DBG_CTRL_ERROR("Unable to request GPS sync");
		return -1;
	}

	/* Remember what we did */
	TGD_DBG_CTRL_INFO("Start receiving GPS updates\n");
	sc->gps_state = eTGD_GPS_FW_SYNCED;

	return (0);
}

int tgd_gps_stop_fw_sync(struct tgd_terra_driver *drv_priv)
{
	struct tgd_terra_gps_state *sc;

	sc = drv_priv->gps_state;
	if (sc == NULL)
		return (-1);

	/* Nothing to be done if not registered yet */
	if (sc->gps_state <= eTGD_GPS_ACQUIRED)
		return (0);

	/* Tell driver we do not want callbacks */
	sc->gps_impl->stop_sync(sc->gps_data);
	sc->gps_state = eTGD_GPS_ACQUIRED;

	return (0);
}

void tgd_gps_send_to_fw(struct tgd_terra_driver *drv_priv, bool enable)
{
	struct tgd_terra_gps_state *sc;

	sc = drv_priv->gps_state;
	if (sc == NULL)
		return;

	if (enable) {
		if (tgd_gps_start_fw_sync(drv_priv) != 0)
			return;
		sc->send_to_fw = enable;
	} else {
		if (tgd_gps_stop_fw_sync(drv_priv) != 0)
			return;
		sc->send_to_fw = enable;
	}
}

int tgd_gps_get_nl_rsp(struct tgd_terra_driver *drv_priv,
		       unsigned char *cmd_ptr, int cmd_len,
		       unsigned char *rsp_buf, int rsp_buf_len)
{
	struct tgd_terra_gps_state *sc;

	sc = drv_priv->gps_state;
	if (sc == NULL)
		return -1;

	return sc->gps_impl->handle_nl_msg(sc->gps_data, cmd_ptr, cmd_len,
					   rsp_buf, rsp_buf_len);
}

void tgd_gps_dev_exit(struct tgd_terra_driver *drv_priv)
{
	struct tgd_terra_gps_state *sc;

	sc = drv_priv->gps_state;
	if (sc == NULL)
		return;
	sc->gps_impl->fini_client(&sc->gps_clnt, sc->gps_data);
	drv_priv->gps_state = NULL;
	kfree(sc);
}

#ifdef TG_ENABLE_GPS

/* Callbacks from GPS driver */
static void tgd_gps_time_update(void *ptr, struct timespec *ts)
{
	struct tgd_terra_gps_state *sc;

	sc = ptr;
	if (sc->send_to_fw)
		tgd_send_gps_time(sc->drv_priv, ts);
}

static void tgd_gps_stat_update(void *ptr, void *stats_buf, int stats_len)
{
	struct tgd_terra_gps_state *sc;

	sc = ptr;
	tgd_nlsdn_push_gps_stat_nb(sc->drv_priv, stats_buf, stats_len);
}

int tgd_gps_dev_init(struct tgd_terra_driver *drv_priv)
{
	struct tgd_terra_gps_state *sc;
	int ret;

	/*
	 * Check if GPS was disables or if no GPS module available
	 * on the system. Do not fail in that case.
	 */
	if (!tgd_enable_gps || tgd_gps_ops == NULL)
		return (0);

	sc = kzalloc(sizeof(*sc), GFP_KERNEL);
	if (sc == NULL) {
		TGD_DBG_CTRL_ERROR("Unable to allocate gps state structure");
		return (-ENOMEM);
	}

	/* Initialize the client structure */
	sc->gps_impl = tgd_gps_ops;
	sc->gps_clnt.time_update = tgd_gps_time_update;
	sc->gps_clnt.stat_update = tgd_gps_stat_update;

	/* Tell the GPS driver about us */
	ret = sc->gps_impl->init_client(&sc->gps_clnt, sc, &sc->gps_data);
	if (ret != 0) {
		TGD_DBG_CTRL_ERROR("Unable to register with GPS driver");
		kfree(sc);
		return (ret);
	}

	/* Success */
	sc->drv_priv = drv_priv;
	sc->gps_state = eTGD_GPS_ACQUIRED;

	drv_priv->gps_state = sc;

	return (0);
}

/*
 * Platform driver for Terragraph-compatible GPS interfaces.
 */
static int tg_gps_probe(struct platform_device *pdev)
{
	struct tgd_gps_platdata *pdata;

	pdata = dev_get_platdata(&pdev->dev);
	if (pdata == NULL)
		return -ENODEV;

	if (pdata->drv_api_version != TGD_GPS_API_VERSION) {
		TGD_DBG_CTRL_ERROR("ERROR: gpsVer: 0x%x != fbVer: 0x%x\n",
				   pdata->drv_api_version, TGD_GPS_API_VERSION);
		return -EPERM;
	}

	tgd_gps_ops = pdata->drv_gps_ops;
	return 0;
}

static int tg_gps_remove(struct platform_device *pdev)
{
	tgd_gps_ops = NULL;
	return 0;
}

static const struct platform_device_id tg_gps_id_table[] = {
    {TGD_GPS_COMPATIBLE_STRING, 0},
    {},
};

static struct platform_driver tg_gps_driver = {
    .probe = tg_gps_probe,
    .remove = tg_gps_remove,
    .id_table = tg_gps_id_table,
    .driver =
	{
	    .name = "terragraph-gps",
	},
};

int tgd_gps_init(void)
{
	return platform_driver_register(&tg_gps_driver);
}

void tgd_gps_exit(void)
{
	platform_driver_unregister(&tg_gps_driver);
}

#else

int tgd_gps_dev_init(struct tgd_terra_driver *drv_priv)
{
	drv_priv->gps_state = NULL;
	return (0);
}

int tgd_gps_init(void)
{
	return 0;
}

void tgd_gps_exit(void)
{
}

#endif /* TG_ENABLE_GPS */
