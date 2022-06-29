/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Trivial software-only Terragraph A-MSDU implementation */

#define pr_fmt(fmt) "fb_tgd_terragraph: " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>

#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/skbuff.h>

#include "fb_tgd_amsdu.h"

struct tg_amsdu_hdr {
	__u8 agg_type;
	__u8 agg_ctx;
	__u8 agg_cnt;
	__u8 reserved[3];
};

void tgd_amsdu_encapsulate(struct sk_buff *skb)
{
	struct tg_amsdu_hdr *msduhdr;
	struct ethhdr *ehdr, *new_ehdr;
	int copy_size = offsetof(struct ethhdr, h_proto);

	ehdr = eth_hdr(skb);

	/* Do not encapsulate EAPOL frames */
	if (ehdr->h_proto == htons(ETH_P_PAE))
		return;

	/* Make space for AMSDU header */
	new_ehdr = (struct ethhdr *)skb_push(skb, 8);

	/* Copy DA + SA to new location */
	memcpy(new_ehdr, ehdr, copy_size);
	/* Set the frame type to TG AMSDU */
	new_ehdr->h_proto = htons(ETH_P_TGAMSDU);

	msduhdr = (struct tg_amsdu_hdr *)(skb->data + sizeof(*new_ehdr));
	msduhdr->agg_type = 0; /* type = short */
	msduhdr->agg_ctx = 6;  /* NSS header context id 0x006 */
	msduhdr->agg_cnt = 1;  /* only one sub-frame */
	memset(msduhdr->reserved, 0, sizeof(msduhdr->reserved));
}

int tgd_amsdu_decapsulate(struct sk_buff *skb, struct sk_buff_head *list)
{
	struct tg_amsdu_hdr *msduhdr;
	const struct ethhdr *ehdr;
	__be16 *sub_len;
	void *payload;
	unsigned int count;

	/* Get the ethernet header */
	ehdr = (struct ethhdr *)skb->data;
	/* Get the amsdu header */
	msduhdr = (struct tg_amsdu_hdr *)skb_pull(skb, sizeof(*ehdr));
	if (msduhdr == NULL)
		goto out;
	/* Get the subframe lengths table */
	sub_len = (__be16 *)skb_pull(skb, sizeof(*msduhdr));
	if (sub_len == NULL)
		goto out;
	/* Skip to the first subframe payload */
	payload = skb_pull(skb, (msduhdr->agg_cnt - 1) << 1);
	if (payload == NULL)
		goto out;

	count = msduhdr->agg_cnt;
	while (count > 0) {
		struct sk_buff *frame;
		unsigned int subframe_len;

		/* Fetch subframe length from sub_len table */
		if (--count > 0)
			subframe_len = ntohs(*sub_len++);
		else
			subframe_len = skb->len;

		/* Validate subframe length */
		if (subframe_len < 2 || subframe_len > skb->len)
			goto purge;

		/* Use original skb for the last frame */
		if (count == 0) {
			frame = skb;
			/* Handle overlapping header data with memmove */
			memmove(
			    skb_push(frame, offsetof(struct ethhdr, h_proto)),
			    ehdr, offsetof(struct ethhdr, h_proto));
		} else {
			/*
			 * Allocate space for for subframe + full ethernet
			 * header + extra two bytes for 4 byte alignment,
			 * since ehdr is 14 bytes in size. There are two
			 * bytes in the payload itself.
			 */
			frame =
			    dev_alloc_skb(subframe_len + sizeof(struct ethhdr));
			if (frame == NULL)
				goto purge;

			/* Reserve space for headers sans h_proto */
			skb_reserve(frame, sizeof(struct ethhdr));

			/* Move the data */
			memcpy(skb_put(frame, subframe_len), skb->data,
			       subframe_len);
			memcpy(
			    skb_push(frame, offsetof(struct ethhdr, h_proto)),
			    ehdr, offsetof(struct ethhdr, h_proto));

			/* Skip the payload in original skb */
			payload = skb_pull(skb, subframe_len);
			if (payload == NULL) {
				dev_kfree_skb(frame);
				goto purge;
			}
		}

		frame->dev = skb->dev;
		frame->priority = skb->priority;

		__skb_queue_tail(list, frame);
	}

	return 0;

purge:
	__skb_queue_purge(list);
out:
	dev_kfree_skb(skb);
	return -1;
}
