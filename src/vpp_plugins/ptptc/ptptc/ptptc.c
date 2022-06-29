/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * ptptc.c - PTP Transparent Clock
 */
#include <ptptc/ptptc.h>
#include <vnet/plugin/plugin.h>

int ptptc_timestamp_dynfield_offset = -1;
u64 ptptc_timestamp_dynflag;

static clib_error_t *ptptc_port_enable_single_step (int port)
{
  uint16_t offset = 70; /* 40 (ipv6) + 8 (udp) + 14 (mac) + 8 correction */
  struct dpaa2_wriop_reg *reg =
      dpaa2_wriop_reg_init (WRIOP_PORT_ADDR (port), WRIOP_PORT_LEN);
  if (!reg)
    return clib_error_return (0, "Could not map regs for port %d", port);

  ptptc_debug ("Enabling single step update on port %d", port);
  dpaa2_set_single_step (reg, true /* enable */, offset, true /* checksum */);
  dpaa2_wriop_reg_uninit (reg);
  return 0;
}

ptptc_main_t ptptc_main;
static clib_error_t *ptptc_port_command_fn (vlib_main_t *vm,
                                            unformat_input_t *input,
                                            vlib_cli_command_t *cmd)
{
  ptptc_main_t *pmp = &ptptc_main;
  int port;

  /* enable single step update in hardware */
  if (unformat (input, "%d", &port))
    {
      clib_error_t *error = ptptc_port_enable_single_step (port);
      if (error)
        return error;

      pmp->use_hw_timestamping = true;
    }

  return 0;
}

VLIB_CLI_COMMAND (ptptc_port_command, static) = {
    .path = "ptptc port",
    .short_help = "ptptc port <port-id, e.g. 8 for dpmac.8, 0 for SW>",
    .function = ptptc_port_command_fn,
};

static clib_error_t *ptptc_offset_command_fn (vlib_main_t *vm,
                                              unformat_input_t *input,
                                              vlib_cli_command_t *cmd)
{
  ptptc_main_t *pmp = &ptptc_main;
  int x;

  if (unformat (input, "%d", &x))
    pmp->timing_offset = x;

  return 0;
}

VLIB_CLI_COMMAND (ptptc_offset_command, static) = {
    .path = "ptptc offset",
    .short_help = "ptptc offset <fixed offset, ns>",
    .function = ptptc_offset_command_fn,
};

static clib_error_t *ptptc_clk_offset_command_fn (vlib_main_t *vm,
                                                  unformat_input_t *input,
                                                  vlib_cli_command_t *cmd)
{
  ptptc_main_t *pmp = &ptptc_main;
  int x;

  if (unformat (input, "%d", &x))
    pmp->clk_offset_ppb = x;

  return 0;
}

VLIB_CLI_COMMAND (ptptc_clk_offset_command, static) = {
    .path = "ptptc clk_offset",
    .short_help = "ptptc clk_offset <clock offset, ppb>",
    .function = ptptc_clk_offset_command_fn,
};

int ptptc_enable_disable (ptptc_main_t *pmp, u32 sw_if_index,
                          int enable_disable)
{
  vnet_sw_interface_t *sw;
  int rv = 0;

  /* Utterly wrong? */
  if (pool_is_free_index (pmp->vnet_main->interface_main.sw_interfaces,
                          sw_if_index))
    return VNET_API_ERROR_INVALID_SW_IF_INDEX;

  /* Not a physical port? */
  sw = vnet_get_sw_interface (pmp->vnet_main, sw_if_index);
  if (sw->type != VNET_SW_INTERFACE_TYPE_HARDWARE)
    return VNET_API_ERROR_INVALID_SW_IF_INDEX;

  vnet_feature_enable_disable ("ip6-unicast", "ptptc", sw_if_index,
                               enable_disable, 0, 0);

  vnet_feature_enable_disable ("interface-output", "ptptc", sw_if_index,
                               enable_disable, 0, 0);
  pmp->egress_index = vnet_get_feature_arc_index ("interface-output");

  return rv;
}

