/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * tgcfg_node.c - Terragraph config plugin datapath nodes
 */
/* Evil: we need to peek into rte_mbuf */
#include <rte_common.h>
#include <rte_mbuf.h>

#include <tgcfg/tgcfg.h>
#include <vlib/unix/unix.h>
#include <vnet/dpo/receive_dpo.h>

/**
 * @brief Hook the input fanout mode into VPP graph
 */
VNET_FEATURE_INIT (tg_link_input, static) = {
    .arc_name = "device-input",
    .node_name = "tg-link-input",
    .runs_before = VNET_FEATURES ("ethernet-input"),
};

typedef struct
{
  u32 next_index;
  u32 bb_sw_if_index;
  u32 tg_sw_if_index;
} tg_link_input_trace_t;

/* packet trace format function */
static u8 *format_tg_link_input_trace (u8 *s, va_list *args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  tg_link_input_trace_t *t = va_arg (*args, tg_link_input_trace_t *);

  s = format (s, "  BB sw_if_index %u, next index %d", t->bb_sw_if_index,
              t->next_index);
  s = format (s, " TG sw_if_index %u", t->tg_sw_if_index);
  return s;
}

extern vlib_node_registration_t tg_link_input_node;

#define foreach_tg_link_input_error  \
  _ (FORWARDED, "Packets forwarded") \
  _ (DROPPED, "Packets dropped")

typedef enum
{
#define _(sym, str) TG_LINK_INPUT_ERROR_##sym,
  foreach_tg_link_input_error
#undef _
      TG_LINK_INPUT_N_ERROR,
} tg_link_input_error_t;

static char *tg_link_input_error_strings[] = {
#define _(sym, string) string,
    foreach_tg_link_input_error
#undef _
};

typedef enum
{
  TG_LINK_INPUT_NEXT_ETHERNET_INPUT,
  TG_LINK_INPUT_NEXT_DROP,
  TG_LINK_INPUT_N_NEXT,
} tg_link_input_next_t;

