/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef FB_TG_QUEUE_STATS_H
#define FB_TG_QUEUE_STATS_H

// Queue stats constants. Some are shared between the tg driver and firmware.

#define QUEUE_STATS_MAX_LINKS (16)
#define QUEUE_STATS_INTERVAL_MILLISECOND (20)
#define QUEUE_STATS_PER_SECOND (1000 / QUEUE_STATS_INTERVAL_MILLISECOND)

// Sleep options: 0 - usleep_range(), 1 - msleep_interruptible()
#define QUEUE_STATS_USE_MSLEEP 0

// usleep_range() parameters
// References:
// http://lxr.free-electrons.com/source/Documentation/timers/timers-howto.txt
// http://www.linuxquestions.org/questions/linux-kernel-70/disadvantage-in-using-usleep_range-for-more-than-20-ms-delay-4175560884/#post5463755
#if QUEUE_STATS_USE_MSLEEP == 0
// A non-zero range enables the kernel to coalesce wakeups/interrupts.
#define QUEUE_STATS_USLEEP_RANGE_USEC (0)
#define QUEUE_STATS_USLEEP_MIN_USEC (1000 * QUEUE_STATS_INTERVAL_MILLISECOND)
#define QUEUE_STATS_USLEEP_MAX_USEC \
  (QUEUE_STATS_USLEEP_MIN_USEC + QUEUE_STATS_USLEEP_RANGE_USEC)
#endif // #if QUEUE_STATS_USE_MSLEEP == 0

// Moving average formula for updating the arrival rate in the tg driver
//
// new-average-arrival-rate =
// (1/8)instantaneous-arrival-rate + (7/8)old-average-arrival-rate
#define QUEUE_STATS_UPDATE_ARRIVAL_RATE(now, old) \
  (((now) >> 3) + ((7 * (old)) >> 3))

#endif
