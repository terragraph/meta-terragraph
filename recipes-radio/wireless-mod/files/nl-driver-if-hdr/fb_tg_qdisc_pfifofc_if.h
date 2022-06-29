/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * Interface file between the terragraph driver and the pfifofc
 * qdisc module.
*/

#ifndef _fb_tg_qdisc_pfifofc_if_h_
#define _fb_tg_qdisc_pfifofc_if_h_

#include <linux/types.h>
#include <net/sch_generic.h>

extern struct Qdisc_ops pfifofc_qdisc_ops;

#define PFIFOFC_BANDS 4

/*
 * Tunable user options
 */
struct tgd_pfifofc_qopt {
  unsigned int max_queue_len; /* Maximum packets per priority queue */
  unsigned int qlen_red_on; /* Queue length to Flow control ON for RED packets
                             */
  unsigned int qlen_red_off; /* Queue length to Flow control OFF for RED packets
                              */
  unsigned int qlen_all_on; /* Queue length to Flow control ON for all packets
                             */
  unsigned int qlen_all_off; /* Queue length to Flow control OFF for all packets
                              */
};

/*
 * FLow Control levels
 */
enum tgd_pfifofc_fc_level { ALL_OFF = 0, RED_ON = 1, ALL_ON = 2 };

/*
 * Stats maintained per band.
 */
struct tgd_pfifofc_band_stats {
  unsigned long long total_pkts; /* total number of packets enqueued. */
  unsigned long long dropped_pkts; /* number of packets dropped due to the queue
                                being full. */
  unsigned int cur_pkts; /* current packets in the band's queue */
};

/*
 * Stats maintained per qdisc.
 */
struct tgd_pfifofc_stats {
  struct tgd_pfifofc_band_stats bstats[PFIFOFC_BANDS];
  unsigned int total_cur_packets;
  unsigned int total_cur_bytes;
};

/*
 * dump the stats.
 */
void pfifofc_dump_stats(struct Qdisc* qdisc, struct tgd_pfifofc_stats* st);

/*
 * Callback function to be registered by the netdev for callbacks for
 * flow control.
 */
void qdisc_dev_register_flow_control_cb(
    struct Qdisc* qdisc, void* fn_ptr, struct tgd_pfifofc_qopt* tune);

#endif /* _fb_tg_qdisc_pfifofc_if_h_ */
