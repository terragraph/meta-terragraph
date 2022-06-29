/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * Macros to help debugging
 */
#ifndef FB_TGD_TERRAGRAPH_H
#define FB_TGD_TERRAGRAPH_H

#include <linux/interrupt.h>
#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h>	  /* struct iphdr */
#include <linux/skbuff.h>
#include <linux/version.h>
#include <asm/checksum.h>
#include <fb_tg_qdisc_pfifofc_if.h>

#define ETH_ALEN 6

// FB_TBD(Roy): We can increase the number of
// Virtual links here
#define TERRAGPH_NUM_OF_VIRTUAL_LINKS 2

/* Default timeout period */
#define TGD_TERRAGRAPH_TIMEOUT 5 /* In jiffies */

// FB_TBD(Roy) : Confirm the lifetime unit with vendor
#define TGD_TX_DATA_LIFETIME 20000 // usec

#define TGD_PIPE_INVALID -1
#define TGD_LINK_INVALID -1

/*
 * 7995 (max MPDU per 11ad) - 32 (MAC header, no HT field) -
 * 24 (GCMP header/MIC) - 4 (FCS) = 7935 (the maximum A-MSDU size).
 * Every MSDU is wrapped with NSS header.
 * So, maximum MTU size is 7935 - 22 (NSS header) - 2 (Length field for 802.3
 * or Type field for Ethernet II) = 7911.
 * Again, Marvell can support upto 7904 MTU size, and they suggest 7900 as
 * MTU for better efficiency.
 */
#define TGD_WLAN_MTU_SIZE 7900

/* Convenience macros to generate enums/strings for nlsdn stats */
#define enumx(x) x

#define NLSDN_STATS_OP(OP)                                                     \
	OP(NL_MSG_SEND)                                                        \
	, OP(NL_MSG_SEND_ERR), OP(NL_NOTIF), OP(NL_MSG_RCVD),                  \
	    OP(NL_CMD_TGINIT), OP(NL_CMD_SET_NODECONFIG),                      \
	    OP(NL_CMD_SET_BMFMCONFIG), OP(NL_CMD_SET_DBGMASK),                 \
	    OP(NL_CMD_GRANTALLOC), OP(NL_CMD_GET_STATS),                       \
	    OP(NL_CMD_PASSTHRU_SB), OP(NL_CMD_SET_DRVR_CONFIG), OP(NL_EVENTS), \
	    OP(NL_NB_INIT_RESP), OP(NL_NB_NODE_CFG_RESP),                      \
	    OP(NL_NB_START_BF_SCAN_RESP), OP(NL_NB_UPDATE_LINK_REQ),           \
	    OP(NL_NB_ADD_LINK_REQ), OP(NL_NB_DEL_LINK_REQ),                    \
	    OP(NL_NB_PASSTHRU), OP(NL_NB_GPS_START_TIME_ACQUISITION),          \
	    OP(NL_NB_GPS_STOP_TIME_ACQUISITION), OP(NL_CMD_DEV_ALLOC),         \
	    OP(NL_NB_LINK_INFO), OP(NL_NB_GPS_GET_SELF_POS), OP(NL_STATS_MAX),

enum nl_sdn_stats { NLSDN_STATS_OP(enumx) };

struct nlsdn_stats {
	atomic_t stats[NL_STATS_MAX];
};

#define NL_STATS_INC(tgd_drv_data, type)                                       \
	atomic_inc(&((tgd_drv_data)->nl_stats.stats[(type)]))
/* increment both msg rvcd and the cmd type */
#define NL_CMD_STATS_INC(tgd_drv_data, type)                                   \
	do {                                                                   \
		NL_STATS_INC(tgd_drv_data, NL_MSG_RCVD);                       \
		NL_STATS_INC(tgd_drv_data, (type));                            \
	} while (0)

struct tgd_terra_gps_state;
struct platform_device;

/**
 * Default A-MSDU frame format to use
 */
typedef enum { TG_SHORT, STD_SHORT } tgd_amsdu_frame_format_t;

#ifdef TG_ENABLE_PFIFOFC
extern int tgd_enable_pfifofc;
#endif

struct tgd_terra_driver {
	struct mv_nss_ops *nss_ops;
	struct list_head dev_q_head;
	struct work_struct rx_event_work;
	struct workqueue_struct *rx_event_wq;
	struct list_head rx_event_q_head;
	struct dentry *debugfs_root_dir;
	struct dentry *debugfs_symlink;
	struct fb_tgd_routing_backend *rt_backend;
	spinlock_t rx_event_q_lock;
	const void *drv_bh_ops;
	void *drv_bh_ctx;
	void *bh_ctx;
	int link_count;
	int max_link_count;
	bool fc_enable;
	bool rx_event_enable;
	struct tgd_terra_gps_state *gps_state;
	void *stats_ctx;
	struct nlsdn_stats nl_stats;
	int idx;
	tgEthAddr mac_addr; // macaddr for the device owned by this driver
	u64 macaddr;	// Above maccaddr stored as a u64
	struct klist_node driver_list_node;
	tgd_amsdu_frame_format_t frame_format;
};

