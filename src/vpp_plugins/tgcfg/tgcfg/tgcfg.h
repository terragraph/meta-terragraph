/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * tgcfg.h - Terragraph config plugin
 */
#ifndef __included_tgcfg_h__
#define __included_tgcfg_h__

#include <rte_common.h>
#include <rte_mbuf.h>
#include <rte_mbuf_dyn.h>

#include <vnet/ethernet/ethernet.h>
#include <vnet/ip/ip.h>
#include <vnet/vnet.h>

#include <vlib/unix/unix.h>

#include <vppinfra/error.h>
#include <vppinfra/hash.h>

#include <rte_wigig_api.h>

/* This dynamic field is registered in the wil6210 pmd for storing the
 * Terragraph link id for each packet. */
#define WIGIG_LINK_ID_DYNFIELD_NAME "wil6210_dynfield_link_id"
/* Offset of link id dynfield in an rte_mbuf */
extern int wigig_link_id_dynfield_offset;

/* Get the link id from a packet dynfield */
static inline u16 wigig_mbuf_link_id_get (const struct rte_mbuf *mbuf)
{
  return *RTE_MBUF_DYNFIELD (mbuf, wigig_link_id_dynfield_offset, u16 *);
}

/* Set the link id in the dynfield of a packet */
static inline void wigig_mbuf_link_id_set (const struct rte_mbuf *mbuf,
                                           u16 link)
{
  *RTE_MBUF_DYNFIELD (mbuf, wigig_link_id_dynfield_offset, u16 *) = link;
}

typedef struct
{
  u32 bb_sw_if_index;
  u32 tg_sw_if_index;
  u32 tg_peer_id;
} tgcfg_link_t;

typedef struct
{
  u32 eth_sw_if_index;
  u32 tap_sw_if_index;
} tgcfg_wired_t;

typedef struct
{
  struct rte_wigig_dev_info di;
  u32 sw_if_index;
  void *dev;
  bool rx_ready;
  u32 clib_file_index;
  int wdev_index;
} tgcfg_wdev_t;

typedef struct
{
  char *vpp_name;
  char *tap_name;
  bool wired_security_enable;
  bool ipv6_slowpath_enable;
} tgcfg_slowpath_map_t;

typedef struct
{
  /* convenience */
  vlib_main_t *vlib_main;
  vnet_main_t *vnet_main;
  /* configuration switches */
  bool auto_probe;
  bool slowpath_enable;
  bool wsec_enable;
  char *host_iface_name;
  /* private API from DPDK plugin */
  const struct rte_wigig_ops *wigig_ops;
  /* dpdk API for rte mbuf dynfield lookup */
  int (*dynfield_lookup) (const char *name, struct rte_mbuf_dynfield *params);
  /* links */
  tgcfg_link_t *terra_links;
  /* wigig info */
  tgcfg_wdev_t *wigig_devs;
  /* wired */
  tgcfg_wired_t *wired_links;
  /* interfaces intercepting local traffic */
  u32 *local_links;
  /* slowpath tap device mappings */
  tgcfg_slowpath_map_t *slowpath_maps;
  /* Primary host interface */
  u32 host_sw_if_index;
  /* Loopback interface */
  u32 loop_sw_if_index;
  /* logging */
  vlib_log_class_t log_default;
  /* ULA prefix used for VPP internal test addresses */
  ip6_address_t ula_test_prefix;
} tgcfg_main_t;

extern tgcfg_main_t tgcfg_main;

#define tgcfg_log_err(...) \
  vlib_log (VLIB_LOG_LEVEL_ERR, tgcfg_main.log_default, __VA_ARGS__)
#define tgcfg_log_warn(...) \
  vlib_log (VLIB_LOG_LEVEL_WARNING, tgcfg_main.log_default, __VA_ARGS__)
#define tgcfg_log_notice(...) \
  vlib_log (VLIB_LOG_LEVEL_NOTICE, tgcfg_main.log_default, __VA_ARGS__)
#define tgcfg_log_info(...) \
  vlib_log (VLIB_LOG_LEVEL_INFO, tgcfg_main.log_default, __VA_ARGS__)

clib_error_t *tg_link_local_rx_fd_read_ready (clib_file_t *uf);

tgcfg_wdev_t *tg_get_wdev_by_sw_if_index (u32 sw_if_index);
int tg_get_wdev_index_by_port_id (u32 port_id);
vnet_sw_interface_t *tg_get_link_if_by_dev_instance (u32 dev_instance);

/* Copy of macros used by DPDK plugin */
#define rte_mbuf_from_vlib_buffer(x) (((struct rte_mbuf *)x) - 1)
#define vlib_buffer_from_rte_mbuf(x) ((vlib_buffer_t *)(x + 1))

clib_error_t *tgcfg_setup_host_interface (void);

#endif /* __included_tgcfg_h__ */
