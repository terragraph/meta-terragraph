/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef _fb_tgd_cfg80211_h_
#define _fb_tgd_cfg80211_h_

#include <linux/ieee80211.h>
#include <net/cfg80211.h>

#include "fb_tgd_fw_common.h"

#define TGD_CFG80211_DEBUG 1

struct tgd_cfg80211_info {
	bool ap_started;   /* start_ap is called, work as authenticator */
	bool tg_connected; /* tg link level up */
	tgWsecAuthType wsec_auth;
	bool m4_sent;
};
extern void tgd_cfg80211_get_info(struct tgd_terra_dev_priv *priv,
				  struct tgd_cfg80211_info *info);
extern void tgd_cfg80211_evt_tg_disconnect(struct tgd_terra_dev_priv *priv,
					   const u8 *pmac);
extern void tgd_cfg80211_evt_tg_connect(struct tgd_terra_dev_priv *priv,
					const u8 *pmac, u8 assocReqIeLen,
					u8 assocRespIeLen, u8 *ies,
					tgWsecAuthType wsec_auth);
extern void tgd_cfg80211_evt_m4_sent(struct tgd_terra_dev_priv *dev_priv);
extern bool tgd_cfg80211_is_4way_m4(struct tgd_terra_dev_priv *dev_priv,
				    struct sk_buff *skb);
extern struct wireless_dev *tgd_cfg80211_init(struct net_device *ndev);
extern void tgd_wdev_free(struct wireless_dev *wdev);

#endif /* _fb_tgd_cfg80211_h_ */
