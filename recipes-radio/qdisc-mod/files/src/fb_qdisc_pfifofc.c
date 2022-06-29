/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * fb_qdisc_pfifofc.c	 4-band  strict priority "scheduler"
 * with two level flow control signaling to the netdev for each band.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/sch_generic.h>
#include <fb_tg_qdisc_pfifofc_if.h>

/*
 * Default values for the maximum queue length, and the
 * queue lengths for flow control signaling for the different
 * flow control levels. The current implementation supports
 * two levels of flow control signaling via a callback.
 */
// Default value for max queue length for each priority band
#define PFIFOFC_QLEN (640)
// Default value for hysteresis between turning Flow Control OFF from ON
// for each Flow Control level.
#define PFIFOFC_FC_HYST (80)
// Default value for signaling flow control ON for RED colored packets
#define PFIFOFC_QLEN_RED_ON (320)
// Default value for signaling flow control OFF for RED colored packets
#define PFIFOFC_QLEN_RED_OFF (PFIFOFC_QLEN_RED_ON - PFIFOFC_FC_HYST)
// Default value for signaling flow control ON for ALL packets
#define PFIFOFC_QLEN_ALL_ON (520)
// Default value for signaling flow control OFF for ALL packets
#define PFIFOFC_QLEN_ALL_OFF (PFIFOFC_QLEN_ALL_ON - PFIFOFC_FC_HYST)

/*
 * Mapping from priority to one of the 4 bands.
 * The 4 bands are in descending order of priority. 0 is the highest priority
 * and 3 is the least priority.
 */
static const u8 prio2fourband[TC_PRIO_MAX + 1] = {2, 3, 3, 2, 1, 1, 0, 0,
						  1, 1, 1, 1, 1, 1, 1, 1};

/*
 *  Below are the details of the flow control state settings.
 *
 *         PFIFOFC_QLEN_RED_OFF      PFIFOFC_QLEN_ALL_OFF
 *               |                          |
 *               v                          v
 * --------------*--------*-----------------*--------*------------------*
 *                        ^                          ^                  ^
 *                        |                          |                  |
 *            PFIFOFC_QLEN_RED_ON        PFIFOFC_QLEN_ALL_ON   PFIFOFC_QLEN
 *
 * Enqueue logic:
 * -------------
 * If qlen >= PFIFOFC_QLEN_RED_ON, and qlen < PFIFOFC_QLEN_ALL_OFF, set flow
 * control state to RED_ON.
 * Do not change flow control state between the queue lengths
 * PFIFOFC_QLEN_ALL_OFF and PFIFOFC_QLEN_ALL_ON. It can be either RED_ON or
 * ALL_ON depending on the last enq/deq operation leading to that interval.
 * If qlen >= PFIFOFC_QLEN_ALL_ON, set state to ALL_ON.
 *
 * Dequeue logic:
 * --------------
 * If qlen <= PFIFOFC_QLEN_ALL_OFF and qlen >= PFIFOFC_QLEN_RED_ON, set to
 * RED_ON. Do not change flow control state between PFIFOFC_QLEN_RED_ON
 * and PFIFOFC_QLEN_RED_OFF. It can be RED_ON (if deq operation lead to that
 * interval, and ALL_OFF if enq operation lead to that interval).
 * If qlen <= PFIFOFC_QLEN_RED_OFF, set state to ALL_OFF.
 */

/*
 * Currently only 100% drop is supported at each level
 */
#define DROP_PROB 100

struct pfifofc_priv {
	u8 prio2band[TC_PRIO_MAX + 1];
	u16 tx_prio_queue_len;
	u16 tx_qlen_red_on;
	u16 tx_qlen_red_off;
	u16 tx_qlen_all_on;
	u16 tx_qlen_all_off;
	/*
	 * Flow control levels are maintained per prio queue.
	 */
	enum tgd_pfifofc_fc_level fc[PFIFOFC_BANDS];

	struct qdisc_skb_head q[PFIFOFC_BANDS];

	struct tgd_pfifofc_band_stats bstats[PFIFOFC_BANDS];
	/*
	 * Call back function the netdev can register to receive notifications
	 * for the flow control.
	 */
	void (*dev_fc_cb)(struct net_device *dev, int color, int prob,
			  int priority);
};

static inline struct qdisc_skb_head *band2list(struct pfifofc_priv *priv,
					       int band)
{
	return priv->q + band;
}

/*
 * Set the flow control state for the band queue.
 * Also send the flow control signal to the netdev, if registered.
 */