VLIB_NODE_FN (tg_link_input_node)
(vlib_main_t *vm, vlib_node_runtime_t *node, vlib_frame_t *frame)
{
  vnet_main_t *vnm = vnet_get_main ();
  tgcfg_main_t *tm;
  u32 thread_index = vm->thread_index;
  u32 n_left_from, *from, *to_next;
  tg_link_input_next_t next_index;
  u32 packets_ok = 0, packets_dropped = 0;
  u32 stats_sw_if_index = node->runtime_data[0], stats_n_packets = 0,
      stats_n_bytes = 0;

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;

  tm = &tgcfg_main;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
        {
          u32 next0 = TG_LINK_INPUT_NEXT_ETHERNET_INPUT;
          u32 next1 = TG_LINK_INPUT_NEXT_ETHERNET_INPUT;
          u32 bb_sw_if_index0, bb_sw_if_index1;
          u32 tg_sw_if_index0, tg_sw_if_index1;
          u32 len0, len1;
          u32 bi0, bi1, l0, l1;
          vlib_buffer_t *b0, *b1;
          struct rte_mbuf *m0, *m1;

          /* Prefetch next iteration. */
          {
            vlib_buffer_t *p2, *p3;

            p2 = vlib_get_buffer (vm, from[2]);
            p3 = vlib_get_buffer (vm, from[3]);

            vlib_prefetch_buffer_header (p2, LOAD);
            vlib_prefetch_buffer_header (p3, LOAD);

            CLIB_PREFETCH (p2->data, CLIB_CACHE_LINE_BYTES, STORE);
            CLIB_PREFETCH (p3->data, CLIB_CACHE_LINE_BYTES, STORE);
          }

          /* speculatively enqueue b0 and b1 to the current next frame */
          to_next[0] = bi0 = from[0];
          to_next[1] = bi1 = from[1];
          from += 2;
          to_next += 2;
          n_left_from -= 2;
          n_left_to_next -= 2;

          b0 = vlib_get_buffer (vm, bi0);
          b1 = vlib_get_buffer (vm, bi1);

          m0 = rte_mbuf_from_vlib_buffer (b0);
          m1 = rte_mbuf_from_vlib_buffer (b1);

          l0 = wigig_mbuf_link_id_get (m0);
          l1 = wigig_mbuf_link_id_get (m1);

          bb_sw_if_index0 = vnet_buffer (b0)->sw_if_index[VLIB_RX];
          bb_sw_if_index1 = vnet_buffer (b1)->sw_if_index[VLIB_RX];

          if (PREDICT_TRUE (l0 < vec_len (tm->terra_links)))
            {
              tgcfg_link_t *li;

              li = vec_elt_at_index (tm->terra_links, l0);
              tg_sw_if_index0 = li->tg_sw_if_index;
              if (PREDICT_TRUE (tg_sw_if_index0 != ~0))
                {
                  vnet_buffer (b0)->sw_if_index[VLIB_RX] = tg_sw_if_index0;
                  packets_ok++;
                }
              else
                {
                  packets_dropped++;
                  next0 = TG_LINK_INPUT_NEXT_DROP;
                }
            }
          else
            {
              tg_sw_if_index0 = ~0;
              packets_dropped++;
              next0 = TG_LINK_INPUT_NEXT_DROP;
            }

          if (PREDICT_TRUE (l1 < vec_len (tm->terra_links)))
            {
              tgcfg_link_t *li;

              li = vec_elt_at_index (tm->terra_links, l1);
              tg_sw_if_index1 = li->tg_sw_if_index;
              if (PREDICT_TRUE (tg_sw_if_index1 != ~0))
                {
                  vnet_buffer (b1)->sw_if_index[VLIB_RX] = tg_sw_if_index1;
                  packets_ok++;
                }
              else
                {
                  packets_dropped++;
                  next1 = TG_LINK_INPUT_NEXT_DROP;
                }
            }
          else
            {
              tg_sw_if_index1 = ~0;
              packets_dropped++;
              next1 = TG_LINK_INPUT_NEXT_DROP;
            }

          /* Increment stats for individual vpp-terra interfaces */
          if (PREDICT_TRUE (tg_sw_if_index0 != ~0 || tg_sw_if_index1 != ~0))
            {
              /* Batch stat increments from the same vpp-terra interface so
               * counters don't need to be incremented for every packet. */
              len0 = b0->current_length;
              len1 = b1->current_length;

              stats_n_packets += 2;
              stats_n_bytes += len0 + len1;

              if (tg_sw_if_index0 != stats_sw_if_index ||
                  tg_sw_if_index1 != stats_sw_if_index)
                {
                  stats_n_packets -= 2;
                  stats_n_bytes -= len0 + len1;

                  if (PREDICT_TRUE (tg_sw_if_index0 != ~0))
                    vlib_increment_combined_counter (
                        vnm->interface_main.combined_sw_if_counters +
                            VNET_INTERFACE_COUNTER_RX,
                        thread_index, tg_sw_if_index0, 1, len0);

                  if (PREDICT_TRUE (tg_sw_if_index1 != ~0))
                    vlib_increment_combined_counter (
                        vnm->interface_main.combined_sw_if_counters +
                            VNET_INTERFACE_COUNTER_RX,
                        thread_index, tg_sw_if_index1, 1, len1);

                  if (tg_sw_if_index0 == tg_sw_if_index1)
                    {
                      /* Increment counters for currently batched interface and
                       * start batching for new interface. */
                      if (stats_n_packets > 0)
                        {
                          vlib_increment_combined_counter (
                              vnm->interface_main.combined_sw_if_counters +
                                  VNET_INTERFACE_COUNTER_RX,
                              thread_index, stats_sw_if_index, stats_n_packets,
                              stats_n_bytes);
                          stats_n_packets = stats_n_bytes = 0;
                        }
                      stats_sw_if_index = tg_sw_if_index0;
                    }
                }
            }

          if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE)))
            {
              if (b0->flags & VLIB_BUFFER_IS_TRACED)
                {
                  tg_link_input_trace_t *t =
                      vlib_add_trace (vm, node, b0, sizeof (*t));
                  t->bb_sw_if_index = bb_sw_if_index0;
                  t->tg_sw_if_index = tg_sw_if_index0;
                  t->next_index = next0;
                }
              if (b1->flags & VLIB_BUFFER_IS_TRACED)
                {
                  tg_link_input_trace_t *t =
                      vlib_add_trace (vm, node, b1, sizeof (*t));
                  t->bb_sw_if_index = bb_sw_if_index1;
                  t->tg_sw_if_index = tg_sw_if_index1;
                  t->next_index = next1;
                }
            }

          /* verify speculative enqueues, maybe switch current next frame */
          vlib_validate_buffer_enqueue_x2 (vm, node, next_index, to_next,
                                           n_left_to_next, bi0, bi1, next0,
                                           next1);
        }

      while (n_left_from > 0 && n_left_to_next > 0)
        {
          u32 bi0;
          u32 l0;
          vlib_buffer_t *b0;
          u32 next0 = TG_LINK_INPUT_NEXT_ETHERNET_INPUT;
          u32 bb_sw_if_index0;
          u32 tg_sw_if_index0;
          u32 len0;
          struct rte_mbuf *m0;

          /* speculatively enqueue b0 to the current next frame */
          bi0 = from[0];
          to_next[0] = bi0;
          from += 1;
          to_next += 1;
          n_left_from -= 1;
          n_left_to_next -= 1;

          b0 = vlib_get_buffer (vm, bi0);

          m0 = rte_mbuf_from_vlib_buffer (b0);

          l0 = wigig_mbuf_link_id_get (m0);

          bb_sw_if_index0 = vnet_buffer (b0)->sw_if_index[VLIB_RX];

          if (PREDICT_TRUE (l0 < vec_len (tm->terra_links)))
            {
              tgcfg_link_t *li;

              li = vec_elt_at_index (tm->terra_links, l0);
              tg_sw_if_index0 = li->tg_sw_if_index;
              if (PREDICT_TRUE (tg_sw_if_index0 != ~0))
                {
                  vnet_buffer (b0)->sw_if_index[VLIB_RX] = tg_sw_if_index0;
                  packets_ok++;
                }
              else
                {
                  packets_dropped++;
                  next0 = TG_LINK_INPUT_NEXT_DROP;
                }
            }
          else
            {
              tg_sw_if_index0 = ~0;
              packets_dropped++;
              next0 = TG_LINK_INPUT_NEXT_DROP;
            }

          /* Increment stats for individual vpp-terra interfaces */
          if (PREDICT_TRUE (tg_sw_if_index0 != ~0))
            {
              /* Batch stat increments from the same vpp-terra interface so
               * counters don't need to be incremented for every packet. */
              len0 = b0->current_length;

              stats_n_packets += 1;
              stats_n_bytes += len0;

              if (tg_sw_if_index0 != stats_sw_if_index)
                {
                  stats_n_packets -= 1;
                  stats_n_bytes -= len0;

                  vlib_increment_combined_counter (
                      vnm->interface_main.combined_sw_if_counters +
                          VNET_INTERFACE_COUNTER_RX,
                      thread_index, tg_sw_if_index0, 1, len0);

                  /* Increment counters for currently batched interface and
                   * start batching for new interface. */
                  if (stats_n_packets > 0)
                    {
                      vlib_increment_combined_counter (
                          vnm->interface_main.combined_sw_if_counters +
                              VNET_INTERFACE_COUNTER_RX,
                          thread_index, stats_sw_if_index, stats_n_packets,
                          stats_n_bytes);
                      stats_n_packets = stats_n_bytes = 0;
                    }
                  stats_sw_if_index = tg_sw_if_index0;
                }
            }

          if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE) &&
                             (b0->flags & VLIB_BUFFER_IS_TRACED)))
            {
              tg_link_input_trace_t *t =
                  vlib_add_trace (vm, node, b0, sizeof (*t));
              t->bb_sw_if_index = bb_sw_if_index0;
              t->tg_sw_if_index = tg_sw_if_index0;
              t->next_index = next0;
            }

          /* verify speculative enqueue, maybe switch current next frame */
          vlib_validate_buffer_enqueue_x1 (vm, node, next_index, to_next,
                                           n_left_to_next, bi0, next0);
        }

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  /* Increment any remaining batched stats */
  if (stats_n_packets > 0)
    {
      vlib_increment_combined_counter (
          vnm->interface_main.combined_sw_if_counters +
              VNET_INTERFACE_COUNTER_RX,
          thread_index, stats_sw_if_index, stats_n_packets, stats_n_bytes);
      node->runtime_data[0] = stats_sw_if_index;
    }

  vlib_node_increment_counter (vm, node->node_index,
                               TG_LINK_INPUT_ERROR_DROPPED, packets_dropped);
  vlib_node_increment_counter (vm, node->node_index,
                               TG_LINK_INPUT_ERROR_FORWARDED, packets_ok);
  return frame->n_vectors;
}

/* *INDENT-OFF* */
VLIB_REGISTER_NODE (tg_link_input_node) = {
    .name = "tg-link-input",
    .vector_size = sizeof (u32),
    .format_trace = format_tg_link_input_trace,
    .type = VLIB_NODE_TYPE_INTERNAL,

    .n_errors = ARRAY_LEN (tg_link_input_error_strings),
    .error_strings = tg_link_input_error_strings,

    .n_next_nodes = TG_LINK_INPUT_N_NEXT,

    /* edit / add dispositions here */
    .next_nodes =
        {
            [TG_LINK_INPUT_NEXT_ETHERNET_INPUT] = "ethernet-input",
            [TG_LINK_INPUT_NEXT_DROP] = "error-drop",
        },
};
/* *INDENT-ON* */

