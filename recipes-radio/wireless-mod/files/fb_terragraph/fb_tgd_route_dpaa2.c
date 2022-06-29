/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Terragraph NSS routing backend */

#define pr_fmt(fmt) "fb_tgd_route_dpaa2: " fmt

#include <linux/kernel.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/version.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>

typedef uint32_t nf_if_id;

#include <amsdu-kmod/tgdn_kern.h>
#include <amsdu-kmod/amsdu_kern_nfapi.h>

#include <fb_tg_fw_driver_if.h>
#include "fb_tgd_terragraph.h"
#include "fb_tgd_fw_if.h"
#include "fb_tgd_debug.h"
#include "fb_tgd_route.h"
#include "fb_tgd_amsdu.h"
#include "fb_tgd_backhaul.h"

#define TGD_ASSERT(c) BUG_ON(!(c))

#define MAX_COS 8u
#define MIN_COS 1u

/*
 * Counter to deal with the global nature of A-MSDU callbacks
 * registered with the vendor module. This is, generally speaking,
 * still racy, but the case of multiple devices attaching and
 * failing in parallel is highly unlikely. This will go away once
 * we implement per-instance callbacks, as initially requested of
 * the vendor.
 */
static atomic_t tgd_dpaa2_instance_count;

/*
 * Spread available QoS values accross available queues.
 */
static uint8_t qid_to_cos[FB_TGD_BH_MQ_QUEUE_NUM] = {[FB_TGD_MQ_BK] = 1,
						     [FB_TGD_MQ_BE] = 3,
						     [FB_TGD_MQ_VI] = 5,
						     [FB_TGD_MQ_VO] = 7};

static struct {
	uint8_t prio;
	uint8_t qid;
} cos_to_prio[MAX_COS] = {
    {.prio = FB_TGD_BH_SKB_PRIO_BK, .qid = FB_TGD_MQ_BK},
    {.prio = FB_TGD_BH_SKB_PRIO_BK, .qid = FB_TGD_MQ_BK},
    {.prio = FB_TGD_BH_SKB_PRIO_BE, .qid = FB_TGD_MQ_BE},
    {.prio = FB_TGD_BH_SKB_PRIO_BE, .qid = FB_TGD_MQ_BE},
    {.prio = FB_TGD_BH_SKB_PRIO_VI, .qid = FB_TGD_MQ_VI},
    {.prio = FB_TGD_BH_SKB_PRIO_VI, .qid = FB_TGD_MQ_VI},
    {.prio = FB_TGD_BH_SKB_PRIO_VO, .qid = FB_TGD_MQ_VO},
    {.prio = FB_TGD_BH_SKB_PRIO_VO, .qid = FB_TGD_MQ_VO},
};

/* Mark external symbols as weak */
extern int32_t amsdu_packet_tx_register(amsdu_packet_tx_cb cb)
    __attribute__((__weak__));
extern int32_t amsdu_packet_rx_handler(struct sk_buff *pkt,
				       struct tgdn_amsdu_rx_meta *meta)
    __attribute__((__weak__));

int32_t tgdn_set_flowcontrol(enum nf_api_control_flags flags,
			     struct tgdn_cfg_set_fc_req_inargs *cmd,
			     struct nf_api_outargs *outargs,
			     struct nf_api_respargs *respargs)
    __attribute__((__weak__));

int32_t tgdn_add_amsdu_context(enum nf_api_control_flags flags,
			       struct tgdn_cfg_add_amsdu_ctxt_inargs *cmd,
			       struct nf_api_outargs *outargs,
			       struct nf_api_respargs *respargs)
    __attribute__((__weak__));

int32_t tgdn_del_amsdu_context(enum nf_api_control_flags flags,
			       struct tgdn_cfg_del_amsdu_ctxt_inargs *cmd,
			       struct nf_api_outargs *outargs,
			       struct nf_api_respargs *respargs)
    __attribute__((__weak__));

int32_t tgdn_fc_register_cb(tgdn_resp_cbfn cb) __attribute__((__weak__));
int32_t tgdn_amsdu_context_register_cb(tgdn_resp_cbfn cb)
    __attribute__((__weak__));
int32_t tgdn_cos_register_cb(tgdn_resp_cbfn cb) __attribute__((__weak__));

struct fb_tgd_dpaa2_rt_backend {
	struct fb_tgd_routing_backend rt_base;
};

