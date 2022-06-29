/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * gps_sync.h - PTP-TC gps-sync definitions
 */
#ifndef __included_gps_sync_h__
#define __included_gps_sync_h__

struct gps_main
{
  int sockfd;          /* connection to gpsd */
  char read_buf[2048]; /* socket read buffer */
  size_t offset;       /* socket read start index */
  u32 clib_file_index; /* index of socket in file_main */
};

int gps_sync_enable (struct gps_main *gm);
void gps_sync_disable (struct gps_main *gm);

#endif /* __included_gps_sync_h__ */
