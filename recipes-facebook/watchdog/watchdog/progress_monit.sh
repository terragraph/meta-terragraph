#!/bin/bash

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# shellcheck source=/dev/null
. /etc/monit_config.sh
# shellcheck source=/dev/null
. /etc/monit_utils.sh

readonly config_enabled="$(check_config_enabled)"
readonly e2e_minion_enabled="$((config_enabled & 1))"
readonly dpdk_enabled="$((config_enabled & 2))"

# We are piggybacking vpp cli deadlock checking on the progress monitoring framework
# shellcheck disable=SC2154
test_vpp_cli_deadlock()
{
  # Skip vpp cli deadlock check if there are no radios or dpdk is disabled
  num_radios=$(cat "${fw_num_radios_file}" 2>/dev/null)
  if [ "${num_radios}" -eq 0 ] || [ "${dpdk_enabled}" -eq 0 ] 2>/dev/null; then
    monit_debug "vpp_cli" "no radios or dpdk diabled, skipping check"
    return 0
  fi

  # Wait for vpp cli deadlock check time
  local vpp_check_time
  vpp_check_time="$(stat -c %Y "${vpp_cli_check_time_file}" 2>/dev/null)"
  local now
  now="$(monotonic-touch -t 2>/dev/null)"
  if [ "${now}" -le "${vpp_check_time}" ] 2>/dev/null; then
    monit_debug "vpp_cli" "waiting for vpp_cli check time"
    return 0
  fi

  # Perform the vpp cli deadlock check
  local res
  timeout -k 2 "${vpp_cli_check_timeout_sec}" vppctl show version >/dev/null 2>/dev/null
  res=$?
  # Note: res=124 means timeout, res=137 means timeout + KILL signal was needed
  if [[ ${res} != 124 && ${res} != 137 ]]; then
    # vpp is ok
    if [ ${res} = 0 ]; then
      monit_debug "vpp_cli" "ok"
    else
      monit_debug "vpp_cli" "down, but not hanging"
    fi
    rm -f "${vpp_cli_backoff_file}" # no need for additional delay
    # Set the next vpp cli check time
    monotonic-touch -o "${vpp_cli_check_period_sec}" "${vpp_cli_check_time_file}"
    return 0 # ok
  elif [ -f "${vpp_cli_backoff_file}" ]; then
    # vpp restart did not help
    monit_debug "vpp_cli" "vpp restart did not help. Back off by ${vpp_cli_backoff_check_delay_sec} sec"
    rm -f "${vpp_cli_backoff_file}"  # allow another vpp restart after the backoff period
    # Set the next vpp cli check time
    monotonic-touch -o "${vpp_cli_backoff_check_delay_sec}" "${vpp_cli_check_time_file}"
    return 0 # ok for now
  else
    return 1 # request vpp restart
  fi
}

# We are piggybacking testcode fallback monitoring on the progress monitoring framework
# shellcheck disable=SC2154
test_testcode_fallback()
{
  if [ ! -e "${cheap_testcode_check_file}" ]; then
    if [ "${e2e_minion_enabled}" -ne 0 ] 2>/dev/null; then
      testcode >/dev/null 2>&1
      if [ $? -eq 1 ]; then # We are testcoding. Set the fallback timeout.
        monotonic-touch -o "${testcode_timeout_sec}" "${testcode_timeout_file}"
        monit_debug "testcode" "set ${testcode_timeout_sec} sec timeout"
      fi
    fi
    touch "${cheap_testcode_check_file}" # no more expensive testcode checks
  fi

  if [ ! -e "${testcode_timeout_file}" ]; then
    monit_debug "testcode" "ok"
    return 0 # ok
  fi

  # We are testcoding. Check if minion has managed to connect to the controller.
  if [ -e "${minion_connected_file}" ]; then
    # Minion has connected to the controller
    testcode c >/dev/null 2>&1
    if [ $? -eq 0 ]; then
      # successfully committed the active image
      rm -f "${testcode_timeout_file}" # cancel further timeout checks
      monit_debug "testcode" "committed"
      rm -f "${preload_fallback_file}" # prevent preload fallback
      return 0 # ok
    else
      monit_debug "testcode" "commit failed"
      return 1 # commit failed, fail fast: request reboot instead of re-trying commit
    fi
  fi

  # We are testcoding and minion has not connected to the controller.
  local fallback_time
  fallback_time="$(stat -c %Y "${testcode_timeout_file}" 2>/dev/null)"
  if [ -z "${fallback_time}" ]; then
    fallback_time=0
  fi
  local now
  now="$(monotonic-touch -t 2>/dev/null)"
  if [ "${now}" -gt "${fallback_time}" ] 2>/dev/null; then
    monit_debug "testcode" "timed out"
    return 1 # timed out, request reboot, fall back to primary partition
  fi
  monit_debug "testcode" "waiting until ${fallback_time}"
  return 0 # ok
}

