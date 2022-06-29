/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include <vlib/vlib.h>
#include <vnet/vnet.h>
#include <vnet/pg/pg.h>
#include <vnet/ethernet/ethernet.h>
#include <vppinfra/error.h>
#include <esmc/esmc.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

/* zl3079x module (PLL chip) ioctls */
#include <sys/ioctl.h>
#define ZL_IOCTL_SET_MODE _IOW (0xfb, 1, int)
#define ZL_IOCTL_SET_DEVICE _IOW (0xfb, 2, uint64_t)
#define ZL_IOCTL_GET_LOCKED _IOR (0xfb, 3, int)
/* DPLL miscdevice */
#define PLL_DEV "/dev/zl3079x"
/* DPLL modes */
#define ZL_DPLL_MODE_NCO 0x4
#define ZL_DPLL_MODE_REFLOCK_SYNCE 0x62

/* ESMC PDU header, after Ethernet octets (14 octets) */
typedef CLIB_PACKED (struct {
  u8 slow_proto_subtype;
  u32 itu_oui : 24;
  u16 itu_subtype;
  u8 flags; /* bits: 7-4 = version, 3 = event, 2-0 = reserved */
  u8 pad[3];
  u8 data[0];
}) esmc_hdr_t;

#define ESMC_SLOW_PROTO_SUBTYPE 0x0a
#define ESMC_ITU_OUI 0x0019a7
#define ESMC_ITU_SUBTYPE 0x0001
#define ESMC_VERSION 0x1

/* ESMC QL TLV */
typedef CLIB_PACKED (struct {
  u8 t;
  u16 l;
  u8 v; /* bits: 7-4 = reserved, 3-0 = ssm */
}) esmc_ql_tlv_t;

#define ESMC_QL_TLV_TYPE 0x01
#define ESMC_QL_TLV_LEN 0x0004
#define ESMC_QL_TLV_RESERVED 0x0

/* Minimum ESMC frame size (bytes) from ITU-T G.8264 section 11.3.1.1(j) */
#define ESMC_MIN_FRAME_SIZE 64
/* Minimum size (bytes) of 'esmc_hdr_t.data' field */
#define ESMC_HDR_DATA_MIN_SIZE \
  (ESMC_MIN_FRAME_SIZE - sizeof (ethernet_header_t) - sizeof (esmc_hdr_t))

/* packet trace data */
typedef struct
{
  u32 next_index;
  u32 sw_if_index;
  u8 ssm;
  u8 event;
} esmc_trace_t;

/* packet trace format function */
static u8 *format_esmc_trace (u8 *s, va_list *args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  esmc_trace_t *t = va_arg (*args, esmc_trace_t *);

  s = format (s, "frame: sw_if_index %d, next index %d\n", t->sw_if_index,
              t->next_index);
  s = format (s, "  ESMC: ssm 0x%x, event %d", t->ssm, t->event);

  return s;
}

/** graph nodes */
extern vlib_node_registration_t esmc_input;
extern vlib_node_registration_t esmc_process;

/* error counters */
#define foreach_esmc_error                \
  _ (PROCESSED, "ESMC packets processed") \
  _ (DROPPED, "Non-ESMC packets dropped") \
  _ (TRANSMITTED, "ESMC packets transmitted")

typedef enum
{
#define _(sym, str) ESMC_ERROR_##sym,
  foreach_esmc_error
#undef _
      ESMC_N_ERROR,
} esmc_error_t;

static char *esmc_error_strings[] = {
#define _(sym, string) string,
    foreach_esmc_error
#undef _
};

/* next graph nodes */
typedef enum
{
  ESMC_NEXT_NORMAL,
  ESMC_N_NEXT,
} esmc_next_t;

/* vlib events */
#define ESMC_RX_CALLBACK 1

/* send ESMC heartbeats at this interval (as written in specification) */
#define ESMC_HEARTBEAT_INTERVAL_S 1.0

