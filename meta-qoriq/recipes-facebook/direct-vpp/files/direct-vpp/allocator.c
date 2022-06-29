/* @lint-ignore-every TXT2 Tab Literal */
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "allocator.h"
#include "dvpp_debug.h"

/* Must match HugePage size, or 2 MBytes */
#define DVPP_BLOCK_SIZE (2 * 1024 * 1024)
#define DVPP_BLOCK_BITS 21
#define DVPP_BLOCK_SIZE_ORDER 9

/*
 * When used in polling mode where the buffer cache cannot be accessed
 * from softirq context, there is no need for taking a psinlock there.
 *
 * #define ENABLE_MINI_CACHE_SPINLOCK 1
 */

/*
 * The packets will be injected into the kernel stack,
 * hence leave enough room so as to help.
 */
#define DVPP_SKB_GUARD 128

struct dvpp_perf perf;

/*
 * Keeps track of whether we have a registered mapping
 * describing the network buffer memory.
 */
unsigned int has_user_block_map;

u32 pool_free_read;
u32 pool_free_write;

/* The descriptors that represents free buffers, owned by direct-vpp.ko. */
struct cache_head mini_cache[DVPP_NUM_THREADS] __attribute((aligned(64)));

/*
 * Default port map:
 * set ports 0,1,2,3 on thread 2.
 */
u8 dvpp_thread_map[DVPP_NUM_PORT] = {2, 2, 2, 2};

/* per HugePage Kernel Virtual address */
static void *blocks[DVPP_MAX_NB_BLOCK];

static unsigned int dvpp_nb_blocks;
static struct dvpp_port_maps *_maps;


int num_pkt;
static inline void dvpp_mini_set_kernel_address(dvpp_desc_t *b, u32 idx);

static inline u32 dvpp_cache_size(u32 thread)
{
	return mini_cache[thread].write - mini_cache[thread].read;
}

static inline void dvpp_cache_enqueue(dvpp_desc_t *b, u32 thread)
{
	u32 idx;
#ifdef ENABLE_MINI_CACHE_SPINLOCK
	spin_lock(&mini_cache[thread].dvpp_lock);
#endif
	idx = mini_cache[thread].write & (DVPP_NB_BUFFERS - 1);
	mini_cache[thread].cache[idx] = *b;
	mini_cache[thread].write++;
#ifdef ENABLE_MINI_CACHE_SPINLOCK
	spin_unlock(&mini_cache[thread].dvpp_lock);
#endif
}

static inline int dvpp_cache_dequeue(dvpp_desc_t *b, u32 thread)
{
	int num, ret = 0, idx;

#ifdef ENABLE_MINI_CACHE_SPINLOCK
	spin_lock(&mini_cache[thread].dvpp_lock);
#endif
	num = mini_cache[thread].write - mini_cache[thread].read;
	if (num > 0) {
		idx = mini_cache[thread].read & (DVPP_NB_BUFFERS - 1);
		*b = mini_cache[thread].cache[idx];
		dvpp_desc_clear(&mini_cache[thread].cache[idx]);
		mini_cache[thread].read++;
		ret = 1;
	}
	if (ret == 0)
		mini_cache[thread].kernel_alloc_fail++;
#ifdef ENABLE_MINI_CACHE_SPINLOCK
	spin_unlock(&mini_cache[thread].dvpp_lock);
#endif
	return ret;
}

int dvpp_port_alloc_mini(u32 port, dvpp_desc_t *mini)
{
	if (port_list.ports[port].enable)
		return dvpp_cache_dequeue(mini, dvpp_thread_map[port]);

	return 0;
}

EXPORT_SYMBOL(dvpp_port_alloc_mini);

