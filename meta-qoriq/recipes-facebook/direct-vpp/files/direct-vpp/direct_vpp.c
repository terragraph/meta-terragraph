/* @lint-ignore-every TXT2 Tab Literal */
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/poll.h>

#include "allocator.h"
#include "dvpp_debug.h"

#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif

#define MODULE_NAME "direct-vpp"

/*
 * Note on direct-vpp.ko interfaces:
 *
 * Architecture of the module is as per drawing.
 * It presents two interfaces:
 *   - ioctl/mmap interface to the Network Stack,
 *     that is VPP, in the case of Facebook Terragraph.
 *   - platform device interface to Network Drivers,
 *     that is wil6210.ko, in the case of Facebook Terragraph.
 *
 *                          -----------------------
 *                          | Network stack = VPP |
 *   User-Land              ----------|------------
 * -----------------------------------|------------
 *   Kernel-Land                      |
 *                                    |
 *  --------------           ---------|------------
 *  | netdev:    |           |    direct-vpp.ko   |
 *  | wil6210.ko +-----------+                    |
 *  --------------           ----------------------
 */


/* To enable verbose TX mode. */
uint tx_dbg;
module_param(tx_dbg, uint, 0644);

/* To enable verbose RX mode. */
uint rx_dbg;
module_param(rx_dbg, uint, 0644);

/* To enable verbose in sync ioctl. */
uint sync_dbg;
module_param(sync_dbg, uint, 0644);

uint dvpp_log_level = LOGLEVEL_DEBUG;
module_param(dvpp_log_level, uint, 0644);

/* To enable kernel dynamic debug */
bool dvpp_dyn_debug;
module_param(dvpp_dyn_debug, bool, 0644);


/*
 * The list of ports known to DVPP.
 *
 * Organized with following hierarchy:
 * port --> pipe --> flow
 */
struct dvpp_port_list port_list;

/* direct-vpp.ko --> driver API */
dvpp_ops_t dvpp_ops;

/* The platform device under which DVPP registers with kernel. */
struct platform_device *dvpp_platform;

/* Debug statistics. */
struct dvpp_stats dvpp_main_stats;

/* Number of user that have opened the dvpp-cmd device drievr. */
static uint user_count;

/* Number of client registered to dvpp_platform. */
static uint platform_client_count;

/* So as to synchronize port state changes with user-land threads. */
static DEFINE_MUTEX(ioctl_lock);

/*
 * USER TO KERNEL INTERFACE
 */

/*
 * The wait queue onto which User-Land client blocks for Ports and Pipes
 * state events.
 *
 * Link state events are provided by the network driver
 * through the port_state and pipe_state APIs,
 * and synchronizate with User-Land through poll() mechanism.
 */
static DECLARE_WAIT_QUEUE_HEAD(dvpp_wait);

/* Whether User-Land needs to wake up because the module is terminating. */
static unsigned int dvpp_poll_wait_exiting;
/* Whether User-Land needs to wake up to register Link-State change. */
static unsigned int dvpp_poll_wait_state_pending;

static unsigned int dvpp_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;
	poll_wait(file, &dvpp_wait, wait);

	if (dvpp_poll_wait_exiting)
		mask |= POLLNVAL;

	if (dvpp_poll_wait_state_pending)
		mask |= POLLIN;

	dvpp_poll_wait_exiting = 0;
	dvpp_poll_wait_state_pending = 0;
	return mask;
}

static const char *ioctl_to_string(uint ioctl_num)
{
	const char *s = "";
	switch (ioctl_num) {
	case DVPP_IOCTL_GET_PORTS:
		s = "DVPP_IOCTL_GET_PORTS";
		break;
	case DVPP_IOCTL_VECTOR_SYNC:
		s = "DVPP_IOCTL_VECTOR_SYNC";
		break;
	case DVPP_IOCTL_REGISTER_MAP:
		s = "DVPP_IOCTL_REGISTER_MAP";
		break;
	default:
		s = "DVPP_IOCTL_UNKNOWN";
		break;
	}
	return s;
}

