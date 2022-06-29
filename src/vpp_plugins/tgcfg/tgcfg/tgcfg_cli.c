/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * tgcfg_cli.c - Terragraph plugin command line handlers
 */
#include <rte_common.h>
#include <rte_mbuf.h>

#include <tgcfg/tgcfg.h>
#include <vnet/plugin/plugin.h>
#include <vnet/vnet.h>
#include <vnet/ethernet/ethernet.h>
#include <vnet/ip/ip46_address.h>
#include <vnet/ip/ip_types.h>
#include <vnet/ip/ip6_link.h>
#include <vnet/ip-neighbor/ip_neighbor.h>
#include <vnet/ip6-nd/ip6_ra.h>

#include <vnet/unix/tuntap.h>
#include <vnet/l2/l2_input.h>

#include <vlibapi/api.h>
#include <vlibmemory/api.h>

/*
 * Questionable practice: allow this plugin to hook up directly
 * with the wigig PMD driver running in DPDK plugin.
 */
#include <rte_wigig_api.h>

const static tgcfg_link_t TG_LINK_INVALID = {
    .bb_sw_if_index = ~0,
    .tg_sw_if_index = ~0,
    .tg_peer_id = ~0,
};

const static tgcfg_wired_t TG_WIRED_INVALID = {
    .eth_sw_if_index = ~0,
    .tap_sw_if_index = ~0,
};

typedef struct
{
  u32 tg_sw_if_index;
  u32 bb_sw_if_index;
  u32 bb_peer_id;
} tg_link_tx_trace_t;

/* Node index to be added as next to link TX */
#define TG_LINK_TX_NEXT_INTERFACE_OUTPUT VNET_INTERFACE_TX_N_NEXT

/*
 * Interface instance for each Terragraph link
 */