static inline void pfifofc_signal_fc(struct Qdisc *qdisc, int band, int color,
				     u32 priority, u32 len)
{
	struct pfifofc_priv *priv = qdisc_priv(qdisc);
	struct net_device *dev = qdisc_dev(qdisc);
	int prob = DROP_PROB;

	priv->fc[band] = color;
	if (priv->dev_fc_cb != NULL)
		priv->dev_fc_cb(dev, color, prob, priority);
}

static int pfifofc_enqueue(struct sk_buff *skb, struct Qdisc *qdisc,
			   struct sk_buff **to_free)
{

	int band = prio2fourband[skb->priority & TC_PRIO_MAX];
	struct pfifofc_priv *priv = qdisc_priv(qdisc);
	struct qdisc_skb_head *list = band2list(priv, band);
	int qlen = list->qlen;

	if (qlen < priv->tx_prio_queue_len) {
		if (qlen >= priv->tx_qlen_red_on) {
			if ((qlen < priv->tx_qlen_all_off) &&
			    (priv->fc[band] != RED_ON)) {
				pfifofc_signal_fc(qdisc, band, RED_ON,
						  skb->priority, qlen);
			} else if ((qlen >= priv->tx_qlen_all_on) &&
				   (priv->fc[band] != ALL_ON)) {
				pfifofc_signal_fc(qdisc, band, ALL_ON,
						  skb->priority, qlen);
			}
		}
		qdisc->q.qlen++;
		priv->bstats[band].total_pkts++;
		return __qdisc_enqueue_tail(skb, qdisc, list);
	}

	priv->bstats[band].dropped_pkts++;
	return qdisc_drop(skb, qdisc, to_free);
}

static struct sk_buff *pfifofc_dequeue(struct Qdisc *qdisc)
{
	struct pfifofc_priv *priv = qdisc_priv(qdisc);
	int band = 0;
	struct sk_buff *skb = NULL;
	struct qdisc_skb_head *list;

	do {
		list = band2list(priv, band);
		skb = __qdisc_dequeue_head(list);
	} while (!skb && (++band < PFIFOFC_BANDS));

	if (likely(skb)) {
		int qlen = list->qlen;
		qdisc->q.qlen--;
		qdisc_qstats_backlog_dec(qdisc, skb);
		qdisc_bstats_update(qdisc, skb);
		if ((qlen <= priv->tx_qlen_red_off) &&
		    (priv->fc[band] != ALL_OFF)) {
			pfifofc_signal_fc(qdisc, band, ALL_OFF, skb->priority,
					  qlen);
		} else if ((qlen >= priv->tx_qlen_red_on) &&
			   (qlen <= priv->tx_qlen_all_off) &&
			   (priv->fc[band] != RED_ON)) {
			pfifofc_signal_fc(qdisc, band, RED_ON, skb->priority,
					  qlen);
		}
	}
	return skb;
}

static struct sk_buff *pfifofc_peek(struct Qdisc *qdisc)
{
	struct pfifofc_priv *priv = qdisc_priv(qdisc);
	int band;
	struct sk_buff *skb = NULL;
	struct qdisc_skb_head *list;

	for (band = 0; band < PFIFOFC_BANDS && !skb; band++) {
		list = band2list(priv, band);
		skb = list->head;
	}
	return skb;
}

static void pfifofc_reset(struct Qdisc *qdisc)
{
	int band;
	struct pfifofc_priv *priv = qdisc_priv(qdisc);

	for (band = 0; band < PFIFOFC_BANDS; band++) {
		__qdisc_reset_queue(band2list(priv, band));
		priv->fc[band] = ALL_OFF;
	}

	qdisc->qstats.backlog = 0;
	qdisc->q.qlen = 0;
}

static int pfifofc_tune(struct Qdisc *qdisc, struct nlattr *opt)
{
	struct pfifofc_priv *priv = qdisc_priv(qdisc);
	struct tgd_pfifofc_qopt *qopt;

	if (nla_len(opt) < sizeof(*qopt))
		return -EINVAL;

	qopt = nla_data(opt);

	priv->tx_prio_queue_len = (u16)qopt->max_queue_len;
	priv->tx_qlen_red_on = (u16)qopt->qlen_red_on;
	priv->tx_qlen_red_off = (u16)qopt->qlen_red_off;
	priv->tx_qlen_all_on = (u16)qopt->qlen_all_on;
	priv->tx_qlen_all_off = (u16)qopt->qlen_all_off;
	return 0;
}