/* The main DVPP ioctl */
static long dvpp_ioctl(struct file *file, uint ioctl_num, ulong arg)
{
	struct dvpp_vector_sync sync;
	struct dvpp_register_map map;
	struct dvpp_thread_map thread_map;
	unsigned long n;
	int ret = 0, i;

	switch (ioctl_num) {
	case DVPP_IOCTL_GET_PORTS:
		dvpp_log_debug("%s: %s\n", __FUNCTION__,
			       ioctl_to_string(ioctl_num));
		mutex_lock(&ioctl_lock);
		n = copy_to_user((void __user *)arg, &port_list,
				 sizeof(port_list));
		if (n)
			ret = -EFAULT;
		mutex_unlock(&ioctl_lock);
		break;
	case DVPP_IOCTL_VECTOR_SYNC:
		n = copy_from_user(&sync, (void __user *)arg, sizeof(sync));
		if (n)
			ret = -EFAULT;
		ret = dvpp_sync_vector(&sync);
		break;
	case DVPP_IOCTL_REGISTER_MAP:
		n = copy_from_user(&map, (void __user *)arg, sizeof(map));
		if (n)
			ret = -EFAULT;
		ret = dvpp_remap_user(&map);
		dvpp_log_debug("%s: %s ret %d\n", __FUNCTION__,
			       ioctl_to_string(ioctl_num), ret);
		break;
	case DVPP_IOCTL_THREAD_MAP:
		n = copy_from_user(&thread_map, (void __user *)arg, sizeof(thread_map));
		if (n)
			ret = -EFAULT;
		for(i = 0; i < DVPP_NUM_PORT; i++)
			dvpp_thread_map[i] = thread_map.thread[i];
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

/* Note: Support only one user at a time */
static int dvpp_open(struct inode *inode, struct file *file)
{
	int ret;
	file->private_data = NULL;
	ret = mutex_lock_interruptible(&ioctl_lock);
	if (ret == 0 && user_count) {
		mutex_unlock(&ioctl_lock);
		return -EAGAIN;
	}
	user_count = 1;
	dvpp_poll_wait_exiting = 0;
	mutex_unlock(&ioctl_lock);
	dvpp_log_debug("%s: ret %d\n", __FUNCTION__, ret);
	return ret;
}

/*
 * USer is gone, hence clean up all buffers and stop traffic.
 */
static int dvpp_release(struct inode *inode, struct file *file)
{
	dvpp_log_debug("%s:\n", __FUNCTION__);

	mutex_lock(&ioctl_lock);
	if (user_count) {
		dvpp_reclaim_user();
		user_count = 0;
	}
	dvpp_poll_wait_exiting = 1;

	mutex_unlock(&ioctl_lock);

	wake_up_interruptible(&dvpp_wait);

	return 0;
}

static int dvpp_mmap(struct file *f, struct vm_area_struct *vma)
{
	int ret = dvpp_remap_port(vma);
	dvpp_log_notice("%s: ret %d\n", __FUNCTION__, ret);
	return ret;
}

static const struct file_operations dvpp_fops = {
	.owner = THIS_MODULE,
	.open = dvpp_open,
	.release = dvpp_release,
	.unlocked_ioctl = dvpp_ioctl,
	.mmap = dvpp_mmap,
	.poll = dvpp_poll,
};

/* The /dev/dvpp-cmd Linux device */
static struct miscdevice dvpp_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "dvpp-cmd",
	.fops = &dvpp_fops,
};

/*
 * KERNEL NETWORK DRIVER TO DIRECT-VPP.KO INTERFACE
 */

/* DVPP platform driver registration *
 *
 * Implements the irect-vpp.ko --> network driver interface
 */
void dvpp_register(dvpp_ops_t *ops)
{
	dvpp_log_notice("%s: ops %p platform_client_count %u\n", __FUNCTION__,
			ops, platform_client_count);
	if (platform_client_count == 0 && ops) {
		dvpp_ops = *ops;
		platform_client_count = 1;
	} else if (platform_client_count == 1 && ops == 0) {
		memset(&dvpp_ops, 0, sizeof(dvpp_ops));
		platform_client_count = 0;
	} else {
		dvpp_log_error("%s: error ops %p, client count %u\n",
			       __FUNCTION__, ops, platform_client_count);
	}
}

/* Notify port existence and state */
int dvpp_port_state(void *ctx, unsigned int port, void *addr,
		    unsigned int enable)
{
	dvpp_log_notice("%s: port %u %pM %s\n", __FUNCTION__, port, addr,
			enable ? "enabled" : "disabled");

	if (port >= DVPP_NUM_PORT)
		return -EINVAL;
	mutex_lock(&ioctl_lock);

	port_list.ports[port].enable = enable;

	if (enable == 0) {
		memset(&port_list.ports[port], 0,
		       sizeof(port_list.ports[port]));
	} else {
		port_list.ports[port].context = ctx;
		memcpy(port_list.ports[port].addr, addr, ETH_ALEN);
		memset(port_list.ports[port].pipes, 0,
		       DVPP_NUM_PIPE_PER_PORT * sizeof(struct dvpp_pipe));
	}
	/* Wake up DVPP, to effect port state change */
	dvpp_poll_wait_state_pending = 1;
	mutex_unlock(&ioctl_lock);

	wake_up_interruptible(&dvpp_wait);

	return 0;
}
EXPORT_SYMBOL(dvpp_port_state);

/* Notify pipe state, i.e. Interface Link Up/Down */
int dvpp_pipe_state(unsigned int port, unsigned int pipe, void *addr,
		    unsigned int enable)
{
	dvpp_log_notice("%s: port %u pipe %u "
			" %pM %s\n",
			__FUNCTION__, port, pipe, addr,
			enable ? "enabled" : "disabled");

	if (port >= DVPP_NUM_PORT)
		return -EINVAL;

	if (pipe >= DVPP_NUM_PIPE_PER_PORT)
		return -EINVAL;
	mutex_lock(&ioctl_lock);

	if (port_list.ports[port].enable) {
		port_list.ports[port].pipes[pipe].enable = enable;
		if (addr && enable)
			memcpy(port_list.ports[port].pipes[pipe].addr, addr,
			       ETH_ALEN);
		else
			memset(port_list.ports[port].pipes[pipe].addr, 0,
			       ETH_ALEN);
	}
	/* Wake up DVPP, to effect network interface state change */
	dvpp_poll_wait_state_pending = 1;
	mutex_unlock(&ioctl_lock);

	wake_up_interruptible(&dvpp_wait);

	return 0;
}
EXPORT_SYMBOL(dvpp_pipe_state);

/* DVPP device platform ops */
static dvpp_platform_ops_t dvpp_platform_ops = {
	.register_ops = dvpp_register,
	.port_state = dvpp_port_state,
	.pipe_state = dvpp_pipe_state,
	.port_free_mini = dvpp_port_free_mini,
	.port_alloc_mini = dvpp_port_alloc_mini,
	.get_desc_kernel_address = dvpp_get_desc_kernel_address,
};

/*
 * MODULE ADMINISTRATION
 */

static void clean_all(void)
{
	misc_deregister(&dvpp_misc);

	dvpp_free_port_map();
	free_buffer_pool();

	dvpp_debugfs_remove();

	dvpp_poll_wait_exiting = 1;
	wake_up_interruptible(&dvpp_wait);

	if (dvpp_platform) {
		platform_device_unregister(dvpp_platform);
	}
	dvpp_platform = 0;
}

static void __exit fini(void)
{
	clean_all();
	dvpp_log_notice("[" MODULE_NAME "] unloaded\n");
}

static int __init init(void)
{
	int rc = misc_register(&dvpp_misc);
	if (rc != 0)
		dvpp_log_error("%s: misc registration failed %d\n",
			       __FUNCTION__, rc);

	init_allocator();

	dvpp_allocate_port_map();
	dvpp_init_buffers();

	memset(&port_list, 0, sizeof(port_list));
	port_list.pipes_per_port = DVPP_NUM_PIPE_PER_PORT;
	port_list.buf_size = DVPP_BUF_SIZE;
	port_list.mem_size = DVPP_BUF_SIZE * DVPP_NB_BUFFERS;
	port_list.nb_ports = DVPP_NUM_PORT;

	dvpp_debugfs_init();

	/*
	 * Register our platform device
	 *
	 * Implements the network driver --> direct-vpp.ko interface
	 */
	dvpp_platform = platform_device_alloc("direct-vpp", -1);
	if (!dvpp_platform) {
		dvpp_log_error("[" MODULE_NAME
			       "] direct-vpp to alloc, duplicate?\n");
		rc = -ENOMEM;
		goto fail;
	}

	rc = platform_device_add_data(dvpp_platform, &dvpp_platform_ops,
				      sizeof(dvpp_platform_ops));
	if (rc < 0) {
		dvpp_log_error("%s: failed to add platform device data, "
			       " err %d\n",
			       __FUNCTION__, rc);
		platform_device_put(dvpp_platform);
		dvpp_platform = 0;
		goto fail;
	}

	rc = platform_device_add(dvpp_platform);
	if (rc) {
		dvpp_log_error("%s: failed to add platform device, "
			       " err %d\n",
			       __FUNCTION__, rc);
		platform_device_put(dvpp_platform);
		dvpp_platform = 0;

		goto fail;
	}

	dvpp_log_notice("[" MODULE_NAME "] initialized\n");
	return 0;
fail:
	dvpp_log_error("[" MODULE_NAME "] fail to initialize\n");
	clean_all();
	return rc;
}

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Facebook Inc.");
MODULE_DESCRIPTION("Direct VPP interface.");
MODULE_VERSION("0.1");

module_init(init);
module_exit(fini);
