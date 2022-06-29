/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * Facebook Terragraph driver - backhaul interface definitions
 */

#ifndef _tgd_backhaul_pub_h_
#define _tgd_backhaul_pub_h_

#include <linux/types.h>

/**
* Incremented when API backward compatibility is lost.
*/
#define TGD_BH_API_VERSION_MAJOR  8
/**
* Incremented for all changes to this header file.
*/
#define TGD_BH_API_VERSION_MINOR  0

/**
 * BH MQ supports 4 ACs: BK/BE/VI/VO, mapped from 8 priorities 0..7
 */
#define BH_MQ_QUEUE_NUM		4 /**< 4 AC queues supported.*/
#define BH_MQ_PRIO_NUM		8 /**< 8 priorities supported (0 to 7). */

/**
 * API version number
 */
#define TGD_BH_API_VERSION	(((TGD_BH_API_VERSION_MAJOR) << 16) | \
				 (TGD_BH_API_VERSION_MINOR))

#define TGD_BH_IOCTL_BUF_SZ	1024	/**< Max size of IOCTL input/output buffer */

#define TGD_BH_LINK_ID_INVALID	(-1)

/*
 * Descriptor structure to identify links for add/remove link info
 * operations.
 */
struct tgd_bh_netdev_desc {
	int devPeerIndex; /**< Peer index from 0..client_max_peers */
	int devNameUnit;  /**< Device name unit, global system-wide */
};

/*
 * Descriptor structure to identify links for add/remove link info
 * operations.
 */
struct tgd_bh_link_info_desc {
	int peerIndex; /**< Peer index from 0..client_max_peers */
	int rxLinkId;  /**< Link ID of the RX link */
	int txLinkId;  /**< Link ID of the TX link */
	void *linkCtx; /**< Link ctx to be passed to caller within
	                        callbacks, valid while adding links only */
	void *linkDev; /**< Network device for the link */
};

/**
 * Transmit Descriptor Structure defining per packet attributes.
 */
struct tgd_bh_data_txd {
	int peerIndex; /**< Peer index */
	int txLinkId;  /**< Identifier for the link (returned by add_link hook)
	                    to which the packet belongs.*/
	int lifetime;  /**< Reserved for future use */
};

/**
 * Receive Descriptor Structure defining per packet attributes.
 */
struct tgd_bh_data_rxd {
	int peerIndex;     /**< Peer index */
	int rxLinkId;      /**< Identifier for the link (returned by add_link hook)
	                        to which the packet belongs.*/
	void *linkCtx;	   /**< Client link context */
};

/**
 * Structure for providing TX/RX statistics.
 */
struct tgd_bh_link_stats {
	uint64_t bytes_sent;     /**< Number of bytes transmitted */
	uint64_t bytes_sent_pad; /**< Number of pad bytes transmitted */
	uint64_t bytes_pending;  /**< Number of bytes pending transmission */
	uint64_t pkts_sent;      /**< Number of packets transmitted */
	uint64_t pkts_pending;   /**< Number of packets pending transmission */
	uint64_t pkts_recved;    /**< Number of packets received */
	uint64_t bytes_recved;   /**< Number of bytes received */
	uint64_t tx_err;         /**< Number of transmit packet errors */
	uint64_t rx_err;         /**< Number of receive packet errors */
	uint64_t pkts_enqueued;  /**< Number of transmit pkts enqueued */
	uint64_t bytes_enqueued; /**< Number of transmit bytes enqueued */
	uint64_t bytes_enqueued_pad; /**< Number of transmit pad bytes enqueued */
	uint64_t bytes_enqueue_fail_pad; /**< Number of pad bytes failed enq */
	uint64_t bytes_sent_failed; /**< Number of bytes transmitted that failed */
	uint64_t bytes_sent_failed_pad; /**< Number of pad bytes transmitted
	                                     that failed */
	uint64_t bytes_enqueue_failed; /**< Number of transmit bytes enqueue
	                                    failed */
};

/**
 * Operation Structure definition exposing the hooks provided by BH client
 * driver to vendor host driver. These include callback functions called
 * from the vendor host driver to the Backhaul Platform Driver.
 */
struct tgd_bh_callback_ops {
	int api_version; /**< API version number set to TGD_BH_API_VERSION.
		 Used by vendor driver for checking compatibility. */