/* There is one driver per device.  They are all chained up here */
extern struct klist tgd_drivers_list;

/* Convenience macros to generate enums/strings for terra dev stats */
#define TERRA_STATS_OP(OP)                                                     \
	OP(RX_PACKETS), OP(RX_DROP_PACKETS), OP(RX_BYTES), OP(TX_PACKETS),     \
	    OP(TX_BYTES), OP(RX_ERR_NO_MDATA), OP(TX_FROM_LINUX),              \
	    OP(TX_FROM_NSS), OP(TX_ERR), OP(TX_TGD_ERR), OP(LINK_SUSPEND),     \
	    OP(LINK_RESUME), OP(TX_TGD_TX_STOPPED), OP(TX_FROM_LNX_DATA_COS),  \
	    OP(TX_FROM_LNX_CTRL_COS), OP(TX_FROM_NSS_DATA_COS),                \
	    OP(TX_FROM_NSS_CTRL_COS), OP(TX_ERR_WLAN_BUSY),                    \
	    OP(TX_PACKETS_COS0), OP(TX_PACKETS_COS1), OP(TX_PACKETS_COS2),     \
	    OP(TX_PACKETS_COS3), OP(TX_TGD_FLOW_ON), OP(TX_TGD_FLOW_OFF),      \
	    OP(RX_TGD_RX_STOPPED), OP(TERRA_DEV_STATS_MAX)

enum terra_stats { TERRA_STATS_OP(enumx) };

struct terra_dev_pcpu_stats {
	u64 stats[TERRA_DEV_STATS_MAX];
	struct u64_stats_sync syncp;
};

/**
 * Structure for providing TX/RX statistics. Same as corresponding
 * backhaul structure, only using 64bit fields. Should be typedef-ed
 * to fb_dhd_bh_link_stats when it becomes 64bit again.
 */
struct fb_tgd_bh_link_stats {
	uint64_t bytes_sent;    /**< Number of bytes transmitted */
	uint64_t bytes_pending; /**< Number of bytes pending transmission */
	uint64_t pkts_sent;     /**< Number of packets transmitted */
	uint64_t pkts_pending;  /**< Number of packets pending transmission */
	uint64_t pkts_recved;   /**< Number of packets received */
	uint64_t bytes_recved;  /**< Number of bytes received */
	uint64_t tx_err;	/**< Number of transmit packet errors */
	uint64_t rx_err;	/**< Number of receive packet errors */
	uint64_t pkts_enqueued;
	uint64_t bytes_enqueued;
	uint64_t bytes_sent_failed;
	uint64_t bytes_enqueue_failed;
	uint64_t bytes_sent_pad;
	uint64_t bytes_sent_failed_pad;
	uint64_t bytes_enqueued_pad;
	uint64_t bytes_enqueue_fail_pad;
	uint64_t
	    qdisc_total_pkts_enqd[PFIFOFC_BANDS]; /**< Total packets enqueued in
						     qdisc band. */
	uint64_t
	    qdisc_total_pkts_dropped[PFIFOFC_BANDS]; /**< Total packets dropped
							in qdisc band. */
	uint32_t
	    qdisc_cur_pkts_backlog[PFIFOFC_BANDS]; /**< Packets currently
						      enqueued in qdisc band. */
	uint32_t
	    qdisc_cur_bytes; /**< Current total bytes in all bands of qdisc. */
	uint32_t
	    qdisc_cur_pkts; /**< Current total packets in all bands of qdisc. */
	uint32_t pipe;
	uint32_t link;
	uint32_t link_state;
	uint8_t src_mac_addr[6];
	uint8_t dst_mac_addr[6];
	uint8_t dev_index;
};

/*
 * This structure is private to each network device
 * Each device will have an associated pipe/link
 */
struct tgd_terra_dev_priv {
	struct tgd_terra_driver *fb_drv_data;
	struct net_device *dev;
	struct wireless_dev *wdev;
	struct terra_dev_pcpu_stats __percpu *pcpu_stats;
	int status;
	int tx_link;
	int rx_link;
	int peer_index;
	int dev_index;
	bool pae_closed;
	bool m4_pending;
	tgEthAddr link_sta_addr;
	tgLinkStatus link_state;
	struct list_head list_entry;
	struct mutex link_lock;
	spinlock_t stats_lock;

	// Route backend data
	struct fb_tgd_routing_backend *rt_backend;
	void *rt_data;
	struct dentry *debugfs_stats_dir;
	struct fb_tgd_bh_link_stats link_stats;
};

static inline void terra_dev_stats_inc(struct tgd_terra_dev_priv *priv,
				       enum terra_stats idx, int len)
{
	struct terra_dev_pcpu_stats *pcpu_stats;

