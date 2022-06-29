/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Terragraph interface to Linux cfg80211 file */

#define pr_fmt(fmt) "fb_tgd_terragraph: " fmt

#include <linux/module.h>

#include <linux/kernel.h> /* printk() */
#include <linux/slab.h>   /* kmalloc() */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */

#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/notifier.h>    /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>

#include <fb_tg_fw_driver_if.h>
#include "fb_tgd_terragraph.h"
#include "fb_tgd_fw_if.h"
#include "fb_tgd_debug.h"
#include "fb_tgd_cfg80211.h"
#include "fb_tgd_backhaul.h"
#include "fb_tgd_nlsdn.h"

#ifdef TGD_CFG80211_DEBUG
#define TGD_CFG_DBG(format, args...) TGD_DBG_CFG80211_DBG(format, ##args)
#define TGD_CFG_FUNC_TRACE()                                                   \
	TGD_CFG_DBG("TGD_CFG_FUNC_TRACE %s line %u\n", __func__, __LINE__)
#define TGD_CFG_HEX_DUMP(msg, ptr, len)                                        \
	do {                                                                   \
		if (TGD_DBG_ENABLED(DBG_LVL_CFG80211_DBG)) {                   \
			print_hex_dump(KERN_INFO, msg, DUMP_PREFIX_NONE, 16,   \
				       1, (void *)ptr, len, 0);                \
		}                                                              \
	} while (0)
#else
#define TGD_CFG_DBG(format, args...)
#define TGD_CFG_FUNC_TRACE()
#define TGD_CFG_HEX_DUMP(msg, ptr, len)
#endif /* TGD_CFG_DEBUG */

#define TGD_ASSERT(c) BUG_ON(!(c))

#define TGD_M4_DELAY msecs_to_jiffies(2) /* 2ms */
#define TGD_M4_MAX_DELAY_CNT 13		 /* max 13 times, ~26ms > 1 BWGD */

#define RSN_VERSION 1
#define RSN_IE_CAPABILITY 0
#define TGD_MAX_IE_LEN 256

#define TGD_CFG80211_USE_GTK 0
#define TGD_CFG80211_NO_GTK 1

#define TGD_MAX_KEY_IX 4
#define TGD_MAX_KEY_LEN 64

#define CHAN60G(_channel, _flags)                                              \
	{                                                                      \
		.band = NL80211_BAND_60GHZ,                                    \
		.center_freq = 56160 + (2160 * (_channel)),                    \
		.hw_value = (_channel), .flags = (_flags),                     \
		.max_antenna_gain = 0, .max_power = 40,                        \
	}

#define TGD_MAX_RSN_IE_SIZE (48)

/*
 * Assumption here is, TG is always point to point, so even the authenticator
 * only has max of 1 connected supplicant.
 *
 * With the above assumption, the connection mgmt can be much simplified
 *
 * The key design decision of this module is to differentiate tg connection
 * state from the cfg80211 connection state. This allows the upper layer still
 * keeps the same control flow: the cfg80211 connection is initiated from the
 * supplicant side.
 *
 * Having a tg_connection up just get wiphy ready for application to move the
 * SMs. After tg_connected, wpa_supplicant 'scan' will immediately get the scan
 * result which has the info about the current tg_connected 'AP'.
 * Wpa_supplicant then can issue the 'connect' command, and this module again
 * immediately answers with connection complete, all without any activity in
 * the tg connection layer.
 *
 * Similar approach for the authenticator side.
 *
 * This design allows us to have 0 change on upper layers and minimus changes
 * to the current tg connection design & implementation.
 */
struct tgd_wiphy_priv {
	struct wiphy *wiphy;
	struct wireless_dev *wdev;
	bool ap_started;   /* start_ap is called, work as authenticator */
	bool tg_connected; /* tg link level up */
	tgWsecAuthType wsec_auth;
	bool m4_sent;
	u8 m4_delay_cnt;

	u8 key_len;
	struct key_params params;
	u8 params_key[TGD_MAX_KEY_LEN];

	u8 pmac[ETH_ALEN]; /* peer mac addr */

	unsigned int sinfo_gen;

	u8 supp_rsnie_len;
	u8 auth_rsnie_len;
	u8 supp_rsnie[TGD_MAX_RSN_IE_SIZE];
	u8 auth_rsnie[TGD_MAX_RSN_IE_SIZE];
	/* add more */

	struct workqueue_struct *wq_service;
	struct delayed_work set_key_worker;
};

/*
 * FIXME: not flexible, only for the initial proof of concept
 */
struct tgd_rsn_ie {
	u8 id;
	u8 len;
	u16 version;
	u8 gtk_suite[4];
	u16 ptk_suite_cnt;
	u8 ptk_suite[4];
	u16 key_mgmt_cnt;
	u8 key_mgmt[4];
	u16 capability;
} __packed;
#define TGD_RSN_IE_LEN sizeof(struct tgd_rsn_ie)
#define TGD_MIN_RSN_IE_SIZE TGD_RSN_IE_LEN

#define IE_ID(ie) (((u8 *)(ie))[0])
#define IE_LEN(ie) (((u8 *)(ie))[1])
#define IE_TOT_LEN(ie) (IE_LEN(ie) + 2)

static struct ieee80211_channel tgd_60ghz_channels[];

const static char fb_terragraph_ssid[] = "terragraph";
const static u8 fb_terragraph_ssid_len = sizeof(fb_terragraph_ssid) - 1;
#define TGD_SSID_IE_LEN (sizeof(fb_terragraph_ssid) - 1 + 2)

