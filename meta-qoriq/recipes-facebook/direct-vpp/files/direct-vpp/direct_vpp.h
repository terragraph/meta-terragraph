/* @lint-ignore-every TXT2 Tab Literal */
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifndef _UAPI_LINUX_DIRECT_VPP_H
#define _UAPI_LINUX_DIRECT_VPP_H

#define DVPP_NUM_PORT 4 /* Terragraph: max number of sectors */
#define DVPP_NUM_PIPE_PER_PORT 16 /* Terragraph: max number of peers */
/* Terragraph: max number of VPP worker threads = # CPU cores
 * participating in datapath */
#define DVPP_NUM_THREADS 4

#define DVPP_ETH_ALEN 6

/* DVPP vector length, must match VPP vector length */
#define DVPP_VLEN 256
/* See below */
#define DVPP_ALLOC_VLEN 1024

/* For case where direct-vpp.ko allocates nrtwork buffer memory */
#define DVPP_BUF_SIZE 4096
#define DVPP_NB_BUFFERS 32768

/* Max number of HugePages that can be used for allocate network buffers */
#define DVPP_MAX_NB_BLOCK 128

/* Max number of segments in a packet DMA chain */
#define DVPP_MAX_NUM_SEGMENTS_IN_PACKET 8

/* HIGH and LOW threshold of the packet segment descriptor cache
 * maintained by direct-vpp.ko
 */
#define DVPP_THRESHOLD_HIGH (8 * 1024)
#define DVPP_THRESHOLD_LOW (4 * 1024)

/* TODO: not implemented */
#define DVPP_VECTOR_SYNC_ALLOCATE 0
/* TODO: not implemtned */
#define DVPP_VECTOR_SYNC_FREE 1
/* Transmit a vector of segments descriptors */
#define DVPP_VECTOR_SYNC_TRANSMIT 2
/* Receive a vector of segments descriptors */
#define DVPP_VECTOR_SYNC_RECEIVE 3
/* Inject a vector of segments descriptors into the kernel */
#define DVPP_VECTOR_SYNC_INJECT 4

/*
 * Head structure used by the DVPP_IOCTL_VECTOR_SYNC ioctl
 */
struct dvpp_vector_sync {
	u16 size;  /* TX or RX size */
	u16 code;  /* DVPP operation (allocate/free/sync_transmit/sync_receive */
	u8 thread; /* The VPP id of the CPU core initiating the ioctl. */
	u8 port;   /* The VPP port targetted */
	u8 pipe;   /* For TX, the VPP pipe targetted */
	u8 flow;   /* For TX, the VPP flow targetted, ~0 if flow is irrelevant. */
	u16 alloc_size; /* Number of descriptor to allocate or release */
} __attribute__((packed));

/*
 * Pipe implemented atop a DVPP device.
 * A pipe correspond to a Link, in Point to Multi-Point configuration.
 *
 * A pipe can implements a single HW transmit queue within the network driver,
 * which represents an ordered stream of descriptor to transmit or receive.
 * In the context of Terragraph with wil6210 driver, pipes correspond
 * to Wigig peers, and support a single transmit queue.
 *
 * A pipe can as well support multiple transmit queues (HW or virtual), for
 * instance so as to differentiate traffic flows accorting to DSCP.
 * In that case the subdivision is :
 * Port -> Pipe -> FLow
 */

struct dvpp_pipe {
	/* TODO: implement API support for individual Flow */
	unsigned int enable;
	u8 addr[DVPP_ETH_ALEN]; /* The peer's MAC address */
} __attribute__((packed));

/*
 * VPP port.
 * A port correspond to a specific instance of a network driver, which can
 * implement multiple transmit and/or receive queues.
 * In the context of Terragraph with wil6210 driver, ports correspond
 * to Wigig Sectors, or individual PCI cards.
 */
struct dvpp_port {
	unsigned int enable;
	unsigned int pci;
	void *context; /* TODO: move context out of interface */
	u8 addr[DVPP_ETH_ALEN]; /* The interface's physical MAC address */
	struct dvpp_pipe pipes[DVPP_NUM_PIPE_PER_PORT];
} __attribute__((packed));

/*
 * Head structure used by the DVPP_IOCTL_GET_PORTS ioctl
 *
 * The global list of Ports->Pipes->Flows managed by direct-vpp.ko
 */
struct dvpp_port_list {
	u32 nb_ports;
	u32 pipes_per_port; /* MAx number of pipes supported on a port */
	u32 mem_size;
	u32 buf_size; /* network buffer size, match VPP's network buffer size */
	struct dvpp_port ports[DVPP_NUM_PORT];
} __attribute__((packed));

/*
 * dvpp_port_map represent the User-Kernel shared memory area
 * (2MB, that is a single HugePage)
 * that is used so as to synchronize packet segment descriptor between User
 * and Kernel Land.
 *
 * DVPP_VLEN: max number of packet descriptors that can be transmitted or
 * received in one shot. max number of packet descriptors that can be freed up.
 * This must correspond to the VPP vector length.

 * DVPP_ALLOC_VLEN: max number of packet descriptors that can be allocated
 * in one shot.
 *
 * DVPP_NUM_THREADS: must match the number of CPU cores available to VPP.
 *
 * Note on the ioctl usage:
 *     The dvpp_port_map shared memory is used by the DVPP ioctls, so as
 *     to synchronize packet segment descriptors between Kernel and User land.
 *        1) the packet segment descriptors themselves are never directly
 *           memcopied from/to User-Land.
 *        2) the DVPP ioctls are never blocking (i.e. the user-land blocking
 *           mechanism is implemented only thorugh the Linux poll() system
 *           calls)
 *        3) Hence, a given CPU core (expected to back up a single VPP worker
 *           thread) can be in only one ioctl at a time. The arrays used
 *           so as to sync packet segment descriptors, are therefore
 *           instantiated per CPO core (or VPP thread).
 */
 /* TODO: makes DVPP_NUM_THREADS dynamically configurable */