/*
 * Slowpath receive processing. Check if receiving interface is one
 * of our special ones and forward the packet over to Linux,
 * otherwise let the packet continue into normal VPP processing
 * path.
 */

typedef struct
{
  u32 next_node_index;
  u32 rd_sw_if_index;
  u32 link_local_val;
} tg_slowpath_receive_trace_t;

/* packet trace format function */
static u8 *format_tg_slowpath_receive_trace (u8 *s, va_list *args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  tg_slowpath_receive_trace_t *t =
      va_arg (*args, tg_slowpath_receive_trace_t *);

  s = format (s, "  sw_if_index %u, next index %u, link_local %u",
              t->rd_sw_if_index, t->next_node_index, t->link_local_val);
  return s;
}

typedef enum
{
  TG_SLOWPATH_RECEIVE_NEXT_LOCAL_TX,
  TG_SLOWPATH_RECEIVE_N_NEXT,
} tg_slowpath_receive_next_t;

/* check if buffer is a neighbor discovery packet of the given type
 *
 * if there is a match, copy target address to *ta
 */
static inline bool tg_packet_check_for_nd (vlib_buffer_t *b0,
                                           icmp6_type_t type,
                                           ip6_address_t *ta)
{
  ethernet_header_t *e0;
  ip6_header_t *ip0;

  e0 = (void *)(b0->data + vnet_buffer (b0)->l2_hdr_offset);
  if (clib_net_to_host_u16 (e0->type) != ETHERNET_TYPE_IP6)
    return false;

  ip0 = vlib_buffer_get_current (b0);

  if (ip0->protocol == IP_PROTOCOL_ICMP6 &&
      clib_net_to_host_u16 (ip0->payload_length) > sizeof (icmp46_header_t) &&
      b0->current_length >= sizeof (icmp46_header_t) + sizeof (*ip0))
    {
      icmp6_neighbor_solicitation_or_advertisement_header_t *icmp_packet =
          (void *)(ip0 + 1);
      if (ta)
        *ta = icmp_packet->target_address;
      return (icmp_packet->icmp.type == type);
    }
  return false;
}

static bool tg_packet_is_test_ula (vlib_buffer_t *b0)
{
  ip6_address_t ta;
  tgcfg_main_t *tm = &tgcfg_main;
  ethernet_header_t *e0;
  ip6_header_t *ip0;

  /* As test ULA prefix is /64 we only need to compare first u64 in address */
  if (tg_packet_check_for_nd (b0, ICMP6_neighbor_solicitation, &ta))
    return ta.as_u64[0] == tm->ula_test_prefix.as_u64[0];

  if (tg_packet_check_for_nd (b0, ICMP6_neighbor_advertisement, &ta))
    return ta.as_u64[0] == tm->ula_test_prefix.as_u64[0];

  e0 = (void *)(b0->data + vnet_buffer (b0)->l2_hdr_offset);
  if (clib_net_to_host_u16 (e0->type) != ETHERNET_TYPE_IP6)
    return false;

  ip0 = vlib_buffer_get_current (b0);

  return ip0->dst_address.as_u64[0] == tm->ula_test_prefix.as_u64[0];
}

static uword tg_slowpath_receive (vlib_main_t *vm, vlib_node_runtime_t *node,
                                  vlib_frame_t *frame)
{
  tgcfg_main_t *tm;
  u32 n_left_from, *from, *to_next;
  tg_slowpath_receive_next_t next_index;

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;

  tm = &tgcfg_main;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
        {
          u32 next0, next1;
          u32 rd_sw_if_index0, rd_sw_if_index1;
          u32 bi0, bi1;
          vlib_buffer_t *b0, *b1;
          receive_dpo_t *dpo0, *dpo1;

          /* Prefetch next iteration. */
          {
            vlib_buffer_t *p2, *p3;

            p2 = vlib_get_buffer (vm, from[2]);
            p3 = vlib_get_buffer (vm, from[3]);

            vlib_prefetch_buffer_header (p2, LOAD);
            vlib_prefetch_buffer_header (p3, LOAD);

            CLIB_PREFETCH (p2->data, CLIB_CACHE_LINE_BYTES, STORE);
            CLIB_PREFETCH (p3->data, CLIB_CACHE_LINE_BYTES, STORE);
          }

          /* speculatively enqueue b0 and b1 to the current next frame */
          to_next[0] = bi0 = from[0];
          to_next[1] = bi1 = from[1];
          from += 2;
          to_next += 2;
          n_left_from -= 2;
          n_left_to_next -= 2;

          b0 = vlib_get_buffer (vm, bi0);
          b1 = vlib_get_buffer (vm, bi1);

          vnet_feature_next (&next0, b0);
          vnet_feature_next (&next1, b1);

          dpo0 = receive_dpo_get (vnet_buffer (b0)->ip.adj_index[VLIB_TX]);
          dpo1 = receive_dpo_get (vnet_buffer (b1)->ip.adj_index[VLIB_TX]);

          rd_sw_if_index0 = dpo0->rd_sw_if_index;
          rd_sw_if_index1 = dpo1->rd_sw_if_index;

          /*
           * Multicast packets are received through non-interface specific DPO
           * with ~0u as rd_sw_if_index
           */
          if ((rd_sw_if_index0 == ~0u ||
               tm->local_links[rd_sw_if_index0] != ~0u) &&
              !tg_packet_is_test_ula (b0))
            next0 = TG_SLOWPATH_RECEIVE_NEXT_LOCAL_TX;
          if ((rd_sw_if_index1 == ~0u ||
               tm->local_links[rd_sw_if_index1] != ~0u) &&
              !tg_packet_is_test_ula (b1))
            next1 = TG_SLOWPATH_RECEIVE_NEXT_LOCAL_TX;
          if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE) &&
                             (b0->flags & VLIB_BUFFER_IS_TRACED)))
            {
              tg_slowpath_receive_trace_t *t =
                  vlib_add_trace (vm, node, b0, sizeof (*t));
              t->rd_sw_if_index = rd_sw_if_index0;
              t->next_node_index = next0;
              t->link_local_val = (rd_sw_if_index0 == ~0u)
                                      ? ~0u
                                      : tm->local_links[rd_sw_if_index0];
            }
          if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE) &&
                             (b1->flags & VLIB_BUFFER_IS_TRACED)))
            {
              tg_slowpath_receive_trace_t *t =
                  vlib_add_trace (vm, node, b1, sizeof (*t));
              t->rd_sw_if_index = rd_sw_if_index1;
              t->next_node_index = next1;
              t->link_local_val = (rd_sw_if_index1 == ~0u)
                                      ? ~0u
                                      : tm->local_links[rd_sw_if_index1];
            }

          /* verify speculative enqueues, maybe switch current next frame */
          vlib_validate_buffer_enqueue_x2 (vm, node, next_index, to_next,
                                           n_left_to_next, bi0, bi1, next0,
                                           next1);
        }

      while (n_left_from > 0 && n_left_to_next > 0)
        {
          u32 next0;
          u32 rd_sw_if_index0;
          u32 bi0;
          vlib_buffer_t *b0;
          receive_dpo_t *dpo0;

          /* speculatively enqueue b0 and b1 to the current next frame */
          to_next[0] = bi0 = from[0];
          from += 1;
          to_next += 1;
          n_left_from -= 1;
          n_left_to_next -= 1;

          b0 = vlib_get_buffer (vm, bi0);

          vnet_feature_next (&next0, b0);

          dpo0 = receive_dpo_get (vnet_buffer (b0)->ip.adj_index[VLIB_TX]);

          rd_sw_if_index0 = dpo0->rd_sw_if_index;

          if ((rd_sw_if_index0 == ~0u ||
               tm->local_links[rd_sw_if_index0] != ~0u) &&
              !tg_packet_is_test_ula (b0))
            next0 = TG_SLOWPATH_RECEIVE_NEXT_LOCAL_TX;

          if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE) &&
                             (b0->flags & VLIB_BUFFER_IS_TRACED)))
            {
              tg_slowpath_receive_trace_t *t =
                  vlib_add_trace (vm, node, b0, sizeof (*t));
              t->rd_sw_if_index = rd_sw_if_index0;
              t->next_node_index = next0;
              t->link_local_val = (rd_sw_if_index0 == ~0u)
                                      ? ~0u
                                      : tm->local_links[rd_sw_if_index0];
            }

          /* verify speculative enqueues, maybe switch current next frame */
          vlib_validate_buffer_enqueue_x1 (vm, node, next_index, to_next,
                                           n_left_to_next, bi0, next0);
        }

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  return frame->n_vectors;
}

