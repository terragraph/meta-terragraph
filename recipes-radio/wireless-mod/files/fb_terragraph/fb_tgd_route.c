/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Terragraph routing common code */

#define pr_fmt(fmt) "fb_tgd_terragraph: " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>

#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/skbuff.h>

#include <fb_tg_fw_driver_if.h>
#include "fb_tgd_terragraph.h"
#include "fb_tgd_fw_if.h"
#include "fb_tgd_debug.h"
#include "fb_tgd_route.h"

#ifdef TG_ENABLE_NSS
extern int tgd_enable_nss;
extern int fb_tgd_rt_nss_module_init(struct tgd_terra_driver *tgd_data);
#endif

#ifdef TG_ENABLE_PFE
extern int tgd_enable_pfe;
extern int fb_tgd_rt_pfe_module_init(struct tgd_terra_driver *tgd_data);
#endif

#ifdef TG_ENABLE_DPAA2
extern int tgd_enable_dpaa2;
extern int fb_tgd_rt_dpaa2_module_init(struct tgd_terra_driver *tgd_data);
#endif

extern int fb_tgd_rt_linux_module_init(struct tgd_terra_driver *tgd_data);

int tgd_rt_init(struct tgd_terra_driver *tgd_data)
{
#ifdef TG_ENABLE_NSS
	if (tgd_enable_nss)
		return fb_tgd_rt_nss_module_init(tgd_data);
#endif
#ifdef TG_ENABLE_PFE
	if (tgd_enable_pfe) {
		struct device_node *node;
		/* Only probe if machine is expected to have PFE */
		node = of_find_compatible_node(NULL, NULL, "fsl,pfe");
		if (node != NULL) {
			of_node_put(node);
			return fb_tgd_rt_pfe_module_init(tgd_data);
		}
	}
#endif
#ifdef TG_ENABLE_DPAA2
	if (tgd_enable_dpaa2) {
		struct device_node *node;
		int ret;

		/* Only probe if machine is expected to have DPAA2 */
		node = of_find_compatible_node(NULL, NULL, "fsl,qoriq-mc");
		if (node != NULL) {
			of_node_put(node);
			ret = fb_tgd_rt_dpaa2_module_init(tgd_data);
			if (ret == 0 || ret != -ENOTSUPP)
				return ret;
		}
	}
#endif
	return fb_tgd_rt_linux_module_init(tgd_data);
}

void tgd_rt_fini(struct tgd_terra_driver *tgd_data)
{
	if (tgd_data->rt_backend != NULL) {
		tgd_data->rt_backend->rt_mod_fini(tgd_data);
		tgd_data->rt_backend = NULL;
	}
}