struct fb_tgd_dpaa2_rt_devpriv {
	spinlock_t cfg_lock;
	int cfg_ret;
	bool cfg_call;
	struct completion cfg_completion;
};

static inline struct fb_tgd_dpaa2_rt_devpriv *
tgd_dpaa2_get_priv(struct tgd_terra_dev_priv *priv)
{
	return priv->rt_data;
}

static inline void tgd_dpaa2_set_priv(struct tgd_terra_dev_priv *priv,
				      struct fb_tgd_dpaa2_rt_devpriv *pfe_priv)
{
	priv->rt_data = pfe_priv;
}

static inline struct fb_tgd_dpaa2_rt_backend *
tgd_dpaa2_get_backend(struct tgd_terra_dev_priv *dev_priv)
{
	struct tgd_terra_driver *fb_drv_data;

	fb_drv_data = dev_priv->fb_drv_data;
	return container_of(fb_drv_data->rt_backend,
			    struct fb_tgd_dpaa2_rt_backend, rt_base);
}

static void fb_tgd_rt_dpaa2_cfg_call_start(struct tgd_terra_dev_priv *dev_priv)
{
	struct fb_tgd_dpaa2_rt_devpriv *rtp;

	rtp = tgd_dpaa2_get_priv(dev_priv);
	TGD_ASSERT(rtp != NULL);

	spin_lock(&rtp->cfg_lock);
	reinit_completion(&rtp->cfg_completion);
	/* Do not support multiple concurrent calls */
	TGD_ASSERT(!rtp->cfg_call);
	rtp->cfg_ret = 0;
	rtp->cfg_call = true;
	spin_unlock(&rtp->cfg_lock);
}

static void fb_tgd_rt_dpaa2_cfg_call_done(struct tgd_terra_dev_priv *dev_priv,
					  int ret)
{
	struct fb_tgd_dpaa2_rt_devpriv *rtp;

	rtp = tgd_dpaa2_get_priv(dev_priv);
	TGD_ASSERT(rtp != NULL);

	spin_lock(&rtp->cfg_lock);
	rtp->cfg_ret = ret;
	complete_all(&rtp->cfg_completion);
	spin_unlock(&rtp->cfg_lock);
}

static int fb_tgd_rt_dpaa2_cfg_call_wait(struct tgd_terra_dev_priv *dev_priv)
{
	struct fb_tgd_dpaa2_rt_devpriv *rtp;
	unsigned short timeout = 2 * HZ;
	static int tgd_terra_aiop_is_dead = 0;
	int ret;

	rtp = tgd_dpaa2_get_priv(dev_priv);
	TGD_ASSERT(rtp != NULL);
	TGD_ASSERT(rtp->cfg_call);

	if (tgd_terra_aiop_is_dead == 0) {
		/* Wait for completion */
		timeout =
		    wait_for_completion_timeout(&rtp->cfg_completion, timeout);
		if (timeout == 0) {
			/* Raise global flag to prevent future calls */
			tgd_terra_aiop_is_dead = 1;

			TGD_DBG_CTRL_ERROR(
			    "Timeout waiting for AIOP to respond\n");
			/* Kill off all registered CFG callbacks */
			(void)tgdn_fc_register_cb(NULL);
			(void)tgdn_amsdu_context_register_cb(NULL);
			(void)tgdn_cos_register_cb(NULL);

			fb_tgd_rt_dpaa2_cfg_call_done(dev_priv, 0xbadf);
		}
	} else {
		fb_tgd_rt_dpaa2_cfg_call_done(dev_priv, 0xbadf);
	}

	/* Get the return code and for next cfg call */
	spin_lock(&rtp->cfg_lock);
	ret = rtp->cfg_ret;
	rtp->cfg_call = false;
	spin_unlock(&rtp->cfg_lock);

	return ret;
}

static void fb_tgd_resp_callback(struct nf_api_outargs *outargs,
				 struct nf_api_respargs *respargs)
{
	struct tgd_terra_dev_priv *dev_priv;

	dev_priv =
	    (struct tgd_terra_dev_priv *)(uintptr_t)respargs->opaque_data;
	if (dev_priv == 0) { /* The call did not really need completion */
		return;
	}

	fb_tgd_rt_dpaa2_cfg_call_done(dev_priv, outargs->error_code);
}

