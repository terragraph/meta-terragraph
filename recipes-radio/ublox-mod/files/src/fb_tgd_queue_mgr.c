/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * Terragraph Manage Queue, used only for GPS now can be made generic with
 * changes like use dynamic allocation instead of static data in use now
 */
#include <linux/timer.h>
#include <linux/device.h>
#include <linux/slab.h>

#include "fb_tgd_queue_mgr.h"

/*****************************************************************************
 * Initialize the queue handler and return the handler
 *
 * q_len - Maximum number of entires allowed in this queue
 * spin_lock - used for mutual exclusion
 * return the queue handler
 *****************************************************************************/
tgd_q_hndlr init_tgd_message_queue(int q_len, spinlock_t *spin_lock)
{
	struct t_queue_desc *q_hndlr;

	q_hndlr = (struct t_queue_desc *)kmalloc(sizeof(struct t_queue_desc),
						 GFP_KERNEL);
	if (!q_hndlr) {
		printk(KERN_WARNING "Error: init_tgd_message_queue  kmalloc\n");
		return NULL;
	}
	INIT_LIST_HEAD(&q_hndlr->q_list_head);
	q_hndlr->max_q_depth = q_len;
	q_hndlr->num_entries = 0;
	q_hndlr->magic = INIT_MAGIC;
	q_hndlr->dbg_mask = 0;
	q_hndlr->spin_lock = spin_lock;

	return q_hndlr;
}

/*****************************************************************************
 * add an entry to the queue
 * q_desc - handle to the descriptor, returned during init_tgd_message_queue
 * data_ptr - buffer pointer (data+hdr) to be added to the queue
 *
 * return    -  0 on success , nonzero error
 *****************************************************************************/
static int tgd_queue_add_entry(tgd_q_hndlr q_desc,
			       struct t_list_q_data *data_ptr)
{
	struct t_queue_desc *q_hndlr = (struct t_queue_desc *)q_desc;
	int l_num_entry;

	if ((!q_hndlr) || (q_hndlr->magic != INIT_MAGIC)) {
		printk(KERN_WARNING "Error: Handler Not Initialized \n");
		return -1;
	}

	QMGR_SPIN_LOCK(q_hndlr->spin_lock);
	if (q_hndlr->num_entries >= q_hndlr->max_q_depth) {
		QMGR_SPIN_UNLOCK(q_hndlr->spin_lock);
		printk(KERN_WARNING "Error: tgd_queue_add_entry QueueFull\n");
		return -1;
	}

	q_hndlr->num_entries++;
	list_add_tail(&data_ptr->list, &q_hndlr->q_list_head);
	l_num_entry = q_hndlr->num_entries;
	QMGR_SPIN_UNLOCK(q_hndlr->spin_lock);

	if (q_hndlr->dbg_mask) {
		printk("tgd_queue_add_entry Len:%d Count:%d \n",
		       data_ptr->data_len, l_num_entry);
	}
	return 0;
}

/*****************************************************************************
 * Check the queue, if not empty detach the first entry from the queue list
 * and return the data pointer to the caller. Once returned, the caller should
 * free the memory by using the tgd_queue_free_queue
 *
 * q_desc - handle to the descriptor, returned during init_tgd_message_queue
 * d_len - place holder to return the length information
 * priv_data - place holder to return the private data
 *
 * return - A queued buffer pointer if available, NULL if empty or error
 *****************************************************************************/
char *tgd_queue_get(tgd_q_hndlr q_desc, int *d_len, unsigned int *priv_data)
{
	char *ret_ptr = NULL;
	int l_num_entry;
	struct t_list_q_data *l_cur_ptr;
	struct t_queue_desc *q_hndlr = (struct t_queue_desc *)q_desc;

	*d_len = 0;
	*priv_data = 0;
	if ((!q_hndlr) || (q_hndlr->magic != INIT_MAGIC)) {
		printk(KERN_WARNING "Error: tgd_queue_get Invalid Handler \n");
		return ret_ptr;
	}

	QMGR_SPIN_LOCK(q_hndlr->spin_lock);
	if (list_empty(&q_hndlr->q_list_head)) {
		QMGR_SPIN_UNLOCK(q_hndlr->spin_lock);
		return ret_ptr;
	}
	l_cur_ptr =
	    list_first_entry(&q_hndlr->q_list_head, struct t_list_q_data, list);
	list_del(&l_cur_ptr->list);
	ret_ptr = l_cur_ptr->data_ptr;
	*d_len = l_cur_ptr->data_len;
	*priv_data = l_cur_ptr->priv_data;
	q_hndlr->num_entries--;
	l_num_entry = q_hndlr->num_entries;
	QMGR_SPIN_UNLOCK(q_hndlr->spin_lock);

	if (q_hndlr->dbg_mask) {
		printk("tgd_queue_get Len:%d RemCount:%d \n", *d_len,
		       l_num_entry);
	}

	return ret_ptr;
}
/*****************************************************************************
 * Free the memory allocated during the queue add.
 * q_desc - handle to the descriptor, returned during init_tgd_message_queue
 * cfg_data_p - pointer to data_ptr[0] in the container of type t_list_q_data
 *****************************************************************************/