static const u32 tgd_cipher_suites[] = {
    WLAN_CIPHER_SUITE_GCMP,
};

static struct ieee80211_channel tgd_60ghz_channels[] = {
    CHAN60G(1, 0), CHAN60G(2, 0), CHAN60G(3, 0),
    /* channel 4 not supported yet */
};

static struct ieee80211_supported_band tgd_band_60ghz = {
    .channels = tgd_60ghz_channels,
    .n_channels = ARRAY_SIZE(tgd_60ghz_channels),
#if 1
    /* FIXME: clean non-AD/TG stuff out */
    .ht_cap =
	{
	    .ht_supported = true,
	    .cap = 0,					  /* TODO */
	    .ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K,   /* TODO */
	    .ampdu_density = IEEE80211_HT_MPDU_DENSITY_8, /* TODO */
	    .mcs =
		{
		    /* MCS 1..12 - SC PHY */
		    .rx_mask = {0xfe, 0x1f},		      /* 1..12 */
		    .tx_params = IEEE80211_HT_MCS_TX_DEFINED, /* TODO */
		},
	},
#endif
};

/*
 * FIXME: clean up unnecessary false info out of this variable
 */
static const struct ieee80211_txrx_stypes tgd_mgmt_stypes[NUM_NL80211_IFTYPES] =
    {
	[NL80211_IFTYPE_STATION] = {.tx = BIT(IEEE80211_STYPE_ACTION >> 4) |
					  BIT(IEEE80211_STYPE_PROBE_RESP >> 4),
				    .rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
					  BIT(IEEE80211_STYPE_PROBE_REQ >> 4)},
	[NL80211_IFTYPE_AP] = {.tx = BIT(IEEE80211_STYPE_ACTION >> 4) |
				     BIT(IEEE80211_STYPE_PROBE_RESP >> 4) |
				     BIT(IEEE80211_STYPE_ASSOC_RESP >> 4) |
				     BIT(IEEE80211_STYPE_DISASSOC >> 4),
			       .rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
				     BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
				     BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
				     BIT(IEEE80211_STYPE_DISASSOC >> 4) |
				     BIT(IEEE80211_STYPE_AUTH >> 4) |
				     BIT(IEEE80211_STYPE_DEAUTH >> 4) |
				     BIT(IEEE80211_STYPE_REASSOC_REQ >> 4)},
};

static inline bool
tgd_wiphy_pae_get_authorized(struct tgd_wiphy_priv *wiphy_priv)
{
	struct tgd_terra_dev_priv *priv;

	priv = netdev_priv(wiphy_priv->wdev->netdev);
	return !priv->pae_closed;
}

static inline void
tgd_wiphy_pae_set_authorized(struct tgd_wiphy_priv *wiphy_priv, bool authorized)
{
	struct tgd_terra_dev_priv *priv;

	priv = netdev_priv(wiphy_priv->wdev->netdev);
	if (!authorized == priv->pae_closed)
		return;

	/* better set in tgd dev since it is checked for every pkt */
	priv->pae_closed = !authorized;
	netdev_info(priv->dev, "PAE authorized change from %d to %d\n",
		    authorized ? 0 : 1, authorized ? 1 : 0);
}

static inline struct tgd_wiphy_priv *tgd_wiphy_priv(struct wiphy *wp)
{
	return wiphy_priv(wp);
}

static inline void tgd_cfg80211_disauth(struct tgd_wiphy_priv *wiphy_priv)
{
	TGD_CFG_FUNC_TRACE();
	tgd_wiphy_pae_set_authorized(wiphy_priv, false);
}

/* Send event to ask the upper controller to disassoc */
static inline void _tgd_cfg80211_disconnect(struct tgd_wiphy_priv *wiphy_priv)
{
	struct tgd_terra_dev_priv *priv;

	if (wiphy_priv->tg_connected) {
		TGD_CFG_FUNC_TRACE();
		priv = netdev_priv(wiphy_priv->wdev->netdev);

		tgd_send_disassoc_req(priv->fb_drv_data,
				      (tgEthAddr *)wiphy_priv->pmac);
	}
}

static int tgd_cfg80211_sendup_linkup_status(struct tgd_wiphy_priv *wiphy_priv)
{
	struct tgd_terra_dev_priv *priv;
	fb_tgd_link_wsec_link_status_t wsecLinkStatus;

	priv = netdev_priv(wiphy_priv->wdev->netdev);

	memset(&wsecLinkStatus, 0, sizeof(wsecLinkStatus));
	if (strscpy(wsecLinkStatus.ifname, netdev_name(priv->dev),
		    TGD_IFNAME_SZ) < 0) {
		netdev_err(priv->dev,
			   "WSEC_SEND_LINKUP: interface name error\n");
		return -1;
	}

	return tgd_nlsdn_send_wsec_linkup_status(
	    priv->fb_drv_data, (unsigned char *)&wsecLinkStatus,
	    sizeof(wsecLinkStatus));
}

static int tgd_cfg80211_build_ssid_ie(u8 *buf, size_t buf_len, const u8 *ssid,
				      u8 ssid_len)
{
	TGD_ASSERT(buf_len >= ssid_len + 2);
	buf[0] = WLAN_EID_SSID;
	buf[1] = ssid_len;
	memcpy(&buf[2], ssid, ssid_len);
	return ssid_len + 2;
}