static uword tg_link_interface_tx (vlib_main_t *vm, vlib_node_runtime_t *node,
                                   vlib_frame_t *frame)
{
  u32 n_left_from, n_pkt, n_left_to_next, n_copy, *from, *to_next;
  u32 next_index = TG_LINK_TX_NEXT_INTERFACE_OUTPUT;
  u32 i, sw_if_index = 0;
  u32 n_pkts = 0, n_bytes = 0;
  vnet_main_t *vnm = vnet_get_main ();
  vnet_hw_interface_t *hw;
  tgcfg_main_t *tm;
  vlib_buffer_t *b;

  n_pkt = n_left_from = frame->n_vectors;
  from = vlib_frame_vector_args (frame);

  tm = &tgcfg_main;
  while (n_left_from > 0)
    {
      struct rte_mbuf *m;

      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      n_copy = clib_min (n_left_from, n_left_to_next);

      clib_memcpy_fast (to_next, from, n_copy * sizeof (from[0]));
      n_left_to_next -= n_copy;
      n_left_from -= n_copy;
      i = 0;
      while (i < n_copy)
        {
          tgcfg_link_t *li;
          u32 n_trace;

          b = vlib_get_buffer (vm, from[i]);

          sw_if_index = vnet_buffer (b)->sw_if_index[VLIB_TX];
          hw = vnet_get_sup_hw_interface (vnm, sw_if_index);

          /* TODO: Validate MAP and drop unknown packets */
          li = vec_elt_at_index (tm->terra_links, hw->dev_instance);
          vnet_buffer (b)->sw_if_index[VLIB_TX] = li->bb_sw_if_index;

          m = rte_mbuf_from_vlib_buffer (b);
          wigig_mbuf_link_id_set (m, li->tg_peer_id);

          /* if a trace was added to this node, mark packets for tracing */
          if (PREDICT_FALSE (n_trace = vlib_get_trace_count (vm, node)))
            vlib_trace_buffer (vm, node, next_index, b, 0);

          if (PREDICT_FALSE (node->flags & VLIB_NODE_FLAG_TRACE || n_trace))
            {
              if (b->flags & VLIB_BUFFER_IS_TRACED)
                {
                  tg_link_tx_trace_t *t0;

                  t0 = vlib_add_trace (vm, node, b, sizeof (t0[0]));
                  t0->tg_sw_if_index = sw_if_index;
                  t0->bb_sw_if_index = li->bb_sw_if_index;
                  t0->bb_peer_id = li->tg_peer_id;

                  if (n_trace)
                    vlib_set_trace_count (vm, node, n_trace - 1);
                }
            }

          i++;
          n_pkts++;
          n_bytes += vlib_buffer_length_in_chain (vm, b);
        }
      from += n_copy;

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return n_pkt - n_left_from;
}

static u8 *format_tg_link_name (u8 *s, va_list *args)
{
  u32 dev_instance = va_arg (*args, u32);
  return format (s, "vpp-terra%d", dev_instance);
}

static u8 *format_tg_link_tx_trace (u8 *s, va_list *args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  CLIB_UNUSED (vnet_main_t * vnm) = vnet_get_main ();
  tg_link_tx_trace_t *t = va_arg (*args, tg_link_tx_trace_t *);

  s = format (s, "  BB sw_if_index %u, peer_id %d", t->bb_sw_if_index,
              t->bb_peer_id);
  s = format (s, ", TG sw_if_index %u", t->tg_sw_if_index);
  return s;
}

static clib_error_t *
tg_link_interface_admin_up_down (vnet_main_t *vnm, u32 hw_if_index, u32 flags)
{
  return /* no error */ 0;
}

/* *INDENT-OFF* */
VNET_DEVICE_CLASS (tg_link_interface_device_class) = {
    .name = "TGLink",
    .format_device_name = format_tg_link_name,
    .format_tx_trace = format_tg_link_tx_trace,
    .tx_function = tg_link_interface_tx,
    .admin_up_down_function = tg_link_interface_admin_up_down,
};
/* *INDENT-ON* */

static void tg_interface_copy_flags (vnet_main_t *vnm, u32 sw_if_index,
                                     u32 flags)
{
  tgcfg_main_t *tm;
  tgcfg_wdev_t *wdev;
  int i;

  tm = &tgcfg_main;

  /* Copy ADMIN_UP flag from wired ports to tap interfaces */
  if (sw_if_index < vec_len (tm->wired_links))
    {
      tgcfg_wired_t *wl = vec_elt_at_index (tm->wired_links, sw_if_index);

      if (wl->tap_sw_if_index != ~0u && wl->tap_sw_if_index != sw_if_index)
        vnet_sw_interface_set_flags (vnm, wl->tap_sw_if_index,
                                     flags & VNET_SW_INTERFACE_FLAG_ADMIN_UP);
    }

  /* Copy ADMIN_UP flag from Wigig port to all link interfaces */
  wdev = tg_get_wdev_by_sw_if_index (sw_if_index);
  if (wdev == NULL)
    return;

  for (i = 0; i < wdev->di.num_links; i++)
    {
      vnet_sw_interface_t *sw;

      sw = tg_get_link_if_by_dev_instance (wdev->di.link[i].if_nameunit);
      if (sw == NULL)
        continue;
      vnet_sw_interface_set_flags (vnm, sw->sw_if_index,
                                   flags & VNET_SW_INTERFACE_FLAG_ADMIN_UP);
    }
}

static void tg_wigig_device_up (tgcfg_wdev_t *wdev)
{
  tgcfg_main_t *tm;
  struct rte_wigig_dev_info wi;
  int rc;

  tm = &tgcfg_main;

  rc = tm->wigig_ops->device_info (wdev->dev, &wi);
  if (rc != 0)
    {
      clib_warning (
          "Failed to get device info after restarting wigig interface");
      return;
    }

  /* Re-enable slowpath packet reading and transmission */
  if (tm->slowpath_enable)
    {
      clib_file_main_t *fm = &file_main;
      clib_file_t template = {0};

      template.read_function = tg_link_local_rx_fd_read_ready;
      template.file_descriptor = wi.data_fd;
      template.description = format (0, "%s", "wigig-local-rx");
      template.private_data = wdev->wdev_index;

      wdev->clib_file_index = clib_file_add (fm, &template);
    }

  memcpy (&wdev->di, &wi, sizeof (wi));
}

/**
 * @brief Callbacks from PMD
 */

static void tg_link_up_handler (u8 *buf)
{
  ip6_address_t ip6_addr;
  ip46_address_t ip46_addr;
  ip_address_t ip_addr;
  mac_address_t mac_addr;
  struct rte_wigig_link_updown_info *data;
  vnet_sw_interface_t *sw;
  tgcfg_main_t *tm;
  tgcfg_link_t *li;

  /* Cast through void to eliminate false alarm alignment warnings */
  data = (struct rte_wigig_link_updown_info *)(void *)buf;

  tm = &tgcfg_main;

  li = vec_elt_at_index (tm->terra_links, data->if_nameunit);
  sw = vnet_get_sw_interface (tm->vnet_main, li->tg_sw_if_index);

  tgcfg_log_info ("Link UP: port %u peer %u\n", data->port_id,
                  data->if_peer_id);
  /* Create the static NDP entry for the peer */
  ip6_link_local_address_from_mac (&ip6_addr, data->if_peer_macaddr);
  ip46_addr = to_ip46 (1, ip6_addr.as_u8);
  ip_address_from_46 (&ip46_addr, FIB_PROTOCOL_IP6, &ip_addr);
  mac_address_from_bytes (&mac_addr, data->if_peer_macaddr);
  ip_neighbor_add (&ip_addr, &mac_addr, li->tg_sw_if_index,
                   IP_NEIGHBOR_FLAG_STATIC, NULL /* TODO: stats_index */);
  vnet_hw_interface_set_flags (tm->vnet_main, sw->hw_if_index,
                               VNET_HW_INTERFACE_FLAG_LINK_UP);

  /* configure link to drop all non-EAPOL packets */
  if (tm->wsec_enable)
    {
      tgcfg_log_info ("Waiting for secure handshake, dropping all non-EAPOL "
                      "packets for terra link %u\n",
                      data->if_nameunit);
      ethernet_set_eapol_only_flag (tm->vnet_main, sw->hw_if_index, 1);
    }
}

static void tg_link_down_handler (u8 *buf)
{
  ip6_address_t ip6_addr;
  ip46_address_t ip46_addr;
  ip_address_t ip_addr;
  struct rte_wigig_link_updown_info *data;
  vnet_sw_interface_t *sw;
  tgcfg_main_t *tm;
  tgcfg_link_t *li;

  /* Cast through void to eliminate false alarm alignment warnings */
  data = (struct rte_wigig_link_updown_info *)(void *)buf;

  tm = &tgcfg_main;

  li = vec_elt_at_index (tm->terra_links, data->if_nameunit);
  sw = vnet_get_sw_interface (tm->vnet_main, li->tg_sw_if_index);

  tgcfg_log_info ("Link DOWN: port %u peer %u\n", data->port_id,
                  data->if_peer_id);
  vnet_hw_interface_set_flags (tm->vnet_main, sw->hw_if_index, 0);
  /* Tear down the static NDP entry for the peer */
  ip6_link_local_address_from_mac (&ip6_addr, data->if_peer_macaddr);
  ip46_addr = to_ip46 (1, ip6_addr.as_u8);
  ip_address_from_46 (&ip46_addr, FIB_PROTOCOL_IP6, &ip_addr);
  ip_neighbor_del (&ip_addr, li->tg_sw_if_index);
}

static void tg_wigig_recovery_handler (u8 *buf)
{
  struct rte_wigig_recovery_info *data;
  tgcfg_main_t *tm;
  tgcfg_wdev_t *wdev;
  int rc, wdev_idx;

  data = (struct rte_wigig_recovery_info *)buf;

  tm = &tgcfg_main;

  clib_warning ("Wigig firmware error, restarting wigig interface: port %u",
                data->port_id);

  wdev_idx = tg_get_wdev_index_by_port_id (data->port_id);
  if (wdev_idx < 0)
    {
      clib_warning ("wdev not found when recovering port_id %u",
                    data->port_id);
      return;
    }

  wdev = &tm->wigig_devs[wdev_idx];

  vnet_sw_interface_set_flags (tm->vnet_main, wdev->sw_if_index, 0 /* down */);
  vnet_sw_interface_set_flags (tm->vnet_main, wdev->sw_if_index,
                               VNET_SW_INTERFACE_FLAG_ADMIN_UP);
}

static void tg_wigig_down_handler (u8 *buf)
{
  struct rte_wigig_recovery_info *data;
  tgcfg_main_t *tm;
  tgcfg_wdev_t *wdev;
  int wdev_idx;

  data = (struct rte_wigig_recovery_info *)buf;

  tm = &tgcfg_main;

  clib_warning ("Wigig firmware error, wigig going down: port %u",
                data->port_id);

  wdev_idx = tg_get_wdev_index_by_port_id (data->port_id);
  if (wdev_idx < 0)
    {
      clib_warning ("wdev not found when doing interface down with port_id %u",
                    data->port_id);
      return;
    }
  wdev = &tm->wigig_devs[wdev_idx];
  vnet_sw_interface_set_flags (tm->vnet_main, wdev->sw_if_index, 0 /* down */);
}

static void tg_link_key_set_handler (u8 *buf)
{
  struct rte_wigig_link_key_set_info *data;
  tgcfg_main_t *tm;
  tgcfg_link_t *li;
  vnet_sw_interface_t *sw;

  data = (struct rte_wigig_link_key_set_info *)buf;

  tm = &tgcfg_main;

  li = vec_elt_at_index (tm->terra_links, data->if_nameunit);
  sw = vnet_get_sw_interface (tm->vnet_main, li->tg_sw_if_index);

  tgcfg_log_info (
      "Link key set for terra link %u, allowing non-EAPOL packets\n",
      data->if_nameunit);

  /* configure link to stop dropping all non-EAPOL packets */
  ethernet_set_eapol_only_flag (tm->vnet_main, sw->hw_if_index, 0);
}

/* VPP forgets to declare this */
void vl_api_force_rpc_call_main_thread (void *fp, u8 *data, u32 data_length);

static void tgcfg_link_up (struct rte_wigig_link_updown_info *data)
{
  /* Process link status in main thread context synchronously */
  vl_api_force_rpc_call_main_thread (tg_link_up_handler, (u8 *)data,
                                     sizeof (*data));
}

static void tgcfg_link_down (struct rte_wigig_link_updown_info *data)
{
  /* Process link status in main thread context synchronously */
  vl_api_force_rpc_call_main_thread (tg_link_down_handler, (u8 *)data,
                                     sizeof (*data));
}

static void tgcfg_wigig_recovery (struct rte_wigig_recovery_info *data)
{
  /* Recover by restarting interface in main thread context synchronously */
  vl_api_force_rpc_call_main_thread (tg_wigig_recovery_handler, (u8 *)data,
                                     sizeof (*data));
}

static void tgcfg_wigig_down (struct rte_wigig_recovery_info *data)
{
  /* Bring wigig interfaces down */
  vl_api_force_rpc_call_main_thread (tg_wigig_down_handler, (u8 *)data,
                                     sizeof (*data));
}

static void tgcfg_link_key_set (struct rte_wigig_link_key_set_info *data)
{
  /* Stop dropping non-EAPOL traffic on this link */
  vl_api_force_rpc_call_main_thread (tg_link_key_set_handler, (u8 *)data,
                                     sizeof (*data));
}

static struct rte_wigig_client_ops tgcfg_client_ops = {
    .link_up = tgcfg_link_up,
    .link_down = tgcfg_link_down,
    .wigig_recovery = tgcfg_wigig_recovery,
    .wigig_down = tgcfg_wigig_down,
    .link_key_set = tgcfg_link_key_set,
};

/**
 * @brief Enable/disable Terragraph extensions on specific BB interface
 *
 */
int tg_interface_enable (tgcfg_main_t *tm, u32 sw_if_index,
                         int enable_slowpath)
{
  tgcfg_wdev_t wdev;
  vnet_main_t *vnm;
  vlib_main_t *vm;
  vnet_sw_interface_t *sw;
  vnet_hw_interface_t *hw, *thw;
  struct rte_wigig_dev_info wi;
  u32 hw_if_index, slot;
  void *wigig_dev;
  clib_error_t *err;
  int i, rc;
  const int enable_disable = 1;

  vnm = tm->vnet_main;
  vm = tm->vlib_main;

  /* Utterly wrong? */
  if (pool_is_free_index (vnm->interface_main.sw_interfaces, sw_if_index))
    return VNET_API_ERROR_INVALID_SW_IF_INDEX;

  /* Not a physical port? */
  sw = vnet_get_sw_interface (vnm, sw_if_index);
  if (sw->type != VNET_SW_INTERFACE_TYPE_HARDWARE)
    return VNET_API_ERROR_INVALID_SW_IF_INDEX;

  /* Not an interface type we recognize? */
  hw = vnet_get_hw_interface (vnm, sw->hw_if_index);
  wigig_dev = tm->wigig_ops->device_lookup (hw->hw_address);
  if (wigig_dev == NULL)
    return VNET_API_ERROR_UNSUPPORTED;

  /* Figure out device names and link ids */
  rc = tm->wigig_ops->device_info (wigig_dev, &wi);
  if (rc != 0)
    return VNET_API_ERROR_UNSUPPORTED;

  /* Subscribe for notifications */
  tm->wigig_ops->set_client_ops (wigig_dev, &tgcfg_client_ops);

  /* Preallocate vector of all links and init them with ~0 */
  if (tm->terra_links == NULL)
    {
      vec_validate_init_empty (tm->terra_links,
                               RTE_WIGIG_MAX_PORTS * RTE_WIGIG_MAX_LINKS,
                               TG_LINK_INVALID);
    }

  /* Create link interfaces */
  for (i = 0; i < wi.num_links; i++)
    {
      struct rte_wigig_link_info *li = &wi.link[i];
      tgcfg_link_t *tl;

      err = ethernet_register_interface (
          vnm, tg_link_interface_device_class.index, li->if_nameunit,
          hw->hw_address, &hw_if_index, NULL);
      (void)err;

      thw = vnet_get_hw_interface (vnm, hw_if_index);

      /* Make sure we have node to forward to in TX path */
      slot = vlib_node_add_named_next_with_slot (
          vm, thw->tx_node_index, "interface-output",
          TG_LINK_TX_NEXT_INTERFACE_OUTPUT);
      ASSERT (slot == TG_LINK_TX_NEXT_INTERFACE_OUTPUT);

      /* Refetch pointer to main interface - the call above has likely
       * invalidated it */
      hw = vnet_get_sup_hw_interface (vnm, sw_if_index);

      /*
       * Copy certain fields from the parent interface
       * TODO: offload flags
       */
      thw->max_packet_bytes = hw->max_packet_bytes;
      thw->max_supported_packet_bytes = hw->max_packet_bytes;

      vnet_hw_interface_set_link_speed (vnm, thw->sw_if_index, hw->link_speed);
      vnet_sw_interface_set_mtu (
          vnm, thw->sw_if_index,
          vnet_sw_interface_get_mtu (vnm, sw_if_index, VNET_MTU_L3));

      /* Populate the map entry */
      tl = vec_elt_at_index (tm->terra_links, li->if_nameunit);
      tl->bb_sw_if_index = sw_if_index;
      tl->tg_sw_if_index = thw->sw_if_index;
      tl->tg_peer_id = li->if_peer_id;

      if (enable_slowpath)
        {
          /* Mark this interface as special */
          vec_validate_init_empty (tm->local_links, thw->sw_if_index, ~0u);
          tm->local_links[thw->sw_if_index] = thw->sw_if_index;

          /* Intercept all local traffic */
          vnet_feature_enable_disable ("ip6-local", "tg-slowpath-terra-rx",
                                       thw->sw_if_index, enable_disable, 0, 0);
          /* Including authentication packets */
          ethernet_register_802_1x_redirect (
              vm, vlib_get_node_by_name (vm, (u8 *)"tg-link-local-tx")->index,
              thw->sw_if_index, 0 /* Wireless */);
        }

      /* Make sure interface can handle all IP traffic */
      ip6_link_enable (thw->sw_if_index, NULL);
      /* Disable router advertisements */
      ip6_ra_config (vm, thw->sw_if_index,
                     /*suppress*/ 1, /*managed*/ 0, /*other*/ 0,
                     /*suppress_ll_option*/ 0, /*send_unicast*/ 0, /*cease*/ 0,
                     /*use_lifetime*/ 0, /*ra_lifetime*/ 0,
                     /*ra_initial_count*/ 0, /*ra_initial_interval*/ 0,
                     /*ra_max_interval*/ 0, /*ra_min_interval*/ 0,
                     /*is_no*/ 0);
      ip4_sw_interface_enable_disable (thw->sw_if_index, 1);

      /* Intercept all unknown L3 traffic (auth?) */
    }

  /*
   * Setup per-wigig information:
   *    allow VPP to accept locally generated packets
   */
  if (enable_slowpath)
    {
      clib_file_main_t *fm = &file_main;
      clib_file_t template = {0};

      template.read_function = tg_link_local_rx_fd_read_ready;
      template.file_descriptor = wi.data_fd;
      template.description = format (0, "%s", "wigig-local-rx");
      template.private_data = vec_len (tm->wigig_devs);

      wdev.clib_file_index = clib_file_add (fm, &template);
    }

  wdev.dev = wigig_dev;
  wdev.sw_if_index = sw_if_index;
  memcpy (&wdev.di, &wi, sizeof (wi));
  wdev.rx_ready = false;
  wdev.wdev_index = vec_len (tm->wigig_devs);
  vec_add1 (tm->wigig_devs, wdev);

  /* Let incoming traffic to be assigned to correct link interface */
  vnet_feature_enable_disable ("device-input", "tg-link-input", sw_if_index,
                               enable_disable, 0, 0);
  return 0;
}

static clib_error_t *tg_interface_enable_disable_command_fn (
    vlib_main_t *vm, unformat_input_t *input, vlib_cli_command_t *cmd)
{
  tgcfg_main_t *tm = &tgcfg_main;
  u32 sw_if_index = ~0;
  int enable_slowpath = 1;
  int rv;

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "disable"))
        clib_error_return (0, "Not implemented...");
      else if (unformat (input, "noslowpath"))
        enable_slowpath = 0;
      else if (unformat (input, "%U", unformat_vnet_sw_interface,
                         tm->vnet_main, &sw_if_index))
        ;
      else
        break;
    }

  if (sw_if_index == ~0)
    return clib_error_return (0, "Please specify an interface...");

  rv = tg_interface_enable (tm, sw_if_index, enable_slowpath);

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
      return clib_error_return (0, "tg_interface_enable_disable returned %d",
                                rv);
    }
  return 0;
}

