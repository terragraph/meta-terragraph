/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
/**
 * @file
 * @brief ESMC Plugin, plugin API / trace / CLI handling.
 */

#include <vnet/vnet.h>
#include <vnet/plugin/plugin.h>
#include <esmc/esmc.h>

#include <vlibapi/api.h>
#include <vlibmemory/api.h>

/* define message IDs */
#include <esmc/esmc_msg_enum.h>

/* define message structures */
#define vl_typedefs
#include <esmc/esmc_all_api_h.h>
#undef vl_typedefs

/* define generated endian-swappers */
#define vl_endianfun
#include <esmc/esmc_all_api_h.h>
#undef vl_endianfun

/* instantiate all the print functions we know about */
#define vl_print(handle, ...) vlib_cli_output (handle, __VA_ARGS__)
#define vl_printfun
#include <esmc/esmc_all_api_h.h>
#undef vl_printfun

/* Get the API version number */
#define vl_api_version(n, v) static u32 api_version = (v);
#include <esmc/esmc_all_api_h.h>
#undef vl_api_version

#define REPLY_MSG_ID_BASE em->msg_id_base
#include <vlibapi/api_helper_macros.h>

/* List of message types that this plugin understands */

#define foreach_esmc_plugin_api_msg \
  _ (ESMC_ENABLE_DISABLE, esmc_enable_disable)

/* *INDENT-OFF* */
VLIB_PLUGIN_REGISTER () = {
    .version = ESMC_PLUGIN_BUILD_VER,
    .description = "SyncE ESMC Plugin",
};
/* *INDENT-ON* */

esmc_main_t esmc_main;

/**
 * @brief Enable/disable the ESMC plugin.
 *
 * Action function shared between message handler and debug CLI.
 */

int esmc_enable_disable (esmc_main_t *em, int enable_disable,
                         u32 input_sw_if_index, u32 output_sw_if_index,
                         u8 enable_tg_input, u8 enable_tg_output)
{
  if (enable_disable)
    {
      esmc_log_notice ("Enabling ESMC plugin");
      em->enabled = 1;
      if (em->if_rx_state)
        {
          vec_free (em->if_rx_state);
        }
      em->if_rx_state = 0;
      em->selected_sw_if_index = ~0;
      em->programmed_sw_if_index = -1;
      em->ssm = ESMC_SSM_QL_UNUSED;
      em->pll_mode = ~0;
      em->pll_locked = 0;
      em->num_tx_pdu_1s = 0;
      em->input_sw_if_index = input_sw_if_index;
      em->output_sw_if_index = output_sw_if_index;
      em->input_sw_if_name = NULL;
      em->output_sw_if_name = NULL;
      em->enable_tg_input = enable_tg_input;
      em->enable_tg_output = enable_tg_output;
    }
  else
    {
      esmc_log_notice ("Disabling ESMC plugin");
      em->enabled = 0;
    }

  return 0;
}

static clib_error_t *esmc_enable_disable_command_fn (vlib_main_t *vm,
                                                     unformat_input_t *input,
                                                     vlib_cli_command_t *cmd)
{
  esmc_main_t *em = &esmc_main;
  u8 enable_disable = ~0;
  u8 enable_tg_input = 0, enable_tg_output = 0;
  u32 input_sw_if_index = ~0, output_sw_if_index = ~0;

  int rv;

  if (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "disable"))
        enable_disable = 0;
      else if (unformat (input, "enable"))
        enable_disable = 1;
      else
        return clib_error_return (0, "Expecting 'enable' or 'disable'");
    }

  if (enable_disable == (u8)~0)
    return clib_error_return (0, "Expecting 'enable' or 'disable'");

  if (enable_disable)
    {
      while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
        {
          if (unformat (input, "input %U", unformat_vnet_sw_interface,
                        em->vnet_main, &input_sw_if_index))
            ;
          else if (unformat (input, "output %U", unformat_vnet_sw_interface,
                             em->vnet_main, &output_sw_if_index))
            ;
          else if (unformat (input, "enable-tg-input"))
            enable_tg_input = 1;
          else if (unformat (input, "enable-tg-output"))
            enable_tg_output = 1;
          else
            return clib_error_return (0, "unknown input `%U'",
                                      format_unformat_error, input);
        }
    }

  rv = esmc_enable_disable (em, enable_disable, input_sw_if_index,
                            output_sw_if_index, enable_tg_input,
                            enable_tg_output);

  switch (rv)
    {
    case 0:
      break;

    case VNET_API_ERROR_UNIMPLEMENTED:
      return clib_error_return (0,
                                "Device driver doesn't support redirection");
      break;

    default:
      return clib_error_return (0, "esmc_enable_disable returned %d", rv);
    }
  return 0;
}