static int fb_tgd_rt_dpaa2_add_amsdu_ctx(struct tgd_terra_dev_priv *dev_priv,
					 int qid)
{
	struct tgd_terra_driver *drv_data;
	struct tgdn_cfg_add_amsdu_ctxt_inargs cmd;
	struct nf_api_outargs outargs;
	struct nf_api_respargs respargs;
	int32_t ret;

	drv_data = dev_priv->fb_drv_data;

	memset(&cmd, 0, sizeof(cmd));
	memset(&outargs, 0, sizeof(outargs));
	memset(&respargs, 0, sizeof(respargs));

	cmd.context_id =
	    dev_priv->dev_index * dev_priv->dev->num_tx_queues + qid;
	cmd.nf_amsdu_out_ifid = dev_priv->dev->ifindex;
	cmd.cookie = (uintptr_t)dev_priv;
	cmd.n_cos_pairs = 1;
	cmd.chanid = drv_data->idx;

	cmd.nf_amsdu_cos_arr[0] = qid_to_cos[qid];

	/* Make this configurable */
	cmd.cfg.type = (drv_data->frame_format == TG_SHORT)
			   ? TGDN_KERN_AMSDU_TG_SHORT_HEADER
			   : TGDN_KERN_AMSDU_STD_SHORT_HEADER;
	/* High-pri packets get no timeout */
	cmd.cfg.timeout = (qid == FB_TGD_MQ_VO) ? 0 : 1000;
	cmd.cfg.size = 6000;

	respargs.opaque_data = (uintptr_t)dev_priv;

	/* Commit to start the async call */
	fb_tgd_rt_dpaa2_cfg_call_start(dev_priv);

	ret = tgdn_add_amsdu_context(NF_API_CTRL_FLAG_ASYNC, &cmd, &outargs,
				     &respargs);
	if (ret != 0)
		fb_tgd_rt_dpaa2_cfg_call_done(dev_priv, ret);

	ret = fb_tgd_rt_dpaa2_cfg_call_wait(dev_priv);
	if (ret != 0)
		TGD_DBG_CTRL_ERROR(
		    "DPAA2 unable to create A-MSDU context %d for %s qid %d\n",
		    cmd.context_id, netdev_name(dev_priv->dev), qid);
	return ret;
}

static void fb_tgd_rt_dpaa2_del_amsdu_ctx(struct tgd_terra_dev_priv *dev_priv,
					  int cos)
{
	struct tgdn_cfg_del_amsdu_ctxt_inargs cmd;
	struct nf_api_outargs outargs;
	struct nf_api_respargs respargs;
	int32_t ret;

	memset(&cmd, 0, sizeof(cmd));
	memset(&outargs, 0, sizeof(outargs));
	memset(&respargs, 0, sizeof(respargs));

	cmd.context_id =
	    dev_priv->dev_index * dev_priv->dev->num_tx_queues + cos;

	respargs.opaque_data = (uintptr_t)dev_priv;

	fb_tgd_rt_dpaa2_cfg_call_start(dev_priv);

	ret = tgdn_del_amsdu_context(NF_API_CTRL_FLAG_ASYNC, &cmd, &outargs,
				     &respargs);
	if (ret != 0)
		fb_tgd_rt_dpaa2_cfg_call_done(dev_priv, ret);

	ret = fb_tgd_rt_dpaa2_cfg_call_wait(dev_priv);
	if (ret != 0) {
		TGD_DBG_CTRL_ERROR(
		    "DPAA2 unable to destroy A-MSDU context %d for %s cos %d\n",
		    cmd.context_id, netdev_name(dev_priv->dev), cos);
	}
}

static int fb_tgd_rt_dpaa2_add_device(struct tgd_terra_dev_priv *dev_priv)
{
	struct fb_tgd_dpaa2_rt_devpriv *rtp;
	int32_t ret, qid;

	rtp = kzalloc(sizeof(*rtp), GFP_KERNEL);
	if (rtp == NULL)
		return -ENOMEM;

	spin_lock_init(&rtp->cfg_lock);
	init_completion(&rtp->cfg_completion);

	tgd_dpaa2_set_priv(dev_priv, rtp);

	/* Create A-MSDU context for each transmit queue */
	for (qid = 0; qid < dev_priv->dev->num_tx_queues; qid++) {
		ret = fb_tgd_rt_dpaa2_add_amsdu_ctx(dev_priv, qid);
		if (ret != 0)
			goto fail;
	}

	return 0;
fail:
	/* Delete all successfully created A-MSDU contexts */
	for (qid = qid - 1; qid >= 0; qid--)
		fb_tgd_rt_dpaa2_del_amsdu_ctx(dev_priv, qid);
	return ret;
}

