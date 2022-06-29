#!/bin/bash

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# shellcheck source=/dev/null
. /etc/monit_config.sh
# shellcheck source=/dev/null
. /etc/monit_utils.sh

# test_fsys helper
# shellcheck disable=SC2154
fsys_used() {
  local used
  used="$(df --output=pcent "$1" 2>/dev/null | sed -n '2p')"
  echo "${used%\%}"
}

# We are piggybacking filesystem monitoring on the timing framework provided
# by the pop reachability monitor.
# shellcheck disable=SC2154
test_fsys()
{
  if [ "$(fsys_used "/tmp")" -gt "${fsys_max_tmp_percent}" ]; then
    monit_debug "fsys" "tmp-full"
    echo "tmp-full" > "${fsys_reboot_reason_file}" 2>/dev/null
    return 1 # request reboot
  fi
  if [ "$(fsys_used "/data")" -gt "${fsys_max_data_percent}" ]; then
    monit_debug "fsys" "data-full"
    echo "data-full" > "${fsys_reboot_reason_file}" 2>/dev/null
    return 1 # request reboot, flash_mtd.sh will clean up /data
  fi
  monit_debug "fsys" "ok"
  return 0
}

# We are piggybacking config fallback monitoring on the timing framework
# provided by the pop reachability monitor.
# shellcheck disable=SC2154
test_config_fallback()
{
  eval "exec ${config_fallback_lock_fd}>${config_fallback_lock_file}"
  flock -n "${config_fallback_lock_fd}" # don't block wdog context
  if [ $? -ne 0 ]; then
    monit_debug "config" "lock failed"
    return 0 # lock failed, better luck next time
  fi

  if [ ! -f "${config_fallback_timeout_file}" ]; then
    # No new config has been set. Nothing to do
    monit_debug "config" "ok - no new config"
    flock -u "${config_fallback_lock_fd}"
    return 0
  fi

  local ret
  local now
  local config_fallback_time
  local delay_expiry_time
  local minion_connected_time

  config_fallback_time="$(stat -c %Y "${config_fallback_timeout_file}" 2>/dev/null)"
  delay_expiry_time="$((config_fallback_time + config_fallback_delay_timeout_sec - config_fallback_timeout_sec))"
  minion_connected_time="$(stat -c %Y "${minion_connected_file}" 2>/dev/null)"
  now="$(monotonic-touch -t 2>/dev/null)"

  ret=0 # assume no fallback
  if [ "${now}" -le "${delay_expiry_time}" ] 2>/dev/null; then
    # Waiting for the new config to take effect.
    # During the delay, minion or other services may start/restart.
    monit_debug "config" "ok - delaying until ${delay_expiry_time}"
  elif [ -n "${minion_connected_time}" ] && \
            [ "${minion_connected_time}" -gt "${delay_expiry_time}" ] 2>/dev/null; then
    # minion has connected to the controller since the new config took effect.
    monit_debug "config" "ok - new config is good"
    cancel_config_fallback
  elif [ "${now}" -lt "${config_fallback_time}" ]; then
    # waiting for minion to connect to the controller
    monit_debug "config" "ok - waiting until ${config_fallback_time}"
  elif [ -e "${monit_disable_config_file}" ]; then
    monit_debug "config" "ok - disabled"
  else
    # minion has failed to connect to the controller with the new config
    monit_debug "config" "timed out"
    echo -n "fallback-timed-out" > "${config_fallback_reboot_reason_file}" 2>/dev/null
    touch "${config_fallback_do_fallback}" # no more reboots with the new/bad config
    ret=1 # request reboot
  fi
  flock -u "${config_fallback_lock_fd}"
  return "${ret}"
}

# oping sample command output
#
#    oping -c 2 -w 2 -i 0.5 -f iplist 2>/dev/null
#
#    PING 2001:a:b:c:d:e:f:1 (2001:a:b:c:d:...
#    ...
#    echo reply from 2001:a:b:c:d:e:f:1 (2001:a:b:c:d:...
#    56 bytes from 2001:a:b:c:d:e:f:2 (2001:a:b:c:d:...
#    ...
#    --- 2001:a:b:c:d:e:f:1 ping statistics ---
#    2 packets transmitted, 0 received, 100.00% packet loss, time 0.0ms
#    ...
#    --- 2001:a:b:c:d:e:f:2 ping statistics ---
#    2 packets transmitted, 2 received, 0.00% packet loss, time 0.3ms
#    RTT[ms]: min = 0, median = 0, p(50) = 0, max = 0

