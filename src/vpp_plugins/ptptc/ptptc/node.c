/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * node.c - PTP Transparent Clock frame processing
 */
#include <arpa/inet.h>
#include <ptptc/ptptc.h>

#define PTP_EVENT 319
#define PTP_GENERAL 320
#define ONE_BILLION 1000000000.0

extern ptptc_main_t ptptc_main;

enum ptp_message_type
{
  PTP_TYPE_SYNC = 0,
  PTP_TYPE_DELAY_REQ = 1,
  PTP_TYPE_FOLLOW_UP = 8,
  PTP_TYPE_DELAY_RESP = 9,
};

struct ptp_header
{
  u8 message_type;
  u8 version_ptp;
  u16 message_length;
  u8 domain_number;
  u8 reserved;
  u16 flag_field;
  u64 correction_field;
  u32 message_type_specific;
  u8 source_port_identity[10];
  u16 sequence_id;
  u8 control_field;
  u8 log_message_interval;
} __attribute__ ((packed));

typedef struct
{
  u64 timestamp;
  u64 cur_ts;
  u32 sw_if_index;
  u16 dport;
  u8 hw_port;
  bool mark;
  bool is_tx;
  struct ptp_header header;
} ptptc_trace_t;

/* This function assume that the packet source is dpdk-input */
always_inline struct rte_mbuf *vlib_to_mbuf (vlib_buffer_t *b)
{
  return (((struct rte_mbuf *)b) - 1);
}

/**
 * @Description This function is based on hardware offload flags.
 *               Returns true if a ptp packet flag is set, false otherwise.
 */
always_inline bool validate_ptp_packet (vlib_buffer_t *b)
{
  return vlib_to_mbuf (b)->ol_flags & PKT_RX_IEEE1588_PTP;
}

always_inline u64 vlib_get_timestamp (vlib_buffer_t *b)
{
  return *RTE_MBUF_DYNFIELD (vlib_to_mbuf (b), ptptc_timestamp_dynfield_offset,
                             rte_mbuf_timestamp_t *);
}

always_inline void vlib_set_timestamp (vlib_buffer_t *b, u64 timestamp)
{
  struct rte_mbuf *mbuf = vlib_to_mbuf (b);

  /* hack: if source interface is not dpdk, then we need to init mbuf
   * (cf. dpdk_validate_rte_mbuf) -- otherwise dpdk plugin will clear
   * offload flags before transmit.
   *
   * The proper way to do this would be to have vlib copy timestamp
   * to and from dpdk, and add new VNET_BUFFER offload flags for this,
   * but that is for another day.
   */
  if (PREDICT_FALSE (!(b->flags & VLIB_BUFFER_EXT_HDR_VALID)))
    {
      rte_pktmbuf_reset (mbuf);
      b->flags |= VLIB_BUFFER_EXT_HDR_VALID;
    }

  mbuf->ol_flags |= PKT_TX_IEEE1588_TMST;
  *RTE_MBUF_DYNFIELD (mbuf, ptptc_timestamp_dynfield_offset,
                      rte_mbuf_timestamp_t *) = timestamp;
}

#ifndef CLIB_MARCH_VARIANT

always_inline u8 *format_ptp_header (u8 *s, va_list *args)
{
  struct ptp_header *p = va_arg (*args, struct ptp_header *);

  s = format (s,
              "message_type %d, "
              "version_ptp %d, "
              "message_length %d, "
              "domain_number %d, "
              "flag_field %d, "
              "correction_field %llu, "
              "message_type_specific 0x%x, "
              "sequence_id %d, "
              "control_field %d, "
              "log_message_interval %d",
              p->message_type & 0x0f, p->version_ptp & 0x0f,
              ntohs (p->message_length), p->domain_number,
              ntohs (p->flag_field),
              (unsigned long long)be64toh (p->correction_field),
              be32toh (p->message_type_specific), ntohs (p->sequence_id),
              p->control_field, p->log_message_interval);
  return s;
}

/* packet trace format function */
always_inline u8 *format_ptptc_trace (u8 *s, va_list *args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  ptptc_trace_t *t = va_arg (*args, ptptc_trace_t *);

  s = format (s,
              "PTPTC: sw_if_index %d, dport %d, "
              "mark: %d, is_tx: %d, "
              "hw_port: %d, "
              "rx_ts 0x%" PRIx64 ", "
              "cur_ts 0x%" PRIx64 "\n",
              t->sw_if_index, t->dport, t->mark, t->is_tx, t->hw_port,
              t->timestamp, t->cur_ts);
  if (t->mark)
    {
      s = format (s, "  ptp header { %U }", format_ptp_header, &t->header);
    }
  return s;
}

