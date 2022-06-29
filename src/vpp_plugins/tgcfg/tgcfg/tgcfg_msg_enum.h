/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifndef included_tgcfg_msg_enum_h
#define included_tgcfg_msg_enum_h

#include <vppinfra/byte_order.h>

#define vl_msg_id(n, h) n,
typedef enum
{
#include <tgcfg/tgcfg.api.h>
  /* We'll want to know how many messages IDs we need... */
  VL_MSG_FIRST_AVAILABLE,
} vl_msg_id_t;
#undef vl_msg_id

#endif