/* *INDENT-OFF* */
VLIB_REGISTER_NODE (tg_slowpath_terra_rx_node) = {
    .function = tg_slowpath_receive,
    .name = "tg-slowpath-terra-rx",
    .vector_size = sizeof (u32),
    .format_trace = format_tg_slowpath_receive_trace,
    .type = VLIB_NODE_TYPE_INTERNAL,

    .n_errors = 0,
    .error_strings = NULL,

    .n_next_nodes = TG_SLOWPATH_RECEIVE_N_NEXT,

    /* edit / add dispositions here */
    .next_nodes =
        {
            [TG_SLOWPATH_RECEIVE_NEXT_LOCAL_TX] = "tg-link-local-tx",
        },
};

VNET_FEATURE_INIT (tg_slowpath_terra_rx_ip6_node, static) = {
    .arc_name = "ip6-local",
    .node_name = "tg-slowpath-terra-rx",
    .runs_before = VNET_FEATURES ("ip6-local-end-of-arc"),
};

VLIB_REGISTER_NODE (tg_slowpath_wired_rx_node) = {
    .function = tg_slowpath_receive,
    .name = "tg-slowpath-wired-rx",
    .vector_size = sizeof (u32),
    .format_trace = format_tg_slowpath_receive_trace,
    .type = VLIB_NODE_TYPE_INTERNAL,

    .n_errors = 0,
    .error_strings = NULL,

    .n_next_nodes = TG_SLOWPATH_RECEIVE_N_NEXT,

    /* edit / add dispositions here */
    .next_nodes =
        {
            [TG_SLOWPATH_RECEIVE_NEXT_LOCAL_TX] = "tg-wired-local-tx",
        },
};

VNET_FEATURE_INIT (tg_slowpath_wired_rx_ip6_node, static) = {
    .arc_name = "ip6-local",
    .node_name = "tg-slowpath-wired-rx",
    .runs_before = VNET_FEATURES ("ip6-local-end-of-arc"),
};

/* *INDENT-ON* */

/**
 * IP local processing
 */
typedef enum tg_link_local_tx_next_t_
{
  TG_LINK_LOCAL_TX_N_NEXT,
} tg_link_local_tx_next_t;

#define foreach_tg_link_local_tx_error \
  _ (FORWARDED, "tg local tx packets") \
  _ (DROPPED, "tg local tx drop")

typedef enum
{
#define _(sym, str) TG_LINK_LOCAL_TX_ERROR_##sym,
  foreach_tg_link_local_tx_error
#undef _
      TG_LINK_LOCAL_TX_N_ERROR,
} tg_link_local_tx_error_t;

static char *tg_link_local_tx_error_strings[] = {
#define _(sym, string) string,
    foreach_tg_link_local_tx_error
#undef _
};

typedef struct
{
  u32 bb_sw_if_index;
  u32 bb_link_id;
} tg_link_local_tx_trace_t;

u8 *format_tg_link_local_tx_trace (u8 *s, va_list *args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  tg_link_local_tx_trace_t *t = va_arg (*args, tg_link_local_tx_trace_t *);

  s = format (s, "  BB sw_if_index %u, link_id %d", t->bb_sw_if_index,
              t->bb_link_id);
  return s;
}

