/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Generic routing backend for Terragraph driver */

#ifndef TGD_ROUTE_H
#define TGD_ROUTE_H

/* Forward declarations */

struct tgd_terra_dev_priv;

typedef void (*tgd_rt_module_fini_t)(struct tgd_terra_driver *tgd_data);

typedef int (*tgd_rt_add_device_t)(struct tgd_terra_dev_priv *dev_priv);
typedef void (*tgd_rt_del_device_t)(struct tgd_terra_dev_priv *dev_priv);

typedef void (*tgd_rt_set_link_state_t)(struct tgd_terra_dev_priv *dev_priv,
					tgLinkStatus state);

typedef void (*tgd_rt_flow_control_t)(struct tgd_terra_dev_priv *dev_priv,
				      unsigned char qid, bool onoff);

typedef void (*tgd_rt_rx_t)(struct tgd_terra_dev_priv *dev_priv,
			    struct sk_buff *skb);
typedef void (*tgd_rt_tx_t)(struct tgd_terra_dev_priv *dev_priv,
			    struct sk_buff *skb);

struct fb_tgd_routing_backend {
	tgd_rt_module_fini_t rt_mod_fini; /* Cleanup the backed on unload */

	tgd_rt_add_device_t rt_add_dev; /* Initialize per-device state */
	tgd_rt_del_device_t rt_del_dev; /* Remove per-device state */

	tgd_rt_set_link_state_t
	    rt_set_link_state; /* Handle  link state change */
	tgd_rt_flow_control_t
	    rt_flow_control; /* Handle backpressure from wlan */

	tgd_rt_tx_t rt_tx; /* Prepare sk_buff for transmisstion */
	tgd_rt_rx_t rt_rx; /* Handle packed received from BH */
};

int tgd_rt_init(struct tgd_terra_driver *driver_data);
void tgd_rt_fini(struct tgd_terra_driver *driver_data);

static inline int tgd_rt_add_device(struct tgd_terra_driver *driver_data,
				    struct tgd_terra_dev_priv *dev_priv)
{
	dev_priv->rt_backend = driver_data->rt_backend;
	return dev_priv->rt_backend->rt_add_dev(dev_priv);
}

static inline void tgd_rt_del_device(struct tgd_terra_dev_priv *dev_priv)
{
	if (dev_priv->rt_backend) {
		dev_priv->rt_backend->rt_del_dev(dev_priv);
		dev_priv->rt_backend = NULL;
	}
}

static inline void tgd_rt_rx(struct tgd_terra_dev_priv *dev_priv,
			     struct sk_buff *skb)
{
	dev_priv->rt_backend->rt_rx(dev_priv, skb);
}

static inline void tgd_rt_tx(struct tgd_terra_dev_priv *dev_priv,
			     struct sk_buff *skb)
{
	dev_priv->rt_backend->rt_tx(dev_priv, skb);
}

static inline void tgd_rt_set_link_state(struct tgd_terra_dev_priv *dev_priv,
					 tgLinkStatus state)
{
	dev_priv->rt_backend->rt_set_link_state(dev_priv, state);
}

static inline void tgd_rt_flow_control(struct tgd_terra_dev_priv *dev_priv,
				       unsigned char qid, bool onoff)
{
	dev_priv->rt_backend->rt_flow_control(dev_priv, qid, onoff);
}

#endif