# We are piggybacking the monitoring of the "timed disable" wdog state
# on the progress monitoring framework.

# shellcheck disable=SC2154
timed_enable_tgscripts_nolock()
{
  if [ -e "${rebooting_file}" ]; then
    monit_debug "en" "skip check - rebooting"
    return # nothing to do, tg watchdog scripts will come up enabled on startup
  fi

  if [ ! -e "${monit_enable_time_file}" ]; then
    monit_debug "en" "not disabled"
    return # tg watchdogs are not time-suppressed
  fi

  local enable_time
  enable_time="$(stat -c %Y "${monit_enable_time_file}" 2>/dev/null)"
  if [ -z "${enable_time}" ]; then
    enable_time=0
  fi

  local now
  now="$(monotonic-touch -t 2>/dev/null)"
  if [ "${now}" -gt "${enable_time}" ] 2>/dev/null; then
    enable_all_tg_scripts >/dev/null 2>&1
    rm -f "${monit_enable_time_file}"
    monit_debug "en" "enabled"
  else
    monit_debug "en" "will enable @ ${enable_time}"
  fi
}

# For the timed enable monitor we "test" and "repair" in one invocation
# behind the monit enable lock that protects the monit_enable_time_file.
# shellcheck disable=SC2154
timed_enable_tgscripts()
{
  eval "exec ${monit_enable_lock_fd}>${monit_enable_lock_file}"
  flock -n "${monit_enable_lock_fd}" # don't block wdog context
  if [ $? -ne 0 ]; then
    # cli is modifying the enable time, better luck on the next "progress_monit.sh test" invocation
    monit_debug "en" "could not lock ${monit_enable_lock_file}"
  else
    monit_debug "en" "locked ${monit_enable_lock_file}"
    timed_enable_tgscripts_nolock
    flock -u "${monit_enable_lock_fd}"
  fi
}

# We are piggybacking gps testing on the progress monitoring framework

# Get the total gps timeout (not the remaining timeout)
# shellcheck disable=SC2154
get_gps_timeout_sec()
{
  local timeout_sec
  timeout_sec="${gps_timeout_sec}" # assume the default/hardcoded gps timeout

  if [ ! -e "${gps_good_received_file}" ] && [ -e "${gps_timeout_tmp_file}" ]; then
    # gps has been continuously bad since startup _and_ the gps timeout from
    # the previous shutdown is available.
    local tout
    tout="$(cat "${gps_timeout_tmp_file}" 2>/dev/null)"
    if [ "${tout}" -eq "${tout}" ] 2>/dev/null && [ "${tout}" -ge 0 ] && [ "${tout}" -lt "${gps_timeout_sec}" ]; then
      timeout_sec=${tout}
      monit_debug "gps" "using prev shutdown timeout ${timeout_sec}"
    fi
  fi

  echo "${timeout_sec}"
}