static int pfifofc_init(struct Qdisc *qdisc, struct nlattr *opt)
{
	struct pfifofc_priv *priv = qdisc_priv(qdisc);
	int band;
	int ret;

	if (opt) {
		/* The default values have been input. */
		ret = pfifofc_tune(qdisc, opt);
		if (ret != 0)
			return ret;
	} else {
		/* Use the system default initial values. */
		priv->tx_prio_queue_len = PFIFOFC_QLEN;
		priv->tx_qlen_red_on = PFIFOFC_QLEN_RED_ON;
		priv->tx_qlen_red_off = PFIFOFC_QLEN_RED_OFF;
		priv->tx_qlen_all_on = PFIFOFC_QLEN_ALL_ON;
		priv->tx_qlen_all_off = PFIFOFC_QLEN_ALL_OFF;
	}

	priv->dev_fc_cb = NULL;

	for (band = 0; band < PFIFOFC_BANDS; band++) {
		qdisc_skb_head_init(band2list(priv, band));
		priv->fc[band] = ALL_OFF;
	}

	return 0;
}

static int pfifofc_dump(struct Qdisc *qdisc, struct sk_buff *skb)
{
	struct pfifofc_priv *priv = qdisc_priv(qdisc);
	struct tgd_pfifofc_qopt opt;

	unsigned char *b = skb_tail_pointer(skb);

	opt.max_queue_len = priv->tx_prio_queue_len;
	opt.qlen_red_on = priv->tx_qlen_red_on;
	opt.qlen_red_off = priv->tx_qlen_red_off;
	opt.qlen_all_on = priv->tx_qlen_all_on;
	opt.qlen_all_off = priv->tx_qlen_all_off;

	if (nla_put(skb, TCA_OPTIONS, sizeof(opt), &opt))
		goto nla_put_failure;

	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

void pfifofc_dump_stats(struct Qdisc *qdisc, struct tgd_pfifofc_stats *st)
{
	struct pfifofc_priv *priv = qdisc_priv(qdisc);
	int band;
	struct qdisc_skb_head *list;

	spin_lock_bh(qdisc_lock(qdisc));
	for (band = 0; band < PFIFOFC_BANDS; band++) {
		list = band2list(priv, band);
		st->bstats[band].cur_pkts = list->qlen;
		st->bstats[band].dropped_pkts = priv->bstats[band].dropped_pkts;
		st->bstats[band].total_pkts = priv->bstats[band].total_pkts;
	}
	st->total_cur_bytes = qdisc->qstats.backlog;
	st->total_cur_packets = qdisc->q.qlen;
	spin_unlock_bh(qdisc_lock(qdisc));

	return;
}
EXPORT_SYMBOL(pfifofc_dump_stats);

void qdisc_dev_register_flow_control_cb(struct Qdisc *qdisc, void *fn_ptr,
					struct tgd_pfifofc_qopt *tune)
{
	struct pfifofc_priv *priv = qdisc_priv(qdisc);
	priv->dev_fc_cb = fn_ptr;
	if (tune != NULL) {
		priv->tx_prio_queue_len = (u16)tune->max_queue_len;
		priv->tx_qlen_red_on = (u16)tune->qlen_red_on;
		priv->tx_qlen_red_off = (u16)tune->qlen_red_off;
		priv->tx_qlen_all_on = (u16)tune->qlen_all_on;
		priv->tx_qlen_all_off = (u16)tune->qlen_all_off;
	}
}
EXPORT_SYMBOL(qdisc_dev_register_flow_control_cb);

struct Qdisc_ops pfifofc_qdisc_ops __read_mostly = {
    .next = NULL,
    .id = "pfifofc",
    .priv_size = sizeof(struct pfifofc_priv),
    .enqueue = pfifofc_enqueue,
    .dequeue = pfifofc_dequeue,
    .peek = pfifofc_peek,
    .init = pfifofc_init,
    .reset = pfifofc_reset,
    .dump = pfifofc_dump,
    .owner = THIS_MODULE,
};
EXPORT_SYMBOL(pfifofc_qdisc_ops);

static int __init pfifofc_module_init(void)
{
	return register_qdisc(&pfifofc_qdisc_ops);
}

static void __exit pfifofc_module_exit(void)
{
	unregister_qdisc(&pfifofc_qdisc_ops);
}

module_init(pfifofc_module_init) module_exit(pfifofc_module_exit)

MODULE_LICENSE("Dual MIT/GPL");