static void tgd_cfg80211_scan_flush(struct tgd_wiphy_priv *wiphy_priv)
{
	struct wireless_dev *wdev = wiphy_priv->wdev;
	struct wiphy *wiphy = wdev->wiphy;
	struct ieee80211_channel *notify_channel = &tgd_60ghz_channels[0];
	struct cfg80211_bss *bss;

	TGD_CFG_FUNC_TRACE();
	TGD_ASSERT(!wiphy_priv->ap_started);

	bss = cfg80211_get_bss(wiphy, notify_channel,
			       (const u8 *)wiphy_priv->pmac, fb_terragraph_ssid,
			       fb_terragraph_ssid_len, IEEE80211_BSS_TYPE_ANY,
			       IEEE80211_PRIVACY_ON);
	if (bss != NULL) {
		cfg80211_unlink_bss(wiphy, bss);
		cfg80211_put_bss(wiphy, bss);
	}
}

static void tgd_cfg80211_notify_disconnect(struct tgd_wiphy_priv *wiphy_priv)
{
	struct wireless_dev *wdev = wiphy_priv->wdev;
	struct net_device *ndev = wdev->netdev;

	TGD_CFG_FUNC_TRACE();
	if (!wiphy_priv->ap_started) {
		/* Make sure no scans return peer BSS anymore */
		tgd_cfg80211_scan_flush(wiphy_priv);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
		cfg80211_disconnected(ndev, WLAN_REASON_UNSPECIFIED, NULL, 0,
				      false, GFP_KERNEL);
#else
		cfg80211_disconnected(ndev, WLAN_REASON_UNSPECIFIED, NULL, 0,
				      GFP_KERNEL);
#endif
	} else {
		cfg80211_del_sta(ndev, wiphy_priv->pmac, GFP_KERNEL);
	}
}

static void tgd_cfg80211_notify_connect(struct tgd_wiphy_priv *wiphy_priv)
{
	struct wireless_dev *wdev = wiphy_priv->wdev;
	// struct wiphy *wiphy = wdev->wiphy;
	struct net_device *ndev = wdev->netdev;
	struct station_info sinfo;
	u8 *bssid = (u8 *)wiphy_priv->pmac;

	TGD_CFG_FUNC_TRACE();

	/* already connected, skip */
	if (wdev->current_bss)
		return;

	if (!wiphy_priv->ap_started) {
		cfg80211_connect_result(ndev, bssid, wiphy_priv->supp_rsnie,
					wiphy_priv->supp_rsnie_len,
					wiphy_priv->auth_rsnie,
					wiphy_priv->auth_rsnie_len,
					WLAN_STATUS_SUCCESS, GFP_KERNEL);
	} else {
		memset(&sinfo, 0, sizeof(sinfo));
		sinfo.generation = wiphy_priv->sinfo_gen++;
		sinfo.assoc_req_ies = wiphy_priv->supp_rsnie;
		sinfo.assoc_req_ies_len = wiphy_priv->supp_rsnie_len;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
		sinfo.filled |= STATION_INFO_ASSOC_REQ_IES;
#endif
		cfg80211_new_sta(ndev, bssid, &sinfo, GFP_KERNEL);
	}
}

/*
 * find the first rsnie, return true means IE valid
 */
static bool tgd_cfg80211_find_rsnie(struct net_device *ndev, u8 *ies, u8 len,
				    u8 **rsnie)
{
	u8 *ies_end = ies + len;
	size_t ie_tot_len;

	*rsnie = NULL;
	while (ies < ies_end) {
		/* runt ie? */
		if (ies + 2 > ies_end) {
			netdev_err(ndev, "%s ies corrupted\n", __func__);
			break;
		}

		ie_tot_len = IE_TOT_LEN(ies);
		/* skip non-rsn ies */
		if (IE_ID(ies) != WLAN_EID_RSN) {
			ies += ie_tot_len;
			continue;
		}

		*rsnie = ies;

		/* verify rsnie is fully available */
		if (ie_tot_len + ies > ies_end) {
			netdev_err(ndev, "%s rsnie incomplete\n", __func__);
			return false;
		}

		/* verify size is in range */
		if ((ie_tot_len < TGD_MIN_RSN_IE_SIZE) ||
		    (ie_tot_len > TGD_MAX_RSN_IE_SIZE)) {
			netdev_err(ndev, "%s rsnie totlen %zu out of range\n",
				   __func__, ie_tot_len);
			return false;
		}

		/* seems valid */
		return true;
	}
	return false;
}

/*
 * Ideally not a job for this layer, FW should have done the screening
 */
static void tgd_cfg80211_parse_rsnies(struct tgd_wiphy_priv *wiphy_priv,
				      u8 assocReqIeLen, u8 assocRespIeLen,
				      u8 *ies, u8 **supp_rsniep,
				      u8 **auth_rsniep)
{
	struct net_device *ndev;
	bool auth_valid, supp_valid;

	/* Note: TG assoc is initiated from the DN side which acts as the
	 * authenticator instead of the supplicant. cfg80211 is expecting
	 * the other way, so switch the two
	 */
	ndev = wiphy_priv->wdev->netdev;
	auth_valid =
	    tgd_cfg80211_find_rsnie(ndev, ies, assocReqIeLen, auth_rsniep);
	supp_valid = tgd_cfg80211_find_rsnie(ndev, ies + assocReqIeLen,
					     assocRespIeLen, supp_rsniep);

	/* No point reporting malformed RSNIE up */
	if (!auth_valid)
		*auth_rsniep = NULL;
	if (!supp_valid)
		*supp_rsniep = NULL;
}