# shellcheck disable=SC2154
get_gps_good_time()
{
  local gps_good_time
  gps_good_time="$(stat -c %Y "${gps_good_file}" 2>/dev/null)"

  if [ -z "${gps_good_time}" ]; then
    # The gps good indicator file is gone.
    gps_good_time=0 # assume that the gps has been bad since startup (monotonic time 0)
    monit_debug "gps" "gps ok file missing"
  elif [ ! -e "${gps_good_received_file}" ]; then
    # We have yet to receive the first gps ok message since startup.
    if [ ! -e "${gps_good_startup_file}" ]; then
      # Save the gps good timestamp that will need to change at least once
      # for us to stop consuming the gps timeout from the previous shutdown.
      # Note that the gps_good_file is created at startup, and by itself it
      # does not indicate that the gps was ever actually good.
      cp -fp "${gps_good_file}" "${gps_good_startup_file}" 2>/dev/null
      monit_debug "gps" "waiting for 1st ok since $(stat -c %Y "${gps_good_startup_file}" 2>/dev/null)"
    fi
    local gps_good_startup_time
    gps_good_startup_time="$(stat -c %Y "${gps_good_startup_file}" 2>/dev/null)"
    if [ "${gps_good_time}" -gt "${gps_good_startup_time}" ]; then
      # First gps ok message since startup -or- first reset if gps has been bad since startup.
      touch "${gps_good_received_file}" 2>/dev/null # stop looking for the first gps ok message
      monit_debug "gps" "First ok/reset @${gps_good_time} sec"
    else
      # gps good timestamp has not changed since startup
      monit_debug "gps" "bad since startup"
      gps_good_time=0 # gps has been bad since startup (monotonic time 0)
    fi
  fi

  echo "${gps_good_time}"
}

# piggybacked gps health test
# shellcheck disable=SC2154
# shellcheck disable=SC2046
test_gps()
{
  local now
  now="$(monotonic-touch -t 2>/dev/null)"
  local lag
  lag="$((now - $(get_gps_good_time)))"
  if [ "${lag}" -lt $(get_gps_timeout_sec) ]; then
    monit_debug "gps" "lag ${lag}"
    return 0 # gps not timed out
  else
    monit_debug "gps" "timed out - lag ${lag}"
    return 1 # gps timed out
  fi
}

# shellcheck disable=SC2154
test_radio_progress()
{
  local minion_time
  minion_time="$(stat -c %Y "$minion_file" 2>/dev/null)"
  if [ "$?" -ne 0 ]; then
    monit_debug "prog" "minion progress file gone"
    return 1 # request reboot
  fi

  local radio_mac
  radio_mac="$1"
  local radio_time
  radio_time="$(stat -c %Y "$radio_mac" 2>/dev/null)"
  if [ "$?" -ne 0 ]; then
    monit_debug "prog" "\"$radio_mac\" is gone"
    return 1 # f/w progress file for this radio is gone
  fi

  if [ $((minion_time - radio_time)) -gt "$fw_radio_timeout_sec" ]; then
    monit_debug "prog" "f/w timed out on \"$radio_mac\""
    return 1 # f/w timed out
  fi

  return 0; # ok
}