/* maximum ESMC PDUs we can transmit per heartbeat interval */
/* (as written in ITU-T G.8264 section 11.3.2.1) */
#define ESMC_MAX_PDUS_PER_INTERVAL 10

/* QL hierarchy in option 1 networks, from ITU-T G.781 section 5.4.2.1 */
/* "preference" is an arbitrary value we assign here (higher is preferred) */
const static int kEsmcSsmPreference[] = {
    /* 0 = invalid */ 0,
    /* 1 = invalid */ 0,
    /* 2 = PRC, PRTC, ePRTC */ 5,
    /* 3 = invalid */ 0,
    /* 4 = SSU-A */ 4,
    /* 5 = invalid */ 0,
    /* 6 = invalid */ 0,
    /* 7 = invalid */ 0,
    /* 8 = SSU-B */ 3,
    /* 9 = invalid */ 0,
    /* a = invalid */ 0,
    /* b = EEC1, eEEC */ 2,
    /* c = invalid */ 0,
    /* d = invalid */ 0,
    /* e = invalid */ 0,
    /* f = DNU */ 1,
};

const static esmc_if_state_t ESMC_IF_STATE_INVALID = {
    .ssm = ESMC_SSM_QL_UNUSED,
    .last_esmc_ts = 0,
};

/**
 * @brief Set the PLL mode.
 */
static void esmc_set_pll_mode (u8 mode)
{
  int fd = open (PLL_DEV, O_RDWR);
  if (fd < 0)
    {
      esmc_log_err ("Could not open PLL device file");
      return;
    }

  if (ioctl (fd, ZL_IOCTL_SET_MODE, mode))
    esmc_log_err ("Could not change PLL mode (ioctl failed)");
  else
    esmc_log_info ("Changed PLL mode to: %d", mode);

  close (fd);
}

/**
 * @brief Set the PLL interface to use.
 */
static void esmc_set_pll_interface (u64 macaddr)
{
  int fd = open (PLL_DEV, O_RDWR);
  if (fd < 0)
    {
      esmc_log_err ("Could not open PLL device file");
      return;
    }

  if (ioctl (fd, ZL_IOCTL_SET_DEVICE, &macaddr))
    esmc_log_err ("Could not change PLL interface (ioctl failed)");
  else
    esmc_log_info ("Changed PLL interface to (u64 macaddr): 0x%012llx",
                   macaddr);

  close (fd);
}

/**
 * @brief Return whether the PLL is in the "locked" SyncE output state.
 */
static u8 esmc_get_pll_locked ()
{
  int fd, val = 0;

  fd = open (PLL_DEV, O_RDWR);
  if (fd < 0)
    {
      esmc_log_err ("Could not open PLL device file");
      return (u8)val;
    }

  if (ioctl (fd, ZL_IOCTL_GET_LOCKED, &val))
    esmc_log_err ("Could not get PLL lock state (ioctl failed)");
  else
    esmc_log_debug ("PLL lock state: %d", val);

  close (fd);
  return (u8)val;
}

/**
 * @brief ESMC packet RX handler.
 *
 * @param em
 * @param sw_if_index the receiving sw interface
 * @param ssm the ESMC SSM code
 * @param event the ESMC event flag
 */
static u8 esmc_rx (esmc_main_t *em, u32 sw_if_index, u8 ssm, u8 event)
{
  vnet_hw_interface_t *hw;
  esmc_if_state_t *rx_state;

  /* validate rx interface */
  if (sw_if_index != em->input_sw_if_index)
    {
      /* is this coming from a TG interface? */
      if (em->enable_tg_input)
        hw = vnet_get_sup_hw_interface (em->vnet_main, sw_if_index);
      if (!em->enable_tg_input ||
          strncmp ((const char *)hw->name, TG_INTERFACE_PREFIX,
                   TG_INTERFACE_PREFIX_LEN))
        {
          esmc_log_info ("Dropping ESMC frame with ssm 0x%x from sw_if_index "
                         "%d != input_sw_if_index %d",
                         ssm, sw_if_index, em->input_sw_if_index);
          return -1;
        }
    }

  esmc_log_debug ("Received ssm 0x%x, event %d from sw_if_index %d", ssm,
                  event, sw_if_index);

  /* update state for this interface */
  vec_validate_init_empty (em->if_rx_state, sw_if_index,
                           ESMC_IF_STATE_INVALID);
  rx_state = vec_elt_at_index (em->if_rx_state, sw_if_index);
  rx_state->last_esmc_ts = clib_time_now (&em->clib_time);
  if (rx_state->ssm != ssm)
    {
      rx_state->ssm = ssm;
      /* SSM changed, check if we need to take any actions */
      vlib_process_signal_event (em->vlib_main, esmc_process.index,
                                 ESMC_RX_CALLBACK, sw_if_index);
    }

  return 0;
}

