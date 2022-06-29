#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

RESTART_DELAY=10

# common file
FB_TG_LOAD_COMMON="/usr/sbin/fb_tg_load_common.sh"

if [ ! -f "$FB_TG_LOAD_COMMON" ]; then
  echo "$FB_TG_LOAD_COMMON does not exist" >&2
  sleep ${RESTART_DELAY}
  exit 1
fi

# shellcheck source=/dev/null
. "$FB_TG_LOAD_COMMON"

E2E_NODE_CONFIG_FILE="/data/cfg/node_config.json"
RUN_E2E_MINION="/usr/sbin/e2e_minion -node_config_file $E2E_NODE_CONFIG_FILE"

# copy node info file (may be modified after baseband init)
E2E_NODE_INFO_FILE="/var/run/node_info"
TMP_E2E_NODE_INFO_FILE="/tmp/node_info"
cp "$E2E_NODE_INFO_FILE" "$TMP_E2E_NODE_INFO_FILE"
RUN_E2E_MINION="$RUN_E2E_MINION -node_info_file $TMP_E2E_NODE_INFO_FILE"

if [ ! -z "$MINION_VERBOSE" ]; then
  RUN_E2E_MINION="$RUN_E2E_MINION -v $MINION_VERBOSE"
else
  RUN_E2E_MINION="$RUN_E2E_MINION -v 2"
fi
if [ ! -z "$MINION_VMODULE" ]; then
  RUN_E2E_MINION="$RUN_E2E_MINION -vmodule=$MINION_VMODULE"
fi

# add shared flags
RUN_E2E_MINION="$RUN_E2E_MINION $(_get_minion_common_flags)"

# use a global control interface for wpa_supplicant
WPA_SUPPLICANT_GLOBAL_CTRL="/var/run/wpa_supplicant-global"
RUN_E2E_MINION="$RUN_E2E_MINION -wpa_supplicant_global_ctrl_iface $WPA_SUPPLICANT_GLOBAL_CTRL"
if [ -S "$WPA_SUPPLICANT_GLOBAL_CTRL" ]; then
  echo "Terminating wpa_supplicant"
  wpa_cli -g "$WPA_SUPPLICANT_GLOBAL_CTRL" terminate
  rm -rf /var/run/wpa_supplicant_terra*
fi

# kill any running hostapd processes so that minion can start wpa_supplicant
echo "Killing hostapd"
pkill -f -9 "hostapd.*terra.*"
rm -rf /var/run/hostapd_terra*

echo "Killing driver_if_daemon"
killall driver_if_daemon

echo "Unloading driver..."
_stop

echo "Starting e2e : $RUN_E2E_MINION"
_run "$RUN_E2E_MINION"

sleep ${RESTART_DELAY}