	/**
	 * Packet receive callback function. The backhaul client is the owner
	 * of the skb after this call. This function should be non-blocking i.e.
	 * received data/event can be put to a queue and
	 * a software interrupt scheduled before returning.
	 *
	 * @param [in]	ctxt	Pointer to context registered with vendor driver
	 * @param [in]	skb	Pointer to the received frame
	 * @param [in]	rxd	Descriptor containing the packet's receive attributes
	 *
	 * @return	None
	 */
	void (*rx_data)(void *ctxt, struct sk_buff *skb, struct tgd_bh_data_rxd *rxd);

	/**
	 * Event receive callback function. Vendor driver owns the event buffer.
	 * The backhaul client should make a copy of the buffer if background
	 * processing is needed. This function should be non-blocking i.e.
	 * received data/event can be put to a queue and
	 * a software interrupt scheduled before returning.
	 *
	 * @param [in]	ctxt	Pointer to context registered with vendor driver
	 * @param [in]	event	Pointer to buffer containing the event data
	 * @param [in]	size	Size of the event data (in bytes)
	 *
	 * @return	None
	 */
	void (*rx_event)(void *ctxt, const uint8_t *event, uint size);

	/**
	 * Indicate that flow control has been turned off on the specified link
	 * and the BH client driver can enqueue more frames for transmission.
	 * Start of flow-control is signalled by link_suspends call.
	 *
	 * @param [in]	ctxt	Pointer to context registered with vendor driver
	 * @param [in]	lnk_ctx	Pointer to link context registeres with vendor
	 *	driver.
	 * @param [in]	link	TX Link-ID
	 * @param [in]	qid	queue index 0..3
	 *
	 * @return	None
	 */
	void (*link_resume)(void *ctxt, void *lnk_ctx, int link, unsigned char qid);

	/**
	 * Indicate that flow control has been started on the specified link.
	 * BH client driver should stop enqueuing frames on specified link until
	 * link_resume is called for the link.
	 *
	 * @param [in]	ctxt	Pointer to context registered with vendor driver
	 * @param [in]	lnk_ctx	Pointer to link context registered with vendor
	 *	driver.
	 * @param [in]	link	Link-ID
	 * @param [in]	qid	queue index 0..3
	 *
	 * @return	None
	 */
	void (*link_suspend)(void *ctxt, void *link_ctx, int link, unsigned char qid);

	/**
	 * Set the MAC address for the baseband sector.
	 * Each BH client context has the unique MAC provided by the vendor
	 * baseband sector.
	 *
	 * @param [in]	ctxt	Pointer to context registered with vendor driver
	 * @param [in]	mac_addr	Pointer to the sector's MAC address.
	 *
	 * @return	None
	 */
	void (*set_mac_addr)(void *ctxt, uint8_t *mac_addr);
};

/*
 * Structure to describe the interface consumer to the BH service provider.
 */
struct tgd_bh_client_info {
	struct tgd_bh_callback_ops *client_ops;
	void *client_ctx;
	uint16_t client_max_peers;
};

/**
 * Operation Structure definition exposing the hooks provided to the BH client
 * driver by vendor host driver. These include callbacks called from the
 * Terragraph platform driver to the vendor  device driver.
 */
struct tgd_bh_ops {
	int api_version; /**< API version number set to TGD_BH_API_VERSION. Used
		by BH client driver for checking compatibility. */

	const unsigned char (*bh_prio_mq_map)[BH_MQ_PRIO_NUM]; /**< Map from
		 priority to queue to be used, following WME spec. */

	/**
	 * Register backhaul client with vendor driver. The pointer to the
	 * operations structure passed to this function should be persistent
	 * & BH Client should not subsequently modify its content.
	 *
	 * @param [in]	plat_data	Pointer to vendor driver context
	 * @param [in]	ops	Pointer to operations structure implemented by BH
	 *	client
	 * @param [in]	ctxt	Pointer to Context structure of BH Client module
	 * @param [out]	dev	Argument to return Pointer to vendor driver context
	 *
	 * @return	Error Code (as defined in errno.h)
	 */
	int (*register_client)(void *plat_data, struct tgd_bh_client_info *, void **dev_ctx);

	/**
	 * Unregister backhaul client with vendor driver.
	 *
	 * @param [in]	dev	Pointer to vendor driver context
	 *
	 * @return	Error Code (as defined in errno.h)
	 */
	int (*unregister_client)(void *dev_ctx);