void tgd_queue_free_queue(tgd_q_hndlr q_desc, char *cfg_data_p)
{
	struct t_list_q_data *l_ptr;
	struct t_queue_desc *q_hndlr = (struct t_queue_desc *)q_desc;

	if (!cfg_data_p) {
		printk(KERN_WARNING "Error: tgd_queue_free_queue NULL Ptr\n");
		return;
	}

	l_ptr = container_of(cfg_data_p, struct t_list_q_data, data_ptr[0]);
	if (q_hndlr->dbg_mask) {
		printk("tgd_queue_free_queue DataPtr:%p  ContPtr:%p \n",
		       cfg_data_p, l_ptr);
	}
	if ((l_ptr) && (l_ptr->magic == INIT_MAGIC)) {
		kfree(l_ptr);
	} else {
		printk(KERN_WARNING
		       "Error: tgd_queue_free_queue InvalidMagic: 0x%x\n",
		       l_ptr->magic);
	}
}

/*****************************************************************************
 * De-initialize the queue, for gps block use.
 * This API is called during the module unload time. No spinlock protection
 * here.
 *
 * q_desc -handle to the descriptor, returned during init_tgd_message_queue.
 *****************************************************************************/
int tgd_queue_deinit_cleanup(tgd_q_hndlr q_desc)
{
	struct t_list_q_data *l_cur_ptr;
	struct t_queue_desc *q_hndlr = (struct t_queue_desc *)q_desc;

	if ((!q_hndlr) || (q_hndlr->magic != INIT_MAGIC)) {
		printk(KERN_WARNING
		       "Error:tgd_queue_deinit_cleanup Invalid Handlr\n");
		return -1;
	}
	while (!list_empty(&q_hndlr->q_list_head)) {
		l_cur_ptr = list_first_entry(&q_hndlr->q_list_head,
					     struct t_list_q_data, list);
		list_del(&l_cur_ptr->list);
		kfree(l_cur_ptr);
	}
	kfree(q_hndlr);
	return 0;
}

/*****************************************************************************
 * Given data is copied to a dynamically allocated memory (after the header).
 * The header in the dynamically allocated memory is used to store information
 * like data length, private data etc.
 * This newly allocated memory gets added to the queue
 * The consumer use the tgd_queue_get to detach a buffer from the queue, the
 * consumer should free the memory using tgd_queue_free_queue.
 *
 * q_desc - handle to the descriptor, returned during init_tgd_message_queue
 * data_ptr - Pointer for the data to be copied
 * d_len    - Number of bytes in the data_pter
 * priv_data - A 32 bit value getting copied into the Queue
 * retur 0 - success, non-zero error
 *****************************************************************************/
int tgd_queue_create_new_entry(tgd_q_hndlr q_desc, char *data_ptr, int d_len,
			       unsigned int priv_d)
{
	struct t_list_q_data *m_ptr;
	int ret_value;
	struct t_queue_desc *q_dptr = (struct t_queue_desc *)q_desc;

	if ((!q_dptr) || (q_dptr->magic != INIT_MAGIC)) {
		printk(KERN_WARNING
		       "Error: tgd_queue_create_new_entry Bad Handler\n");
		return -1;
	}

	if (!data_ptr) {
		printk(KERN_WARNING
		       "tgd_queue_create_new_entry NullDataPtr \n");
		return -1;
	}
	m_ptr = (struct t_list_q_data *)kmalloc(
	    d_len + sizeof(struct t_list_q_data), GFP_KERNEL);
	if (!m_ptr) {
		printk(KERN_WARNING
		       "tgd_queue_create_new_entry Memalloc Failed \n");
		return -1;
	}
	m_ptr->data_len = d_len;
	m_ptr->priv_data = priv_d;
	m_ptr->magic = INIT_MAGIC;
	memcpy(m_ptr->data_ptr, data_ptr, d_len);

	ret_value = tgd_queue_add_entry(q_desc, m_ptr);
	if (ret_value) {
		kfree(m_ptr);
		printk(KERN_WARNING "tgd_queue_create_new_entry  Failed\n");
		return -1;
	}

	return 0;
}
/**************************************************************************
**************************************************************************/
void tgd_queue_set_dbg_lvl(tgd_q_hndlr q_desc, unsigned int dbg_mask)
{
	struct t_queue_desc *q_dptr = (struct t_queue_desc *)q_desc;

	q_dptr->dbg_mask = dbg_mask;
	printk("tgd_queue_dbg %s \n", dbg_mask ? "Enabled" : "Disabled");
}
