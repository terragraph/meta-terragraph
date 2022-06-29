/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/* Trivial software-only Terragraph A-MSDU implementation */

#ifndef TGD_AMSDU_H
#define TGD_AMSDU_H

/*
 * Ethernet proto value for Terragraph AMSDU frames.
 */
#define ETH_P_TGAMSDU 0x89FB
#define ETH_P_TGSTDAMSDU 0x89FC

struct sk_buff;
struct sk_buff_head;

void tgd_amsdu_encapsulate(struct sk_buff *skb);
int tgd_amsdu_decapsulate(struct sk_buff *skb, struct sk_buff_head *list);

#endif