static void tgd_cfg80211_update_rsnie_info(struct tgd_wiphy_priv *wiphy_priv,
					   u8 *supp_rsnie, u8 *auth_rsnie,
					   tgWsecAuthType wsec_auth)
{
	if (supp_rsnie) {
		wiphy_priv->supp_rsnie_len = IE_TOT_LEN(supp_rsnie);
		memcpy(wiphy_priv->supp_rsnie, supp_rsnie,
		       wiphy_priv->supp_rsnie_len);
	} else
		wiphy_priv->supp_rsnie_len = 0;

	if (auth_rsnie) {
		wiphy_priv->auth_rsnie_len = IE_TOT_LEN(auth_rsnie);
		memcpy(wiphy_priv->auth_rsnie, auth_rsnie,
		       wiphy_priv->auth_rsnie_len);
	} else
		wiphy_priv->auth_rsnie_len = 0;

	wiphy_priv->wsec_auth = wsec_auth;
}

/*
 * Note: TG assoc request is initiated by the DN side which acts as
 * the authenticator.
 */
void tgd_cfg80211_evt_tg_connect(struct tgd_terra_dev_priv *dev_priv,
				 const u8 *pmac, u8 assocReqIeLen,
				 u8 assocRespIeLen, u8 *ies,
				 tgWsecAuthType wsec_auth)
{
	struct wireless_dev *wdev = dev_priv->wdev;
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wdev->wiphy);
	u8 *supp_rsnie = NULL, *auth_rsnie = NULL;

	TGD_CFG_FUNC_TRACE();

	tgd_cfg80211_parse_rsnies(wiphy_priv, assocReqIeLen, assocRespIeLen,
				  ies, &supp_rsnie, &auth_rsnie);

	if (wiphy_priv->tg_connected) {
		TGD_CFG_FUNC_TRACE();
		/* ignore it if the link state didn't change  */
		if ((wiphy_priv->wsec_auth == wsec_auth) &&
		    (!memcmp(wiphy_priv->pmac, pmac, ETH_ALEN)))
			return;

		/* something changed, send up a disconnect event before
		 * notifying this linkup event */
		TGD_CFG_FUNC_TRACE();
		netdev_err(dev_priv->dev, "rsn cfg changed while connected\n");
		wiphy_priv->tg_connected = false;

		tgd_cfg80211_notify_disconnect(wiphy_priv);
	}

	tgd_cfg80211_update_rsnie_info(wiphy_priv, supp_rsnie, auth_rsnie,
				       wsec_auth);

	memcpy(wiphy_priv->pmac, pmac, ETH_ALEN);
	wiphy_priv->tg_connected = true;

	/* need 4 way handshake before openning the port for other frames */
	if (wsec_auth != TGF_WSEC_DISABLE) {
		tgd_wiphy_pae_set_authorized(wiphy_priv, false);
		if (!wiphy_priv->ap_started)
			dev_priv->m4_pending = true;
	} else {
		tgd_wiphy_pae_set_authorized(wiphy_priv, true);
		netdev_info(dev_priv->dev, "dev=%pM: Connect with wsec OFF.\n",
			    wiphy_priv->pmac);
	}

	if (wiphy_priv->ap_started)
		tgd_cfg80211_notify_connect(wiphy_priv);
}

void tgd_cfg80211_get_info(struct tgd_terra_dev_priv *dev_priv,
			   struct tgd_cfg80211_info *info)
{
	struct wireless_dev *wdev = dev_priv->wdev;
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wdev->wiphy);

	info->ap_started = wiphy_priv->ap_started;
	info->tg_connected = wiphy_priv->tg_connected;
	info->wsec_auth = wiphy_priv->wsec_auth;
	info->m4_sent = wiphy_priv->m4_sent;
}

void tgd_cfg80211_evt_tg_disconnect(struct tgd_terra_dev_priv *dev_priv,
				    const u8 *pmac)
{
	struct wireless_dev *wdev = dev_priv->wdev;
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wdev->wiphy);

	TGD_CFG_FUNC_TRACE();
	if (!wiphy_priv->tg_connected) {
		return;
	}

	wiphy_priv->tg_connected = false;
	tgd_cfg80211_notify_disconnect(wiphy_priv);

	if (wiphy_priv->wsec_auth != TGF_WSEC_DISABLE) {
		// Secure link is being disconnected
		tgd_wiphy_pae_set_authorized(wiphy_priv, false);
		wiphy_priv->m4_sent = false;
		wiphy_priv->wsec_auth = TGF_WSEC_DISABLE;
	}
}

static inline int tgd_cfg80211_set_key(struct tgd_terra_dev_priv *dev_priv,
				       const unsigned char *key, int key_len)
{
	struct wireless_dev *wdev = dev_priv->wdev;
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wdev->wiphy);

	TGD_CFG_DBG("%s: dev %pM key_len %u\n", __func__, wiphy_priv->pmac,
		    key_len);
	TGD_CFG_HEX_DUMP("Pairwise key: ", key, key_len);
	return fb_tgd_bh_set_key(dev_priv, wiphy_priv->pmac, key, key_len);
}