static void fb_tgd_rt_dpaa2_del_device(struct tgd_terra_dev_priv *dev_priv)
{
	struct fb_tgd_dpaa2_rt_devpriv *rtp;
	int32_t cos;

	rtp = tgd_dpaa2_get_priv(dev_priv);
	if (rtp == NULL)
		return;

	/* Delete A-MSDU contexts */
	for (cos = 0; cos < dev_priv->dev->num_tx_queues; cos++) {
		fb_tgd_rt_dpaa2_del_amsdu_ctx(dev_priv, cos);
	}

	tgd_dpaa2_set_priv(dev_priv, NULL);
	kfree(rtp);
}

static void fb_tgd_rt_dpaa2_set_link_state(struct tgd_terra_dev_priv *dev_priv,
					   tgLinkStatus state)
{
}

static void fb_tgd_rt_dpaa2_rx(struct tgd_terra_dev_priv *dev_priv,
			       struct sk_buff *skb)
{
	struct tgdn_amsdu_rx_meta mdata;
	struct ethhdr *ehdr;
	int ret;

	ehdr = (struct ethhdr *)skb->data;

	/* Check inline packet type and convert it to metadata */
	if (ehdr->h_proto == htons(ETH_P_TGAMSDU))
		mdata.type = TGDN_AMSDU_TG_SHORT_FF;
	else if (ehdr->h_proto == htons(ETH_P_TGSTDAMSDU))
		mdata.type = TGDN_AMSDU_STD_SHORT_FF;
	else
		mdata.type = TGDN_MSDU_FF;
	mdata.in_ifid = dev_priv->dev->ifindex;

	ret = amsdu_packet_rx_handler(skb, &mdata);
	if (ret != 0) { /* Fall back to software decode */
		netdev_err(dev_priv->dev, "Packet rx_handler error %d\n", ret);
	}
}

static bool tgd_dpaa_handle_local = false;
module_param(tgd_dpaa_handle_local, bool, 0644);

static void fb_tgd_rt_dpaa2_tx(struct tgd_terra_dev_priv *dev_priv,
			       struct sk_buff *skb)
{
	struct tgdn_amsdu_rx_meta mdata;
	struct ethhdr *ehdr;
	int ret;

	if (unlikely(!tgd_dpaa_handle_local)) {
		tgd_terra_bh_tx_common(dev_priv, skb);
		return;
	}

	ehdr = (struct ethhdr *)skb->data;

	/* Check inline packet type and convert it to metadata */
	if (unlikely(ehdr->h_proto == htons(ETH_P_TGAMSDU) ||
		     ehdr->h_proto == htons(ETH_P_TGSTDAMSDU))) {
		tgd_terra_bh_tx_common(dev_priv, skb);
	} else {
		mdata.type = TGDN_LOCAL_OUT_FF;
		mdata.in_ifid = dev_priv->dev->ifindex;

		ret = amsdu_packet_rx_handler(skb, &mdata);
		if (ret != 0) { /* Fall back to software decode */
			netdev_err(dev_priv->dev,
				   "Packet local rx_handler error %d\n", ret);
		}
	}
}

static void fb_tgd_rt_dpaa2_flow_control(struct tgd_terra_dev_priv *dev_priv,
					 unsigned char qid, bool state)
{
	struct tgdn_cfg_set_fc_req_inargs cmd;
	struct nf_api_outargs outargs;
	struct nf_api_respargs respargs;
	int32_t ret;

	cmd.ifid = dev_priv->dev->ifindex;
	cmd.cos = qid_to_cos[qid];
	cmd.fc_enable = state;

	TGD_ASSERT(cmd.cos >= MIN_COS && cmd.cos <= MAX_COS);

	memset(&outargs, 0, sizeof(outargs));
	memset(&respargs, 0, sizeof(respargs));

	ret = tgdn_set_flowcontrol(NF_API_CTRL_FLAG_ASYNC |
				       NF_API_CTRL_FLAG_NO_RESP_EXPECTED,
				   &cmd, &outargs, &respargs);
	if (ret != 0) {
		TGD_DBG_CTRL_ERROR(
		    "DPAA2 unable to %s flow control for %s qos %u\n",
		    state ? "enable" : "disable", netdev_name(dev_priv->dev),
		    qid);
	}
}