struct dvpp_port_map {
	dvpp_desc_t rx_vector
			[DVPP_NUM_THREADS][DVPP_VLEN + DVPP_MAX_NUM_SEGMENTS_IN_PACKET];
	dvpp_desc_t tx_vector
			[DVPP_NUM_THREADS][DVPP_VLEN + DVPP_MAX_NUM_SEGMENTS_IN_PACKET];
	u32 alloc_vector[DVPP_NUM_THREADS][DVPP_ALLOC_VLEN];
	u32 release_vector[DVPP_NUM_THREADS][DVPP_VLEN];
	/*  No need per thread, for a given port it is
        accessed only from one core */
	u32 tx_avail[DVPP_NUM_PIPE_PER_PORT];
} __attribute__((packed));

/*
 * Global structure describing the User-Kernel shared memory area.
 * Must expands over a single HugePage, hence 2MBytes.
 */
struct dvpp_port_maps {
	union {
		struct {
			/* The array of dvpp_port_map. */
			struct dvpp_port_map maps[DVPP_NUM_PORT];
			/* For case where VPP supplies the memory backing up the
			 * network buffers : the vectors used by direct-vpp.ko so as to
			 * synchronize its cache of free descriptors/ */
			u32 cache_level[DVPP_NUM_THREADS];
			u32 release_count[DVPP_NUM_THREADS];
		};
		u8 data[2 * 1024 * 1024];
	};
} __attribute__((packed));

/*
 * Head structure used by the DVPP_IOCTL_REGISTER_MAP ioctl
 *
 * Used in case VPP allocates the phys memory area backing up the
 * network buffers.
 */
 /* TODO: implement support for multiple disjointed phys memory areas */
struct dvpp_register_map {
	/* User-Land virtual address of the phys memory area allocate dby VPP. */
	void *virt;
	/*
	 * Number of consecutive huge pages comprising the phys mem area.
	 * Must be less than DVPP_MAX_NB_BLOCK
	 */
	u32 n_pages;
	/* Physical Addresses of each Huge Page. */
	void *pa[DVPP_MAX_NB_BLOCK];
} __attribute__((packed));

/*
 * Head structure used by the DVPP_IOCTL_THREAD_MAP ioctl
 *
 * Represent mapping of port to cpu/thread.
 */
struct dvpp_thread_map {
	u8 thread[DVPP_NUM_PORT];
};

#define DVPP_TYPE 'v'

/*
 * DVPP_IOCTL_GET_PORTS
 *
 *    Retrieve port list.
 */
#define DVPP_IOCTL_GET_PORTS _IOWR(DVPP_TYPE, 1, struct dvpp_port_list)
/*
 * DVPP_IOCTL_VECTOR_SYNC
 *
 * Synchronize packet descriptor vectors:
 * This ioctl comprises of 4 subcommands:
 * - DVPP_VECTOR_SYNC_ALLOCATE:
 *        For DPDK-less mode: where direct-vpp.ko supplies the memory area
 *                            backing up the VPP network buffers.
 *        Allocate a number of buffer.
 * - DVPP_VECTOR_SYNC_FREE:
 *        For DPDK-less mode: when direct-vpp.ko supplies the memory area
 *                           backing up the VPP network buffers.
 *        Release a number of buffer.
 * - DVPP_VECTOR_SYNC_TRANSMIT:
 *        For all cases: transmit a bunch of packets.
 * - DVPP_VECTOR_SYNC_RECEIVE:
 *        For all cases:
 *                      - Receive a vector of packets
 *        For DPDK mode: when VPP supplies the memory area backing up the
 *                       VPP network buffers, and therefore direct-vpp.ko acts
 *                       as a slave of the VPP allocator:
 *                      - synchronize the descriptor cache that direct-vpp.ko
 *                        maintains.
 */
#define DVPP_IOCTL_VECTOR_SYNC _IOWR(DVPP_TYPE, 2, struct dvpp_vector_sync)
/*
 * DVPP_IOCTL_VECTOR_SYNC
 *
 *
 *       For DPDK mode: when VPP supplies the memory area backing up the
 *                      VPP network buffers, and therefore direct-vpp.ko acts
 *                      as a slave of the VPP allocator:
 *       Allows VPP to register a vlib_physmem_map_t area, backing up the
 *       VPP network buffer allocator.
 */
#define DVPP_IOCTL_REGISTER_MAP _IOWR(DVPP_TYPE, 3, struct dvpp_register_map)

/*
 * DVPP_IOCTL_THREAD_MAP
 *
 *  configure thread map, i.e. mapping of port to thread
 */
#define DVPP_IOCTL_THREAD_MAP _IOWR(DVPP_TYPE, 4, struct dvpp_thread_map)


#endif
