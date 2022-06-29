/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * dpaa_wriop.c - NXP WRIOP register access
 */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "dpaa2_wriop.h"

struct dpaa2_wriop_reg *dpaa2_wriop_reg_init (size_t phys_addr, size_t len)
{
  struct dpaa2_wriop_reg *ret;
  int fd;
  void *mm;

  ret = malloc (sizeof (*ret));
  if (!ret)
    return NULL;

  fd = open ("/dev/mem", O_RDWR | O_SYNC);
  if (fd == -1)
    {
      goto out_free;
    }

  mm = mmap (NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, phys_addr);
  close (fd);

  if (mm == MAP_FAILED)
    {
      goto out_free;
    }

  ret->mm = mm;
  ret->mm_len = len;
  return ret;

out_free:
  free (ret);
  return NULL;
}

void dpaa2_wriop_reg_uninit (struct dpaa2_wriop_reg *reg)
{
  if (!reg)
    return;

  munmap (reg->mm, reg->mm_len);
  free (reg);
}

uint32_t dpaa2_wriop_reg_read (struct dpaa2_wriop_reg *regs, uint32_t addr)
{
  return le32toh (*(volatile uint32_t *)(regs->mm + addr));
}

void dpaa2_wriop_reg_write (struct dpaa2_wriop_reg *regs, uint32_t addr,
                            uint32_t val)
{
  *((uint32_t *)(regs->mm + addr)) = htole32 (val);
}

void dpaa2_disable_1588_interrupts (struct dpaa2_wriop_reg *reg)
{
  dpaa2_wriop_reg_write (reg, WRIOP_TMR_TMASK, 0);
}

void dpaa2_set_pps_offset (struct dpaa2_wriop_reg *reg, uint64_t value)
{
  dpaa2_wriop_reg_write (reg, WRIOP_TMR_OFF_H, value >> 32);
  dpaa2_wriop_reg_write (reg, WRIOP_TMR_OFF_L, value & 0xffffffff);
}

uint64_t dpaa2_get_pps_offset (struct dpaa2_wriop_reg *reg)
{
  uint64_t value;

  if (!reg)
    return -1;

  value = dpaa2_wriop_reg_read (reg, WRIOP_TMR_OFF_L);
  value |= ((uint64_t)dpaa2_wriop_reg_read (reg, WRIOP_TMR_OFF_H)) << 32;
  return value;
}

uint64_t dpaa2_get_current_timestamp (struct dpaa2_wriop_reg *reg)
{
  uint64_t value;

  if (!reg)
    return -1;

  /* must read low first to latch the high portion */
  value = dpaa2_wriop_reg_read (reg, WRIOP_TMR_CNT_L);
  value |= ((uint64_t)dpaa2_wriop_reg_read (reg, WRIOP_TMR_CNT_H)) << 32;
  return value;
}

void dpaa2_set_timestamp_counter (struct dpaa2_wriop_reg *reg, uint64_t value)
{
  dpaa2_wriop_reg_write (reg, WRIOP_TMR_CNT_L, value & 0xFFFFFFFF);
  dpaa2_wriop_reg_write (reg, WRIOP_TMR_CNT_H, (value >> 32) & 0xFFFFFFFF);
}

/*
 * Read current time from PTP clock and include the offset from
 * TMR_OFF.  Normally, the offset is applied by hardware timestamping
 * but not from direct reads of the counter.
 */
uint64_t dpaa2_get_current_timestamp_with_offset (struct dpaa2_wriop_reg *reg)
{
  uint64_t value, offs;

  if (!reg)
    return -1;

  value = dpaa2_get_current_timestamp (reg);
  offs = dpaa2_get_pps_offset (reg);

  return value + offs;
}

uint64_t dpaa2_get_pps_timestamp (struct dpaa2_wriop_reg *reg)
{
  uint64_t value;
  uint32_t status;

  if (!reg)
    return -1;

  /*
   * If there is no timestamp available, GPS/trigger may not be active;
   * return all ones in this case.
   */
  status = dpaa2_wriop_reg_read (reg, WRIOP_TMR_STAT);
  if (!(status & WRIOP_TMR_STAT_ETS1_VLD))
    return -1;

  /* Up to 16 timestamps may be buffered.  We have no need for any
   * but the most recent, so poll until the TMR_STAT[ETSn_VLD] bit
   * is clear (see Table 12-19 in LS1080A DPAA2 low-level hardware
   * manual, Rev B).  Note that if buffer is already full, the most
   * recent timestamp might be stale, so call this once at startup
   * and discard the result.
   */
  do
    {
      value = dpaa2_wriop_reg_read (reg, WRIOP_TMR_ETTS1_L);
      value |= ((uint64_t)dpaa2_wriop_reg_read (reg, WRIOP_TMR_ETTS1_H)) << 32;

      status = dpaa2_wriop_reg_read (reg, WRIOP_TMR_STAT);
    }
  while (status & WRIOP_TMR_STAT_ETS1_VLD);
  return value;
}

uint32_t dpaa2_get_pps_addend (struct dpaa2_wriop_reg *reg)
{
  return dpaa2_wriop_reg_read (reg, WRIOP_TMR_ADD);
}

void dpaa2_set_pps_addend (struct dpaa2_wriop_reg *reg, uint32_t value)
{
  dpaa2_wriop_reg_write (reg, WRIOP_TMR_ADD, value);
}

uint32_t dpaa2_get_ctrl (struct dpaa2_wriop_reg *reg)
{
  return dpaa2_wriop_reg_read (reg, WRIOP_TMR_CTRL);
}

void dpaa2_set_ctrl (struct dpaa2_wriop_reg *reg, uint32_t value)
{
  dpaa2_wriop_reg_write (reg, WRIOP_TMR_CTRL, value);
}

void dpaa2_set_single_step (struct dpaa2_wriop_reg *reg, bool enable,
                            uint16_t offset, bool checksum)
{
  if (!reg)
    return;

  uint32_t value =
      (offset << WRIOP_SINGLE_STEP_OFFSET_S) & WRIOP_SINGLE_STEP_OFFSET_M;
  if (enable)
    value |= WRIOP_SINGLE_STEP_EN;
  if (checksum)
    value |= WRIOP_SINGLE_STEP_CH;

  dpaa2_wriop_reg_write (reg, WRIOP_SINGLE_STEP, value);
}