void dvpp_port_free_mini(dvpp_desc_t *mini, u32 port)
{
#ifdef DVPP_PERF_CACHE
	u64 t2, t1 = dvpp_clock();
#endif
	/*
	 * In case we do not have a user mapping, we
	 * cannot free anything from the cache, so just
	 * forget that the packet segment descriptor ever existed.
	 *
	 * This case applies when the user land network stack is gone
	 * (most likely terminated or crashed) and the network buffer
	 * memory has consequently been released.
	 */
	if (unlikely(has_user_block_map == 0))
		return;
	dvpp_main_stats.ports[port].driver_free++;

	mini->seg.offset = DVPP_DATA_HEADROOM;
	dvpp_cache_enqueue(mini, dvpp_thread_map[port]);

#ifdef DVPP_PERF_CACHE
	t2 = dvpp_clock();
	perf.time[dvpp_thread_map[port]][TC_PERF_0] += (t2 - t1);
#endif
}
EXPORT_SYMBOL(dvpp_port_free_mini);

void dvpp_init_buffers(void)
{
	int i, p;
	for (p = 0; p < DVPP_NUM_THREADS; p++) {
		spin_lock_init(&mini_cache[p].dvpp_lock);
		for (i = 0; i < DVPP_MINI_CACHE_SIZE; i++) {
			dvpp_desc_clear(&mini_cache[p].cache[i]);
		}
		mini_cache[p].read = mini_cache[p].write = 0;
		mini_cache[p].kernel_alloc_fail = 0;
	}
}

void dvpp_reclaim_user(void)
{
	int i;
	has_user_block_map = 0;

	if (dvpp_ops.cancel_dma_fn)
		for (i = 0; i < DVPP_NUM_PORT; i++) {
			dvpp_ops.cancel_dma_fn(port_list.ports[i].context);
		}

	dvpp_init_buffers();

	if (_maps)
		memset(_maps, 0, sizeof(*_maps));
}

void init_allocator(void)
{
	dvpp_init_buffers();
}

void dvpp_free_port_map(void)
{
	if (_maps) {
		kfree(_maps);
	}
	_maps = 0;
}

int dvpp_allocate_port_map(void)
{
	_maps = kzalloc(sizeof(struct dvpp_port_maps), GFP_KERNEL);
	dvpp_log_debug("%s: size %lu maps %p\n", __FUNCTION__,
		       sizeof(struct dvpp_port_maps), _maps);

	return 0;
}

/* TODO: implements alloc/free statistics. */
u32 dvpp_num_sync_alloc = 0;
u32 dvpp_num_sync_free = 0;
/* Number of transmit call. */
u32 dvpp_num_sync_tx = 0;

/* Translate VPP buffer index to block index. */
static inline u32 index_to_block(u32 index)
{
	return (index >> (DVPP_BLOCK_BITS - DVPP_LO_SHIFT)) &
	       (DVPP_MAX_NB_BLOCK - 1);
}

/*
 * Translate a VPP buffer index, that is seg.lo, into
 * a Kernel virtual Address that can be used for regular
 * Linux DMA mapping.
 */
void *dvpp_get_desc_kernel_address(dvpp_desc_t *b)
{
	void *data;
	u32 idx = b->seg.lo;
	/* Offset within block */
	u32 offset = (idx << DVPP_LO_SHIFT) & (DVPP_BLOCK_SIZE - 1);
	/* Get block index */
	u32 block_id = index_to_block(idx);
	/* fill buffer */
	data = (u8 *)blocks[block_id] + offset;
	/* set id */
	b->data = (u64)data;
	return data;
}
EXPORT_SYMBOL(dvpp_get_desc_kernel_address);

/* Calculate kernel address from mini buf */
void *dvpp_desc_kernel_address(dvpp_desc_t *b)
{
	void *data;
	u32 idx = b->seg.lo;
	/* Offset within block */
	u32 offset = (idx << DVPP_LO_SHIFT) & (DVPP_BLOCK_SIZE - 1);
	/* Get block index */
	u32 block_id = index_to_block(idx);
	/* Get address */
	data = (u8 *)blocks[block_id] + offset;
	return data;
}