# shellcheck disable=SC2154
test_prog()
{
  if [ -e "${rebooting_file}" ] || [ -e "${prog_repair_file}" ]; then
    monit_debug "prog" "skip test - rebooting/repairing"
    monit_debug "gps" "skip test - rebooting/repairing"
    monit_debug "en" "skip test - rebooting/repairing"
    monit_debug "testcode" "skip test - rebooting/repairing"
    monit_debug "vpp_cli" "skip test - rebooting/repairing"
    echo 0 # repair not needed
    return
  fi

  # Perform the piggbacked monitoring of the "timed disable" wdog state
  timed_enable_tgscripts

  # Perform the vpp cli deadlock test
  if [ -e "${monit_disable_vpp_file}" ]; then
    monit_debug "vpp_cli" "ok disabled"
  else
    test_vpp_cli_deadlock
    if [ $? -ne 0 ]; then
      touch "${prog_repair_file}" # prevent spurious, concurrent tests/repairs
      monit_debug "vpp_cli" "timed out"
      echo 4 # vpp cli timed out
      return
    fi
  fi

  # Perform testcode monitoring
  test_testcode_fallback
  if [ $? -ne 0 ]; then
    touch "${prog_repair_file}" # prevent spurious, concurrent tests/repairs
    monit_debug "testcode" "timed out"
    echo 3 # testcode has timed out
    return
  fi

  # Perform the piggypacked gps test
  if [ -e "${monit_disable_gps_file}" ]; then
    monit_debug "gps" "ok disabled"
  else
    # Note: We monitor the gps when e2e_minion is disabled.
    #       This can cause spurious gps resets in a non-use case:
    #       gps is good, but driver-if is not loaded for hours.
    test_gps
    if [ $? -ne 0 ]; then
      touch "${prog_repair_file}" # prevent spurious, concurrent tests/repairs
      monit_debug "gps" "timed out"
      echo 2 # gps timed out
      return
    fi
  fi

  #
  # Perform f/w progress test
  #

  if [ -e "${monit_disable_prog_file}" ] || [ "${e2e_minion_enabled}" -eq 0 ] 2>/dev/null; then
    monit_debug "prog" "ok disabled"
    echo 0 # repair not needed
    return
  fi

  # Wait for (some) baseband cards to be initialized with a mac address
  if [ -e "${fw_init_time_file}" ]; then
    local now
    now="$(monotonic-touch -t 2>/dev/null)"
    local fw_init_time
    fw_init_time="$(stat -c %Y "$fw_init_time_file" 2>/dev/null)"
    if [ "${now}" -le "${fw_init_time}" ] 2>/dev/null; then
      monit_debug "prog" "ok - waiting for init"
      echo 0
      return # keep waiting
    fi
    monit_debug "prog" "init period expired"
    rm -f "${fw_init_time_file}"
  fi

  # Verify that the minimum required number of baseband cards have reported
  # their mac address at least once.
  local radios
  radios="$(ls "${fw_health_dir}" 2>/dev/null)"
  local num_radios_found
  num_radios_found="$(wc -w <<< "${radios}")"
  local num_radios_expected
  num_radios_expected="$(cat "${fw_num_radios_file}" 2>/dev/null)"
  monit_debug "prog" "found \"${num_radios_found}\" expected \"${num_radios_expected}\""
  if [ "${num_radios_found}" -ge "1" ] 2>/dev/null && [ "$((num_radios_found + fw_max_dead_radios))" -ge "${num_radios_expected}" ] 2>/dev/null; then
    : # the expected minimum number of radios are alive
  else
    monit_debug "prog" "Only found \"${radios}\""
    touch "${prog_repair_file}" # prevent spurious, concurrent tests/repairs
    echo "init" 2>/dev/null > "${prog_repair_file}"
    echo 1 # too many baseband cards are dead, or f/w health indicators have disappeared
    return
  fi

  # Check each radio for f/w timeout and RF link formation timeout
  # Note: radios that come to life after the init period are expected to stay that way
  for r in ${radios};
  do
    test_radio_progress "${fw_health_dir}/${r}"
    if [ $? -ne 0 ]; then
      monit_debug "prog" "${r} timed out"
      touch "${prog_repair_file}" # prevent spurious, concurrent tests/repairs
      echo "${r}-timeout" 2>/dev/null > "${prog_repair_file}"
      echo 1 # f/w timed out
      return
    elif [ -e "${fw_nolink_dir}/${r}" ]; then
      monit_debug "prog" "${r} no RF link"
      touch "${prog_repair_file}" # prevent spurious, concurrent tests/repairs
      echo "${r}-nolink" 2>/dev/null > "${prog_repair_file}"
      echo 1 # no RF link formed for 15 minutes
    fi
  done

  # No baseband card has timed timed out, or minion is disabled
  monit_debug "prog" "ok"
  echo 0 # repair not needed
}

# shellcheck disable=SC2154
e2e_minion_restart_prog()
{
  if timeout -k 1 "${e2e_minion_restart_timeout_sec}" restart_e2e_minion -r "${e2e_minion_restart_retries}"; then
    monit_debug "$1" "e2e_minion restart ok"
    rm -f "${prog_repair_file}" # allow all prog testing to resume
    echo 0
  else
    monit_debug "$1" "e2e_minion restart failed"
    touch "${rebooting_file}"
    echo "$1" > "${e2e_minion_restart_failed_reboot_indicator}"
    update_wdog_repair_history "progress_monit.sh : e2e_minion_restart_failed" "${wdog_event_reboot}"
    /etc/init.d/tg_shutdown
    echo 1
  fi
}