#endif /* CLIB_MARCH_VARIANT */

#define foreach_ptptc_error _ (HANDLED, "PTP packets processed")

typedef enum
{
#define _(sym, str) PTPTC_ERROR_##sym,
  foreach_ptptc_error
#undef _
      PTPTC_N_ERROR,
} ptptc_error_t;

#ifndef CLIB_MARCH_VARIANT
static char *ptptc_error_strings[] = {
#define _(sym, string) string,
    foreach_ptptc_error
#undef _
};
#endif /* CLIB_MARCH_VARIANT */

/* Configures the internal PTPTC's packet drop index */
typedef enum
{
  PTPTC_NEXT_DROP,
  PTPTC_N_NEXT,
} ptptc_next_t;

always_inline u32 validate_checksum (vlib_main_t *vm, vlib_buffer_t *p0,
                                     ip6_header_t *ip0, udp_header_t *udp0)
{
  u16 sum16;
  int bogus_length;

  if (udp0->checksum == 0)
    {
      p0->flags |= (VNET_BUFFER_F_L4_CHECKSUM_COMPUTED |
                    VNET_BUFFER_F_L4_CHECKSUM_CORRECT);
      return p0->flags;
    }

  sum16 = ip6_tcp_udp_icmp_compute_checksum (vm, p0, ip0, &bogus_length);

  p0->flags |= (VNET_BUFFER_F_L4_CHECKSUM_COMPUTED |
                ((sum16 == 0) << VNET_BUFFER_F_LOG2_L4_CHECKSUM_CORRECT));

  return p0->flags;
}

always_inline bool ptptc_set_timestamp (vlib_buffer_t *buf,
                                        struct ptp_header *header,
                                        u32 *pkts_handled)
{
  ptptc_main_t *pmp = &ptptc_main;
  u8 message_type = header->message_type & 0x0f;

  if (message_type == PTP_TYPE_SYNC || message_type == PTP_TYPE_DELAY_REQ)
    {

      /* Specifies a handling of PTP sync or delay-req packets */
      *pkts_handled += 1;

      if (vnet_buffer (buf)->feature_arc_index != pmp->egress_index)
        { /*Ingress*/

          /* Inject the packet timestamp into the PTP header message */
          header->message_type_specific =
              clib_host_to_net_u32 ((u32)vlib_get_timestamp (buf));

          return true;
        }
      else
        { /* Egress */

          /* Read the hardware timestamp with offset from 1588Timer block */
          u64 cur_ts =
              dpaa2_get_current_timestamp_with_offset (pmp->wriop_regs);
          u32 cur_ts_low = cur_ts & 0xffffffff;
          u32 recover_ts_low =
              clib_net_to_host_u32 (header->message_type_specific);
          u32 diff = cur_ts_low - recover_ts_low;
          u64 recover_ts = cur_ts - diff;

          /* apply fixes for unaccounted delay and clock offset */
          recover_ts -= pmp->timing_offset;
          recover_ts += (int64_t) (diff * (pmp->clk_offset_ppb / ONE_BILLION));

          /*Message type specific must be zero before the egress*/
          header->message_type_specific = 0;

          if (PREDICT_TRUE (pmp->use_hw_timestamping))
            {
              /* hw timestamping, pass ts back to driver */
              vlib_set_timestamp (buf, recover_ts);
            }
          else
            {
              /* sw timestamping: set correction field directly */
              header->correction_field +=
                  clib_host_to_net_u64 ((cur_ts - recover_ts) << 16);

              return true;
            }
        }
    }

  return false;
}