static inline void dvpp_mini_set_kernel_address(dvpp_desc_t *b, u32 idx)
{
	/* Offset within block */
	u32 offset = (idx << DVPP_LO_SHIFT) & (DVPP_BLOCK_SIZE - 1);
	/* Get block index */
	u32 block_id = index_to_block(idx);
	/* fill buffer */
	b->data = (u64)blocks[block_id] + offset;
}

/**
 * DVPP_IOCTL_VECTOR_SYNC ioctl implementation.
 *
 * @param sync
 *     ioctl parameter struct
 * @return
 *     For DVPP_VECTOR_SYNC_TRANSMIT:
 *          if Success, number of packet transmitted
 *          if Failure, <0
 *     For DVPP_VECTOR_SYNC_RECEIVE:
 *          if Success, number of packet received
 *          if Failure, <0
 */
int dvpp_sync_vector(struct dvpp_vector_sync *sync)
{
	dvpp_desc_t *dvector;

	int cnt = 0;
	u32 i, supply, len;
	int enabled;
	u32 *vector;
#ifdef DVPP_PERF
	u64 t0, t1, t2, t3, t4, t5, t6;
#endif
	int n_tx_pkts = 0;
	u32 port = sync->port;
	u32 pipe = sync->pipe;
	u32 thread = task_cpu(current);
	u32 cache_id;

	if (sync->port >= DVPP_NUM_PORT ||
	    sync->pipe >= DVPP_NUM_PIPE_PER_PORT) {
		return -EINVAL;
	}

	if (sync_dbg || sync->port >= DVPP_NUM_PORT ||
	    sync->pipe > DVPP_NUM_PIPE_PER_PORT) {
		dvpp_log_debug("%s: size %u code %u port %u pipe %u cpu %u\n",
			       __FUNCTION__, sync->size, sync->code, sync->port,
			       sync->pipe, thread);
	}

	cache_id = dvpp_thread_map[sync->port];

	switch (sync->code) {
	case DVPP_VECTOR_SYNC_TRANSMIT:
#ifdef DVPP_PERF
		t0 = dvpp_clock();
#endif
		dvector = _maps->maps[port].tx_vector[sync->thread];
		enabled = port_list.ports[port].pipes[pipe].enable;

		n_tx_pkts = sync->size;
		if (n_tx_pkts && enabled) {
			cnt = dvpp_ops.tx_fn(
				port_list.ports[sync->port].context, sync->pipe,
				dvector, n_tx_pkts, tx_dbg);
			if (cnt < n_tx_pkts) {
				dvpp_main_stats.ports[port]
					.pipes[pipe]
					.drops_from_vpp += n_tx_pkts - cnt;
			}
		} else {
			cnt = 0;
		}
#ifdef DVPP_PERF
		t6 = dvpp_clock();
		perf.time[cache_id][TX_PERF_4] += (t6 - t0);
#endif
		break;
	case DVPP_VECTOR_SYNC_RECEIVE:
#ifdef DVPP_PERF
		t0 = dvpp_clock();
#endif
		dvpp_main_stats.ports[port].vector_sync_rx++;

		/* Store allocated packet segment descriptor in kernel cache */
		if (sync->alloc_size) {
			dvpp_desc_t b = {};
			vector = _maps->maps[port].alloc_vector[sync->thread];

			for (cnt = 0; cnt < sync->alloc_size; cnt++) {
				/* fill buffer address and write to cache*/
				dvpp_mini_set_kernel_address(&b, vector[cnt]);
				b.seg.lo = vector[cnt];
				b.seg.offset = DVPP_DATA_HEADROOM;
				dvpp_cache_enqueue(&b, cache_id);
			}
			/* Inform User-Land of our cache level */
			_maps->cache_level[sync->thread] = dvpp_cache_size(cache_id);
		}

#ifdef DVPP_PERF
		t1 = dvpp_clock();
#endif
		/* Handle Receive */
		enabled = port_list.ports[port].enable;
		if (unlikely(enabled == 0 || dvpp_ops.rx_fn == 0))
			return -ENODEV;
		dvector = _maps->maps[port].rx_vector[sync->thread];

		cnt = dvpp_ops.rx_fn(port_list.ports[sync->port].context,
				     dvector, sync->size, 0);
		dvpp_main_stats.ports[port].pkts_from_driver += cnt;

#ifdef DVPP_PERF
		t2 = dvpp_clock();
#endif

		/* Handle Transmit Completion */
		if (likely(dvpp_ops.tx_complete_fn)) {
			dvpp_ops.tx_complete_fn(
				port_list.ports[sync->port].context);
		}

#ifdef DVPP_PERF
		t3 = dvpp_clock();
#endif

		/* Inform VPP of Transmit queues fill levels. */
		if (likely(dvpp_ops.tx_avail_fn)) {
			dvpp_ops.tx_avail_fn(
				port_list.ports[sync->port].context,
				_maps->maps[port].tx_avail,
				DVPP_NUM_PIPE_PER_PORT);
		}
#ifdef DVPP_PERF
		t4 = dvpp_clock();
#endif

		/* Release excess packet segment descriptors form kernel cache */
		supply = dvpp_cache_size(cache_id);
		if (supply > DVPP_THRESHOLD_HIGH) {
			vector = _maps->maps[port].release_vector[sync->thread];

			for (i = 0; i < DVPP_VLEN; i++) {
				/* Dequeue cached buffer. */
				dvpp_desc_t b = {};
				dvpp_cache_dequeue(&b, cache_id);
				dvpp_main_stats.ports[port].free_to_vpp++;
				/* Release to User-Land */
				*vector++ = b.seg.lo;
			}
			/* Inform number buffers we release. */
			_maps->release_count[sync->thread] = i;
		} else {
			_maps->release_count[sync->thread] = 0;
		}

		/* Inform User-Land of our cache level */
		_maps->cache_level[sync->thread] = dvpp_cache_size(cache_id);

#ifdef DVPP_PERF
		t5 = dvpp_clock();
		perf.time[cache_id][RX_PERF_0] += (t1 - t0);
		perf.time[cache_id][RX_PERF_1] += (t2 - t1);
		perf.time[cache_id][RX_PERF_2] += (t3 - t2);
		perf.time[cache_id][RX_PERF_3] += (t4 - t3);
		perf.time[cache_id][RX_PERF_4] += (t5 - t4);
#endif
		break;
	case DVPP_VECTOR_SYNC_INJECT:
		dvector = _maps->maps[port].tx_vector[sync->thread];
		struct sk_buff *head = 0;
		u32 pre;
		cnt = sync->size;
		for (i = 0; i < cnt; i++) {
			struct sk_buff *skb;
			void * p, *src;

			if (dvector->port_id >= DVPP_NUM_PORT
				|| dvector->pipe_id >= DVPP_NUM_PIPE_PER_PORT)
					break; /* TODO implement proper error recovery */

			enabled = port_list.ports[dvector->port_id].pipes[dvector->pipe_id].enable;
			if (enabled == 0) {
				dvpp_log_notice("%s: not enabled, thread %u port %u pipe %u -> drop\n",
					__FUNCTION__,
					sync->thread, dvector->port_id, dvector->pipe_id);
				break;
			}

			len = dvector->seg.len;
			skb = alloc_skb(len + 2 * DVPP_SKB_GUARD, GFP_KERNEL);
			if (skb == NULL)
				break; /* Drop remaining segments */
			skb_reserve(skb, DVPP_SKB_GUARD);

			if (head == 0) {
				/* First segment of a packet:
				 * Rewind ether header, which is always valid as it
				 * came from the VPP inject node.
				 */
				pre = sizeof(struct ethhdr);
			} else {
				pre = 0;
			}

			p = skb_put(skb, len + pre);
			if (p == NULL)
				break; /* Drop remaining segments */
			src = dvpp_desc_kernel_address(dvector);
			src += dvector->seg.offset - pre;
			memcpy(p, src, len + pre);
			if (is_multicast_ether_addr(src)) {
				dvpp_main_stats.ports[dvector->port_id]
					.pipes[dvector->pipe_id]
					.inject_mcast++;
			}
			dvpp_log_txrx(
				"%s: injecting eop %u len %u head %p hlen %u\n",
				__FUNCTION__, dvector->seg.eop, len, head,
				head ? head->len : 0 );
			if (head) {
				/* Middle and End segments of a packet:
				 * Coalesce all segments into a single skb
				 */
				bool headstolen;
				int delta;
				if (skb_try_coalesce(head, skb, &headstolen, &delta)) {
					kfree_skb_partial(skb, headstolen);
				} else {
					/* Failed to merge SKB */
					kfree_skb(skb);
					kfree_skb(head);
					break; /* Drop remaining segments */
				}
			} else {
				/* Keep track of first skb pf a packet */
				head = skb;
			}

			if (dvector->seg.eop) {
				dvpp_ops.inject_fn(port_list.ports[dvector->port_id].context,
					head,
					dvector->pipe_id);
				head = 0;
			}
			dvector++;
		}
		break;
	default:
		return -EINVAL;
		break;
	}
	return cnt;
}