/**
 * @brief CLI command to enable/disable the tgcfg plugin.
 */
VLIB_CLI_COMMAND (tg_setup_command, static) = {
    .path = "tg setup interface",
    .short_help = "tg setup interface <interface-name> [disable]",
    .function = tg_interface_enable_disable_command_fn,
};

#define TG_PORT_ADD_DEL_EVENT 1
#define TG_PORT_UP_EVENT 2

static int tg_wired_interface_enable (tgcfg_main_t *tm, u32 sw_if_index,
                                      const tgcfg_slowpath_map_t *sp);
/*
 * Function to check whether the passed interface is supposed to be mapped
 * to a linux tap device, and set up the mapping if so.
 */
static void tg_interface_check_enable_tap (tgcfg_main_t *tm, u32 sw_if_index)
{
  /* Find the string name of the interface */
  vnet_sw_interface_t *si = vnet_get_sw_interface (tm->vnet_main, sw_if_index);
  if (si->type != VNET_SW_INTERFACE_TYPE_HARDWARE)
    return;
  const char *name =
      (const char *)vnet_get_hw_interface (tm->vnet_main, si->hw_if_index)
          ->name;

  tgcfg_slowpath_map_t *mi;
  vec_foreach (mi, tm->slowpath_maps)
  {
    if (strcmp (mi->vpp_name, name) == 0)
      {
        tg_wired_interface_enable (tm, sw_if_index, mi);
        break;
      }
  }
}