/**
 * @brief ESMC packet TX handler.
 *
 * @param em
 * @param sw_if_index the transmit sw interface
 * @param ssm the ESMC SSM code
 * @param event the ESMC event flag
 */
static u8 esmc_tx (esmc_main_t *em, u32 sw_if_index, u8 ssm, u8 event)
{
  u32 *to_next;
  vlib_frame_t *f;
  u32 bi0;
  vlib_buffer_t *b0;
  vnet_hw_interface_t *hw;
  ethernet_header_t *e0;
  esmc_hdr_t *eh0;
  esmc_ql_tlv_t *eqlv0;

  hw = vnet_get_sup_hw_interface (em->vnet_main, sw_if_index);

  /* allocate a buffer */
  if (vlib_buffer_alloc (em->vlib_main, &bi0, 1) != 1)
    return -1;

  b0 = vlib_get_buffer (em->vlib_main, bi0);

  /* initialize the buffer */
  VLIB_BUFFER_TRACE_TRAJECTORY_INIT (b0);

  /* construct the ESMC PDU */
  e0 = vlib_buffer_get_current (b0);
  eh0 = (esmc_hdr_t *)(e0 + 1);
  eqlv0 = (void *)eh0->data;

  // Per ITU-T G.8264 section 11.3.1.1(a), set destination address to the slow
  // protocol multicast address defined in IEEE 802.3 Annex 57B
  e0->dst_address[0] = 0x01;
  e0->dst_address[1] = 0x80;
  e0->dst_address[2] = 0xc2;
  e0->dst_address[3] = 0x00;
  e0->dst_address[4] = 0x00;
  e0->dst_address[5] = 0x02;
  clib_memcpy (e0->src_address, hw->hw_address, vec_len (hw->hw_address));
  e0->type = clib_host_to_net_u16 (ETHERNET_TYPE_SLOW_PROTOCOLS);
  eh0->slow_proto_subtype = ESMC_SLOW_PROTO_SUBTYPE;
  eh0->itu_oui = clib_host_to_net_u32 (ESMC_ITU_OUI << 8);
  eh0->itu_subtype = clib_host_to_net_u16 (ESMC_ITU_SUBTYPE);
  eh0->flags = (ESMC_VERSION << 4) | ((!!event) << 3);
  clib_memset (eh0->pad, 0, sizeof (eh0->pad));
  clib_memset (eh0->data, 0, ESMC_HDR_DATA_MIN_SIZE);
  eqlv0->t = ESMC_QL_TLV_TYPE;
  eqlv0->l = clib_host_to_net_u16 (ESMC_QL_TLV_LEN);
  eqlv0->v = (ESMC_QL_TLV_RESERVED << 4) | (ssm & 0xf);

  /* set the outbound packet length */
  b0->current_length = sizeof (*e0) + sizeof (*eh0) + ESMC_HDR_DATA_MIN_SIZE;
  b0->flags |= VLIB_BUFFER_TOTAL_LENGTH_VALID;

  /* set the outbound interface */
  vnet_buffer (b0)->sw_if_index[VLIB_TX] = hw->sw_if_index;

  /* enqueue the packet */
  f = vlib_get_frame_to_node (em->vlib_main, hw->output_node_index);
  to_next = vlib_frame_vector_args (f);
  to_next[0] = bi0;
  f->n_vectors = 1;

  vlib_put_frame_to_node (em->vlib_main, hw->output_node_index, f);

#if 0
  esmc_log_debug ("Sent ESMC frame on sw_if_index %d (ssm 0x%x, event %d)",
                  sw_if_index, ssm, event);
#endif

  return 0;
}

