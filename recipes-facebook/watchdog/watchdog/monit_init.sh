#!/bin/bash

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# shellcheck source=/dev/null
. /etc/monit_config.sh

# Perform initialization that is required before every minion startup.
# shellcheck disable=SC2154
if true; then # this if block is only for suppressing shellcheck spam within

  # Create monitor related directories
  mkdir -p "$progress_dir" "$fw_dump_dir" "$fw_health_dir" "$fw_nolink_dir" "$monit_log_dir"

  # Initialize the f/w progress monitor
  monotonic-touch -o "$fw_init_time_sec" "$fw_init_time_file"
  monotonic-touch "$minion_file"
  rm -f "$fw_nolink_dir"/* # Delete stale "no RF link for 15 minutes" indicators

  # Initialize the link monitor
  monotonic-touch "$running_file" "$reachable_file"

  # Initialize the vpp cli deadlock monitor
  monotonic-touch -o "$vpp_cli_startup_check_delay_sec" "$vpp_cli_check_time_file"
fi

# Perform initialization that is required when the watchdog is (re)started.
# shellcheck disable=SC2154
if [ "x$1" = "xall" ]; then
  monit_debug "wdog" "monit_init.sh all"
  monotonic-touch "$pop_last_ping_reply_file" "$pop_last_ping_try_file"
  rm -f "$rebooting_file" "$prog_repair_file"  "$link_repair_file" "$pop_repair_file"

  if [ -e "$gps_timeout_data_file" ]; then
    # Move the remaining gps timeout file from /data to /tmp for:
    # 1. Deterministic gps timeouts. The saved gps timeout is only used once,
    #    when it was actually computed and saved by the previous shutdown.
    # 2. Minimize flash access.
    mv -f "$gps_timeout_data_file" "$gps_timeout_tmp_file"
    monit_debug "gps" "previous shutdown timeout: $(cat "$gps_timeout_tmp_file" 2>/dev/null) sec"
  fi
  if [ ! -e "$gps_good_file" ]; then
    monotonic-touch "$gps_good_file"
    monit_debug "gps" "gps_good_file created at $(stat -c %Y "$gps_good_file" 2>/dev/null)"
  fi

  if [ ! -e "$pop_traceroute_time_file" ]; then
    monotonic-touch "$pop_traceroute_time_file"
    monit_debug "pop" "pop_traceroute_time_file created at $(stat -c %Y "$pop_traceroute_time_file" 2>/dev/null)"
  fi

  # Find the number of baseband cards
  num_radios="$(/usr/bin/lspci -d 17cb:1201 2>/dev/null | wc -l 2>/dev/null)"
  echo "${num_radios}" > "${fw_num_radios_file}"
  monit_debug "wdog" "num_radios: $(cat "${fw_num_radios_file}" 2>/dev/null)"

  # Until the watchdog is better tested in Puma, we disable all non-critical
  # watchdogs.
  #
  # The following watchdogs are critical and are *never* disabled on startup
  #
  #   CFGx  - config revert (only with FB e2e controller)
  #   DATA  - /data full
  #   TMP   - /tmp full
  #   UPG   - upgrade revert (only with FB e2e controller)
  #   VPP   - vpp cli deadlock
  #
  # Disabling critical watchdogs
  #
  #   VPP can be disabled manually via the following commands
  #      /etc/init.d/watchdog.sh dis_vpp # disable VPP
  #      /etc/init.d/watchdog.sh en_vpp  # enable VPP
  #
  # See Troubleshooting.md for more details.
  #
  # For testing purposes, all watchdog features can be persistently
  # re-enabled manually with the following *dangerous* commands:
  #
  #    touch /data/wdogtestmode
  #    /etc/init.d/watchdog.sh enable_tgscripts
  #
  if [ ! -e /data/wdogtestmode ] || [ -e "${persistent_disable_file}" ]; then
    disable_all_tg_scripts
  fi
fi