static uword tg_link_local_tx (vlib_main_t *vm, vlib_node_runtime_t *node,
                               vlib_frame_t *frame)
{
  struct rte_mbuf *mbufs[VLIB_FRAME_SIZE];
  u32 n_left_from, *from;
  u32 packets_sent, packets_left;
  tgcfg_main_t *tm;
  vnet_main_t *vnm;

  tm = &tgcfg_main;
  vnm = tm->vnet_main;

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;

  while (n_left_from >= 4)
    {
      u32 tg_sw_if_index0, tg_sw_if_index1;
      u32 bi0, bi1;
      vlib_buffer_t *b0, *b1;
      vnet_hw_interface_t *hw0, *hw1;
      struct rte_mbuf *m0, *m1;

      {
        vlib_buffer_t *p2, *p3;

        p2 = vlib_get_buffer (vm, from[2]);
        p3 = vlib_get_buffer (vm, from[3]);

        vlib_prefetch_buffer_header (p2, LOAD);
        vlib_prefetch_buffer_header (p3, LOAD);

        CLIB_PREFETCH (p2->data, CLIB_CACHE_LINE_BYTES, STORE);
        CLIB_PREFETCH (p3->data, CLIB_CACHE_LINE_BYTES, STORE);
      }

      /* speculatively enqueue b0 and b1 to the current next frame */
      bi0 = from[0];
      bi1 = from[1];
      from += 2;
      n_left_from -= 2;

      b0 = vlib_get_buffer (vm, bi0);
      b1 = vlib_get_buffer (vm, bi1);

      VLIB_BUFFER_TRACE_TRAJECTORY_INIT (b0);
      VLIB_BUFFER_TRACE_TRAJECTORY_INIT (b1);

      tg_sw_if_index0 = vnet_buffer (b0)->sw_if_index[VLIB_RX];
      tg_sw_if_index1 = vnet_buffer (b1)->sw_if_index[VLIB_RX];

      /*
       * TODO: consider the direct map
       */
      hw0 = vnet_get_sup_hw_interface (vnm, tg_sw_if_index0);
      hw1 = vnet_get_sup_hw_interface (vnm, tg_sw_if_index1);

      m0 = rte_mbuf_from_vlib_buffer (b0);
      m1 = rte_mbuf_from_vlib_buffer (b1);

      wigig_mbuf_link_id_set (m0, hw0->dev_instance);
      wigig_mbuf_link_id_set (m1, hw1->dev_instance);

      if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE)))
        {
          if (b0->flags & VLIB_BUFFER_IS_TRACED)
            {
              tg_link_local_tx_trace_t *t =
                  vlib_add_trace (vm, node, b0, sizeof (*t));
              t->bb_link_id = wigig_mbuf_link_id_get (m0);
            }
          if (b1->flags & VLIB_BUFFER_IS_TRACED)
            {
              tg_link_local_tx_trace_t *t =
                  vlib_add_trace (vm, node, b1, sizeof (*t));
              t->bb_link_id = wigig_mbuf_link_id_get (m1);
            }
        }
    }

  while (n_left_from > 0)
    {
      u32 tg_sw_if_index0;
      u32 bi0;
      vlib_buffer_t *b0;
      vnet_hw_interface_t *hw0;
      struct rte_mbuf *m0;

      /* speculatively enqueue b0 to the current next frame */
      bi0 = from[0];
      from += 1;
      n_left_from -= 1;

      b0 = vlib_get_buffer (vm, bi0);

      VLIB_BUFFER_TRACE_TRAJECTORY_INIT (b0);

      tg_sw_if_index0 = vnet_buffer (b0)->sw_if_index[VLIB_RX];

      /*
       * TODO: consider the direct map
       */
      hw0 = vnet_get_sup_hw_interface (vnm, tg_sw_if_index0);

      m0 = rte_mbuf_from_vlib_buffer (b0);
      wigig_mbuf_link_id_set (m0, hw0->dev_instance);

      if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE) &&
                         (b0->flags & VLIB_BUFFER_IS_TRACED)))
        {
          tg_link_local_tx_trace_t *t =
              vlib_add_trace (vm, node, b0, sizeof (*t));
          t->bb_link_id = wigig_mbuf_link_id_get (m0);
        }
    }

  /* Translate buffers into mbufs */
  vlib_get_buffers_with_offset (vm, vlib_frame_vector_args (frame),
                                (void **)mbufs, frame->n_vectors,
                                -(i32)sizeof (struct rte_mbuf));
  /* Send packets to dhd local path to be injected into host stack */
  int n_sent = tm->wigig_ops->slowpath_tx (NULL, mbufs, frame->n_vectors);

  if (n_sent < 0)
    {
      packets_sent = 0;
      packets_left = frame->n_vectors;
    }
  else
    {
      packets_sent = n_sent;
      packets_left = frame->n_vectors - packets_sent;
    }
  /* Trace */

  /* If there is no callback then drop any non-transmitted packets */
  if (PREDICT_FALSE (packets_left))
    {
      /* Account for dropped packets */
      vlib_error_count (vm, node->node_index, TG_LINK_LOCAL_TX_ERROR_DROPPED,
                        packets_left);

      /* Free (drop) all buffers not accepted by Linux */
      vlib_buffer_free (vm,
                        (u32 *)vlib_frame_vector_args (frame) + packets_sent,
                        packets_left);
    }

  vlib_node_increment_counter (vm, node->node_index,
                               TG_LINK_LOCAL_TX_ERROR_FORWARDED, packets_sent);
  return packets_sent + packets_left;
}

/* *INDENT-OFF* */
VLIB_REGISTER_NODE (tg_link_local_tx_node, static) = {
    .function = tg_link_local_tx,
    .name = "tg-link-local-tx",
    .vector_size = sizeof (u32),
    .n_next_nodes = TG_LINK_LOCAL_TX_N_NEXT,
    .format_trace = format_tg_link_local_tx_trace,
    .n_errors = ARRAY_LEN (tg_link_local_tx_error_strings),
    .error_strings = tg_link_local_tx_error_strings,
};

/* *INDENT-ON* */

typedef enum
{
  TG_LINK_LOCAL_RX_PACKETS,
  TG_LINK_LOCAL_RX_N_ERROR,
} tg_link_local_rx_error_t;

static char *tg_link_local_rx_error_strings[TG_LINK_LOCAL_RX_N_ERROR] = {
    "tg local rx packets",
};

enum tg_link_local_rx_next_e
{
  TG_LINK_LOCAL_RX_NEXT_INTERFACE_OUTPUT,
  TG_LINK_LOCAL_RX_N_NEXT
};

typedef struct
{
  u32 next_index;
  u32 bb_sw_if_index;
  u32 bb_peer_id;
} tg_link_local_rx_trace_t;

