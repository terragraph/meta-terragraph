/* @lint-ignore-every TXT2 Tab Literal */
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifndef _UAPI_LINUX_DIRECT_VPP_MODULE_INTERFACE_H
#define _UAPI_LINUX_DIRECT_VPP_MODULE_INTERFACE_H

#include <linux/skbuff.h>

/**
 * Transmit a batch of packets
 *
 * @param p
 *   opaque context associated to the port
 * @param flow
 *   flow id for the packet batch
 * @param b
 *   pointer to an array of packet segment descriptor
 * @param n_pkts
 *   number of descriptors
 * @param verbose
 *   reserved for for debug
 * @return number of descriptors transmitted
 */
typedef int (*tx_fn_t)(void *p, u32 flow, dvpp_desc_t *b, u32 n_pkts,
		       u32 verbose);
/**
 * Read a batch of packets from the network driver's queue
 *
 * @param p
 *   opaque context associated to the port
 * @param b
 *   pointer to an array of packet segment descriptor
 * @param n_pkts
 *   number of descriptors
 * @param verbose
 *   reserved for for debug
 * @return number of descriptors received
 */
typedef int (*rx_fn_t)(void *p, dvpp_desc_t *b, u32 n_pkts, u32 verbose);
/**
 * Report number of packet segment that can be transmitted
 * on each pipe of a given port.
 *
 * @param p
 *   opaque context associated to the port
 * @param avail
 *   pointer to array that will store the number of descriptors
 * @param n_pipe
 *   number of pipe and size of the *avail array
 * @return
 *   0 if successful, negative otherwise
 */
typedef int (*tx_avail_fn_t)(void *p, u32 *avail, u32 n_pipe);
/**
 * Handle transmit completion for this port.
 * This primitive causes the network driver to free up the descriptors
 * associated to transmitted packets, as well to update the
 * network statistics.
 *
 * @param p
 *   opaque context associated to the port
 * @return
 *   0 if successful, negative otherwise
 */
typedef int (*tx_complete_fn_t)(void *p);
/**
 * Cancel currently occuring DMA and free up the descriptors
 * currently stored in the Transmit and Receive Rings.
 *
 * This function is not called during normal operation and
 * is not used for normal Link State management. It is called
 * so as to perform clean up when handling failure cases, such as,
 * software crash within VPP.
 *
 * @param p
 *   opaque context associated to the port
 * @return
 *   0 if successful, negative otherwise
 */
typedef int (*cancel_dma_fn_t)(void *p);
/**
 * Injects a packet from VPP into the Linux stack.
 *
 * @param p
 *   opaque context associated to the port
 * @param skb
 *   the packet to inject
 * @param pipe
 *    the pipe onto which the packet needs to be injected
 *    where pipe <=> netdev
 * @return
 *   gro_result_t status code
 */
typedef int (*inject_fn_t)(void *p, struct sk_buff *skb, u32 pipe_id);

/* Interface at direct-vpp.ko --> linux network driver */
typedef struct {
	tx_fn_t tx_fn;
	rx_fn_t rx_fn;
	tx_avail_fn_t tx_avail_fn;
	tx_complete_fn_t tx_complete_fn;
	cancel_dma_fn_t cancel_dma_fn;
	inject_fn_t inject_fn;
} dvpp_ops_t;

/* Interface at the linux network driver --> direct-vpp.ko */
typedef struct {
	/* Register interface ops. */
	void (*register_ops)(dvpp_ops_t *ops);
	/* Set port state. */
	int (*port_state)(void *context, unsigned int port, void *addr,
			   unsigned int enable);
	/* Set pipe state, within a port. */
	int (*pipe_state)(unsigned int port, unsigned int pipe, void *addr,
			   unsigned int enable);
	/* Free up a buffer descriptor (after transmission).
	 * Note: for accounting, keep track of which port
	 * releases the buffer.
	 */
	void (*port_free_mini)(dvpp_desc_t *mini, u32 port);
	/* Allocate a buffer descriptor (to populate the Rx queues).
	 * Note: for accounting, keep track of which port
	 * allocates the buffer.
	*/
	int (*port_alloc_mini)(u32 port, dvpp_desc_t *mini);

	/* Get the Kernel virtual Address associated to a Descriptor */
	void *(*get_desc_kernel_address)(dvpp_desc_t *b);

} dvpp_platform_ops_t;


#endif