# shellcheck disable=SC2154
test_pop_reachable()
{
  if [ -e "${rebooting_file}" ] || [ -e "${pop_repair_file}" ]; then
    monit_debug "pop" "skip test - rebooting/repairing"
    echo 0
    return
  fi

  # Figure out if its ping time again.
  local now
  now="$(monotonic-touch -t 2>/dev/null)"
  local ping_try_time
  ping_try_time="$(stat -c %Y "${pop_last_ping_try_file}" 2>/dev/null)"
  if [ "$((now - ping_try_time))" -lt "${pop_ping_period_sec}" ]; then
    monit_debug "pop" "ok - waiting to ping"
    echo 0
    return # wait longer to ping again
  fi

  # Time to ping again and run all the piggybacked tests
  monotonic-touch "${pop_last_ping_try_file}" # update last ping attempt time

  # Perform filesystem tests
  test_fsys
  if [ $? -ne 0 ]; then
    touch "${pop_repair_file}"
    echo 4 # /tmp or /data is full
    return
  fi

  # Perform config fallback test
  test_config_fallback
  if [ $? -ne 0 ]; then
    touch "${pop_repair_file}"
    echo 3 # reboot and do config fallback
    return
  fi

  # Check if the pop reachability monitor is disabled
  if [ -e "${monit_disable_pop_file}" ]; then
    # prevent spurious pop reachability timeouts when it is re-enabled
    monotonic-touch "${pop_last_ping_reply_file}"
    monit_debug "pop" "ok disabled"
    echo 0 # no more to do: pop reachability monitor is disabled
    return
  fi

  # Get in-band POP addresses
  rm -f "${pop_ip_list_file}"
  touch "${pop_ip_list_file}"
  /usr/sbin/get_pop_ip 2>/dev/null > "${pop_ip_list_file}"

  # Ping all the POPs and check if at least one ping has succeded.
  # See above for the oping command output format.
  p="$(oping -6 -i "${pop_ping_interval_sec}" -c "${pop_ping_packet_count}" \
    -w "${pop_ping_reply_wait_sec}" -f "${pop_ip_list_file}" 2>/dev/null | \
    grep received | cut -d " " -f 4 | grep -m 1 '[1-9]')"

  # Got ping reply from at least one POP
  if [ "${p}" != "" ]; then
    monit_debug "pop" "ok - got ping reply"
    monotonic-touch "${pop_last_ping_reply_file}" # indicate progress
    # Extend the traceroute debug log collection deadline.
    monotonic-touch -o "${pop_traceroute_interval_sec}" "${pop_traceroute_time_file}"
    echo 0
    return # Got ping reply from at least one POP
  fi

  # No ping reply from any pop, so check if we have timed out.
  now="$(monotonic-touch -t 2>/dev/null)"
  local pop_reach_time
  pop_reach_time="$(stat -c %Y "${pop_last_ping_reply_file}" 2>/dev/null)"
  if [ "$((now - pop_reach_time))" -gt "${pop_ping_timeout_sec}" ]; then
    monit_debug "pop" "timed out"
    touch "${pop_repair_file}"
    echo 1 # pop ping attempts timed out
  else
    monit_debug "pop" "ok - not timed out"
    # Collect traceroute debug logs
    local traceroute_time
    traceroute_time="$(stat -c %Y "${pop_traceroute_time_file}" 2>/dev/null)"
    if [ "${now}" -gt "${traceroute_time}" ]; then
      monit_debug "pop" "traceroute"
      timeout -k "${pop_traceroute_kill_timeout_sec}" \
        "${pop_traceroute_timeout_sec}" /usr/sbin/pop_unreachable_cmd.py \
        traceroute --file_name="${pop_ip_list_file}" --timeout=15
      # Set the time for the next traceroute debug log collection.
      monotonic-touch -o "${pop_traceroute_interval_sec}" "${pop_traceroute_time_file}"
    fi
    echo 0 # keep trying to ping POPs
  fi
}

# shellcheck disable=SC2154
repair_pop_reachable()
{
  local failed_test
  if [ "$1" = "1" ]; then
    failed_test="pop"
  fi
  local repair_message
  repair_message="${failed_test}"
  if [ "$1" = "3" ]; then
    failed_test="config"
    repair_message="config-$(cat "${config_fallback_reboot_reason_file}" 2>/dev/null)"
  elif [ "$1" = "4" ]; then
    failed_test="fsys"
    repair_message="fsys-$(cat "${fsys_reboot_reason_file}" 2>/dev/null)"
  fi

  monit_debug "${failed_test}" "trylock"
  lock "${wdog_repair_lock_file}" "${wdog_repair_lock_fd}"
  if [ $? -ne 0 ] || [ -e "${rebooting_file}" ]; then
    monit_debug "${failed_test}" "nolock"
    rm -f "${pop_repair_file}"
    echo 0 # another watchdog is collecting logs, rebooting, repairing etc.
    return
  fi
  monit_debug "${failed_test}" "lock"

  touch "${rebooting_file}" # Tell other watchdogs that we are rebooting
  update_wdog_repair_history "pop_reachable_monit.sh : ${repair_message}" "${wdog_event_reboot}"
  if [ "${failed_test}" = "pop" ]; then
    touch "${pop_reboot_indicator}" # config and fsys test functions set more verbose indicators in reason files
  fi
  /etc/init.d/tg_shutdown
  echo 1 # Reboot: try to recover pop reachability, also clean /data and revert config when necessary

  # Unlock explicitly. This prevents spawned background processes - such as the
  # tps daemon - from hanging on to the lock.
  flock -u "${wdog_repair_lock_fd}" >/dev/null 2>/dev/null
}

# Do nothing if minion is disabled.
if [ "$(check_e2e_minion_enabled)" -eq 0 ]; then
  monit_debug "pop" "ok disabled"
  monit_debug "config" "ok disabled"
  monit_debug "fsys" "ok disabled"
  # Prevent spurious timeouts in the future when minion is enabled.
  monotonic-touch "${pop_last_ping_reply_file}" "${pop_last_ping_try_file}"
  exit 0
fi

ret=0
if [ "x$1" = "xtest" ]; then
  ret=$(test_pop_reachable)
elif [ "x$1" = "xrepair" ]; then
  ret=$(repair_pop_reachable "$2") # $2 is the return value of test_pop_reachable
fi

exit "$ret"
