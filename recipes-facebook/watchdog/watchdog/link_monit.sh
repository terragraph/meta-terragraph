#!/bin/bash

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# shellcheck source=/dev/null
. /etc/monit_config.sh
# shellcheck source=/dev/null
. /etc/monit_utils.sh

#
# Test data path timeout.
# This script checks if NDP is not able to reach any peer and if yes, recover from it by reloading minion.
# This helps in cases when wireless data path is stuck but it cannot idenitfy if link level data path is only stuck.
#
# shellcheck disable=SC2154
test_link()
{
  if [ -e "${rebooting_file}" ] || [ -e "${link_repair_file}" ]; then
    monit_debug "link" "skip test - rebooting/repairing"
    echo 0
    return
  fi

  for ((i=0; i<max_links; i++))
  do
    ifconfig terra${i} > /dev/null 2>&1
    # Check if interface is present
    if [ $? -ne 0 ]; then
      continue # This interface is not present
    fi
    #Check if interface is running
    local is_running
    is_running=$(ifconfig terra${i} 2>/dev/null | grep -c RUNNING)
    if [[ $is_running -eq 0 ]]; then
      continue # This interface is not running
    fi
    prev_running_time="$(stat -c %Y "$running_file" 2>/dev/null)"
    if [ "$?" -ne 0 ]; then
      # running file is gone
      monit_debug "link" "fail running file gone 1"
      touch "${link_repair_file}"
      echo 1
      return
    fi
    monotonic-touch "$running_file"
    running_time="$(stat -c %Y "$running_file" 2>/dev/null)"

    # Being paranoid here, if the file is lost after previous touch
    if [ "$?" -ne 0 ]; then
      # running file is gone
      monit_debug "link" "fail running file gone 2"
      touch "${link_repair_file}"
      echo 1
      return
    fi
    if [ $((running_time - prev_running_time)) -gt 2 ]; then
      # We moved from non-running to running, touch reachable to prevent false restarts
      monotonic-touch "$reachable_file"
    fi
    # piggy-back on ipv6 NDP to check data path is up. NDP checks at 20-30 secs interval
    # and it can take same time for NDP to realize peer is unreachable
    peer_reachable=$(ip -6 neigh show dev terra${i} | grep -c "REACHABLE\|STALE\|DELAY\|PROBE\|PERMANENT")
    if [[ $peer_reachable -ge 1 ]]; then
      monotonic-touch "$reachable_file"
    fi
    reachable_time="$(stat -c %Y "$reachable_file" 2>/dev/null)"
    if [ "$?" -ne 0 ]; then
      # reachable file is gone
      monit_debug "link" "fail reachable file gone"
      touch "${link_repair_file}"
      echo 1
      return
    fi
    # Declare data path down after data_timeout_sec consecutive seconds
    # of no peers reachable from any interface
    if [ "$((running_time - reachable_time))" -gt "$data_timeout_sec" ]; then
      # Peer link not reachable
      monit_debug "link" "timed out"
      touch "${link_repair_file}"
      echo 1
      return
    fi
  done

  monit_debug "link" "ok"
  echo 0
}

# shellcheck disable=SC2154
repair_link()
{
  monit_debug "link" "trylock"
  lock "${wdog_repair_lock_file}"
  if [ $? -ne 0 ] || [ -e "${rebooting_file}" ]; then
    monit_debug "link" "nolock"
    rm -f "${link_repair_file}"
    echo 0 # another watchdog is collecting logs or rebooting
    return
  fi
  monit_debug "link" "lock"

  # Try to collect logs before unloading the f/w.
  local link_type
  TG_IF2IF=$(/usr/sbin/get_hw_info TG_IF2IF)
  if [ -n "$TG_IF2IF" ] && [ "$TG_IF2IF" == 1 ]; then
    link_type="IF"
  else
    link_type="RF"
  fi
  mkdir -p "${fw_dump_dir}/${link_type}"
  # Add the reason for fwdump to dmesg file, which is collected as part of fwdump
  echo "Data path stuck recovery from watchdog, take fwdump" >> /dev/kmsg
  /usr/bin/dump_20130.sh -t "${link_type}" -o "${fw_dump_dir}/${link_type}" > /dev/null 2>&1
  # Keep only the three most recent log dumps.
  ls "${fw_dump_dir}/${link_type}"/* > /dev/null 2>&1;
  if [ "$?" -eq 0 ]; then
    # fw_dump_dir is not empty
    # shellcheck disable=SC2012
    ls "${fw_dump_dir}/${link_type}"/* -A1dt | tail -n +4 | xargs rm -rf
  fi

  # Attempt minion restart
  update_wdog_repair_history "link_monit.sh" "${wdog_event_e2e_restart}"
  if timeout -k 1 "${e2e_minion_restart_timeout_sec}" restart_e2e_minion -r "${e2e_minion_restart_retries}"; then
    monit_debug "link" "e2e_minion restart ok"
    rm -f "${link_repair_file}"
    echo 0
  else
    monit_debug "link" "e2e_minion restart failed"
    touch "${rebooting_file}"
    echo "link" > "${e2e_minion_restart_failed_reboot_indicator}"
    update_wdog_repair_history "link_monit.sh : e2e_minion_restart_failed" "${wdog_event_reboot}"
    /etc/init.d/tg_shutdown
    echo 1 # request reboot
  fi
}

# Do nothing if wdog or minion is disabled.
# shellcheck disable=SC2154
if [ -e "${monit_disable_link_file}" ] || [ "$(check_e2e_minion_enabled)" -eq 0 ]; then
  # Prevent spurious timeouts in the future when link_monit and minion are enabled.
  monit_debug "link" "ok disabled"
  monotonic-touch "$running_file" "$reachable_file"
  exit 0
fi
ret=0
if [ "x$1" = "xtest" ]; then
  ret=$(test_link)
elif [ "x$1" = "xrepair" ]; then
  ret=$(repair_link)
fi

monit_debug "link" "exit $ret"
exit "$ret"
