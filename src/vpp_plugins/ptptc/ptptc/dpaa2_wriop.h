/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifndef _DPAA2_WRIOP_H
#define _DPAA2_WRIOP_H

#include <stdbool.h>

#ifndef BIT
#define BIT(x) (1 << (x))
#endif

#define WRIOP_GLOBAL_ADDR 0x8b80000
#define WRIOP_GLOBAL_LEN 0x16000

#define WRIOP_PORT_BASE 0x8c00000
#define WRIOP_PORT_LEN 0x4000
#define WRIOP_PORT_ADDR(port) (WRIOP_PORT_BASE + WRIOP_PORT_LEN * (port))

/* registers relative to WRIOP_GLOBAL_ADDR */
#define WRIOP_TMR_CTRL 0x15080
#define WRIOP_TMR_STAT 0x15094
#define WRIOP_TMR_STAT_ETS1_VLD BIT (24)

#define WRIOP_TMR_TMASK 0x15088

#define WRIOP_TMR_CNT_H 0x15098
#define WRIOP_TMR_CNT_L 0x1509c

#define WRIOP_TMR_ADD 0x150A0

#define WRIOP_TMR_OFF_H 0x150B0
#define WRIOP_TMR_OFF_L 0x150B4

#define WRIOP_TMR_ETTS1_H 0x150E0
#define WRIOP_TMR_ETTS1_L 0x150E4

/* registers relative to WRIOP_PORT_ADDR */
#define WRIOP_SINGLE_STEP 0x30c0

#define WRIOP_SINGLE_STEP_EN 0x80000000
#define WRIOP_SINGLE_STEP_CH 0x00000080
#define WRIOP_SINGLE_STEP_OFFSET_M 0x0000ff00
#define WRIOP_SINGLE_STEP_OFFSET_S 8

struct dpaa2_wriop_reg
{
  void *mm;
  size_t mm_len;
};

struct dpaa2_wriop_reg *dpaa2_wriop_reg_init (size_t phys_addr, size_t len);
void dpaa2_wriop_reg_uninit (struct dpaa2_wriop_reg *reg);

uint64_t dpaa2_get_current_timestamp (struct dpaa2_wriop_reg *reg);
uint64_t dpaa2_get_current_timestamp_with_offset (struct dpaa2_wriop_reg *reg);
uint64_t dpaa2_get_pps_timestamp (struct dpaa2_wriop_reg *reg);
void dpaa2_set_pps_offset (struct dpaa2_wriop_reg *reg, uint64_t value);
uint64_t dpaa2_get_pps_offset (struct dpaa2_wriop_reg *reg);
void dpaa2_set_pps_addend (struct dpaa2_wriop_reg *reg, uint32_t value);
uint32_t dpaa2_get_pps_addend (struct dpaa2_wriop_reg *reg);
void dpaa2_disable_1588_interrupts (struct dpaa2_wriop_reg *reg);
void dpaa2_set_single_step (struct dpaa2_wriop_reg *reg, bool enable,
                            uint16_t offset, bool checksum);

uint32_t dpaa2_get_ctrl (struct dpaa2_wriop_reg *reg);

void dpaa2_set_ctrl (struct dpaa2_wriop_reg *reg, uint32_t value);

#endif /* _DPAA2_WRIOP_H */