static inline uword tg_link_local_rx_dev (vlib_main_t *vm,
                                          vlib_node_runtime_t *node,
                                          tgcfg_wdev_t *wdev)
{
  struct rte_mbuf *mbufs[VLIB_FRAME_SIZE];
  u32 next_index = TG_LINK_LOCAL_RX_NEXT_INTERFACE_OUTPUT;
  u32 n_trace;
  tgcfg_main_t *tm = &tgcfg_main;
  vlib_buffer_t *b0;
  int n_rx_packets, n;

  n_rx_packets =
      tm->wigig_ops->slowpath_rx (wdev->dev, mbufs, ARRAY_LEN (mbufs));
  if (n_rx_packets <= 0)
    return 0;

  for (n = 0; n < n_rx_packets; n++)
    {
      struct rte_mbuf *mb;
      tgcfg_link_t *li;

      mb = mbufs[n];

      /*
       * Initialize the buffer from mbuf. Since that is coming from
       * the kernel, assume nothing and just pass the data verbatim
       * to interface-output.
       */
      b0 = vlib_buffer_from_rte_mbuf (mb);
      b0->current_data = 0;
      b0->current_length = rte_pktmbuf_data_len (mb);
      b0->total_length_not_including_first_buffer = 0;
      b0->flags = VLIB_BUFFER_TOTAL_LENGTH_VALID;

      /*
       * Wigig expects peer_id in link_id dynfield, translate absolute index
       * from slowpath into peer_id using cached data
       */
      li = vec_elt_at_index (tm->terra_links, wigig_mbuf_link_id_get (mb));
      wigig_mbuf_link_id_set (mb, li->tg_peer_id);

      vnet_buffer (b0)->sw_if_index[VLIB_TX] = wdev->sw_if_index;
      vnet_buffer (b0)->sw_if_index[VLIB_RX] = wdev->sw_if_index;
    }

  u32 *to_next, n_left_to_next;

  vlib_get_new_next_frame (vm, node, next_index, to_next, n_left_to_next);
  vlib_get_buffer_indices_with_offset (vm, (void **)mbufs, to_next,
                                       n_rx_packets, sizeof (struct rte_mbuf));
  /* packet trace if enabled */
  if (PREDICT_FALSE ((n_trace = vlib_get_trace_count (vm, node))))
    {
      struct rte_mbuf **mb;
      u32 n_left;

      n_left = n_rx_packets;
      mb = &mbufs[0];
      while (n_trace && n_left)
        {
          b0 = vlib_get_buffer (vm, to_next[0]);
          vlib_trace_buffer (vm, node, next_index, b0, /* follow_chain */ 0);

          tg_link_local_rx_trace_t *t0 =
              vlib_add_trace (vm, node, b0, sizeof t0[0]);
          t0->bb_sw_if_index = wdev->sw_if_index;
          t0->bb_peer_id = wigig_mbuf_link_id_get (mb[0]);
          t0->next_index = next_index;

          n_trace--;
          n_left--;
          to_next++;
          mb++;
        }
      vlib_set_trace_count (vm, node, n_trace);
    }

  n_left_to_next -= n_rx_packets;
  vlib_put_next_frame (vm, node, next_index, n_left_to_next);

  vlib_node_increment_counter (vm, node->node_index, TG_LINK_LOCAL_RX_PACKETS,
                               n_rx_packets);
  return n_rx_packets;
}

static uword tg_link_local_rx (vlib_main_t *vm, vlib_node_runtime_t *node,
                               vlib_frame_t *frame)
{
  tgcfg_main_t *tm = &tgcfg_main;
  u32 total_count = 0;
  int i;

  for (i = 0; i < vec_len (tm->wigig_devs); i++)
    {
      if (!tm->wigig_devs[i].rx_ready)
        continue;
      tm->wigig_devs[i].rx_ready = false;
      total_count += tg_link_local_rx_dev (vm, node, &tm->wigig_devs[i]);
    }
  return total_count;
}

u8 *format_tg_link_local_rx_trace (u8 *s, va_list *args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  tg_link_local_rx_trace_t *t = va_arg (*args, tg_link_local_rx_trace_t *);

  s = format (s, "  BB sw_if_index %u, peer_id %d", t->bb_sw_if_index,
              t->bb_peer_id);
  return s;
}

/* *INDENT-OFF* */
VLIB_REGISTER_NODE (tg_link_local_rx_node, static) = {
    .function = tg_link_local_rx,
    .name = "tg-link-local-rx",
    .type = VLIB_NODE_TYPE_INPUT,
    .state = VLIB_NODE_STATE_INTERRUPT,
    .vector_size = sizeof (u32),
    .n_errors = TG_LINK_LOCAL_RX_N_ERROR,
    .error_strings = tg_link_local_rx_error_strings,
    .n_next_nodes = TG_LINK_LOCAL_RX_N_NEXT,
    .next_nodes =
        {
            [TG_LINK_LOCAL_RX_NEXT_INTERFACE_OUTPUT] = "interface-output",
        },
    .format_trace = format_tg_link_local_rx_trace,
};
/* *INDENT-ON* */

clib_error_t *tg_link_local_rx_fd_read_ready (clib_file_t *uf)
{
  tgcfg_main_t *tm = &tgcfg_main;
  vlib_main_t *vm = tm->vlib_main;

  if (uf->private_data < vec_len (tm->wigig_devs))
    {
      /* Mark which device is being ready */
      tm->wigig_devs[uf->private_data].rx_ready = true;
      /* Schedule the rx node */
      vlib_node_set_interrupt_pending (vm, tg_link_local_rx_node.index);
    }
  return 0;
}

/**
 * IP local processing for wired interfaces
 */
typedef enum tg_wired_local_tx_next_t_
{
  TG_WIRED_LOCAL_TX_NEXT_INTERFACE_OUTPUT,
  TG_WIRED_LOCAL_TX_NEXT_BYPASS,
  TG_WIRED_LOCAL_TX_NEXT_DROP,
  TG_WIRED_LOCAL_TX_N_NEXT,
} tg_wired_local_tx_next_t;

#define foreach_tg_wired_local_tx_error \
  _ (FORWARDED, "tg wired local sent")  \
  _ (DROPPED, "tg wired local drop")

typedef enum
{
#define _(sym, str) TG_WIRED_LOCAL_TX_ERROR_##sym,
  foreach_tg_wired_local_tx_error
#undef _
      TG_WIRED_LOCAL_TX_N_ERROR,
} tg_wired_local_tx_error_t;

static char *tg_wired_local_tx_error_strings[] = {
#define _(sym, string) string,
    foreach_tg_wired_local_tx_error
#undef _
};

typedef struct
{
  u32 src_sw_if_index;
  u32 dst_sw_if_index;
} tg_wired_local_tx_trace_t;

u8 *format_tg_wired_local_tx_trace (u8 *s, va_list *args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  tg_wired_local_tx_trace_t *t = va_arg (*args, tg_wired_local_tx_trace_t *);

  s = format (s, "  src sw_if_index %u, dst sw_if_index %u",
              t->src_sw_if_index, t->dst_sw_if_index);
  return s;
}