/**
 * @brief Transmit an ESMC packet on all configured output interfaces.
 *
 * @param em
 * @param ssm the ESMC SSM code
 * @param event the ESMC event flag
 * @param from_sw_if_index the receiving sw interface when this is called in
 *                         response to a QL change, or ~0 otherwise
 */
static void esmc_tx_broadcast (esmc_main_t *em, u8 ssm, u8 event,
                               u32 from_sw_if_index)
{
  vnet_interface_main_t *im = &em->vnet_main->interface_main;
  vnet_hw_interface_t *hw;
  u32 pkts_sent = 0;

  /* reached maximum transmitted PDUs this interval? */
  if (em->num_tx_pdu_1s >= ESMC_MAX_PDUS_PER_INTERVAL)
    {
      esmc_log_info (
          "Max ESMC PDUs reached, dropping frame (ssm 0x%x, event %d)", ssm,
          event);
      return;
    }

  /* for the currently-selected interface, emit QL-DNU to avoid timing loops */
  /* skip sending these QL-DNU on "event" frames */

  /* emit on ESMC output interface */
  if (em->output_sw_if_index != ~0)
    {
      if (em->output_sw_if_index == em->selected_sw_if_index)
        {
          if (!event)
            {
              esmc_tx (em, em->output_sw_if_index, ESMC_SSM_QL_DNU, event);
              pkts_sent++;
            }
        }
      else
        {
          esmc_tx (em, em->output_sw_if_index, ssm, event);
          pkts_sent++;
        }
    }

  /* emit on all TG interfaces */
  if (em->enable_tg_output)
    {
      pool_foreach (hw, im->hw_interfaces)
      {
        if (hw->sw_if_index != from_sw_if_index &&
            hw->sw_if_index != em->output_sw_if_index &&
            !strncmp ((const char *)hw->name, TG_INTERFACE_PREFIX,
                      TG_INTERFACE_PREFIX_LEN))
          {
            if (hw->sw_if_index == em->selected_sw_if_index)
              {
                if (!event)
                  {
                    esmc_tx (em, hw->sw_if_index, ESMC_SSM_QL_DNU, event);
                    pkts_sent++;
                  }
              }
            else
              {
                esmc_tx (em, hw->sw_if_index, ssm, event);
                pkts_sent++;
              }
          }
      }
    }

  if (pkts_sent)
    {
      em->num_tx_pdu_1s++;
      vlib_node_increment_counter (em->vlib_main, esmc_input.index,
                                   ESMC_ERROR_TRANSMITTED, pkts_sent);
    }
}

/**
 * @brief ESMC state update, which should be invoked at 1-second intervals or
 * whenever received SSM changes on any interface.
 *
 * This function checks if we need to send any corresponding ESMC events and/or
 * signal a PLL interface/mode change.
 *
 * @param em
 * @param from_sw_if_index the receiving sw interface, or ~0 if this is called
 *                         for the heartbeat message
 */
