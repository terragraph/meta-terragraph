/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * tgcfg_main.c - Terragraph config plugin
 */
#include <tgcfg/tgcfg.h>
#include <vnet/plugin/plugin.h>
#include <vnet/vnet.h>

#include <vlibapi/api.h>
#include <vlibmemory/api.h>

tgcfg_main_t tgcfg_main;
int wigig_link_id_dynfield_offset = -1;

tgcfg_wdev_t *tg_get_wdev_by_sw_if_index (u32 sw_if_index)
{
  tgcfg_main_t *tm = &tgcfg_main;
  int i;

  for (i = 0; i < vec_len (tm->wigig_devs); i++)
    if (tm->wigig_devs[i].sw_if_index == sw_if_index)
      return &tm->wigig_devs[i];
  return NULL;
}

int tg_get_wdev_index_by_port_id (u32 port_id)
{
  tgcfg_main_t *tm = &tgcfg_main;
  int i;

  for (i = 0; i < vec_len (tm->wigig_devs); i++)
    if (tm->wigig_devs[i].di.port_id == port_id)
      return i;
  return -1;
}

vnet_sw_interface_t *tg_get_link_if_by_dev_instance (u32 dev_instance)
{
  tgcfg_main_t *tm = &tgcfg_main;

  if (dev_instance < vec_len (tm->terra_links) &&
      tm->terra_links[dev_instance].tg_sw_if_index != ~0)
    return vnet_get_sw_interface (
        tm->vnet_main, tm->terra_links[dev_instance].tg_sw_if_index);
  else
    return NULL;
}

static uword unformat_boolean (unformat_input_t *input, va_list *args)
{
  bool *result = va_arg (*args, bool *);

  if (unformat (input, "on"))
    *result = true;
  else if (unformat (input, "off"))
    *result = false;
  else
    return 0;

  return 1;
}

static clib_error_t *tgcfg_interface_config (tgcfg_main_t *tm,
                                             tgcfg_slowpath_map_t *sp,
                                             unformat_input_t *input)
{
  sp->tap_name = NULL;
  sp->wired_security_enable = false;
  sp->ipv6_slowpath_enable = true;

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "tap %s", &sp->tap_name))
        {
        }
      else if (unformat (input, "ipv6 slowpath %U", unformat_boolean,
                         &sp->ipv6_slowpath_enable))
        {
        }
      else if (unformat (input, "wired security %U", unformat_boolean,
                         &sp->wired_security_enable))
        {
        }
      else
        return clib_error_return (0, "unknown input `%U'",
                                  format_unformat_error, input);
    }

  if (!sp->tap_name)
    return clib_error_return (0, "no tap name provided for `%s'",
                              sp->vpp_name);

  return NULL;
}

static clib_error_t *tgcfg_config (vlib_main_t *vm, unformat_input_t *input)
{
  tgcfg_main_t *tm = &tgcfg_main;
  clib_error_t *error = 0;
  tgcfg_slowpath_map_t sp;
  unformat_input_t sub_input;
  u32 prefix_len = 0;

  /* Defaults */
  tm->auto_probe = true;
  tm->slowpath_enable = true;
  tm->wsec_enable = false;

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "auto-probe %U", unformat_boolean, &tm->auto_probe))
        {
        }
      else if (unformat (input, "slowpath %U", unformat_boolean,
                         &tm->slowpath_enable))
        {
        }
      else if (unformat (input, "wsec %U", unformat_boolean, &tm->wsec_enable))
        {
        }
      else if (unformat (input, "host interface %s", &tm->host_iface_name))
        {
        }
      /* Cannot use unformat_vnet_sw_interface because this happens before the
       * interfaces are created */
      else if (unformat (input, "interface %s %U", &sp.vpp_name,
                         unformat_vlib_cli_sub_input, &sub_input))
        {
          error = tgcfg_interface_config (tm, &sp, &sub_input);
          if (error)
            return error;

          vec_add1 (tm->slowpath_maps, sp);
        }
      else if (unformat (input, "ula-test-prefix %U/%u", unformat_ip6_address,
                         &tm->ula_test_prefix, &prefix_len))
        {
          if (prefix_len != 64 ||
              tm->ula_test_prefix.as_u16[0] != ntohs (0xfd00))
            return clib_error_return (0, "ula-test-prefix must be in "
                                         "the form fd00:xxxx:xxxx:xxxx/64");
        }
      else
        return clib_error_return (0, "unknown input `%U'",
                                  format_unformat_error, input);
    }

  if (tm->host_iface_name != NULL)
    error = tgcfg_setup_host_interface ();

  return error;
}

VLIB_CONFIG_FUNCTION (tgcfg_config, "terragraph");

static clib_error_t *tgcfg_init (vlib_main_t *vm)
{
  rte_wigig_get_ops_t *get_ops;
  tgcfg_main_t *tm = &tgcfg_main;

  tm->vlib_main = vm;
  tm->vnet_main = vnet_get_main ();
  tm->host_sw_if_index = ~0u;
  tm->ula_test_prefix.as_u64[0] = tm->ula_test_prefix.as_u64[1] = 0;

  get_ops = vlib_get_plugin_symbol ("dpdk_plugin.so", "rte_wigig_get_ops");
  if (get_ops == NULL)
    return (clib_error_return (0, "Unable to bind to DPDK plugin"));
  tm->wigig_ops = get_ops ();
  if (tm->wigig_ops == NULL)
    return (clib_error_return (0, "No wigig_ops"));

  tm->dynfield_lookup =
      vlib_get_plugin_symbol ("dpdk_plugin.so", "rte_mbuf_dynfield_lookup");
  if (tm->dynfield_lookup == NULL)
    return (clib_error_return (
        0,
        "Failed to get symbol rte_mbuf_dynfield_lookup from dpdk_plugin.so"));
  tm->log_default = vlib_log_register_class ("tgcfg", 0);

  return NULL;
}

VLIB_INIT_FUNCTION (tgcfg_init);

static clib_error_t *tgcfg_main_loop_enter (vlib_main_t *vm)
{
  tgcfg_main_t *tm = &tgcfg_main;

  /* Get the mbuf dynfield offset for storing link id metadata.
   * This needs to be happen after after the driver has loaded, which happens
   * in dpdk's config function rte_eal_init() */
  wigig_link_id_dynfield_offset =
      tm->dynfield_lookup (WIGIG_LINK_ID_DYNFIELD_NAME, NULL);
  if (wigig_link_id_dynfield_offset < 0)
    return clib_error_return (0, "Unable to find wigig link id dynfield");

  return NULL;
}

VLIB_MAIN_LOOP_ENTER_FUNCTION (tgcfg_main_loop_enter);

static clib_error_t *tgcfg_exit (vlib_main_t *vm)
{
  tgcfg_main_t *tm = &tgcfg_main;
  int i;

  for (i = 0; i < vec_len (tm->wigig_devs); i++)
    vnet_sw_interface_set_flags (tm->vnet_main, tm->wigig_devs[i].sw_if_index,
                                 0 /* down */);
  return 0;
}

VLIB_MAIN_LOOP_EXIT_FUNCTION (tgcfg_exit);

VLIB_PLUGIN_REGISTER () = {
    .version = "1.0",
    .description = "Terragraph Extensions",
};