static void tgd_set_key_worker(struct work_struct *work)
{
	struct tgd_terra_dev_priv *priv;
	struct tgd_wiphy_priv *wiphy_priv =
	    container_of(work, struct tgd_wiphy_priv, set_key_worker.work);
	struct key_params *params = &wiphy_priv->params;
	int err;

	TGD_CFG_FUNC_TRACE();
	priv = netdev_priv(wiphy_priv->wdev->netdev);

	wiphy_priv->m4_delay_cnt++;
	if (tgd_link_pkts_pending(priv) &&
	    (wiphy_priv->m4_delay_cnt < TGD_M4_MAX_DELAY_CNT)) {
		/* not done yet, delay for a few ms, wait for it to finish */
		queue_delayed_work(wiphy_priv->wq_service,
				   &wiphy_priv->set_key_worker, TGD_M4_DELAY);
		return;
	}

	TGD_CFG_HEX_DUMP("params: ", params, sizeof(*params));
	priv->m4_pending = false;
	err = tgd_cfg80211_set_key(priv, params->key, params->key_len);
	if (err)
		netdev_err(priv->dev, "%s set key err %d\n", __func__, err);
}

void tgd_cfg80211_evt_m4_sent(struct tgd_terra_dev_priv *dev_priv)
{
	struct wireless_dev *wdev = dev_priv->wdev;
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wdev->wiphy);

	TGD_CFG_FUNC_TRACE();
	if (!wiphy_priv->tg_connected)
		return;

	if (wiphy_priv->ap_started)
		return;

	TGD_CFG_DBG("m4_sent true for %pM\n", wiphy_priv->pmac);
	dev_priv->m4_pending = false;
	wiphy_priv->m4_sent = true;
	if (wiphy_priv->params.key_len) {
		queue_delayed_work(wiphy_priv->wq_service,
				   &wiphy_priv->set_key_worker, TGD_M4_DELAY);
		return;
	}
}

struct tgd_eapol_hdr {
	u8 version;
	u8 type;
	u16 length;
} __packed;
#define EAPOL_HDR_LEN (sizeof(struct tgd_eapol_hdr))

struct tgd_eapol_wpa_key_hdr {
	u8 type;
	u16 key_info;
	u16 key_len;
	u8 ext[0]; /* rest not interesting to m4 identification */
} __packed;
#define EAPOL_WPA_KEY_HDR_LEN (sizeof(struct tgd_eapol_wpa_key_hdr))

#define EAPOL_KEY_WPA 254
#define EAPOL_KEY_WPA2 2

#define WPA_KEY_PAIR 0x08
#define WPA_KEY_INSTALL 0x40
#define WPA_KEY_ACK 0x80
#define WPA_KEY_MIC 0x100
#define WPA_KEY_SECURE 0x200
#define WPA_KEY_ERR 0x400
#define WPA_KEY_REQ 0x800

#define EAPOL_KEY_INFO_M4_MASK                                                 \
	(WPA_KEY_PAIR | WPA_KEY_REQ | WPA_KEY_MIC | WPA_KEY_ERR |              \
	 WPA_KEY_ACK | WPA_KEY_SECURE)
#define EAPOL_KEY_INFO_M4 (WPA_KEY_PAIR | WPA_KEY_MIC | WPA_KEY_SECURE)

#define EAPOL_KEY 3

bool tgd_cfg80211_is_4way_m4(struct tgd_terra_dev_priv *dev_priv,
			     struct sk_buff *skb)
{
	struct ethhdr *eh;
	struct tgd_eapol_hdr *tehdr;
	struct tgd_eapol_wpa_key_hdr *tewkh;
	int len = skb->len;
	u16 key_info;

	if (len <
	    sizeof(struct ethhdr *) + EAPOL_HDR_LEN + EAPOL_WPA_KEY_HDR_LEN)
		return false;

	eh = (struct ethhdr *)skb->data;
	tehdr = (struct tgd_eapol_hdr *)(eh + 1);

	/* skip EAPOL key version, type has to be EAPOL_KEY */
	if (tehdr->type != EAPOL_KEY)
		return false;

	/* now key type */
	tewkh = (struct tgd_eapol_wpa_key_hdr *)(tehdr + 1);
	if ((tewkh->type != EAPOL_KEY_WPA) && (tewkh->type != EAPOL_KEY_WPA2))
		return false;

	key_info = ntohs(tewkh->key_info);
	if ((key_info & EAPOL_KEY_INFO_M4_MASK) != EAPOL_KEY_INFO_M4)
		return false;

	TGD_CFG_DBG("%s: TRUE\n", __func__);
	return true;
}