/*
 * Function to handle deferred ethernet interface tasting and
 * probing and handle individual link up-down status updates.
 */
static uword tgcfg_interface_event_process (vlib_main_t *vm,
                                            vlib_node_runtime_t *rt,
                                            vlib_frame_t *f)
{
  tgcfg_main_t *tm = &tgcfg_main;
  uword event_type;
  uword *event_data = 0;
  u32 sw_if_index;
  tgcfg_wdev_t *wdev;
  int rc, i;
  clib_error_t *err;

  while (1)
    {
      vlib_process_wait_for_event (vm);
      event_type = vlib_process_get_events (vm, &event_data);

      switch (event_type)
        {
        case TG_PORT_ADD_DEL_EVENT:
          {
            for (i = 0; i < vec_len (event_data); i++)
              {
                sw_if_index = event_data[i];
                err = vnet_sw_interface_set_flags (
                    tm->vnet_main, sw_if_index,
                    VNET_SW_INTERFACE_FLAG_ADMIN_UP);
                rc =
                    tg_interface_enable (tm, sw_if_index, tm->slowpath_enable);
                if (rc != 0 && rc != VNET_API_ERROR_UNSUPPORTED)
                  clib_warning ("Unable to setup Terragraph data path");
                if (err)
                  {
                    clib_warning ("Unable to set device up: %s", err->what);
                    clib_error_free (err);
                    continue;
                  }
                tg_interface_copy_flags (tm->vnet_main, sw_if_index,
                                         VNET_SW_INTERFACE_FLAG_ADMIN_UP);
                tg_interface_check_enable_tap (tm, sw_if_index);
              }
          }
          break;
        case TG_PORT_UP_EVENT:
          for (i = 0; i < vec_len (event_data); i++)
            {
              wdev = (tgcfg_wdev_t *)event_data[i];
              tg_wigig_device_up (wdev);
            }
          break;
        }

      vec_reset_length (event_data);
    }
  return 0; /* or not */
}

