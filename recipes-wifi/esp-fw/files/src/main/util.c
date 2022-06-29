/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "terragraph.h"
#include <stdio.h>
#include <string.h>

// Convert string representation of mac to uint8_t
// mac - at least TG_MAC_LEN
int
str2mac(const char* s, uint8_t* mac) {
  int i;
  unsigned int t[TG_MAC_LEN];
  if (TG_MAC_LEN != sscanf(
                        s,
                        "%02x:%02x:%02x:%02x:%02x:%02x",
                        &t[0],
                        &t[1],
                        &t[2],
                        &t[3],
                        &t[4],
                        &t[5])) {
    return 1;
  }
  for (i = 0; i < TG_MAC_LEN; i++) {
    mac[i] = (uint8_t)t[i];
  }
  return 0;
}

// Get string representation of mac
// buf - at least TG_MAC_STR_LEN bytes
const char*
mac2str(const uint8_t* mac, char* buf) {
  if (17 != sprintf(
                buf,
                "%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0],
                mac[1],
                mac[2],
                mac[3],
                mac[4],
                mac[5])) {
    strcpy(buf, "??");
  }
  return buf;
}