# Repair f/w progress failure or piggybacked gps failure
# shellcheck disable=SC2154
repair_prog()
{
  local failed_test
  if [ "$1" = "1" ]; then
    failed_test="prog"
  elif [ "$1" = "2" ]; then
    failed_test="gps"
  elif [ "$1" = "3" ]; then
    failed_test="testcode"
  elif [ "$1" = "4" ]; then
    failed_test="vpp_cli"
  fi

  monit_debug "${failed_test}" "trylock"
  lock "${wdog_repair_lock_file}"
  if [ $? -ne 0 ] || [ -e "${rebooting_file}" ]; then
    monit_debug "${failed_test}" "nolock"
    rm -f "${prog_repair_file}"
    echo 0 # another watchdog is collecting logs or rebooting
    return
  fi
  monit_debug "${failed_test}" "lock"

  if [ "${failed_test}" = "testcode" ]; then
    update_wdog_repair_history "progress_monit.sh : ${failed_test}" "${wdog_event_reboot}"
    touch "${rebooting_file}" # tell other watchdogs that we are rebooting
    touch "${testcode_reboot_indicator}" # persist the restart reason
    /etc/init.d/tg_shutdown
    echo 1 # reboot and fall back to the primary boot partition
    return
  fi

  if [ "${failed_test}" = "gps" ]; then
    update_wdog_repair_history "progress_monit.sh : ${failed_test}" "${wdog_event_no_e2e_restart}"
    echo 1 2>/dev/null >/sys/class/fb_tgd_gps/ublox_reset # repair gps
    monotonic-touch "${gps_good_file}" # reset the gps timeout
    rm -f "${prog_repair_file}" # allow f/w and gps testing to resume
    monit_debug "gps" "reset!"
    echo 0 # gps has been fixed, don't reboot
    return
  fi

  if [ "${failed_test}" = "vpp_cli" ]; then
    update_wdog_repair_history "progress_monit.sh : ${failed_test}" "${wdog_event_no_e2e_restart}"
    touch "${vpp_cli_backoff_file}" # delay the next vpp restart if this restart does not help
    killall -q -9 vpp 2>/dev/null # kill vpp and force its restart
    rm -f "${prog_repair_file}" # allow vpp_cli testing to resume
    monit_debug "vpp_cli" "vpp killed, should restart soon"
    echo 0 # vpp will restart shortly, don't reboot
    return
  fi

  update_wdog_repair_history "progress_monit.sh : ${failed_test}-$(cat "${prog_repair_file}")" "${wdog_event_e2e_restart}"

  # Try to collect logs before unloading the f/w.
  # Collect at most 3 and if we already collected 3, then collect one more
  # only if the last one was more than 5 minutes ago and remove the oldest
  # one.

  # How many we have collected so far
  num_fwdumps=$(find "$fw_dump_dir" -maxdepth 2 -type f -name "fwdump-*" | wc -l)
  perform_dump=1
  if [ "$num_fwdumps" -ge 3 ]; then
    # Don't collect a fw dump if we collected only
    # recently and we already have collected at least
    # 3 of them
    latest_fw_dump_file=$(find "$fw_dump_dir" -maxdepth 2 -type f \
      -name "fwdump-*" -exec stat --format '%Y:%n' "{}" \; \
      | sort -nr  | head -n1 | cut -d: -f2)

    #current time
    curr_time=$(date +%s)
    # last fwdump collection time
    file_time=$(stat -c %Y "$latest_fw_dump_file" 2> /dev/null)

    if [ "$curr_time" -gt "$file_time" ] &&
       [ $((curr_time - file_time)) -lt "$fw_min_dump_intvl_sec" ]; then
      # Collecting fwdumps too fast.  Don't do it
      perform_dump=0
    fi
  fi

  if [ "$perform_dump" -eq 1 ]; then

    # Keep only the three most recent log dumps.
    find "$fw_dump_dir" -maxdepth 2 -type f -name "fwdump-*" \
      -printf '%Ts\t%p\n' | sort -nr | cut -f2 | tail -n +3 \
      | xargs rm -rf

    local link_type
    TG_IF2IF=$(/usr/sbin/get_hw_info TG_IF2IF)
    if [ -n "$TG_IF2IF" ] && [ "$TG_IF2IF" == 1 ]; then
      link_type="IF"
    else
      link_type="RF"
    fi
    mkdir -p "${fw_dump_dir}/${link_type}"
    # Add the reason for fwdump to dmesg file, which is collected as part of fwdump
    echo "Firmware keep alives lost, take fwdump" >> /dev/kmsg
    /usr/bin/dump_20130.sh -t "${link_type}" -o "${fw_dump_dir}/${link_type}" > /dev/null 2>&1
  fi

  e2e_minion_restart_prog "prog"
}

ret=0
if [ "x$1" = "xtest" ]; then
  ret=$(test_prog)
elif [ "x$1" = "xrepair" ]; then
  ret=$(repair_prog "$2") # $2 is the return value of test_prog
fi
exit $ret