static clib_error_t *ptptc_enable_disable_wrapper (ptptc_main_t *pmp,
                                                   u32 sw_if_index,
                                                   int enable_disable)
{
  int rv = ptptc_enable_disable (pmp, sw_if_index, enable_disable);
  switch (rv)
    {
    case 0:
      break;

    case VNET_API_ERROR_INVALID_SW_IF_INDEX:
      return clib_error_return (
          0, "Invalid interface, only works on physical ports");
      break;

    case VNET_API_ERROR_UNIMPLEMENTED:
      return clib_error_return (0,
                                "Device driver doesn't support redirection");
      break;

    default:
      return clib_error_return (0, "ptptc_enable_disable returned %d", rv);
    }
  return 0;
}

static clib_error_t *ptptc_enable_disable_command_fn (vlib_main_t *vm,
                                                      unformat_input_t *input,
                                                      vlib_cli_command_t *cmd)
{
  ptptc_main_t *pmp = &ptptc_main;
  u32 sw_if_index = ~0;
  int enable_disable = 1;
  int use_gps_sync = 0;

  int rv;
  pmp->rx_checksum_offload_capa = false;
  pmp->rx_ptp_classify_offload_capa = false;
  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "disable"))
        enable_disable = 0;
      else if (unformat (input, "gps-sync"))
        use_gps_sync = 1;
      else if (unformat (input, "rx-checksum-offload"))
        pmp->rx_checksum_offload_capa = true;
      else if (unformat (input, "rx-ptp-classify-offload"))
        pmp->rx_ptp_classify_offload_capa = true;
      else if (unformat (input, "%U", unformat_vnet_sw_interface,
                         pmp->vnet_main, &sw_if_index))
        ;
      else
        break;
    }

  if (sw_if_index == ~0)
    return clib_error_return (0, "Please specify an interface...");

  /* turn on/off GPS clock correction */
  if (use_gps_sync)
    {
      if (enable_disable)
        {
          rv = gps_sync_enable (&pmp->gm);
          if (rv < 0)
            clib_warning ("Could not connect to gpsd. GPS sync is disabled!");
        }
      else
        {
          gps_sync_disable (&pmp->gm);
        }
    }

  return ptptc_enable_disable_wrapper (pmp, sw_if_index, enable_disable);
}

/**
 * @brief CLI command to enable/disable the PTP-TC node on a specified
 * interface rx/tx flow.
 */
VLIB_CLI_COMMAND (ptptc_enable_disable_command, static) = {
    .path = "ptptc enable-disable",
    .short_help = "ptptc enable-disable <interface-name> [gps-sync] "
                  "[rx-checksum-offload] [rx-ptp-classify-offload] [disable]",
    .function = ptptc_enable_disable_command_fn,
};

/*
 * Hook into interface creation path and enable ptptc node on the configured
 * interface.
 */
static clib_error_t *ptptc_interface_add_del_function (vnet_main_t *vnm,
                                                       u32 sw_if_index,
                                                       u32 is_add)
{
  ptptc_main_t *pmp = &ptptc_main;
  vnet_hw_interface_t *hw;
  clib_error_t *error = 0;
  int *port;

  /* Tear down is not supported yet */
  if (!is_add)
    return NULL;

  /* Did we configure ptptc auto-enable? */
  if (!pmp->enable_sw_if_name)
    return NULL;

  /* Is this the configured sw_if_index? */
  hw = vnet_get_sup_hw_interface (vnm, sw_if_index);
  if (strncmp ((const char *)hw->name, pmp->enable_sw_if_name,
               strlen (pmp->enable_sw_if_name)))
    return NULL;

  vec_free (pmp->enable_sw_if_name); /* no need to keep checking */

  /* Configure port(s) */
  if (pmp->ports)
    {
      vec_foreach (port, pmp->ports)
      {
        error = ptptc_port_enable_single_step (*port);
        if (error)
          break;
      }
      vec_free (pmp->ports);
      if (error)
        return error;

      pmp->use_hw_timestamping = true;
    }

  /* Enable ptptc */
  ptptc_debug ("Enabling ptptc on sw_if_index %d (%s)", hw->sw_if_index,
               (const char *)hw->name);
  return ptptc_enable_disable_wrapper (pmp, hw->sw_if_index, true);
}

