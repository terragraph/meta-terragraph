/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * ptptc.h - PTP-TC common definitions
 */
#ifndef __included_ptptc_h__
#define __included_ptptc_h__
#include <stddef.h>

#include <vnet/vnet.h>
#include <vnet/ip/ip.h>
#include <vnet/ip/ip6_inlines.h>

#include "dpaa2_wriop.h"
#include "gps_sync.h"

/*need to undefine the always_inline macro before including any
of the rte libraries*/
#undef always_inline
#include <rte_mbuf.h>
#include <rte_mbuf_dyn.h>

/* redefining the always_inline macro */
#if CLIB_DEBUG > 0
#define always_inline static inline
#else
#define always_inline static inline __attribute__ ((__always_inline__))
#endif

#ifndef container_of
#define container_of(ptr, type, member)                 \
  ({                                                    \
    typeof (((type *)0)->member) (*__mptr) = (ptr);     \
    (type *)((char *)__mptr - offsetof (type, member)); \
  })
#endif

#define ptptc_debug(...) \
  vlib_log (VLIB_LOG_LEVEL_DEBUG, ptptc_main.log_default, __VA_ARGS__)

extern int ptptc_timestamp_dynfield_offset;
extern u64 ptptc_timestamp_dynflag;

typedef struct
{
  /* convenience */
  vlib_main_t *vlib_main;
  vnet_main_t *vnet_main;
  vlib_log_class_t log_default;

  /* hack to work inside VLIB_CONFIG_FUNCTION */
  char *enable_sw_if_name;

  /* dpmac(s) for output (vector, nulled after config is set) */
  int *ports;

  /* whether to use hardware timestamping (false = software timestamping) */
  bool use_hw_timestamping;

  /* offset added to correction field, e.g. external cable delays */
  int timing_offset;

  /* local clock - gps clock, e.g. crystal precision */
  int clk_offset_ppb;

  /* hardware register mapping */
  struct dpaa2_wriop_reg *wriop_regs;

  /* state for GPS clock sync */
  struct gps_main gm;

  /* interface output feature arc index */
  u32 egress_index;

  /* enabled RX offload checksum capability by the driver */
  bool rx_checksum_offload_capa;

  /* enables use of ptp classfication bit in the ol_flag */
  bool rx_ptp_classify_offload_capa;

  /* dpdk API for rte mbuf timestamp dynfield register/lookup */
  int (*dyn_rx_timestamp_register) (int *field_offset, u64 *rx_flag);
} ptptc_main_t;

static inline ptptc_main_t *gps_main_to_pmp (struct gps_main *ptr)
{
  return container_of (ptr, ptptc_main_t, gm);
}

extern ptptc_main_t ptptc_main;

#endif /* __included_ptptc_h__ */
