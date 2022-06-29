/* @lint-ignore-every TXT2 Tab Literal */
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include <linux/debugfs.h>
#include <linux/module.h>

#include "allocator.h"

static struct dentry *dbg;
static u64 last_perf_print;

static int buffer_debugfs_show(struct seq_file *s, void *data)
{
	u32 i, p;
	u32 n_cache_free[DVPP_NUM_THREADS] = {};

	for (p = 0; p <  DVPP_NUM_THREADS; p++) {
		for (i = 0; i < DVPP_NB_BUFFERS; i++) {
			if (mini_cache[p].cache[i].seg.lo)
				n_cache_free[p]++;
		}
		seq_printf(s, "n_cache_free      : %d\n", n_cache_free[p]);
	}

	seq_printf(s, "sync free   : %u\n", dvpp_num_sync_free);
	seq_printf(s, "sync alloc  : %u\n", dvpp_num_sync_alloc);
	seq_printf(s, "sync tx     : %u\n", dvpp_num_sync_tx);

	for (p = 0; p <  DVPP_NUM_THREADS; p++) {
		if (mini_cache[p].kernel_alloc_fail) {
			seq_printf(s, "kernel alloc fail thread %u : %d\n",
						p, mini_cache[p].kernel_alloc_fail);
		}
	}
	return 0;
}

static int buffer_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, buffer_debugfs_show, inode->i_private);
}

static const struct file_operations fops_buffers = {
	.open = buffer_seq_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek,
};

static int stats_debugfs_show(struct seq_file *s, void *data)
{
	u32 i, j, len = 0;
	u32 cnt;
	char buf[1024];
	for (i = 0; i < DVPP_NUM_PORT; i++) {
		seq_printf(s, "\nport %d (%s):\n", i,
				port_list.ports[i].enable?"enabled":"disabled");
		seq_printf(s, "     driver free: %10u",
			   dvpp_main_stats.ports[i].driver_free);
		seq_printf(s, "     free to vpp: %10u\n",
			   dvpp_main_stats.ports[i].free_to_vpp);
		seq_printf(s, "     sync rx:     %10u",
			   dvpp_main_stats.ports[i].vector_sync_rx);
		seq_printf(s, "     rx from drv: %10u\n",
			   dvpp_main_stats.ports[i].pkts_from_driver);

		for (j = 0; j < DVPP_NUM_PIPE_PER_PORT; j++) {
			len = 0;
			if ((cnt = dvpp_main_stats.ports[i]
					   .pipes[j]
					   .pkts_from_driver))
				len += sprintf(buf + len,
					       " pkts_from_drv %10u", cnt);
			if ((cnt = dvpp_main_stats.ports[i]
					   .pipes[j]
					   .drops_from_driver))
				len += sprintf(buf + len,
					       " drop_from_drv %10u", cnt);
			if ((cnt = dvpp_main_stats.ports[i]
					   .pipes[j]
					   .pkts_from_vpp))
				len += sprintf(buf + len,
					       " pkts_from_vpp %10u", cnt);
			if ((cnt = dvpp_main_stats.ports[i]
					   .pipes[j]
					   .drops_from_vpp))
				len += sprintf(buf + len,
					       " drop_from_vpp %10u", cnt);
			if ((cnt = dvpp_main_stats.ports[i]
					   .pipes[j]
					   .errors_from_vpp))
				len += sprintf(buf + len,
					       " errs_from_vpp %10u", cnt);
			if ((cnt = dvpp_main_stats.ports[i]
					   .pipes[j]
					   .disabled_from_vpp))
				len += sprintf(buf + len,
					       " disb_from_vpp %10u",
					       cnt);
			if ((cnt = dvpp_main_stats.ports[i]
					   .pipes[j]
					   .tx_black_hole))
				len += sprintf(buf + len,
					       " tx_black_hole %10u", cnt);
			if ((cnt = dvpp_main_stats.ports[i]
					   .pipes[j]
					   .tx_black_hole))
				len += sprintf(buf + len,
					       " inject_mcasts %10u", cnt);
			if (len) {
				seq_printf(s, "     pipe %2u:   %s\n", j, buf);
			}
		}
	}
	return 0;
}

static int stats_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, stats_debugfs_show, inode->i_private);
}

static const struct file_operations fops_stats = {
	.open = stats_seq_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek,
};

static int perf_debugfs_show(struct seq_file *s, void *data)
{
	u32 i, t;
	u64 now = dvpp_clock();
	u64 diff = (now - last_perf_print) * CPU_CLOCK_TO_NANO;
	u64 total = 0;
	last_perf_print = now;
	for (t = 0; t < DVPP_NUM_THREADS; t++) {
		bool print = false;
		for (i = 0; i < DVPP_NUM_PERF_STATS; i++) {
			if (perf.time[t][i])
				print = true;
		}
		if (!print)
			continue;
		total = 0;
		seq_printf(s, "Thread %u perf: %12llu nano\n", t, diff);
		for (i = 0; i < DVPP_NUM_PERF_STATS; i++) {
			seq_printf(s, "          intv %u:\t%12llu\n", i,
				CPU_CLOCK_TO_NANO * perf.time[t][i]);
			total += perf.time[t][i];
			perf.time[t][i] = 0;
		}
		seq_printf(s, "Total: %12llu\n", CPU_CLOCK_TO_NANO * total);
		seq_printf(s, "\n");
	}
	return 0;
}

static int perf_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, perf_debugfs_show, inode->i_private);
}

static const struct file_operations fops_perf = {
	.open = perf_seq_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek,
};

void dvpp_debugfs_init(void)
{
	dbg = debugfs_create_dir("dvpp", NULL);
	if (IS_ERR_OR_NULL(dbg))
		return;

	debugfs_create_file("buffers", 0444, dbg, 0, &fops_buffers);
	debugfs_create_file("stats", 0444, dbg, 0, &fops_stats);
	debugfs_create_file("perf", 0444, dbg, 0, &fops_perf);
}

void dvpp_debugfs_remove(void)
{
	if (dbg)
		debugfs_remove_recursive(dbg);
}