void free_buffer_pool(void)
{
	int i;
	/* User-Land supplied buffer: nothing to do, just forget the
     * page to kernel address mapping */
	for (i = 0; i < DVPP_MAX_NB_BLOCK; i++) {
		blocks[i] = 0;
	}
}

/* Calculate Linux Page Frame Number of a given Block */
unsigned long block_pfn(unsigned int id)
{
	if (id >= dvpp_nb_blocks)
		return 0;
	return (unsigned long)virt_to_phys(blocks[id]) >> PAGE_SHIFT;
}

/* Calculate Linux Page Frame Number of the dvpp_port_maps */
unsigned long maps_pfn(void)
{
	return (unsigned long)virt_to_phys(_maps) >> PAGE_SHIFT;
}

/*
 * Implement USer-Kernel shared memory,
 * i.e. Map dvpp_port_maps into User-Land
 */
int dvpp_remap_port(struct vm_area_struct *vma)
{
	int ret;
	unsigned int size;

	size = vma->vm_end - vma->vm_start;

	if (size != sizeof(*_maps)) {
		dvpp_log_error("%s: incorrect size %u Bytes need %lu\n",
			       __FUNCTION__, size, sizeof(*_maps));

		return -EINVAL;
	}

	vma->vm_flags |= VM_LOCKED;

	if ((ret = remap_pfn_range(vma, vma->vm_start, maps_pfn(), size,
					vma->vm_page_prot)) < 0) {
		dvpp_log_error("%s: cannot remap _maps size %u ret %d\n",
			       __FUNCTION__, size, ret);
		return -EIO;
	}

	return 0;
}

/*
 * Implement mapping of network b uffer memory.
 *
 * Case when User-Land supplies the phys mem area used for network buffers.
 * Simply records the phys addresses that the user sends doan to kernel.
 *
 * TODO: case where diterct-vpp.ko supplies the meory.
 */
int dvpp_remap_user(struct dvpp_register_map *map)
{
	int i;
	int ret = 0;

	dvpp_log_debug("dvpp_remap_user: %p n_pages %u\n", map->virt,
		       map->n_pages);

	for (i = 0; i < map->n_pages; i++) {
		blocks[i] = (void *)__phys_to_virt(map->pa[i]);
		dvpp_log_debug("      i:%u got virt %p pa %p\n", i,
			       (void *)blocks[i], (void *)map->pa[i]);
	}

	has_user_block_map = 1;
	return ret;
}