static void fb_tgd_rt_dpaa2_module_fini(struct tgd_terra_driver *tgd_data)
{
	struct fb_tgd_dpaa2_rt_backend *rtn;

	if (tgd_data->rt_backend == NULL)
		return;

	rtn = container_of(tgd_data->rt_backend, struct fb_tgd_dpaa2_rt_backend,
			   rt_base);
	tgd_data->rt_backend = NULL;

	/* Unregister various callbacks */
	if (atomic_dec_and_test(&tgd_dpaa2_instance_count)) {
		tgdn_cos_register_cb(NULL);
		tgdn_amsdu_context_register_cb(NULL);
		tgdn_fc_register_cb(NULL);
		amsdu_packet_tx_register(NULL);
	}

	/* Free the backend */
	kfree(rtn);
}

static int32_t fb_tgd_rt_dpaa2_packet_tx(struct sk_buff *skb,
					 amsdu_tx_meta *meta)
{
	struct net_device *out_dev;
	uint32_t ifid;
	uint8_t cos;
#ifdef TG_ENABLE_PFIFOFC
	int err;
#endif

	ifid = ntohl(meta->out_ifid);
	out_dev = dev_get_by_index(&init_net, ifid);
	if (out_dev == NULL)
		goto drop;
	/* Truncate CoS value to supported range */
	cos = max_t(uint8_t, meta->cos, MIN_COS);
	cos = min_t(uint8_t, meta->cos, MAX_COS);
	/* Assign priority and queue info */
	skb->priority = cos_to_prio[cos - 1].prio;

#ifdef TG_ENABLE_PFIFOFC
	if (likely(tgd_enable_pfifofc)) {
		// Send packet to qdisc to enqueue for transmission
		skb->dev = out_dev;
		dev_put(out_dev);
		if ((err = dev_queue_xmit(skb)) != 0) {
			TGD_DBG_DATA_ERROR(
			    "%s: ifid=%d dev_xmit error=%d skb=%p prio=%d\n",
			    netdev_name(out_dev), ifid, err, skb,
			    skb->priority);
		}
		return 0;
	}
#endif /* TG_ENABLE_PFIFOFC */
	skb_set_queue_mapping(skb, cos_to_prio[cos - 1].qid);
	tgd_terra_bh_tx_common(netdev_priv(out_dev), skb);
	dev_put(out_dev);
	return 0;

drop:
	kfree_skb(skb);
	return -1;
}

int fb_tgd_rt_dpaa2_module_init(struct tgd_terra_driver *tgd_data)
{
	struct fb_tgd_dpaa2_rt_backend *rtn;
	struct fb_tgd_routing_backend *rtb;
	void *fptr;
	int ret;

	fptr = &amsdu_packet_tx_register;
	if (fptr == NULL) {
		TGD_DBG_CTRL_ERROR(
		    "DPAA2 packet offload support module is not found\n");
		return -ENOTSUPP;
	}

	/* Allocate the backend */
	rtn = kzalloc(sizeof(*rtn), GFP_KERNEL);
	if (rtn == NULL) {
		return -ENOMEM;
	}

	/* Keep track of number of instances that called init */
	atomic_inc(&tgd_dpaa2_instance_count);

	/* Do register our callbacks with vendor driver */
	ret = amsdu_packet_tx_register(fb_tgd_rt_dpaa2_packet_tx);
	if (ret != 0)
		goto fail;

	ret = tgdn_fc_register_cb(fb_tgd_resp_callback);
	if (ret != 0)
		goto fail;
	ret = tgdn_amsdu_context_register_cb(fb_tgd_resp_callback);
	if (ret != 0)
		goto fail;
	ret = tgdn_cos_register_cb(fb_tgd_resp_callback);
	if (ret != 0)
		goto fail;

	tgd_data->rt_backend = rtb = &rtn->rt_base;

	/* Populate the method table */
	rtb->rt_mod_fini = fb_tgd_rt_dpaa2_module_fini;
	rtb->rt_add_dev = fb_tgd_rt_dpaa2_add_device;
	rtb->rt_del_dev = fb_tgd_rt_dpaa2_del_device;
	rtb->rt_set_link_state = fb_tgd_rt_dpaa2_set_link_state;
	rtb->rt_flow_control = fb_tgd_rt_dpaa2_flow_control;
	rtb->rt_tx = fb_tgd_rt_dpaa2_tx;
	rtb->rt_rx = fb_tgd_rt_dpaa2_rx;

	return 0;
fail:
	fb_tgd_rt_dpaa2_module_fini(tgd_data);
	return ret;
}
