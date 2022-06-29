/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef FB_TGD_QUEUE_STATS_H
#define FB_TGD_QUEUE_STATS_H

struct tgd_terra_driver;

int fb_tgd_queue_stats_init(struct tgd_terra_driver *fb_drv_data);
void fb_tgd_queue_stats_exit(struct tgd_terra_driver *fb_drv_data);

#endif