static void esmc_update (esmc_main_t *em, u32 from_sw_if_index)
{
  u8 best_ssm = ESMC_SSM_QL_UNUSED;
  u32 best_sw_if_index = ~0, sw_if_index;
  u8 new_pll_mode = ~0;
  int interface_index;
  esmc_if_state_t *rx_state;
  f64 now = clib_time_now (&em->clib_time);

  /* find "best" received SSM */
  vec_foreach_index (sw_if_index, em->if_rx_state)
  {
    rx_state = vec_elt_at_index (em->if_rx_state, sw_if_index);
    if (rx_state->ssm == ESMC_SSM_QL_UNUSED)
      {
        /* uninitialized */
        continue;
      }
    if (now - rx_state->last_esmc_ts >= ESMC_TIMEOUT_SEC &&
        rx_state->ssm != ESMC_SSM_QL_DNU)
      {
        /* timed out, set QL-DNU */
        esmc_log_info ("ESMC timeout on sw_if_index %d, setting to QL-DNU",
                       sw_if_index);
        rx_state->ssm = ESMC_SSM_QL_DNU;
      }
    if (kEsmcSsmPreference[rx_state->ssm] > kEsmcSsmPreference[best_ssm])
      {
        best_ssm = rx_state->ssm;
        best_sw_if_index = sw_if_index;
      }
  }
  esmc_log_debug ("Best ssm = 0x%x on sw_if_index %d", best_ssm,
                  best_sw_if_index);

  /* determine associated PLL mode and interface index */
  if (best_ssm == ESMC_SSM_QL_UNUSED || best_ssm == ESMC_SSM_QL_DNU)
    {
      /* QL = invalid or DNU, so don't process any HTSF messages */
      new_pll_mode = ZL_DPLL_MODE_NCO;
      interface_index = -1;
    }
  else if (best_sw_if_index == em->input_sw_if_index)
    {
      /* using wired SyncE input interface, so drop WiGig HTSF messages */
      new_pll_mode = ZL_DPLL_MODE_REFLOCK_SYNCE;
      interface_index = -1;
    }
  else
    {
      /* process WiGig HTSF messages from given deviceIdx */
      new_pll_mode = ZL_DPLL_MODE_NCO;
      interface_index = best_sw_if_index;
    }

  /* interface changed? */
  if (best_sw_if_index != em->selected_sw_if_index ||
      interface_index != em->programmed_sw_if_index)
    {
      em->selected_sw_if_index = best_sw_if_index;
      em->programmed_sw_if_index = interface_index;
      em->pll_locked = 0;

      if (interface_index == -1)
        {
          esmc_set_pll_interface (0);
        }
      else
        {
          vnet_hw_interface_t *hw;
          u8 *a;
          u64 macaddr;

          /* get ethernet (MAC) address of vpp-terraX interface as u64 */
          hw = vnet_get_sup_hw_interface (em->vnet_main, interface_index);
          a = hw->hw_address;
          macaddr = ((u64)a[0] << 40) | ((u64)a[1] << 32) | ((u64)a[2] << 24) |
                    ((u64)a[3] << 16) | ((u64)a[4] << 8) | (u64)a[5];
          esmc_log_info ("Setting PLL interface to %s, MAC 0x%012llx",
                         (const char *)hw->name, macaddr);
          esmc_set_pll_interface (macaddr);
        }
    }

  /* PLL mode changed? */
  if (new_pll_mode != em->pll_mode)
    {
      em->pll_mode = new_pll_mode;
      em->pll_locked = 0;
      esmc_set_pll_mode (new_pll_mode);
    }

  /* ssm changed? */
  if (!em->pll_locked)
    {
      /* send QL-DNU while PLL reports holdover */
      esmc_log_debug ("PLL in holdover, using QL-DNU instead of ssm 0x%x",
                      best_ssm);
      best_ssm = ESMC_SSM_QL_DNU;
    }
  if (best_ssm != em->ssm)
    {
      /* emit ESMC event immediately when SSM changes */
      esmc_log_info ("Sending ESMC event (ssm 0x%x from sw_if_index %d)",
                     best_ssm, best_sw_if_index);
      em->ssm = best_ssm;
      esmc_tx_broadcast (em, best_ssm, 1, from_sw_if_index);
    }
  else if (from_sw_if_index == ~0 && best_ssm != ESMC_SSM_QL_UNUSED)
    {
      /* emit ESMC heartbeat (if initialized) */
      esmc_log_debug ("Sending ESMC heartbeat (ssm 0x%x)", best_ssm);
      esmc_tx_broadcast (em, best_ssm, 0, ~0);
    }
}

