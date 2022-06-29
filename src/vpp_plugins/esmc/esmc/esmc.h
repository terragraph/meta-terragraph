/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifndef __included_esmc_h__
#define __included_esmc_h__

#include <vnet/vnet.h>
#include <vnet/ip/ip.h>
#include <vnet/ethernet/ethernet.h>

#include <vppinfra/hash.h>
#include <vppinfra/error.h>
#include <vppinfra/elog.h>

/* Logging */
#define esmc_log_err(...) \
  vlib_log (VLIB_LOG_LEVEL_ERR, esmc_main.log_default, __VA_ARGS__)
#define esmc_log_warn(...) \
  vlib_log (VLIB_LOG_LEVEL_WARNING, esmc_main.log_default, __VA_ARGS__)
#define esmc_log_notice(...) \
  vlib_log (VLIB_LOG_LEVEL_NOTICE, esmc_main.log_default, __VA_ARGS__)
#define esmc_log_info(...) \
  vlib_log (VLIB_LOG_LEVEL_INFO, esmc_main.log_default, __VA_ARGS__)
#define esmc_log_debug(...) \
  vlib_log (VLIB_LOG_LEVEL_DEBUG, esmc_main.log_default, __VA_ARGS__)

/* ESMC message timeout (in seconds) after which to trigger holdover mode */
#define ESMC_TIMEOUT_SEC 5

/* Unused SSM code in option 1 networks (used here for uninitialized values) */
#define ESMC_SSM_QL_UNUSED 0x0
/* SSM code for QL-DNU (Do Not Use) in option 1 networks */
#define ESMC_SSM_QL_DNU 0xf

/* TG interface prefix */
#define TG_INTERFACE_PREFIX "vpp-terra"
#define TG_INTERFACE_PREFIX_LEN 9

typedef struct
{
  /* SSM (synchronization status message) QL (quality level) */
  u8 ssm;

  /* last received ESMC message time, as returned by clib_time_now() */
  f64 last_esmc_ts;
} esmc_if_state_t;

typedef struct
{
  /* API message ID base */
  u16 msg_id_base;

  /* convenience */
  vnet_main_t *vnet_main;
  vlib_main_t *vlib_main;

  /* logging */
  vlib_log_class_t log_default;

  /* timing */
  clib_time_t clib_time;

  /* feature enabled? */
  u8 enabled;

  /* config options */
  u32 input_sw_if_index;
  u32 output_sw_if_index;
  /* hack to work inside VLIB_CONFIG_FUNCTION */
  char *input_sw_if_name;
  char *output_sw_if_name;
  u8 enable_tg_input;
  u8 enable_tg_output;

  /* per-interface ESMC rx state (vec indexed by sw_if_index) */
  esmc_if_state_t *if_rx_state;
  /* sw_if_index with best SSM */
  u32 selected_sw_if_index;
  /* sw_if_index programmed to PLL (-1 in some cases) */
  int programmed_sw_if_index;
  /* current SSM from selected_if_index */
  u8 ssm;
  /* PLL chip mode */
  u8 pll_mode;
  /* Whether PLL has reported the current mode/interface is locked */
  u8 pll_locked;
  /* number of generated/broadcasted PDUs in this 1-second interval */
  u8 num_tx_pdu_1s;
} esmc_main_t;

extern esmc_main_t esmc_main;

extern vlib_node_registration_t esmc_input;
extern vlib_node_registration_t esmc_process;

#define ESMC_PLUGIN_BUILD_VER "1.0"

#endif /* __included_esmc_h__ */
