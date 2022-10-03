#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Manage serial console based on envParams.SERIAL_CONSOLE_DISABLE
# shellcheck source=/dev/null
. /usr/sbin/config_get_env.sh
F="/tmp/config_serial_console_disable"
if [ "${SERIAL_CONSOLE_DISABLE}" = "1" ]; then
  touch "$F"
  killall getty >/dev/null 2>/dev/null
elif [ -f "$F" ]; then
  rm -f "$F"
fi
