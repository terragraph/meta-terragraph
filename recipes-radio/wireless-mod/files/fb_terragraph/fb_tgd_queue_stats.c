/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <uapi/linux/sched/types.h>
#endif
#include <linux/sched/rt.h>
#include <linux/kthread.h>

#include <net/genetlink.h>

#include "fb_tgd_debug.h"
#include "fb_tgd_queue_stats.h"
#include "fb_tgd_backhaul.h"
#include "fb_tg_queue_stats.h"
#include "fb_tg_fw_driver_if.h"
#include "fb_tgd_fw_if.h"
#include "fb_tgd_terragraph.h"

#define LOG_FREQ_SECONDS (1)
#define LOG_FREQ_MAINLOOPS (LOG_FREQ_SECONDS * QUEUE_STATS_PER_SECOND)

#define MAX_BYTES_PENDING (ULONG_MAX)
#define MAX_ARRIVED_BYTES_PER_MS (ULONG_MAX)

#define ROUND_DIV(x, y) (((x) + (y) / 2) / (y))
#define ROUND_DIV64(x, y) div64_u64(((x) + (y) / 2), (y))

static int tgd_enable_kernel_stats = 1;
module_param(tgd_enable_kernel_stats, int, 0444);

struct cumul_stats {
	uint64_t tot_arrived_bytes; // sent + pending
	uint64_t avg_arrived_bytes_per_ms;
	struct timespec tlast; // last time stats for this link were sampled
};

struct stats_thread_data {
	struct tgd_terra_driver *fb_drv;
	struct task_struct *stats_thread;
	tgSbQueueStats queue_stats[QUEUE_STATS_MAX_LINKS];
	struct cumul_stats cumulative_stats[QUEUE_STATS_MAX_LINKS];
};

static inline uint64_t timespec_to_millis(const struct timespec *t)
{
	return ((uint64_t)t->tv_sec) * MSEC_PER_SEC +
	       (uint64_t)ROUND_DIV((uint32_t)t->tv_nsec,
				   (uint32_t)NSEC_PER_MSEC);
}

