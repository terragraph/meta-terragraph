#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Run vpp_chaperone and tunnel_monitor on config changes

# load tg config environment variables
# shellcheck disable=1091
. /usr/sbin/config_get_env.sh

_date() {
  date +"%Y-%m-%dT%H:%M:%S%z"
}

# Run vpp_chaperone
if [ "$DPDK_ENABLED" -eq "1" ] || [ "$DVPP_ENABLED" -eq "1" ]; then
  echo "[$(_date)] Running vpp_chaperone..."
  until sv status vpp_chaperone | grep "down: vpp_chaperone" > /dev/null 2>&1 ; do
    echo "[$(_date)] Wait for vpp_chaperone to finish..."
    sleep 1
  done
  sv once vpp_chaperone

  # Run tunnel monitor after every config change or vpp_chaperone run
  sv restart tunnel_monitor
else
  echo "[$(_date)] VPP not enabled, skipping vpp_chaperone and tunnel_monitor"
  exit 1
fi