static int tgd_cfg80211_add_key(struct wiphy *wiphy, struct net_device *ndev,
				u8 key_index, bool pairwise, const u8 *mac_addr,
				struct key_params *params)
{
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wiphy);
	struct tgd_terra_dev_priv *priv;
	int err;

	TGD_CFG_FUNC_TRACE();

	TGD_ASSERT(wiphy_priv && wiphy_priv->wdev && wiphy_priv->wdev->netdev);
	priv = netdev_priv(wiphy_priv->wdev->netdev);

	TGD_CFG_DBG("wp=%p, ndev=%p, kix=%u, pw=%u, mac=%pM, klen=%u\n", wiphy,
		    ndev, key_index, pairwise, mac_addr, params->key_len);
	/* TG BH does not support group key, but has to say OK to move on */
	if (!pairwise)
		return 0;

	if (key_index > TGD_MAX_KEY_IX)
		return -EINVAL;

	if ((params->cipher != WLAN_CIPHER_SUITE_GCMP) &&
	    (params->cipher != WLAN_CIPHER_SUITE_GCMP_256))
		return -EINVAL;

	if (!wiphy_priv->tg_connected ||
	    memcmp(wiphy_priv->pmac, mac_addr, ETH_ALEN))
		return -ENODEV;

	if (params->key_len > TGD_MAX_KEY_LEN) {
		netdev_err(priv->dev, "%s: pairwise key_len %u > %u\n",
			   __func__, params->key_len, TGD_MAX_KEY_LEN);
		return -EINVAL;
	}

	/* for supplicant, must wait till m4 being sent out before setting the
	 * key */
	if (!wiphy_priv->ap_started) {
		/* do a deep copy of key params */
		wiphy_priv->params = *params;
		memcpy(wiphy_priv->params_key, params->key, params->key_len);
		wiphy_priv->params.key = wiphy_priv->params_key;
		wiphy_priv->m4_delay_cnt = 0;
		TGD_CFG_HEX_DUMP("params: ", params, sizeof(*params));
		TGD_CFG_HEX_DUMP("params key: ", params->key, params->key_len);

		TGD_CFG_FUNC_TRACE();
		if ((!wiphy_priv->m4_sent) || (tgd_link_pkts_pending(priv))) {
			TGD_CFG_FUNC_TRACE();
			TGD_CFG_DBG("m4_sent=%u pkt_pending=%u\n",
				    wiphy_priv->m4_sent ? 1 : 0,
				    tgd_link_pkts_pending(priv));
			queue_delayed_work(wiphy_priv->wq_service,
					   &wiphy_priv->set_key_worker,
					   TGD_M4_DELAY);
			return 0;
		}
	}

	err = tgd_cfg80211_set_key(priv, params->key, params->key_len);
	if (err)
		netdev_err(priv->dev, "bh_set_key err %d\n", err);
	return err ? -EIO : 0;
}

static int tgd_cfg80211_del_key(struct wiphy *wiphy, struct net_device *ndev,
				u8 key_index, bool pairwise, const u8 *mac_addr)
{
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wiphy);
	struct tgd_terra_dev_priv *dev_priv;

	TGD_CFG_FUNC_TRACE();

	netdev_info(ndev, "%s: ki %u pw %u mac %pM\n", __func__, key_index,
		    pairwise ? 1 : 0, mac_addr);
	if (!pairwise)
		return 0;

	dev_priv = netdev_priv(wiphy_priv->wdev->netdev);
	wiphy_priv->m4_sent = false;
	dev_priv->m4_pending = false;
	if (!wiphy_priv->ap_started) {

		TGD_CFG_FUNC_TRACE();
		memset(&wiphy_priv->params, 0, sizeof(wiphy_priv->params));
		cancel_delayed_work_sync(&wiphy_priv->set_key_worker);

		memset(wiphy_priv->params_key, 0,
		       sizeof(wiphy_priv->params_key));

		TGD_CFG_FUNC_TRACE();
		tgd_cfg80211_set_key(netdev_priv(wiphy_priv->wdev->netdev),
				     wiphy_priv->params_key, 0);
	}

	tgd_wiphy_pae_set_authorized(wiphy_priv, false);
	return 0;
}

/* Need to be present or wiphy_new() will WARN */
static int tgd_cfg80211_set_default_key(struct wiphy *wiphy,
					struct net_device *ndev, u8 key_index,
					bool unicast, bool multicast)
{
	return 0;
}

static void tgd_cfg80211_scan_result(struct tgd_wiphy_priv *wiphy_priv,
				     struct cfg80211_scan_request *request)
{
	struct wireless_dev *wdev = wiphy_priv->wdev;
	struct wiphy *wiphy = wdev->wiphy;
	struct ieee80211_channel *notify_channel = &tgd_60ghz_channels[0];
	struct cfg80211_bss *bss;

	u16 notify_capability = 3 | WLAN_CAPABILITY_PRIVACY;
	u16 notify_interval = 100;
	u8 notify_ie[TGD_MAX_RSN_IE_SIZE + TGD_SSID_IE_LEN];
	size_t notify_ielen = 0;
	s32 notify_signal = 55;

	TGD_CFG_FUNC_TRACE();
	TGD_ASSERT(!wiphy_priv->ap_started);

	notify_ielen += tgd_cfg80211_build_ssid_ie(notify_ie, sizeof(notify_ie),
						   fb_terragraph_ssid,
						   fb_terragraph_ssid_len);

	if (wiphy_priv->auth_rsnie_len) {
		memcpy(notify_ie + notify_ielen, wiphy_priv->auth_rsnie,
		       wiphy_priv->auth_rsnie_len);
		notify_ielen += wiphy_priv->auth_rsnie_len;
		TGD_ASSERT(notify_ielen <= sizeof(notify_ie));
	}

	bss = cfg80211_inform_bss(
	    wiphy, notify_channel, CFG80211_BSS_FTYPE_UNKNOWN,
	    (const u8 *)wiphy_priv->pmac, 0, notify_capability, notify_interval,
	    notify_ie, notify_ielen, notify_signal, GFP_KERNEL);
	if (bss != NULL)
		cfg80211_put_bss(wiphy, bss);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	{
		struct cfg80211_scan_info info;

		memset(&info, 0, sizeof(info));
		cfg80211_scan_done(request, &info);
	}
#else
	cfg80211_scan_done(request, false);
#endif
}