/* Common implementation of RX/TX path */
static void tg_wired_handle_na (vlib_main_t *vm, vlib_node_runtime_t *node,
                                u32 *from, u32 n_left_from)
{
  u32 *to_next, n_left_to_next, next_index, next0;

  /* Figure out where the buffer is to go next */
  next_index = TG_WIRED_LOCAL_TX_NEXT_BYPASS;

  while (n_left_from > 0)
    {
      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from > 0 && n_left_to_next > 0)
        {
          u32 bi0;
          vlib_buffer_t *b0;

          bi0 = from[0];
          from += 1;
          n_left_from -= 1;

          b0 = vlib_get_buffer (vm, bi0);

          next0 = TG_WIRED_LOCAL_TX_NEXT_BYPASS;

          to_next[0] = bi0;
          to_next += 1;
          n_left_to_next -= 1;

          if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE) &&
                             (b0->flags & VLIB_BUFFER_IS_TRACED)))
            {
              tg_wired_local_tx_trace_t *t =
                  vlib_add_trace (vm, node, b0, sizeof (*t));
              t->src_sw_if_index = next0;
              t->dst_sw_if_index = 0;
            }

          vlib_validate_buffer_enqueue_x1 (vm, node, next_index, to_next,
                                           n_left_to_next, bi0, next0);
          if (PREDICT_FALSE (n_left_to_next == 0))
            {
              vlib_put_next_frame (vm, node, next_index, n_left_to_next);
              vlib_get_next_frame (vm, node, next_index, to_next,
                                   n_left_to_next);
            }
        }

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }
}

static inline uword tg_wired_local_impl (vlib_main_t *vm,
                                         vlib_node_runtime_t *node,
                                         vlib_frame_t *frame, bool is_tx)
{
  tgcfg_main_t *tm;
  u32 n_left_from, *from, *to_next;
  tg_wired_local_tx_next_t next_index;
  u32 packets_ok = 0, packets_dropped = 0, packets_na = 0;
  u32 na_buffer[VLIB_FRAME_SIZE];

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;

  tm = &tgcfg_main;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from >= 4 && n_left_to_next >= 2)
        {
          u32 next0 = TG_WIRED_LOCAL_TX_NEXT_INTERFACE_OUTPUT;
          u32 next1 = TG_WIRED_LOCAL_TX_NEXT_INTERFACE_OUTPUT;
          u32 src_sw_if_index0, src_sw_if_index1;
          u32 dst_sw_if_index0, dst_sw_if_index1;
          u32 bi0, bi1;
          vlib_buffer_t *b0, *b1;

          /* Prefetch next iteration. */
          {
            vlib_buffer_t *p2, *p3;

            p2 = vlib_get_buffer (vm, from[2]);
            p3 = vlib_get_buffer (vm, from[3]);

            vlib_prefetch_buffer_header (p2, LOAD);
            vlib_prefetch_buffer_header (p3, LOAD);

            CLIB_PREFETCH (p2->data, CLIB_CACHE_LINE_BYTES, STORE);
            CLIB_PREFETCH (p3->data, CLIB_CACHE_LINE_BYTES, STORE);
          }

          /* speculatively enqueue b0 and b1 to the current next frame */
          to_next[0] = bi0 = from[0];
          to_next[1] = bi1 = from[1];
          from += 2;
          to_next += 2;
          n_left_from -= 2;
          n_left_to_next -= 2;

          b0 = vlib_get_buffer (vm, bi0);
          b1 = vlib_get_buffer (vm, bi1);

          /*
           * Clone neighbour advertisements and send them both to VPP and Linux
           * at the same time.
           */
          if (is_tx &&
              tg_packet_check_for_nd (b0, ICMP6_neighbor_advertisement, NULL))
            {
              vlib_buffer_t *c0;
              u32 ci0;

              c0 = vlib_buffer_copy (vm, b0);
              if (c0 != NULL)
                {
                  ci0 = vlib_get_buffer_index (vm, c0);
                  vnet_buffer (c0)->feature_arc_index =
                      vnet_buffer (b0)->feature_arc_index;
                  vlib_buffer_copy_trace_flag (vm, b0, ci0);
                  VLIB_BUFFER_TRACE_TRAJECTORY_INIT (c0);
                  na_buffer[packets_na++] = ci0;
                }
            }

          if (is_tx &&
              tg_packet_check_for_nd (b1, ICMP6_neighbor_advertisement, NULL))
            {
              vlib_buffer_t *c1;
              u32 ci1;

              c1 = vlib_buffer_copy (vm, b1);
              if (c1 != NULL)
                {
                  ci1 = vlib_get_buffer_index (vm, c1);
                  vnet_buffer (c1)->feature_arc_index =
                      vnet_buffer (b1)->feature_arc_index;
                  vlib_buffer_copy_trace_flag (vm, b1, ci1);
                  VLIB_BUFFER_TRACE_TRAJECTORY_INIT (c1);
                  na_buffer[packets_na++] = ci1;
                }
            }

          src_sw_if_index0 = vnet_buffer (b0)->sw_if_index[VLIB_RX];
          src_sw_if_index1 = vnet_buffer (b1)->sw_if_index[VLIB_RX];

          if (PREDICT_TRUE (src_sw_if_index0 < vec_len (tm->wired_links)))
            {
              tgcfg_wired_t *li;

              li = vec_elt_at_index (tm->wired_links, src_sw_if_index0);
              dst_sw_if_index0 =
                  is_tx ? li->tap_sw_if_index : li->eth_sw_if_index;
              if (PREDICT_TRUE (dst_sw_if_index0 != ~0))
                {
                  vlib_buffer_reset (b0);

                  vnet_buffer (b0)->sw_if_index[VLIB_TX] = dst_sw_if_index0;
                  packets_ok++;
                }
              else
                {
                  packets_dropped++;
                  next0 = TG_WIRED_LOCAL_TX_NEXT_DROP;
                }
            }
          else
            {
              dst_sw_if_index0 = ~0;
              packets_dropped++;
              next0 = TG_WIRED_LOCAL_TX_NEXT_DROP;
            }

          if (PREDICT_TRUE (src_sw_if_index1 < vec_len (tm->wired_links)))
            {
              tgcfg_wired_t *li;

              li = vec_elt_at_index (tm->wired_links, src_sw_if_index1);
              dst_sw_if_index1 =
                  is_tx ? li->tap_sw_if_index : li->eth_sw_if_index;
              if (PREDICT_TRUE (dst_sw_if_index1 != ~0))
                {
                  vlib_buffer_reset (b1);
                  vnet_buffer (b1)->sw_if_index[VLIB_TX] = dst_sw_if_index1;
                  packets_ok++;
                }
              else
                {
                  packets_dropped++;
                  next1 = TG_WIRED_LOCAL_TX_NEXT_DROP;
                }
            }
          else
            {
              dst_sw_if_index1 = ~0;
              packets_dropped++;
              next1 = TG_WIRED_LOCAL_TX_NEXT_DROP;
            }

          if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE)))
            {
              if (b0->flags & VLIB_BUFFER_IS_TRACED)
                {
                  tg_wired_local_tx_trace_t *t =
                      vlib_add_trace (vm, node, b0, sizeof (*t));
                  t->src_sw_if_index = src_sw_if_index0;
                  t->dst_sw_if_index = dst_sw_if_index0;
                }
              if (b1->flags & VLIB_BUFFER_IS_TRACED)
                {
                  tg_wired_local_tx_trace_t *t =
                      vlib_add_trace (vm, node, b1, sizeof (*t));
                  t->src_sw_if_index = src_sw_if_index1;
                  t->dst_sw_if_index = dst_sw_if_index1;
                }
            }

          /* verify speculative enqueues, maybe switch current next frame */
          vlib_validate_buffer_enqueue_x2 (vm, node, next_index, to_next,
                                           n_left_to_next, bi0, bi1, next0,
                                           next1);
        }

      while (n_left_from > 0 && n_left_to_next > 0)
        {
          u32 bi0;
          vlib_buffer_t *b0;
          u32 next0 = TG_WIRED_LOCAL_TX_NEXT_INTERFACE_OUTPUT;
          u32 src_sw_if_index0;
          u32 dst_sw_if_index0;

          /* speculatively enqueue b0 to the current next frame */
          bi0 = from[0];
          to_next[0] = bi0;
          from += 1;
          to_next += 1;
          n_left_from -= 1;
          n_left_to_next -= 1;

          b0 = vlib_get_buffer (vm, bi0);

          if (is_tx &&
              tg_packet_check_for_nd (b0, ICMP6_neighbor_advertisement, NULL))
            {
              vlib_buffer_t *c0;
              u32 ci0;

              c0 = vlib_buffer_copy (vm, b0);
              if (c0 != NULL)
                {
                  ci0 = vlib_get_buffer_index (vm, c0);
                  vnet_buffer (c0)->feature_arc_index =
                      vnet_buffer (b0)->feature_arc_index;
                  vlib_buffer_copy_trace_flag (vm, b0, ci0);
                  VLIB_BUFFER_TRACE_TRAJECTORY_INIT (c0);
                  na_buffer[packets_na++] = ci0;
                }
            }

          src_sw_if_index0 = vnet_buffer (b0)->sw_if_index[VLIB_RX];

          if (PREDICT_TRUE (src_sw_if_index0 < vec_len (tm->wired_links)))
            {
              tgcfg_wired_t *li;

              li = vec_elt_at_index (tm->wired_links, src_sw_if_index0);
              dst_sw_if_index0 =
                  is_tx ? li->tap_sw_if_index : li->eth_sw_if_index;
              if (PREDICT_TRUE (dst_sw_if_index0 != ~0))
                {
                  vlib_buffer_reset (b0);
                  vnet_buffer (b0)->sw_if_index[VLIB_TX] = dst_sw_if_index0;
                  packets_ok++;
                }
              else
                {
                  packets_dropped++;
                  next0 = TG_WIRED_LOCAL_TX_NEXT_DROP;
                }
            }
          else
            {
              dst_sw_if_index0 = ~0;
              packets_dropped++;
              next0 = TG_WIRED_LOCAL_TX_NEXT_DROP;
            }

          if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE) &&
                             (b0->flags & VLIB_BUFFER_IS_TRACED)))
            {
              tg_wired_local_tx_trace_t *t =
                  vlib_add_trace (vm, node, b0, sizeof (*t));
              t->src_sw_if_index = src_sw_if_index0;
              t->dst_sw_if_index = dst_sw_if_index0;
            }

          /* verify speculative enqueue, maybe switch current next frame */
          vlib_validate_buffer_enqueue_x1 (vm, node, next_index, to_next,
                                           n_left_to_next, bi0, next0);
        }

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  if (PREDICT_FALSE (packets_na > 0))
    {
      tg_wired_handle_na (vm, node, na_buffer, packets_na);
    }

  vlib_node_increment_counter (
      vm, node->node_index, TG_WIRED_LOCAL_TX_ERROR_DROPPED, packets_dropped);
  vlib_node_increment_counter (vm, node->node_index,
                               TG_WIRED_LOCAL_TX_ERROR_FORWARDED, packets_ok);
  return frame->n_vectors;
}