/* *INDENT-OFF* */
VLIB_REGISTER_NODE (tgcfg_interface_event_process_node) = {
    .function = tgcfg_interface_event_process,
    .type = VLIB_NODE_TYPE_PROCESS,
    .name = "tg-interface-event-process",
};
/* *INDENT-ON* */

/*
 * Hook into interface creation path and auto-attach Terragraph
 * data path to all recognized devices. The actual work is
 * deferred to separate VPP 'process', because link updates
 * are coming in asynchronously to VPP control flow and because
 * interface add callback is invoked VPP knows the port MAC address,
 * and we need that address to identify ports this plugin can handle.
 */
static clib_error_t *
tg_interface_add_del_function (vnet_main_t *vnm, u32 sw_if_index, u32 is_add)
{
  tgcfg_main_t *tm = &tgcfg_main;
  vnet_hw_interface_t *hw;
  vnet_device_class_t *dev_class;

  /* Tear down is not supported yet */
  if (!is_add)
    return NULL;

  /* Make sure our local links vector covers this interface */
  vec_validate_init_empty (tm->local_links, sw_if_index, ~0u);

  /* Check if auto-probing is enabled */
  if (!tm->auto_probe)
    return NULL;

  /* Check for DPDK interface */
  hw = vnet_get_sup_hw_interface (vnm, sw_if_index);
  dev_class = vnet_get_device_class (vnm, hw->dev_class_index);

  if (strcmp (dev_class->name, "dpdk") != 0)
    return NULL;

  /*
   * Try to attach to the device soon, when whomever is in process
   * of initializing it is done.
   */
  vlib_process_signal_event (tm->vlib_main,
                             tgcfg_interface_event_process_node.index,
                             TG_PORT_ADD_DEL_EVENT, sw_if_index);
  return 0;
}