always_inline uword ptptc_node_inline (vlib_main_t *vm,
                                       vlib_node_runtime_t *node,
                                       vlib_frame_t *frame)
{
  volatile ptptc_main_t *pmp = &ptptc_main;
  ptptc_next_t next_index;
  u32 pkts_handled = 0;
  u32 n_left_from, *from, *to_next;

  from = vlib_frame_vector_args (frame);
  n_left_from = frame->n_vectors;
  next_index = node->cached_next_index;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      /* Processed two element at a time */
      while (n_left_from >= 4 && n_left_to_next >= 2)
        {
          vlib_buffer_t *b0, *b1;
          ip6_header_t *ip60, *ip61;
          udp_header_t *udp0 = 0;
          udp_header_t *udp1 = 0;
          struct ptp_header *header0 = 0;
          struct ptp_header *header1 = 0;
          bool mark0 = false;
          bool mark1 = false;
          u32 ip_len0, udp_len0, ol_flags0, flags0, next0;
          u32 ip_len1, udp_len1, ol_flags1, flags1, next1;
          i32 len_diff0, len_diff1;
          u32 bi0, bi1;
          u32 udp_offset0 = 0;
          u32 iph_offset0 = 0;
          u32 ip_protocol0 = 0;
          u32 udp_offset1 = 0;
          u32 iph_offset1 = 0;
          u32 ip_protocol1 = 0;
          u32 error0 = 0;
          u32 error1 = 0;
          u32 good_l4_csum0 = 0;
          u32 good_l4_csum1 = 0;

          /* Prefetch next iteration. */
          {
            vlib_buffer_t *p2, *p3;

            p2 = vlib_get_buffer (vm, from[2]);
            p3 = vlib_get_buffer (vm, from[3]);

            vlib_prefetch_buffer_header (p2, STORE);
            vlib_prefetch_buffer_header (p3, STORE);

            CLIB_PREFETCH (p2->data, sizeof (*ip60), STORE);
            CLIB_PREFETCH (p3->data, sizeof (*ip61), STORE);
          }

          /* speculatively enqueue b0 and b1 to the current next frame */
          to_next[0] = bi0 = from[0];
          to_next[1] = bi1 = from[1];
          from += 2;
          to_next += 2;
          n_left_from -= 2;
          n_left_to_next -= 2;

          b0 = vlib_get_buffer (vm, bi0);

          vnet_feature_next (&next0, b0);

          if (vnet_buffer (b0)->feature_arc_index != pmp->egress_index)
            { /* Ingress Traffic */
              /* HW offload flags */
              ol_flags0 = vlib_to_mbuf (b0)->ol_flags;

              if (PREDICT_TRUE (pmp->rx_ptp_classify_offload_capa &&
                                ((ol_flags0 & PKT_RX_IEEE1588_PTP) == 0)))
                goto exit0;

              if (pmp->rx_checksum_offload_capa)
                {
                  good_l4_csum0 =
                      PREDICT_TRUE ((ol_flags0 & PKT_RX_L4_CKSUM_BAD) == 0);
                  if (!good_l4_csum0)
                    goto exit0;
                }
            }
          else
            { /* Egress Traffic */
              /* offset associated with the location ipv6 */
              iph_offset0 = vnet_buffer (b0)->ip.save_rewrite_length;
            }

          /* Retrieve pointer to the beginning of the IPv6 header */
          ip60 = (ip6_header_t *)((u8 *)vlib_buffer_get_current (b0) +
                                  iph_offset0);

          /* locate the next UDP header through the packet */
          ip_protocol0 =
              ip6_locate_header (b0, ip60, IP_PROTOCOL_UDP, &udp_offset0);

          /* Filter out non UDP packets */
          if (PREDICT_FALSE (ip_protocol0 != IP_PROTOCOL_UDP))
            goto exit0;

          /* Retrieve the pointer to UDP header */
          udp0 = (udp_header_t *)((u8 *)ip60 + udp_offset0);
          /* check for error based on the length */
          ip_len0 = clib_net_to_host_u16 (ip60->payload_length);
          udp_len0 = clib_net_to_host_u16 (udp0->length);
          len_diff0 = ip_len0 - udp_len0;

          error0 = len_diff0 >= 0 ? 0 : IP6_ERROR_UDP_LENGTH;
          if (PREDICT_FALSE (error0))
            goto exit0;

          /* Filter through the non PTP packets based on the PTP port */
          if (PREDICT_TRUE (udp0->dst_port !=
                            clib_host_to_net_u16 (PTP_EVENT)))
            goto exit0;

          /* Check for checksum validation flags */
          flags0 = b0->flags;
          good_l4_csum0 |=
              PREDICT_TRUE ((flags0 & VNET_BUFFER_F_L4_CHECKSUM_CORRECT ||
                             flags0 & VNET_BUFFER_F_OFFLOAD_UDP_CKSUM) != 0);

          /* if not validated and not computed, validate checksum */
          if (PREDICT_FALSE (!good_l4_csum0 &&
                             !(flags0 & VNET_BUFFER_F_L4_CHECKSUM_COMPUTED)))
            {
              flags0 = validate_checksum (vm, b0, ip60, udp0);
              good_l4_csum0 = PREDICT_TRUE (
                  (flags0 & VNET_BUFFER_F_L4_CHECKSUM_CORRECT) != 0);
            }

          /* if checksum not good, set as error */
          error0 = good_l4_csum0 ? 0 : IP6_ERROR_UDP_CHECKSUM;
          if (PREDICT_FALSE (error0))
            goto exit0;

          header0 = (struct ptp_header *)((u8 *)udp0 + sizeof (*udp0));
          /* perform ptptc packet update, and compute checksum if necessary */
          if (PREDICT_TRUE (ptptc_set_timestamp (b0, header0, &pkts_handled)))
            {
              int bogus_length;
              udp0->checksum = 0;
              udp0->checksum = ip6_tcp_udp_icmp_compute_checksum (
                  vm, b0, ip60, &bogus_length);
            }

          mark0 = true;

        exit0:
          next0 = PREDICT_FALSE (error0) ? PTPTC_NEXT_DROP : next0;
          b0->error = PREDICT_FALSE (error0) ? node->errors[error0] : 0;

          if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE) &&
                             (b0->flags & VLIB_BUFFER_IS_TRACED)))
            {
              ptptc_trace_t *t = vlib_add_trace (vm, node, b0, sizeof (*t));
              t->timestamp = vlib_get_timestamp (b0);
              t->cur_ts = t->timestamp;
              t->sw_if_index = vnet_buffer (b0)->sw_if_index[VLIB_RX];
              t->mark = mark0;
              if (udp0)
                t->dport = ntohs (udp0->dst_port);
              if (header0)
                t->header = *header0;
            }

          b1 = vlib_get_buffer (vm, bi1);

          vnet_feature_next (&next1, b1);

          if (vnet_buffer (b1)->feature_arc_index != pmp->egress_index)
            { /* Ingress Traffic */
              /* HW offload flags */
              ol_flags1 = vlib_to_mbuf (b1)->ol_flags;

              if (PREDICT_TRUE (pmp->rx_ptp_classify_offload_capa &&
                                ((ol_flags1 & PKT_RX_IEEE1588_PTP) == 0)))
                goto exit1;

              if (pmp->rx_checksum_offload_capa)
                {
                  good_l4_csum0 =
                      PREDICT_TRUE ((ol_flags0 & PKT_RX_L4_CKSUM_BAD) == 0);
                  if (!good_l4_csum0)
                    goto exit1;
                }
            }
          else
            { /* Egress Traffic */
              /* offset associated with the location ipv6 */
              iph_offset1 = vnet_buffer (b1)->ip.save_rewrite_length;
            }
          /* Retrieve pointer to the beginning of the IPv6 header */
          ip61 = (ip6_header_t *)((u8 *)vlib_buffer_get_current (b1) +
                                  iph_offset1);

          /* locate the next UDP header through the packet */
          ip_protocol1 =
              ip6_locate_header (b1, ip61, IP_PROTOCOL_UDP, &udp_offset1);

          /* Filter out non UDP packets */
          if (PREDICT_FALSE (ip_protocol1 != IP_PROTOCOL_UDP))
            goto exit1;

          /* Retrieve the pointer to UDP header */
          udp1 = (udp_header_t *)((u8 *)ip61 + udp_offset1);
          /* check for error based on the length */
          ip_len1 = clib_net_to_host_u16 (ip61->payload_length);
          udp_len1 = clib_net_to_host_u16 (udp1->length);
          len_diff1 = ip_len1 - udp_len1;

          error1 = len_diff1 >= 0 ? 0 : IP6_ERROR_UDP_LENGTH;
          if (PREDICT_FALSE (error1))
            goto exit1;

          /* Filter through the non PTP packets based on the PTP port */
          if (PREDICT_TRUE (udp1->dst_port !=
                            clib_host_to_net_u16 (PTP_EVENT)))
            goto exit1;

          /* Check for checksum validation flags */
          flags1 = b1->flags;
          good_l4_csum1 |=
              PREDICT_TRUE ((flags1 & VNET_BUFFER_F_L4_CHECKSUM_CORRECT ||
                             flags1 & VNET_BUFFER_F_OFFLOAD_UDP_CKSUM) != 0);

          /* if not validated and not computed, validate checksum */
          if (PREDICT_FALSE (!good_l4_csum1 &&
                             !(flags1 & VNET_BUFFER_F_L4_CHECKSUM_COMPUTED)))
            {
              flags1 = validate_checksum (vm, b1, ip61, udp1);
              good_l4_csum1 = PREDICT_TRUE (
                  (flags1 & VNET_BUFFER_F_L4_CHECKSUM_CORRECT) != 0);
            }

          /* if checksum not good, set as error */
          error1 = good_l4_csum1 ? 0 : IP6_ERROR_UDP_CHECKSUM;
          if (PREDICT_FALSE (error1))
            goto exit1;

          header1 = (struct ptp_header *)((u8 *)udp1 + sizeof (*udp1));
          /* perform ptptc packet update, and compute checksum if necessary */
          if (PREDICT_TRUE (ptptc_set_timestamp (b1, header1, &pkts_handled)))
            {
              int bogus_length;
              udp1->checksum = 0;
              udp1->checksum = ip6_tcp_udp_icmp_compute_checksum (
                  vm, b1, ip61, &bogus_length);
            }

          mark1 = true;

        exit1:

          next1 = PREDICT_FALSE (error1) ? PTPTC_NEXT_DROP : next1;
          b1->error = PREDICT_FALSE (error1) ? node->errors[error1] : 0;

          if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE) &&
                             (b1->flags & VLIB_BUFFER_IS_TRACED)))
            {
              ptptc_trace_t *t = vlib_add_trace (vm, node, b1, sizeof (*t));
              t->timestamp = vlib_get_timestamp (b1);
              t->cur_ts = t->timestamp;
              t->sw_if_index = vnet_buffer (b1)->sw_if_index[VLIB_RX];
              t->mark = mark1;
              if (udp1)
                t->dport = ntohs (udp1->dst_port);
              if (header1)
                t->header = *header1;
            }

          /* verify speculative enqueues, maybe switch current next frame */
          vlib_validate_buffer_enqueue_x2 (vm, node, next_index, to_next,
                                           n_left_to_next, bi0, bi1, next0,
                                           next1);
        }

      /* Process remaining packets one at a time. */
      while (n_left_from > 0 && n_left_to_next > 0)
        {
          vlib_buffer_t *b0;
          ip6_header_t *ip60;
          udp_header_t *udp0 = 0;
          struct ptp_header *header0 = 0;
          bool mark0 = false;
          u32 bi0, ip_len0, udp_len0, flags0, next0, udp_offset0, ol_flags0;
          i32 len_diff0;
          u32 iph_offset0 = 0;
          u32 ip_protocol0 = 0;
          u32 error0 = 0;
          u32 good_l4_csum0 = 0;

          /* speculatively enqueue b0 to the current next frame */
          bi0 = from[0];
          to_next[0] = bi0;
          from += 1;
          to_next += 1;
          n_left_from -= 1;
          n_left_to_next -= 1;

          b0 = vlib_get_buffer (vm, bi0);

          vnet_feature_next (&next0, b0);

          if (vnet_buffer (b0)->feature_arc_index != pmp->egress_index)
            { /* Ingress Traffic */
              /* HW offload flags */
              ol_flags0 = vlib_to_mbuf (b0)->ol_flags;

              if (PREDICT_TRUE (pmp->rx_ptp_classify_offload_capa &&
                                (ol_flags0 & PKT_RX_IEEE1588_PTP) == 0))
                goto exit;

              if (pmp->rx_checksum_offload_capa)
                {
                  good_l4_csum0 =
                      PREDICT_TRUE ((ol_flags0 & PKT_RX_L4_CKSUM_BAD) == 0);
                  if (!good_l4_csum0)
                    goto exit;
                }
            }
          else
            { /* Egress Traffic */
              /* offset associated with the location ipv6 */
              iph_offset0 = vnet_buffer (b0)->ip.save_rewrite_length;
            }

          /* Retrieve pointer to the beginning of the IPv6 header */
          ip60 = (ip6_header_t *)((u8 *)vlib_buffer_get_current (b0) +
                                  iph_offset0);

          /* locate the next UDP header through the packet */
          ip_protocol0 =
              ip6_locate_header (b0, ip60, IP_PROTOCOL_UDP, &udp_offset0);

          /* Filter out non UDP packets */
          if (PREDICT_FALSE (ip_protocol0 != IP_PROTOCOL_UDP))
            goto exit;

          /* Retrieve the pointer to UDP header */
          udp0 = (udp_header_t *)((u8 *)ip60 + udp_offset0);
          /* check for error based on the length */
          ip_len0 = clib_net_to_host_u16 (ip60->payload_length);
          udp_len0 = clib_net_to_host_u16 (udp0->length);
          len_diff0 = ip_len0 - udp_len0;

          error0 = len_diff0 >= 0 ? 0 : IP6_ERROR_UDP_LENGTH;
          if (PREDICT_FALSE (error0))
            goto exit;

          /* Filter through the non PTP packets based on the PTP port */
          if (PREDICT_TRUE (udp0->dst_port !=
                            clib_host_to_net_u16 (PTP_EVENT)))
            goto exit;

          /* Check for checksum validation flags */
          flags0 = b0->flags;
          good_l4_csum0 |=
              PREDICT_TRUE ((flags0 & VNET_BUFFER_F_L4_CHECKSUM_CORRECT ||
                             flags0 & VNET_BUFFER_F_OFFLOAD_UDP_CKSUM) != 0);

          /* if not validated and not computed, validate checksum */
          if (PREDICT_FALSE (!good_l4_csum0 &&
                             !(flags0 & VNET_BUFFER_F_L4_CHECKSUM_COMPUTED)))
            {
              flags0 = validate_checksum (vm, b0, ip60, udp0);
              good_l4_csum0 = PREDICT_TRUE (
                  (flags0 & VNET_BUFFER_F_L4_CHECKSUM_CORRECT) != 0);
            }

          /* if checksum not good, set as error */
          error0 = good_l4_csum0 ? 0 : IP6_ERROR_UDP_CHECKSUM;
          if (PREDICT_FALSE (error0))
            goto exit;

          header0 = (struct ptp_header *)((u8 *)udp0 + sizeof (*udp0));
          /*  perform ptptc packet update, and compute checksum if necessary */
          if (PREDICT_TRUE (ptptc_set_timestamp (b0, header0, &pkts_handled)))
            {
              int bogus_length;
              udp0->checksum = ip6_tcp_udp_icmp_compute_checksum (
                  vm, b0, ip60, &bogus_length);
            }

          mark0 = true;
        exit:
          next0 = PREDICT_FALSE (error0) ? PTPTC_NEXT_DROP : next0;
          b0->error = PREDICT_FALSE (error0) ? node->errors[error0] : 0;

          if (PREDICT_FALSE ((node->flags & VLIB_NODE_FLAG_TRACE) &&
                             (b0->flags & VLIB_BUFFER_IS_TRACED)))
            {
              ptptc_trace_t *t = vlib_add_trace (vm, node, b0, sizeof (*t));
              t->timestamp = vlib_get_timestamp (b0);
              t->cur_ts = t->timestamp;
              t->sw_if_index = vnet_buffer (b0)->sw_if_index[VLIB_RX];
              t->mark = mark0;
              if (udp0)
                t->dport = ntohs (udp0->dst_port);
              if (header0)
                t->header = *header0;
            }

          vlib_validate_buffer_enqueue_x1 (vm, node, next_index, to_next,
                                           n_left_to_next, bi0, next0);
        }

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  vlib_node_increment_counter (vm, node->node_index, PTPTC_ERROR_HANDLED,
                               pkts_handled);
  return frame->n_vectors;
}

VLIB_NODE_FN (ptptc_node)
(vlib_main_t *vm, vlib_node_runtime_t *node, vlib_frame_t *frame)
{
  return ptptc_node_inline (vm, node, frame);
}

#ifndef CLIB_MARCH_VARIANT
VLIB_REGISTER_NODE (ptptc_node) = {.name = "ptptc",
                                   .vector_size = sizeof (u32),
                                   .format_trace = format_ptptc_trace,
                                   .type = VLIB_NODE_TYPE_INTERNAL,

                                   .n_errors = ARRAY_LEN (ptptc_error_strings),
                                   .error_strings = ptptc_error_strings,
                                   .n_next_nodes = PTPTC_N_NEXT,
                                   .next_nodes = {
                                       [PTPTC_NEXT_DROP] = "error-drop",
                                   }};

#endif /* CLIB_MARCH_VARIANT */