static uword tg_wired_local_tx (vlib_main_t *vm, vlib_node_runtime_t *node,
                                vlib_frame_t *frame)
{
  return tg_wired_local_impl (vm, node, frame, true);
}

/* *INDENT-OFF* */
VLIB_REGISTER_NODE (tg_wired_local_tx_node, static) = {
    .function = tg_wired_local_tx,
    .name = "tg-wired-local-tx",
    .vector_size = sizeof (u32),
    .format_trace = format_tg_wired_local_tx_trace,
    .n_errors = ARRAY_LEN (tg_wired_local_tx_error_strings),
    .error_strings = tg_wired_local_tx_error_strings,
    .n_next_nodes = TG_WIRED_LOCAL_TX_N_NEXT,
    .next_nodes =
        {
            [TG_WIRED_LOCAL_TX_NEXT_INTERFACE_OUTPUT] = "interface-output",
            [TG_WIRED_LOCAL_TX_NEXT_BYPASS] = "ip6-local-end-of-arc",
            [TG_WIRED_LOCAL_TX_NEXT_DROP] = "error-drop",
        },
};
/* *INDENT-ON* */

VNET_FEATURE_INIT (tg_wired_local_tx_ip6_node, static) = {
    .arc_name = "ip6-local",
    .node_name = "tg-wired-local-tx",
    .runs_before = VNET_FEATURES ("ip6-local-end-of-arc"),
};

static uword tg_wired_local_rx (vlib_main_t *vm, vlib_node_runtime_t *node,
                                vlib_frame_t *frame)
{
  return tg_wired_local_impl (vm, node, frame, false);
}

/* *INDENT-OFF* */
VLIB_REGISTER_NODE (tg_wired_local_rx_node, static) = {
    .function = tg_wired_local_rx,
    .name = "tg-wired-local-rx",
    .vector_size = sizeof (u32),
    .format_trace = format_tg_wired_local_tx_trace,
    .n_errors = ARRAY_LEN (tg_wired_local_tx_error_strings),
    .error_strings = tg_wired_local_tx_error_strings,
    .n_next_nodes = TG_WIRED_LOCAL_TX_N_NEXT,
    .next_nodes =
        {
            [TG_WIRED_LOCAL_TX_NEXT_INTERFACE_OUTPUT] = "interface-output",
            [TG_WIRED_LOCAL_TX_NEXT_BYPASS] = "ip6-local-end-of-arc",
            [TG_WIRED_LOCAL_TX_NEXT_DROP] = "error-drop",
        },
};
/* *INDENT-ON* */

VNET_FEATURE_INIT (tg_wired_local_rx_node, static) = {
    .arc_name = "device-input",
    .node_name = "tg-wired-local-rx",
    .runs_before = VNET_FEATURES ("ethernet-input"),
};

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
