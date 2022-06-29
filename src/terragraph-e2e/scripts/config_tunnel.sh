#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Called by e2e minion whenever tunnel configuration is changed

# load tg config environment variables
# shellcheck disable=1091
. /usr/sbin/config_get_env.sh

if [ "$DPDK_ENABLED" -eq "1" ] || [ "$DVPP_ENABLED" -eq "1" ]; then
  # vpp_chaperone handles tunnel configuration in DPDK/VPP mode
  /usr/sbin/run_vpp_chaperone_and_monitor.sh
else
  echo "Tunnel configuration only supported in DPDK/VPP mode"
  exit 1
fi