VNET_SW_INTERFACE_ADD_DEL_FUNCTION (tg_interface_add_del_function);

static clib_error_t *tg_interface_up_down (vnet_main_t *vnm, u32 sw_if_index,
                                           u32 flags)
{
  tgcfg_main_t *tm;
  tgcfg_wdev_t *wdev;

  tg_interface_copy_flags (vnm, sw_if_index, flags);

  wdev = tg_get_wdev_by_sw_if_index (sw_if_index);

  /* if wigig device has not been enabled yet for tgcfg plugin, let
   * tg_interface_enable handle it */
  if (wdev != NULL && (flags & VNET_SW_INTERFACE_FLAG_ADMIN_UP))
    {
      tm = &tgcfg_main;
      /* signal further tg event handling for wigig devices that must happen
       * after dpdk device admin_up_down function */
      vlib_process_signal_event (tm->vlib_main,
                                 tgcfg_interface_event_process_node.index,
                                 TG_PORT_UP_EVENT, (uword)wdev);
    }
  return NULL;
}

VNET_SW_INTERFACE_ADMIN_UP_DOWN_FUNCTION (tg_interface_up_down);

/*
 * Wired interfaces handling codes
 */
static int tg_wired_interface_enable (tgcfg_main_t *tm, u32 sw_if_index,
                                      const tgcfg_slowpath_map_t *sp)
{
  vnet_main_t *vnm;
  vlib_main_t *vm;
  vnet_sw_interface_t *sw;
  vnet_hw_interface_t *hw;
  vnet_device_class_t *dev_class;
  u32 tap_sw_if_index;
  int rc;

  vnm = tm->vnet_main;
  vm = tm->vlib_main;

  /* Utterly wrong? */
  if (pool_is_free_index (vnm->interface_main.sw_interfaces, sw_if_index))
    return VNET_API_ERROR_INVALID_SW_IF_INDEX;

  /* Not a physical port? */
  sw = vnet_get_sw_interface (vnm, sw_if_index);
  if (sw->type != VNET_SW_INTERFACE_TYPE_HARDWARE)
    return VNET_API_ERROR_INVALID_SW_IF_INDEX;

  /* Check for DPDK interface */
  hw = vnet_get_hw_interface (vnm, sw_if_index);
  dev_class = vnet_get_device_class (vnm, hw->dev_class_index);
  if (strcmp (dev_class->name, "dpdk") != 0)
    return VNET_API_ERROR_UNSUPPORTED;

  /* Skip Wigig interface */
  if (NULL != tm->wigig_ops->device_lookup (hw->hw_address))
    return VNET_API_ERROR_UNSUPPORTED;

  /* Setup the TAP interface */
  vnet_tap_connect_args_t tca;
  memset (&tca, 0, sizeof (tca));
  tca.intfc_name = (u8 *)sp->tap_name;
  tca.intfc_hwaddr_arg = hw->hw_address;
  tca.sw_if_indexp = &tap_sw_if_index;
  tca.sw_if_name = format (NULL, "vpp-%s\0", sp->tap_name);

  rc = vnet_tap_connect (vm, &tca);
  vec_free (tca.sw_if_name);
  if (rc != 0)
    return rc;

  /* Redirect all traffic from TAP device to wired interface */
  rc = set_int_l2_mode (vm, vnm, MODE_L2_XC, tap_sw_if_index, 0,
                        L2_BD_PORT_TYPE_NORMAL, 0, sw_if_index);
  switch (rc)
    {
    case MODE_ERROR_ETH:
      rc = VNET_API_ERROR_NON_ETHERNET;
      break;
    case MODE_ERROR_BVI_DEF:
      rc = VNET_API_ERROR_BD_ALREADY_HAS_BVI;
      break;
    }

  if (rc != 0)
    goto out;

  /* Make sure vector is big enough */
  vec_validate_init_empty (tm->wired_links,
                           clib_max (sw_if_index, tap_sw_if_index),
                           TG_WIRED_INVALID);
  vec_validate_init_empty (tm->local_links,
                           clib_max (sw_if_index, tap_sw_if_index), ~0u);

  /* Put mapping entries in */
  tgcfg_wired_t *wl = vec_elt_at_index (tm->wired_links, sw_if_index);
  wl->eth_sw_if_index = sw_if_index;
  wl->tap_sw_if_index = tap_sw_if_index;

  wl = vec_elt_at_index (tm->wired_links, tap_sw_if_index);
  wl->eth_sw_if_index = sw_if_index;
  wl->tap_sw_if_index = tap_sw_if_index;

  if (sp->ipv6_slowpath_enable)
    {
      /* Mark this interface as special */
      tm->local_links[sw_if_index] = sw_if_index;

      /* Intercept all local traffic for wired interface to tap */
      vnet_feature_enable_disable ("ip6-local", "tg-slowpath-wired-rx",
                                   sw_if_index, 1, 0, 0);

      /* Disable router advertisements */
      ip6_ra_config (vm, sw_if_index,
                     /*suppress*/ 1, /*managed*/ 0, /*other*/ 0,
                     /*suppress_ll_option*/ 0, /*send_unicast*/ 0, /*cease*/ 0,
                     /*use_lifetime*/ 0, /*ra_lifetime*/ 0,
                     /*ra_initial_count*/ 0, /*ra_initial_interval*/ 0,
                     /*ra_max_interval*/ 0, /*ra_min_interval*/ 0,
                     /*is_no*/ 0);
    }

  /* Send all local fraffic from tap interface over wired */
  vnet_feature_enable_disable ("device-input", "tg-wired-local-rx",
                               tap_sw_if_index, 1, 0, 0);

  /* Make sure interface can handle all IP traffic */
  ip6_link_enable (sw_if_index, NULL);
  ip4_sw_interface_enable_disable (sw_if_index, 1);

  /* Copy ADMIN_UP flag */
  vnet_sw_interface_set_flags (vnm, tap_sw_if_index,
                               vnet_sw_interface_get_flags (vnm, sw_if_index) &
                                   VNET_SW_INTERFACE_FLAG_ADMIN_UP);

  /* Including authentication packets */
  ethernet_register_802_1x_redirect (
      vm, vlib_get_node_by_name (vm, (u8 *)"tg-wired-local-tx")->index,
      sw_if_index, 1 /* Wired */);

  /* configure link to drop all non-EAPOL packets */
  if (sp->wired_security_enable)
    {
      tgcfg_log_info ("Waiting for secure authentication, dropping all "
                      "non-EAPOL packets for wired interface %u\n",
                      sw->hw_if_index);
      ethernet_set_eapol_only_flag (tm->vnet_main, sw->hw_if_index, 1);
    }

out:
  /* Remove tap here */
  if (rc != 0)
    {
      vnet_tap_delete (vm, tap_sw_if_index);
    }
  return rc;
}