	/**
	 * Configure the network device for the  link. The only parameters that
	 * vendors are allowed to modify are hardware offload and scatter/gather
	 * support flags
	 */
	int (*setup_netdev)(void *dev_ctx, struct net_device *ndev,
			     struct tgd_bh_netdev_desc *dev_desc);

	/**
	 * Add the link information to the vendor driver. A link is identified
	 * by the linkID, direction and the link context pointer attributes.
	 *  For bidirectional traffic separate TX and RX links have to be created.
	 *
	 * @param [in]	dev	Pointer to vendor driver context
	 * @param [in]	ld	Descriptor containing links attributes
	 *
	 * @return	On success, Link-ID (Positive integer). On failure, Error Code
	 * (as defined in errno.h)
	 */
	int (*add_link_info)(void *dev, struct tgd_bh_link_info_desc *ld);

	/**
	 * Delete an existing link.
	 *
	 * @param [in]	dev	Pointer to vendor driver context
	 * @param [in]	ld	Descriptor containing links attributes
	 *
	 * @return	Error Code (as defined in errno.h)
	 */
	int (*delete_link_info)(void *dev, struct tgd_bh_link_info_desc *ld);

	/**
	 * Enqueue a packet for transmission. Vendor host driver owns the skb and
	 * will free the skb upon tx completion. For tx flow control, callbacks
	 * link_suspend/resume get invoked.
	 *
	 * @param [in]	dev	Pointer to vendor driver context
	 * @param [in]	skb	Pointer to the skb containing the packet
	 * @param [in]	txd	Descriptor containing the packet's transmit attributes
	 *
	 * @return void. skb is either queued to HW or gets dropped & freed in
	 * vendor host driver.
	 */
	void (*tx_data)(void *dev, struct sk_buff *skb, struct tgd_bh_data_txd *txd);

	/**
	 * Query link specific statistics
	 * This API is not supported for this release.
	 *
	 * @param [in]	dev	Pointer to vendor driver context
	 * @param [in]	peer	Iindex of the desired peer
	 * @param [out]	stats	Pointer to buffer for returning the statistics
	 *
	 * @return	Error Code (as defined in errno.h)
	 */
	int (*link_stats)(void *dev, int peer, struct tgd_bh_link_stats *stats);


	/**
	 * Configuration interface (IOCTL path) to the FW component of the backhaul
	 *  module. It is allowed to pass same copy of buffer for req and resp
	 * buf fields.
	 *
	 * @param [in]	dev         Pointer to vendor driver context
	 * @param [in]	req_buf   Pointer to the buffer containing the IOCTL
	 * Request to be sent to firmware
	 * @param [in]	req_len   Length of the IOCTL Request buffer
	 * @param [out]	resp_buf Pointer to a buffer to hold the IOCTL Response
	 * received from firmware
	 * @param [in]	resp_len Length of the IOCTL Response buffer
	 *
	 * @return	Error Code (as defined in errno.h)
	 */
	int (*ioctl)(void *dev, const uint8_t *req_buf, uint req_len, uint8_t *resp_buf, uint resp_len);

	/**
	 * Set the encryption key for the link. Currently only 128 bit GCMP
	 * encryption is supported by vendor.
	 *
	 * @param [in]	dev	Pointer to vendor driver context
	 * @param [in]	dest_mac Mac addr of Destination STA to add the key
	 * @param [in]	key_data Pointer to the key data
	 * @param [in]	key_len	Length of the key data
	 *
	 * @return	Error Code (as defined in errno.h)
	 */
	int (*set_key)(void *dev, int peer_index, const uint8_t *dest_mac, const uint8_t *key_data,
	  uint key_len);
};

/**
 * Platform driver data registered by the vendor host driver with the
 *  platform device during the probe operation.
 */
struct tgd_bh_platdata {
	void *drv_bh_ctx; /**< dev pointer to vendor driver context. */
	const struct tgd_bh_ops *drv_bh_ops; /**< Pointer to vendor driver
		callback operations. */
	uint8_t mac_addr[6];
};

/**
 * Platform driver 'compatible' string.
 */
#define TGD_BH_COMPATIBLE_STRING "terragraph_bh"

#endif /* _dhd_backhaul_pub_h_ */
