/* @lint-ignore-every TXT2 Tab Literal */
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifndef _DIRECT_VPP_ALLOCATOR_H
#define _DIRECT_VPP_ALLOCATOR_H

#include "dvpp_descriptor.h"
#include "direct_vpp.h"
#include "dvpp_module_interface.h"

extern void dvpp_debugfs_init(void);
extern void dvpp_debugfs_remove(void);

struct dvpp_pipe_stats {
	u32 drops_from_driver;
	u32 pkts_from_driver;
	u32 pkts_from_vpp;
	u32 drops_from_vpp;
	u32 errors_from_vpp;
	u32 disabled_from_vpp;
	u32 tx_black_hole;
	u32 bytes_from_driver;
	u32 bytes_from_vpp;
	u32 inject_mcast;
};

struct dvpp_port_stats {
	u32 tx_black_hole;
	u32 driver_free;
	u32 free_to_vpp;
	u32 vector_sync_tx;
	u32 vector_sync_rx;
	u32 pkts_from_driver;
	struct dvpp_pipe_stats pipes[DVPP_NUM_PIPE_PER_PORT];
};

/* The global stats data-base */
struct dvpp_stats {
	struct dvpp_port_stats ports[DVPP_NUM_PORT] __attribute((aligned(64)));
};

extern struct dvpp_stats dvpp_main_stats;

extern u32 dvpp_num_sync_alloc;
extern u32 dvpp_num_sync_free;
extern u32 dvpp_num_sync_tx;

/* DVPP module parameters */
extern uint tx_dbg;
extern uint rx_dbg;
extern uint sync_dbg;
extern uint tx_time;
extern uint null_alloc_pkt;

extern struct dvpp_port_list port_list;

extern dvpp_ops_t dvpp_ops;

extern int allocate_buffer_pool(unsigned int size);
extern unsigned long pool_pfn(unsigned int id);
extern unsigned long pool_size(void);
extern void free_buffer_pool(void);
extern int dvpp_reset_mappings(void);
extern int dvpp_remap(struct vm_area_struct *vma);
extern int dvpp_remap_port(struct vm_area_struct *vma);
extern int dvpp_allocate_port_map(void);
extern void dvpp_free_port_map(void);
extern void dvpp_reclaim_user(void);
extern int dvpp_sync_vector(struct dvpp_vector_sync *sync);
extern void dvpp_init_buffers(void);
extern int dvpp_remap_user(struct dvpp_register_map *map);
extern void init_allocator(void);

extern int dvpp_port_alloc_mini(u32 port, dvpp_desc_t *mini);
extern void dvpp_port_free_mini(dvpp_desc_t *mini, u32 port);
extern void *dvpp_get_desc_kernel_address(dvpp_desc_t *b);

extern u8 dvpp_thread_map[DVPP_NUM_PORT];


/*
 * Perf and profiling trace points
 *
 * Enable profiling with
       #define DVPP_PERF 1
 */
enum {
	TX_PERF_0,
	TX_PERF_1,
	TX_PERF_2,
	TX_PERF_3,
	TX_PERF_4,
	TX_PERF_5,
	RX_PERF_0,
	RX_PERF_1,
	RX_PERF_2,
	RX_PERF_3,
	RX_PERF_4,
	RX_PERF_5,
	TC_PERF_0,
	TC_PERF_1,
	DVPP_NUM_PERF_STATS,
};

/* Store profiling trace points */
struct dvpp_perf {
	u64 time[DVPP_NUM_THREADS][DVPP_NUM_PERF_STATS];
};

/*
 * Get fast access to high resolution timebase.
 * Architecture specific and for profiling purpose only.
 */
#if defined(__aarch64__)
#define CPU_CLOCK_TO_NANO 40 /* Adjust for cpu */
static inline u64 dvpp_clock(void)
{
	u64 tsc;
	__asm__ volatile("mrs %0, cntvct_el0" : "=r"(tsc));
	return tsc;
}
#else
static inline u64 dvpp_clock(void)
{
	return 0;
}
#endif

/* The cache of packet segment descriptors mainted by direct-vpp.ko */
#define DVPP_MINI_CACHE_SIZE DVPP_NB_BUFFERS
struct cache_head {
	dvpp_desc_t cache[DVPP_MINI_CACHE_SIZE];
	u32 read;
	u32 write;
	u32 kernel_alloc_fail;
	spinlock_t dvpp_lock;
} __attribute((aligned(64)));

extern struct cache_head mini_cache[DVPP_NUM_THREADS]
		__attribute((aligned(64)));


extern struct dvpp_perf perf;

#endif