/**
 * @brief Perform asynchronous initialization of certain fields that cannot be
 * done during VLIB_INIT_FUNCTION or VLIB_CONFIG_FUNCTION.
 */
static void esmc_post_init (esmc_main_t *em)
{
  ethernet_main_t *enm = &ethernet_main;
  ethernet_type_info_t *ti = 0;
  vnet_interface_main_t *im = &em->vnet_main->interface_main;
  vnet_hw_interface_t *hw;

  if (em->enabled)
    {
      /* check if esmc-input node is registered */
      /* this is normally registered by the lacp plugin */
      ti = ethernet_get_type_info (enm, ETHERNET_TYPE_SLOW_PROTOCOLS);
      if (!ti || ti->node_index != esmc_input.index)
        {
          esmc_log_notice ("Registering ESMC input node (previous "
                           "handler index %d)",
                           ti->node_index);
          ethernet_register_input_type (
              em->vlib_main, ETHERNET_TYPE_SLOW_PROTOCOLS, esmc_input.index);
        }

      /* convert interface names to vpp index */
      if (em->input_sw_if_name != NULL)
        {
          pool_foreach (hw, im->hw_interfaces)
          {
            if (!em->input_sw_if_name)
              break;
            if (!strncmp ((const char *)hw->name, em->input_sw_if_name,
                          strlen (em->input_sw_if_name)))
              {
                em->input_sw_if_index = hw->sw_if_index;
                vec_free (em->input_sw_if_name);
              }
          }
        }
      if (em->output_sw_if_name != NULL)
        {
          pool_foreach (hw, im->hw_interfaces)
          {
            if (!em->output_sw_if_name)
              break;
            if (!strncmp ((const char *)hw->name, em->output_sw_if_name,
                          strlen (em->output_sw_if_name)))
              {
                em->output_sw_if_index = hw->sw_if_index;
                vec_free (em->output_sw_if_name);
              }
          }
        }
    }
  else
    {
      /* TODO how to unregister? */
    }
}

static uword esmc_process_fn (vlib_main_t *vm, vlib_node_runtime_t *rt,
                              vlib_frame_t *f)
{
  esmc_main_t *em = &esmc_main;
  uword event_type, *event_data = 0;
  int i;
  uword data;
  f64 last_time, timeout_s = ESMC_HEARTBEAT_INTERVAL_S;

  while (1)
    {
      /* wait for events, triggering heartbeats at 1sec absolute intervals */
      last_time = clib_time_now (&em->clib_time);
      vlib_process_wait_for_event_or_clock (vm, timeout_s);

      event_type = vlib_process_get_events (vm, &event_data);
      switch (event_type)
        {
        case ESMC_RX_CALLBACK:
          {
            for (i = 0; i < vec_len (event_data); i++)
              {
                data = event_data[i];
                esmc_update (em, data);
              }

            /* maintain heartbeat timeout */
            timeout_s -= (clib_time_now (&em->clib_time) - last_time);
          }
          break;

        case ~0: /* timeout */
          {
            if (em->enabled)
              {
                /* reset PDU counter */
                em->num_tx_pdu_1s = 0;

                /* check if PLL is locked or in holdover */
                em->pll_locked = esmc_get_pll_locked ();

                /* 1pps logic update */
                esmc_update (em, ~0);
              }

            /* TODO find a better place to put this... */
            esmc_post_init (em);

            timeout_s = ESMC_HEARTBEAT_INTERVAL_S;
          }
        }

      vec_reset_length (event_data);
    }
  return 0;
}

/* *INDENT-OFF* */
VLIB_REGISTER_NODE (esmc_process) = {
    .function = esmc_process_fn,
    .name = "esmc-process",
    .type = VLIB_NODE_TYPE_PROCESS,
};
/* *INDENT-ON* */