	pcpu_stats = this_cpu_ptr(priv->pcpu_stats);
	u64_stats_update_begin(&pcpu_stats->syncp);
	pcpu_stats->stats[idx] += len;
	u64_stats_update_end(&pcpu_stats->syncp);
}

#define TGD_CONVERT_MACADDR_TO_LONG(ethaddr)                                   \
	(((uint64_t)ethaddr.addr[0] << 5 * 8) |                                \
	 ((uint64_t)ethaddr.addr[1] << 4 * 8) |                                \
	 ((uint64_t)ethaddr.addr[2] << 3 * 8) |                                \
	 ((uint64_t)ethaddr.addr[3] << 2 * 8) |                                \
	 ((uint64_t)ethaddr.addr[4] << 1 * 8) | ((uint64_t)ethaddr.addr[5]))

#define TGD_CONVERT_LONG_TO_MACADDR(val, eth_addr)                             \
	eth_addr.addr[0] = (val >> 5 * 8) & 0xFF;                              \
	eth_addr.addr[1] = (val >> 4 * 8) & 0xFF;                              \
	eth_addr.addr[2] = (val >> 3 * 8) & 0xFF;                              \
	eth_addr.addr[3] = (val >> 2 * 8) & 0xFF;                              \
	eth_addr.addr[4] = (val >> 1 * 8) & 0xFF;                              \
	eth_addr.addr[5] = (val)&0xFF;

#define TGD_MAX_EVENT_SIZE 1024

typedef struct tgd_terra_rx_event {
	struct list_head entry;
	unsigned long stamp;
	uint16_t size;
	uint8_t data[0]; // Should be the last element
} tgd_terra_rx_event_t;

// Set the interface mac addresses
void tgd_set_if_mac_addr(struct tgd_terra_driver *fb_drv_data, u8 *mac_addr);
void tgd_flow_control_common(struct tgd_terra_driver *fb_dvr_data,
			     struct tgd_terra_dev_priv *priv, int link,
			     unsigned char qid, bool stop_tx);

int tgd_terra_set_link_status(struct tgd_terra_driver *fb_drv_data,
			      tgEthAddr *mac_addr, tgLinkStatus link_state);
void tgd_terra_set_link_mac_addr(struct tgd_terra_driver *fb_drv_data,
				 tgEthAddr *link_mac_addr, uint8_t rxLinkId,
				 uint8_t txLinkId);
struct net_device *
tgd_terra_find_net_device_by_mac(struct tgd_terra_driver *fb_drv_data,
				 tgEthAddr *link_mac_addr);
struct net_device *
tgd_terra_find_net_device_by_link(struct tgd_terra_driver *fb_drv_data,
				  int pipe);
int tgd_terra_del_link_info(struct tgd_terra_driver *fb_drv_data,
			    tgEthAddr *link_mac_addr);
void tgd_terra_rx_data_handler(struct tgd_terra_driver *fb_drv_data,
			       struct tgd_terra_dev_priv *priv,
			       struct sk_buff *skb, int link);
void tgd_terra_rx_event_handler(struct tgd_terra_driver *fb_drv_data,
				const uint8_t *event, unsigned long size);
int tgd_terra_bh_tx_pre(struct tgd_terra_dev_priv *priv, struct sk_buff *skb);
int tgd_terra_bh_tx_post(struct tgd_terra_dev_priv *priv, struct sk_buff *skb);
void tgd_terra_bh_tx_common(struct tgd_terra_dev_priv *priv,
			    struct sk_buff *skb);

struct tgd_terra_driver *tgd_find_fb_drv(u64 key);

unsigned int set_debug_mask(unsigned int dbg_mask);

void tgd_terra_get_net_link_stat(struct net_device *dev,
				 struct fb_tgd_bh_link_stats *link_stat_ptr);
void tgd_terra_get_net_if_stat(struct net_device *dev,
			       struct fb_tgd_bh_link_stats *if_stat_ptr);
int get_gps_nl_rsp(unsigned char *cmd_ptr, int cmd_len, unsigned char *rsp_buf,
		   int rsp_buf_len, int reload_stat);
int tgd_nlsdn_push_gps_stat_nb(struct tgd_terra_driver *fb_drv,
			       unsigned char *gps_rsp_buf, int len);

struct tgd_terra_dev_priv *
tgd_terra_lookup_link_by_mac_addr(struct tgd_terra_driver *fb_drv_data,
				  tgEthAddr *mac_addr);

struct tgd_terra_dev_priv *
tgd_terra_dev_reserve(struct tgd_terra_driver *fb_drv_data,
		      const tgEthAddr *link_mac_addr);

void tgd_terra_link_stats(struct tgd_terra_dev_priv *priv,
			  struct fb_tgd_bh_link_stats *stats);

#endif