VNET_SW_INTERFACE_ADD_DEL_FUNCTION (ptptc_interface_add_del_function);

static clib_error_t *ptptc_config (vlib_main_t *vm, unformat_input_t *input)
{
  ptptc_main_t *pmp = &ptptc_main;
  int i;

  /* config defaults */
  pmp->enable_sw_if_name = NULL;
  pmp->ports = NULL;
  pmp->use_hw_timestamping = false;
  pmp->timing_offset = 0;
  pmp->clk_offset_ppb = 0;
  pmp->rx_checksum_offload_capa = false;
  pmp->rx_ptp_classify_offload_capa = false;
  pmp->egress_index = 0;

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      /* Cannot use unformat_vnet_sw_interface because this happens before the
       * interfaces are created */
      if (unformat (input, "interface %s", &pmp->enable_sw_if_name))
        ;
      else if (unformat (input, "port %d", &i))
        vec_add1 (pmp->ports, i);
      else if (unformat (input, "offset-ns %d", &i))
        pmp->timing_offset = i;
      else if (unformat (input, "clk-offset-ppb %d", &i))
        pmp->clk_offset_ppb = i;
      else if (unformat (input, "rx-checksum-offload"))
        pmp->rx_checksum_offload_capa = true;
      else if (unformat (input, "rx-ptp-classify-offload"))
        pmp->rx_ptp_classify_offload_capa = true;
      else
        return clib_error_return (0, "unknown input `%U'",
                                  format_unformat_error, input);
    }

  return 0;
}

VLIB_CONFIG_FUNCTION (ptptc_config, "ptptc");

static clib_error_t *ptptc_init (vlib_main_t *vm)
{
  ptptc_main_t *pmp = &ptptc_main;

  pmp->vlib_main = vm;
  pmp->vnet_main = vnet_get_main ();
  pmp->wriop_regs = dpaa2_wriop_reg_init (WRIOP_GLOBAL_ADDR, WRIOP_GLOBAL_LEN);
  pmp->log_default = vlib_log_register_class ("ptptc", 0);
  memset (&pmp->gm, 0, sizeof (pmp->gm));

  pmp->enable_sw_if_name = NULL;
  pmp->ports = NULL;
  pmp->use_hw_timestamping = false;
  pmp->timing_offset = 0;
  pmp->clk_offset_ppb = 0;

  pmp->dyn_rx_timestamp_register = vlib_get_plugin_symbol (
      "dpdk_plugin.so", "rte_mbuf_dyn_rx_timestamp_register");
  if (pmp->dyn_rx_timestamp_register == NULL)
    return clib_error_return (
        0, "Failed to get symbol rte_mbuf_dyn_rx_timestamp_register from "
           "dpdk_plugin.so");

  return 0;
}

static clib_error_t *ptptpc_main_loop_enter (vlib_main_t *vm)
{
  ptptc_main_t *pmp = &ptptc_main;
  int ret;
  /* Get the mbuf timestamp dynfield offset.
   * This needs to happen after rte_eal_init in dpdk's config function. */
  ret = pmp->dyn_rx_timestamp_register (&ptptc_timestamp_dynfield_offset,
                                        &ptptc_timestamp_dynflag);
  if (ret != 0)
    return clib_error_return (
        0, "Failed to register rx timestamp dynfield: %d", ret);
  return 0;
}

VLIB_MAIN_LOOP_ENTER_FUNCTION (ptptpc_main_loop_enter);

VLIB_INIT_FUNCTION (ptptc_init);

VNET_FEATURE_INIT (ptptc6_input, static) = {
    .arc_name = "ip6-unicast",
    .node_name = "ptptc",
};

VNET_FEATURE_INIT (ptptc_output, static) = {
    .arc_name = "interface-output",
    .node_name = "ptptc",
};

VLIB_PLUGIN_REGISTER () = {
    .version = "1.0",
    .description = "Precision Timing Protocol Transparent Clock",
};