VLIB_NODE_FN (esmc_input)
(vlib_main_t *vm, vlib_node_runtime_t *node, vlib_frame_t *frame)
{
  u32 n_left_from, *from;
  u32 pkts_processed = 0, pkts_dropped = 0;

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;

  while (n_left_from > 0)
    {
      u32 bi0;
      vlib_buffer_t *b0;
      u32 next0 = ESMC_NEXT_NORMAL;
      u32 sw_if_index0;
      esmc_hdr_t *eh0;
      esmc_ql_tlv_t *eqlv0;
      u8 ssm, event;

      bi0 = from[0];
      b0 = vlib_get_buffer (vm, bi0);

      eh0 = vlib_buffer_get_current (b0);
      eqlv0 = (void *)eh0->data;

      sw_if_index0 = vnet_buffer (b0)->sw_if_index[VLIB_RX];

      /* validate expected header fields */
      if (eh0->slow_proto_subtype != ESMC_SLOW_PROTO_SUBTYPE ||
          (clib_net_to_host_u32 (eh0->itu_oui) >> 8) != ESMC_ITU_OUI ||
          clib_net_to_host_u16 (eh0->itu_subtype) != ESMC_ITU_SUBTYPE)
        {
          /* not an ESMC packet */
          pkts_dropped += 1;
          esmc_log_warn ("Dropping non-ESMC packet (slow protocol subtype "
                         "0x%x, ITU-OUI 0x%x, ITU subtype 0x%x)",
                         eh0->slow_proto_subtype,
                         clib_net_to_host_u32 (eh0->itu_oui) >> 8,
                         clib_net_to_host_u16 (eh0->itu_subtype));
        }
      else if ((eh0->flags >> 4) != ESMC_VERSION)
        {
          /* unexpected ESMC version */
          pkts_dropped += 1;
          esmc_log_warn ("Unexpected ESMC version (0x%x)", eh0->flags >> 4);
        }
      else if (PREDICT_FALSE (eqlv0->t != ESMC_QL_TLV_TYPE ||
                              clib_net_to_host_u16 (eqlv0->l) !=
                                  ESMC_QL_TLV_LEN ||
                              (eqlv0->v >> 4) != ESMC_QL_TLV_RESERVED))
        {
          /* invalid QL TLV */
          pkts_dropped += 1;
          esmc_log_warn (
              "Invalid QL TLV (type 0x%x, len 0x%x, reserved bits 0x%x)",
              eqlv0->t, clib_net_to_host_u16 (eqlv0->l), eqlv0->v >> 4);
        }
      else
        {
          pkts_processed += 1;

          ssm = eqlv0->v & 0xf;
          event = (eh0->flags & 0x8) >> 3;
          esmc_rx (&esmc_main, sw_if_index0, ssm, event);

          if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE) &&
                             (b0->flags & VLIB_BUFFER_IS_TRACED)))
            {
              esmc_trace_t *t = vlib_add_trace (vm, node, b0, sizeof (*t));
              t->sw_if_index = sw_if_index0;
              t->next_index = next0;
              t->ssm = ssm;
              t->event = event;
            }
        }

      vlib_set_next_frame_buffer (vm, node, next0, bi0);

      from += 1;
      n_left_from -= 1;
    }

  if (pkts_processed)
    {
      vlib_node_increment_counter (vm, esmc_input.index, ESMC_ERROR_PROCESSED,
                                   pkts_processed);
    }
  if (pkts_dropped)
    {
      vlib_node_increment_counter (vm, esmc_input.index, ESMC_ERROR_DROPPED,
                                   pkts_dropped);
    }
  return frame->n_vectors;
}

/* *INDENT-OFF* */
VLIB_REGISTER_NODE (esmc_input) = {
    .name = "esmc-input",
    .vector_size = sizeof (u32),
    .type = VLIB_NODE_TYPE_INTERNAL,

    .n_errors = ARRAY_LEN (esmc_error_strings),
    .error_strings = esmc_error_strings,

    .format_trace = format_esmc_trace,

    .n_next_nodes = ESMC_N_NEXT,
    .next_nodes =
        {
            [ESMC_NEXT_NORMAL] = "error-drop",
        },
};
/* *INDENT-ON* */

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