static clib_error_t *
tg_wired_interface_enable_command_fn (vlib_main_t *vm, unformat_input_t *input,
                                      vlib_cli_command_t *cmd)
{
  tgcfg_main_t *tm = &tgcfg_main;
  tgcfg_slowpath_map_t sp = {.vpp_name = NULL,
                             .tap_name = NULL,
                             .wired_security_enable = false,
                             .ipv6_slowpath_enable = true};
  u32 sw_if_index = ~0;
  clib_error_t *err = NULL;
  int rc;

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "%U", unformat_vnet_sw_interface, tm->vnet_main,
                    &sw_if_index))
        ;
      else if (unformat (input, "tap %s", &sp.tap_name))
        ;
      else if (unformat (input, "security on"))
        sp.wired_security_enable = true;
      else if (unformat (input, "slowpath off"))
        sp.ipv6_slowpath_enable = false;
      else
        break;
    }

  if (sp.tap_name == NULL)
    {
      err = clib_error_return (0, "Please specify tap interface name...");
      goto out;
    }

  if (sw_if_index == ~0)
    {
      err = clib_error_return (0, "Please specify an interface...");
      goto out;
    }

  rc = tg_wired_interface_enable (tm, sw_if_index, &sp);
  if (rc != 0)
    {
      err = clib_error_return (0, "tg_wired_interface_enable returned %d", rc);
      goto out;
    }

