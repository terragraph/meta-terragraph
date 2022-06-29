/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * tgcfg_api.c
 */
#include <tgcfg/tgcfg.h>
#include <vnet/unix/tapcli.h>

#include <vnet/vnet.h>
#include <vlibapi/api.h>
#include <vlibmemory/api.h>

/* define message IDs */
#include <tgcfg/tgcfg_msg_enum.h>

#define vl_typedefs /* define message structures */
#include <tgcfg/tgcfg.api.h>
#undef vl_typedefs

#define vl_endianfun /* define message structures */
#include <tgcfg/tgcfg.api.h>
#undef vl_endianfun

/* instantiate all the print functions we know about */
#define vl_print(handle, ...) vlib_cli_output (handle, __VA_ARGS__)
#define vl_printfun
#include <tgcfg/tgcfg.api.h>
#undef vl_printfun

/* Get the API version number */
#define vl_api_version(n, v) static u32 api_version = (v);
#include <tgcfg/tgcfg.api.h>
#undef vl_api_version

#include <vlibapi/api_helper_macros.h>

#define foreach_tgcfg_api_msg _ (INTERFACE_MAP_DUMP, interface_map_dump)

typedef struct
{
  u16 msg_id_base;
} tgcfg_api_main_t;

static tgcfg_api_main_t tgcfg_api_main;

#define TGCFG_MSG_BASE tgcfg_api_main.msg_id_base

static void send_interface_map_details (vl_api_registration_t *rp, u32 context,
                                        u32 sw_if_index, const char *tap_name)
{
  vl_api_interface_map_details_t *mp = vl_msg_api_alloc (sizeof (*mp));
  clib_memset (mp, 0, sizeof (*mp));
  mp->_vl_msg_id = ntohs (TGCFG_MSG_BASE + VL_API_INTERFACE_MAP_DETAILS);
  mp->context = context;

  mp->sw_if_index = ntohl (sw_if_index);
  strncpy ((char *)mp->linux_tap_name, tap_name,
           ARRAY_LEN (mp->linux_tap_name) - 1);

  vl_api_send_msg (rp, (u8 *)mp);
}

static void
vl_api_interface_map_dump_t_handler (vl_api_interface_map_dump_t *mp)
{
  tgcfg_main_t *tm = &tgcfg_main;

  vl_api_registration_t *rp;
  rp = vl_api_client_index_to_registration (mp->client_index);

  if (rp == 0)
    {
      clib_warning ("Client %d AWOL", mp->client_index);
      return;
    }
  tapcli_interface_details_t *tapifs = NULL, *ti = NULL;
  vnet_tap_dump_ifs (&tapifs);

  vec_foreach (ti, tapifs)
  {
    tgcfg_wired_t *wl;
    vec_foreach (wl, tm->wired_links)
    {
      if (wl->tap_sw_if_index == ti->sw_if_index)
        {
          send_interface_map_details (rp, mp->context, wl->eth_sw_if_index,
                                      (char *)ti->dev_name);
          break;
        }
    }
  }

  vec_free (tapifs);
}

#define vl_msg_name_crc_list
#include <tgcfg/tgcfg.api.h>
#undef vl_msg_name_crc_list

static void setup_message_id_table (api_main_t *am)
{
#define _(id, n, crc) \
  vl_msg_api_add_msg_name_crc (am, #n "_" #crc, id + TGCFG_MSG_BASE);
  foreach_vl_msg_name_crc_tgcfg;
#undef _
}

static clib_error_t *tgcfg_api_hookup (vlib_main_t *vm)
{
  api_main_t *am = vlibapi_get_main ();
  u8 *name = format (0, "tgcfg_%08x%c", api_version, 0);

  /* Ask for a correctly-sized block of API message decode slots */
  tgcfg_api_main.msg_id_base =
      vl_msg_api_get_msg_ids ((char *)name, VL_MSG_FIRST_AVAILABLE);

#define _(N, n)                                                         \
  vl_msg_api_set_handlers (VL_API_##N + TGCFG_MSG_BASE, #n,             \
                           vl_api_##n##_t_handler, vl_noop_handler,     \
                           vl_api_##n##_t_endian, vl_api_##n##_t_print, \
                           sizeof (vl_api_##n##_t), 1);
  foreach_tgcfg_api_msg;
#undef _

  setup_message_id_table (am);

  vec_free (name);
  return 0;
}

VLIB_API_INIT_FUNCTION (tgcfg_api_hookup);