static int tgd_cfg80211_scan(struct wiphy *wiphy,
			     struct cfg80211_scan_request *request)
{
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wiphy);

	/* check we are client side */
	if (wiphy_priv->wdev->iftype != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;
	/*
	 * TODO: remove again when failures to associate from time
	 * to time are understood.
	 */
	if (!wiphy_priv->tg_connected)
		return -ENOLINK;

	if (wiphy_priv->tg_connected)
		tgd_cfg80211_scan_result(wiphy_priv, request);
	else {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
		{
			struct cfg80211_scan_info info;

			memset(&info, 0, sizeof(info));
			cfg80211_scan_done(request, &info);
		}
#else
		cfg80211_scan_done(request, false);
#endif
	}
	return 0;
}

static int tgd_cfg80211_connect(struct wiphy *wiphy, struct net_device *ndev,
				struct cfg80211_connect_params *sme)
{
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wiphy);

	TGD_CFG_FUNC_TRACE();
	if (!wiphy_priv->tg_connected)
		return -ENOLINK;

	/* TBD: check params to see if it matches */

	tgd_cfg80211_notify_connect(wiphy_priv);
	return 0;
}

static int tgd_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *ndev,
				   u16 reason_code)
{
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wiphy);

	TGD_CFG_FUNC_TRACE();

	if (!wiphy_priv->tg_connected)
		return 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
	cfg80211_disconnected(ndev, reason_code, NULL, 0, true, GFP_KERNEL);
#else
	cfg80211_disconnected(ndev, reason_code, NULL, 0, GFP_KERNEL);
#endif

	tgd_cfg80211_disauth(wiphy_priv);
	_tgd_cfg80211_disconnect(wiphy_priv);

	return 0;
}

static int tgd_cfg80211_get_station(struct wiphy *wiphy,
				    struct net_device *ndev, const u8 *mac,
				    struct station_info *sinfo)
{
	TGD_CFG_FUNC_TRACE();
	return 0;
}

static int tgd_cfg80211_dump_station(struct wiphy *wiphy,
				     struct net_device *ndev, int idx, u8 *mac,
				     struct station_info *sinfo)
{
	TGD_CFG_FUNC_TRACE();
	return 0;
}

static int tgd_cfg80211_change_iface(struct wiphy *wiphy,
				     struct net_device *ndev,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0) /* flags in vif_params */
				     enum nl80211_iftype type,
#else
				     enum nl80211_iftype type, u32 *flags,
#endif
				     struct vif_params *params)
{
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wiphy);
	struct wireless_dev *wdev = wiphy_priv->wdev;

	TGD_CFG_FUNC_TRACE();

	if (NL80211_IFTYPE_MONITOR == type) {
		// add monitor flags
	}
	wdev->iftype = type;
	return 0;
}

static int tgd_cfg80211_start_ap(struct wiphy *wiphy, struct net_device *ndev,
				 struct cfg80211_ap_settings *info)
{
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wiphy);

	TGD_CFG_FUNC_TRACE();
	wiphy_priv->ap_started = true;

	/* make sure ap_started is updated before tg_connected is checked */
	if (wiphy_priv->tg_connected) {
		netdev_err(ndev, "%s WARN already tg_connected\n", __func__);
		tgd_cfg80211_notify_connect(wiphy_priv);
	}

	return 0;
}

static int tgd_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *ndev)
{
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wiphy);

	TGD_CFG_FUNC_TRACE();
	wiphy_priv->ap_started = false;
	return 0;
}

static int tgd_cfg80211_del_station(struct wiphy *wiphy,
				    struct net_device *ndev,
				    struct station_del_parameters *params)
{
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wiphy);

	TGD_CFG_FUNC_TRACE();
	tgd_cfg80211_disauth(wiphy_priv);
	_tgd_cfg80211_disconnect(wiphy_priv);

	return 0;
}

static int tgd_cfg80211_change_bss(struct wiphy *wiphy, struct net_device *ndev,
				   struct bss_parameters *params)
{
	TGD_CFG_FUNC_TRACE();
	return 0;
}

static int tgd_cfg80211_change_station(struct wiphy *wiphy,
				       struct net_device *ndev, const u8 *mac,
				       struct station_parameters *params)
{
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wiphy);
	s32 err = 0;
	TGD_CFG_FUNC_TRACE();

	netdev_info(ndev, "Enter, MAC %pM, mask 0x%04x set 0x%04x\n", mac,
		    params->sta_flags_mask, params->sta_flags_set);

	/* Ignore all 00 MAC */
	if (is_zero_ether_addr(mac))
		return 0;

	if (!(params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED)))
		return 0;

	if (params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED)) {
		netdev_info(ndev, "set port authorized\n");
		tgd_wiphy_pae_set_authorized(wiphy_priv, true);
		tgd_cfg80211_sendup_linkup_status(wiphy_priv);
	} else {
		netdev_info(ndev, "clr port authorized\n");
		tgd_wiphy_pae_set_authorized(wiphy_priv, false);
	}

	if (err < 0)
		netdev_err(ndev, "Setting SCB (de-)authorize failed, %d\n",
			   err);

	return err;
}

/* keep the stabs here for those not appliable to TG yet */
static int tgd_cfg80211_set_channel(struct wiphy *wiphy,
				    struct cfg80211_chan_def *chandef)
{
	TGD_CFG_FUNC_TRACE();
	return 0;
}