out:
  return err;
}

/**
 * @brief CLI command to enable/disable the tgcfg plugin.
 */
VLIB_CLI_COMMAND (tg_wired_setup_command, static) = {
    .path = "tg setup wired interface",
    .short_help = "tg setup wired interface <interface-name> tap <tap-name> "
                  "security [on|off] slowpath [on|off]",
    .function = tg_wired_interface_enable_command_fn,
};

clib_error_t *tgcfg_setup_host_interface (void)
{
  tgcfg_main_t *tm = &tgcfg_main;
  vlib_main_t *vm = tm->vlib_main;
  vnet_main_t *vnm = tm->vnet_main;
  vnet_tap_connect_args_t tca;
  int rc;

  memset (&tca, 0, sizeof (tca));
  tca.intfc_name = (u8 *)tm->host_iface_name;
  tca.sw_if_indexp = &tm->host_sw_if_index;
  tca.sw_if_name = format (NULL, "vpp-%s\0", tm->host_iface_name);

  rc = vnet_tap_connect (vm, &tca);
  vec_free (tca.sw_if_name);
  if (rc != 0)
    return clib_error_return (0, "vnet_tap_connect(%s) returned %d",
                              tm->host_iface_name, rc);
  ip6_link_enable (tm->host_sw_if_index, NULL);
  vnet_sw_interface_set_flags (vnm, tm->host_sw_if_index,
                               VNET_SW_INTERFACE_FLAG_ADMIN_UP);

  /* Create complementary loopback interface */
  u8 mac_address[6];
  clib_memset (mac_address, 0, sizeof (mac_address));
  rc = vnet_create_loopback_interface (&tm->loop_sw_if_index, mac_address, 0,
                                       0);
  if (rc != 0)
    return clib_error_return (0, "vnet_create_loopback_interface failed");

  vnet_sw_interface_set_flags (vnm, tm->loop_sw_if_index,
                               VNET_SW_INTERFACE_FLAG_ADMIN_UP);
  return NULL;
}
