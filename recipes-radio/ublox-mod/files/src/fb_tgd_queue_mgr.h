/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * Terragraph Manage Queue, used only for GPS now can be made generic with
 *  changes like use dynamic allocation instead of static data in use now
 */
#ifndef FB_WL_API_LIB_HDR_
#define FB_WL_API_LIB_HDR_

#ifdef __cplusplus
extern "C" {
#endif

#define TXQ_DATA_MAX_LEN 16

typedef void *tgd_q_hndlr;

tgd_q_hndlr init_tgd_message_queue(int q_len, spinlock_t *spin_lock);

char *tgd_queue_get(tgd_q_hndlr q_desc, int *d_len, unsigned int *priv_data);

int tgd_queue_create_new_entry(tgd_q_hndlr q_desc, char *data_ptr, int d_len,
			       unsigned int priv_data);

int tgd_queue_deinit_cleanup(tgd_q_hndlr q_desc);

void tgd_queue_free_queue(tgd_q_hndlr q_desc, char *cfg_data_p);

void tgd_queue_set_dbg_lvl(tgd_q_hndlr q_desc, unsigned int dbg_mask);

#define INIT_MAGIC 0xFE01DC23

#define QMGR_SPIN_LOCK(spin_h)                                                 \
	do {                                                                   \
		if (spin_h) {                                                  \
			spin_lock_bh(spin_h);                                  \
		}                                                              \
	} while (0)

#define QMGR_SPIN_UNLOCK(spin_h)                                               \
	do {                                                                   \
		if (spin_h) {                                                  \
			spin_unlock_bh(spin_h);                                \
		}                                                              \
	} while (0)

struct t_queue_desc {
	unsigned int magic;
	int num_entries;
	int max_q_depth;
	int dbg_mask;
	spinlock_t *spin_lock;
	struct list_head q_list_head;
};

struct t_list_q_data {
	struct list_head list;
	int data_len;
	unsigned int priv_data;
	unsigned int magic;
	char data_ptr[0];
};

#ifdef __cplusplus
}
#endif
#endif //#ifndef _TG_SDNCLIENT_LIB_HDR_