#define TGD_CFG80211_NA 1
#ifdef TGD_CFG80211_NA
static int tgd_remain_on_channel(struct wiphy *wiphy, struct wireless_dev *wdev,
				 struct ieee80211_channel *chan,
				 unsigned int duration, u64 *cookie)
{
	return 0;
}

static int tgd_cancel_remain_on_channel(struct wiphy *wiphy,
					struct wireless_dev *wdev, u64 cookie)
{
	return 0;
}

static int tgd_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
				struct cfg80211_mgmt_tx_params *params,
				u64 *cookie)
{
	return 0;
}

static int tgd_cfg80211_change_beacon(struct wiphy *wiphy,
				      struct net_device *ndev,
				      struct cfg80211_beacon_data *bcon)
{
	return 0;
}

static int tgd_cfg80211_probe_client(struct wiphy *wiphy,
				     struct net_device *ndev, const u8 *peer,
				     u64 *cookie)
{
	return 0;
}
#endif /* TGD_CFG80211_NA */

static struct cfg80211_ops tgd_cfg80211_ops = {
    .add_key = tgd_cfg80211_add_key,
    .del_key = tgd_cfg80211_del_key,
    .set_default_key = tgd_cfg80211_set_default_key,

    .scan = tgd_cfg80211_scan,
    .connect = tgd_cfg80211_connect,
    .disconnect = tgd_cfg80211_disconnect,
    .get_station = tgd_cfg80211_get_station,
    .dump_station = tgd_cfg80211_dump_station,

    .change_virtual_intf = tgd_cfg80211_change_iface,
    /* AP mode */
    .start_ap = tgd_cfg80211_start_ap,
    .stop_ap = tgd_cfg80211_stop_ap,
    .del_station = tgd_cfg80211_del_station,
    .change_bss = tgd_cfg80211_change_bss,
    .change_station = tgd_cfg80211_change_station,

    .set_monitor_channel = tgd_cfg80211_set_channel,
#ifdef TGD_CFG80211_NA
    .change_beacon = tgd_cfg80211_change_beacon,
    .probe_client = tgd_cfg80211_probe_client,
    .remain_on_channel = tgd_remain_on_channel,
    .cancel_remain_on_channel = tgd_cancel_remain_on_channel,
    .mgmt_tx = tgd_cfg80211_mgmt_tx,
#endif
};

static int tgd_wiphy_priv_init(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct tgd_wiphy_priv *wiphy_priv = tgd_wiphy_priv(wiphy);

	wiphy_priv->wdev = wdev;
	wiphy_priv->wiphy = wiphy;

	INIT_DELAYED_WORK(&wiphy_priv->set_key_worker, tgd_set_key_worker);
	wiphy_priv->wq_service = create_singlethread_workqueue("tg_wq_service");
	if (!wiphy_priv->wq_service)
		return -1;

	return 0;
}

static void tgd_wiphy_init(struct wiphy *wiphy)
{
	wiphy->max_scan_ssids = 1;
	wiphy->max_scan_ie_len = TGD_MAX_IE_LEN;
	wiphy->max_num_pmkids = 0 /* TODO: */;

	wiphy->interface_modes =
	    BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP);
	wiphy->flags |= WIPHY_FLAG_HAVE_AP_SME;

#ifdef FB_TGD_MONITOR
	wiphy->interface_modes |= BIT(NL80211_IFTYPE_MONITOR);
#endif

	wiphy->bands[NL80211_BAND_60GHZ] = &tgd_band_60ghz;

	/* TODO: figure this out */
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_UNSPEC;

	wiphy->cipher_suites = tgd_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(tgd_cipher_suites);
	wiphy->mgmt_stypes = tgd_mgmt_stypes;
	//	wiphy->features |= NL80211_FEATURE_SK_TX_STATUS;
}

struct wireless_dev *tgd_cfg80211_init(struct net_device *ndev)
{
	int rc = 0;
	struct wireless_dev *wdev;

	TGD_CFG_FUNC_TRACE();
	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (!wdev) {
		return ERR_PTR(-ENOMEM);
	}

	wdev->wiphy =
	    wiphy_new(&tgd_cfg80211_ops, sizeof(struct tgd_wiphy_priv));
	if (!wdev->wiphy) {
		rc = -ENOMEM;
		goto out;
	}

	tgd_wiphy_init(wdev->wiphy);
	tgd_wiphy_priv_init(wdev->wiphy, wdev);

	rc = wiphy_register(wdev->wiphy);
	if (rc < 0) {
		goto out_failed_reg;
	}

	wdev->iftype = NL80211_IFTYPE_STATION; /* TODO */
	wdev->netdev = ndev;
	ndev->ieee80211_ptr = wdev;
	return wdev;

out_failed_reg:
	wiphy_free(wdev->wiphy);
out:
	kfree(wdev);

	return ERR_PTR(rc);
}

void tgd_wdev_free(struct wireless_dev *wdev)
{
	struct tgd_wiphy_priv *wiphy_priv;

	TGD_CFG_FUNC_TRACE();
	if (!wdev)
		return;

	wiphy_priv = tgd_wiphy_priv(wdev->wiphy);
	if (!wiphy_priv)
		return;

	cancel_delayed_work_sync(&wiphy_priv->set_key_worker);
	if (wiphy_priv->wq_service)
		destroy_workqueue(wiphy_priv->wq_service);

	wiphy_unregister(wdev->wiphy);
	wiphy_free(wdev->wiphy);
	kfree(wdev);
}