static int queue_stats_thread_main(void *arg)
{
	struct stats_thread_data *ctx = arg;
	struct tgd_terra_driver *fb_drv = ctx->fb_drv;
	int num_loops = 1;
	unsigned int num_interrupted_sleep = 0;
	int too_many_links = 0;
	int i;

	// Initialize the last stats sample time for each link
	for (i = 0; i < QUEUE_STATS_MAX_LINKS; i++) {
		getrawmonotonic(&ctx->cumulative_stats[i].tlast);
	}

	printk(KERN_INFO "queue_stats_thread_main starting\n");
	while (!kthread_should_stop()) {
		tgSbQueueStats *sb_stats = ctx->queue_stats;
		struct cumul_stats *cum_stats = ctx->cumulative_stats;
		struct tgd_terra_dev_priv *priv;
		int linkNumber = 0;

// TODO: Measure sleep accuracy over, say, 100 sleeps and push
// sum of deltas to f/w which can then push it back up as a new f/w
// statistic.
#if QUEUE_STATS_USE_MSLEEP == 1
		if (0 !=
		    msleep_interruptible(QUEUE_STATS_INTERVAL_MILLISECOND)) {
			num_interrupted_sleep++;
			continue; // Skip stats reporting and error logging on
				  // early wakeup.
		}
#else
		usleep_range(QUEUE_STATS_USLEEP_MIN_USEC,
			     QUEUE_STATS_USLEEP_MAX_USEC);
#endif

		// Get the stats for each software queue
		list_for_each_entry(priv, &fb_drv->dev_q_head, list_entry)
		{
			uint64_t tot_arrived_bytes = 0;
			uint64_t arrived_bytes = 0;
			uint64_t arrived_bytes_per_ms =
			    0; // instantaneous arrival rate
			unsigned int stats_resets = 0;
			uint64_t deltaMillis = 0;
			struct timespec tnow;
			struct fb_tgd_bh_link_stats bh_stats;

			if (linkNumber >= QUEUE_STATS_MAX_LINKS) {
				too_many_links = 1;
				continue;
			}

			if (priv->link_state != TG_LINKUP) {
				memset(sb_stats, 0x0, sizeof(*sb_stats));
				goto link_done;
			}

			// Compute the time interval since the last stats sample
			// for this link
			getrawmonotonic(&tnow);
			deltaMillis = timespec_to_millis(&tnow) -
				      timespec_to_millis(&cum_stats->tlast);
			if (deltaMillis == 0)
				deltaMillis = 1;

			// Retrieve stats for the current link
			tgd_terra_link_stats(priv, &bh_stats);

			// Update the last stats sample time for the current
			// link. Note: tlast = tnow might be good enough here
			getrawmonotonic(&cum_stats->tlast);

			// Start updating the southbound stats
			sb_stats->bytesPending =
			    (uint32_t)min(bh_stats.bytes_pending,
					  (uint64_t)MAX_BYTES_PENDING);
			memcpy(sb_stats->dstMacAddr, bh_stats.dst_mac_addr,
			       sizeof(bh_stats.dst_mac_addr));

			// Compute the instantaneous arrival rate in bytes/ms
			tot_arrived_bytes = bh_stats.bytes_enqueued_pad +
					    bh_stats.bytes_enqueued +
					    bh_stats.bytes_enqueue_fail_pad +
					    bh_stats.bytes_enqueue_failed;
			if (cum_stats->tot_arrived_bytes >
			    tot_arrived_bytes) { // stats were reset
				cum_stats->avg_arrived_bytes_per_ms = 0;
				arrived_bytes = tot_arrived_bytes;
				stats_resets++;
			} else { // No re-association, no stats hiccups.
				arrived_bytes = tot_arrived_bytes -
						cum_stats->tot_arrived_bytes;
			}
			cum_stats->tot_arrived_bytes = tot_arrived_bytes;
			arrived_bytes_per_ms =
			    ROUND_DIV64(arrived_bytes, deltaMillis);

			// Update the average arrival rate
			cum_stats->avg_arrived_bytes_per_ms =
			    QUEUE_STATS_UPDATE_ARRIVAL_RATE(arrived_bytes_per_ms, /* instantaneous rate */
							    cum_stats
								->avg_arrived_bytes_per_ms /* old average arrival rate */);

			// Update the 32-bit reported (south bound) arrival rate
			//
			// Also ensure consistent report of "zero arrival rate"
			// regardless
			// of the averaging method, sampling rate, or the units
			// used
			// for the arrival rate.
			if (cum_stats->avg_arrived_bytes_per_ms != 0) {
				// Updated arrival rate moving average != 0
				sb_stats->arrivalRate = (uint32_t)min(
				    cum_stats->avg_arrived_bytes_per_ms,
				    (uint64_t)MAX_ARRIVED_BYTES_PER_MS);
			} else if (arrived_bytes_per_ms != 0) {
				// Instantaneous arrival rate != 0
				sb_stats->arrivalRate = (uint32_t)min(
				    arrived_bytes_per_ms,
				    (uint64_t)MAX_ARRIVED_BYTES_PER_MS);
			} else if (arrived_bytes != 0) {
				// Received some bytes
				sb_stats->arrivalRate = 1;
			} else {
				// Average arrival rate == 0 and no bytes
				// received
				sb_stats->arrivalRate = 0;
			}

			if (num_loops >= LOG_FREQ_MAINLOOPS ||
			    TGD_DBG_QUEUE_STATS_DISABLE_THROTTLE) {
				const uint8_t *mac = sb_stats->dstMacAddr;
				TGD_DBG_QUEUE_STATS_DBG(
				    "link %d tot %llu arr %llu pend %u rate %u "
				    "resets %u "
				    "ms %llu mac "
				    "%02x:%02x:%02x:%02x:%02x:%02x\n",
				    linkNumber, tot_arrived_bytes,
				    arrived_bytes, sb_stats->bytesPending,
				    sb_stats->arrivalRate, stats_resets,
				    deltaMillis, mac[0], mac[1], mac[2], mac[3],
				    mac[4], mac[5]);
			}

		link_done:
			cum_stats++;
			sb_stats++;
			linkNumber++;
		} // list_for_each_entry(priv, &fb_drv->dev_q_head, list_entry)

		// TODO: Keep stats for send failures
		tgd_send_queue_stats(fb_drv, ctx->queue_stats, linkNumber);

		// Log errors
		if (num_loops >= LOG_FREQ_MAINLOOPS &&
		    (num_interrupted_sleep != 0 || too_many_links != 0)) {
			printk(KERN_WARNING
			       "num_interrupted_sleep %d too_many_links %d\n",
			       num_interrupted_sleep, too_many_links);
			num_interrupted_sleep = 0;
			too_many_links = 0;
		}

		if (++num_loops > LOG_FREQ_MAINLOOPS) {
			num_loops = 1;
		}
	} // while (!kthread_should_stop())

	printk(KERN_INFO "queue_stats_thread_main exiting\n");
	return 0;
}

int fb_tgd_queue_stats_init(struct tgd_terra_driver *fb_drv_data)
{
	struct sched_param param = {.sched_priority = MAX_RT_PRIO - 1};
	struct stats_thread_data *stats_data;
	struct task_struct *stats_thread;
	char name[32];

	if (tgd_enable_kernel_stats == 0) {
		return 0;
	}

	/* Allocate thread working block */
	stats_data = kzalloc(sizeof(*stats_data), GFP_KERNEL);
	if (stats_data == NULL)
		return -ENOMEM;

	/* Create the thread */
	snprintf(name, sizeof(name), "queue_stats.%d", fb_drv_data->idx);
	stats_thread =
	    kthread_create(queue_stats_thread_main, stats_data, name);
	if (IS_ERR(stats_thread)) {
		int err = PTR_ERR(stats_thread);
		printk(KERN_ERR "Failed to create queue stats thread %d\n",
		       err);
		kfree(stats_data);
		return err;
	}

	/* Setup links */
	stats_data->stats_thread = stats_thread;
	stats_data->fb_drv = fb_drv_data;
	fb_drv_data->stats_ctx = stats_data;

	/* Start the thread */
	sched_setscheduler(stats_thread, SCHED_FIFO, &param);
	wake_up_process(stats_thread);
	return 0;
}

void fb_tgd_queue_stats_exit(struct tgd_terra_driver *fb_drv_data)
{
	struct stats_thread_data *stats_data;

	stats_data = fb_drv_data->stats_ctx;
	if (stats_data == NULL)
		return;

	kthread_stop(stats_data->stats_thread);
	fb_drv_data->stats_ctx = NULL;

	kfree(stats_data);
}
