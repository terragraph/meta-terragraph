#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

export LC_ALL=en_US

TG_CONFIG_ENV="/data/cfg/config"
TG_CONFIG_FILE="/data/cfg/node_config.json"

exec 200>/tmp/configlockfile
flock -x 200

if [ -f "$TG_CONFIG_ENV" ]; then
  # shellcheck source=/dev/null
  . "$TG_CONFIG_ENV"
fi

if [ -f "$TG_CONFIG_FILE" ]; then
  LAST_MOD=$(stat -c "%Y" "$TG_CONFIG_FILE")
fi

if [ -z "$CONFIG_LAST_MOD" ] || [ -z "$LAST_MOD" ] || [ "$LAST_MOD" != "$CONFIG_LAST_MOD" ]; then
  # generate and read TG_CONFIG_ENV
  /usr/sbin/config_read_env
  # shellcheck source=/dev/null
  . "$TG_CONFIG_ENV"
fi

flock -u 200

# Force NSS to zero for non-Marvell platforms
OPENR_USE_FIB_NSS="0"