/**
 * @brief CLI command to enable/disable the ESMC plugin.
 */
VLIB_CLI_COMMAND (esmc_enable_disable_command, static) = {
    .path = "esmc",
    .short_help = "esmc enable [input <interface>] [output <interface>] "
                  "[enable-tg-input] [enable-tg-output] | disable",
    .function = esmc_enable_disable_command_fn,
};

/**
 * @brief Plugin API message handler.
 */
static void
vl_api_esmc_enable_disable_t_handler (vl_api_esmc_enable_disable_t *mp)
{
  vl_api_esmc_enable_disable_reply_t *rmp;
  esmc_main_t *em = &esmc_main;
  int rv;

  rv = esmc_enable_disable (em, (int)(mp->enable_disable),
                            mp->input_sw_if_index, mp->output_sw_if_index,
                            mp->enable_tg_input, mp->enable_tg_output);

  REPLY_MACRO (VL_API_ESMC_ENABLE_DISABLE_REPLY);
}

/**
 * @brief Set up the API message handling tables.
 */
static clib_error_t *esmc_plugin_api_hookup (vlib_main_t *vm)
{
  esmc_main_t *em = &esmc_main;
#define _(N, n)                                                         \
  vl_msg_api_set_handlers ((VL_API_##N + em->msg_id_base), #n,          \
                           vl_api_##n##_t_handler, vl_noop_handler,     \
                           vl_api_##n##_t_endian, vl_api_##n##_t_print, \
                           sizeof (vl_api_##n##_t), 1);
  foreach_esmc_plugin_api_msg;
#undef _

  return 0;
}

#define vl_msg_name_crc_list
#include <esmc/esmc_all_api_h.h>
#undef vl_msg_name_crc_list

static void setup_message_id_table (esmc_main_t *em, api_main_t *am)
{
#define _(id, n, crc) \
  vl_msg_api_add_msg_name_crc (am, #n "_" #crc, id + em->msg_id_base);
  foreach_vl_msg_name_crc_esmc;
#undef _
}

static clib_error_t *esmc_config (vlib_main_t *vm, unformat_input_t *input)
{
  esmc_main_t *em = &esmc_main;
  clib_error_t *error = 0;

  /* config defaults */
  em->enabled = 0;
  em->input_sw_if_index = ~0;
  em->output_sw_if_index = ~0;
  em->input_sw_if_name = NULL;
  em->output_sw_if_name = NULL;
  em->enable_tg_input = 0;
  em->enable_tg_output = 0;

  /* ESMC field initialization */
  em->if_rx_state = 0;
  em->selected_sw_if_index = ~0;
  em->programmed_sw_if_index = -1;
  em->ssm = ESMC_SSM_QL_UNUSED;
  em->pll_mode = ~0;
  em->pll_locked = 0;
  em->num_tx_pdu_1s = 0;

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "on"))
        em->enabled = 1;
      /* Cannot use unformat_vnet_sw_interface because this happens before the
       * interfaces are created */
      else if (unformat (input, "input %s", &em->input_sw_if_name))
        ;
      else if (unformat (input, "output %s", &em->output_sw_if_name))
        ;
      else if (unformat (input, "enable-tg-input"))
        em->enable_tg_input = 1;
      else if (unformat (input, "enable-tg-output"))
        em->enable_tg_output = 1;
      else
        return clib_error_return (0, "unknown input `%U'",
                                  format_unformat_error, input);
    }

  return error;
}

VLIB_CONFIG_FUNCTION (esmc_config, "esmc");

/**
 * @brief Initialize the ESMC plugin.
 */
static clib_error_t *esmc_init (vlib_main_t *vm)
{
  esmc_main_t *em = &esmc_main;
  clib_error_t *error = 0;
  u8 *name;

  em->vnet_main = vnet_get_main ();
  em->vlib_main = vm;
  em->log_default = vlib_log_register_class ("esmc", 0);
  clib_time_init (&em->clib_time);

  /* API initialization */
  name = format (0, "esmc_%08x%c", api_version, 0);
  /* Ask for a correctly-sized block of API message decode slots */
  em->msg_id_base =
      vl_msg_api_get_msg_ids ((char *)name, VL_MSG_FIRST_AVAILABLE);
  error = esmc_plugin_api_hookup (vm);
  /* Add our API messages to the global name_crc hash table */
  setup_message_id_table (em, vlibapi_get_main ());
  vec_free (name);

  return error;
}

VLIB_INIT_FUNCTION (esmc_init);
