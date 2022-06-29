/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Wrapper around driver-specific backhaul API */

#ifndef TGD_BACKHAUL_H
#define TGD_BACKHAUL_H

#define FB_TGD_BH_MAX_HDR_SIZE 66

#define FB_TGD_MQ_BK 0
#define FB_TGD_MQ_BE 1
#define FB_TGD_MQ_VI 2
#define FB_TGD_MQ_VO 3

/* Constant possibly in need of being vendor-dependent */
#define FB_TGD_BH_MQ_QUEUE_NUM 4

/*
 * Priorities for SKBs, based on 802.1p priority code
 * points (https://en.wikipedia.org/wiki/IEEE_802.11e-2005)
 * We only support two for locally originated traffic
 */
#define FB_TGD_BH_SKB_PRIO_BE 0
#define FB_TGD_BH_SKB_PRIO_BK 2
#define FB_TGD_BH_SKB_PRIO_VI 5
#define FB_TGD_BH_SKB_PRIO_VO 6

/* Forward declarations */
struct sk_buff;
struct tgd_terra_driver;
struct tgd_terra_dev_priv;
struct fb_tgd_bh_link_stats;

int fb_tgd_bh_api_version(struct tgd_terra_driver *fb_drv, int *drv_version,
			  int *bh_ver);
int fb_tgd_bh_api_init(struct device *dev, struct tgd_terra_driver *fb_drv);
int fb_tgd_bh_ioctl(struct tgd_terra_driver *fb_drv, uint8_t *req_buf,
		    uint req_len, uint8_t *resp_buf, uint resp_len);
int fb_tgd_bh_set_key(struct tgd_terra_dev_priv *priv, const uint8_t *dest_mac,
		      const uint8_t *key_data, uint key_len);
int fb_tgd_bh_tx_data(struct tgd_terra_dev_priv *priv, struct sk_buff *skb);
uint16_t fb_tgd_bh_select_queue(struct tgd_terra_dev_priv *priv, int prio);

int fb_tgd_bh_add_links_info(struct tgd_terra_dev_priv *priv,
			     uint8_t *link_mac_addr, uint8_t rxLinkId,
			     uint8_t txLinkId);
int fb_tgd_bh_del_links_info(struct tgd_terra_dev_priv *priv);
void fb_tgd_bh_setup_netdev(struct tgd_terra_dev_priv *priv);
void fb_tgd_bh_cleanup_links(struct tgd_terra_driver *fb_drv_data);

void tgd_terra_update_link_stats(struct tgd_terra_dev_priv *priv);

extern int fb_tgd_bh_register_client(struct tgd_terra_driver *fb_drv_data);
extern int fb_tgd_bh_unregister_client(struct tgd_terra_driver *fb_drv_data);
extern int tgd_link_pkts_pending(struct tgd_terra_dev_priv *priv);

#endif
