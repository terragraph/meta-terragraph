#!/bin/bash

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# shellcheck disable=SC2034

# common config
readonly progress_dir="/var/volatile/progress"
readonly minion_file="$progress_dir/minion"
readonly fw_dump_dir="/var/log/fwdumps"
readonly wdog_repair_lock_file="$progress_dir/wdog_repair.lock"
readonly prev_reboot_dirty_indicator="/var/volatile/prev_reboot_dirty"
readonly reboot_mode_file="/data/clean_boot"
readonly rebooting_file="$progress_dir/rebooting"
readonly persistent_disable_file="/data/wdogdisable"
readonly wdog_cli_lock_wait_sec=3
readonly wdog_repair_lock_fd=100
readonly e2e_minion_restart_failed_reboot_indicator="/data/e2e_minion_restart_failed_reboot"
readonly e2e_minion_restart_timeout_sec=30
readonly e2e_minion_restart_retries=5

# Watchdog repair event types. See "tg event add --id".
readonly wdog_event_e2e_restart="WDOG_REPAIR_FW_RESTART"
readonly wdog_event_no_e2e_restart="WDOG_REPAIR_NO_FW_RESTART"
readonly wdog_event_reboot="WDOG_REBOOT"

# wdog logging/tracing (log appears in /data/log/wdog)
#
# wdog tracing can be enabled in a persistent or non-persistent manner.
# Rebooting with the wdog trace enabled can be useful, but it can also fill
# up /data.
readonly monit_log_dir="/data/log/wdog"
readonly monit_log_enable_file="$progress_dir/wdog_log_enable"
readonly monit_log_enable_file2="$monit_log_dir/wdog_log_enable"

# wdog disable
readonly monit_disable_prog_file="$progress_dir/wdog_disable_prog"
readonly monit_disable_link_file="$progress_dir/wdog_disable_link"
readonly monit_disable_pop_file="$progress_dir/wdog_disable_pop"
readonly monit_disable_gps_file="$progress_dir/wdog_disable_gps"
readonly monit_disable_config_file="$progress_dir/wdog_disable_config"
readonly monit_disable_vpp_file="$progress_dir/wdog_disable_vpp"

# wdog timed-disable specific config
readonly monit_enable_time_file="$progress_dir/wdog_enable.time"
readonly monit_enable_lock_file="$progress_dir/wdog_enable.lock"
readonly monit_enable_lock_fd=199
readonly monit_disable_max_minutes=1440

# testcode fallback specific config
readonly cheap_testcode_check_file="$progress_dir/cheap_testcode_check"
readonly minion_connected_file="$progress_dir/minion.connected"
readonly testcode_timeout_file="$progress_dir/testcode_timeout"
readonly testcode_reboot_indicator="/data/testcode_reboot"
readonly testcode_timeout_sec=360 # fall back if minion fails to connect

# config fallback specific config
# NB:
# * The fallback config file (config_fallback_file) must be in /data so that
#   fallback monitoring can be persisted across reboots.
# * The watchdog only allows one reboot with an unverified new config. On the
#   second reboot, the unverified new config will fall back. This second reboot
#   does not need to be triggered by the config_fallback monitor.
# * Config fallback is performed by flash_mtd.sh on startup.
readonly config_fallback_dir="/data/cfg/fallback"
readonly config_fallback_file="$config_fallback_dir/node_config.json"
readonly config_fallback_tmp_file="$config_fallback_file.tmp"
readonly config_fallback_bad_config_file="$config_fallback_file.bad"
readonly config_fallback_lock_file="$progress_dir/config_fallback.lock"
readonly config_fallback_timeout_file="$progress_dir/config_fallback.timeout"
readonly config_fallback_do_fallback="$config_fallback_dir/config_fallback.do_fallback"
readonly config_fallback_reboot_reason_file="/data/config_fallback.reboot_reason"

readonly preloaded_config_dir="/data/cfg/preload"
readonly preload_fallback_file="${preloaded_config_dir}/fallback.json"
readonly preloaded_version_file="${preloaded_config_dir}/image_version"
readonly preloaded_config="${preloaded_config_dir}/node_config.json"
# wdog considers the new config "active" after config_fallback_delay_timeout_sec
# The delay must allow for the time it takes for re-ignition after a reboot.
# In principle it is possible for the new config never to take effect, for example
# if config activation only requires some service to be restarted. This failure will only
# be noticed be the config wdog if minion stops receiving status acks after the
# delay period.
readonly config_fallback_delay_timeout_sec=185 # must be less than config_fallback_timeout_sec
# config_fallback_timeout_sec is the overall deadline for unverified new config
# to become active and for minion to receive a status ack from the controller.
readonly config_fallback_timeout_sec=427 # includes the delay period
readonly config_fallback_lock_fd=198

# fsys monitor specific config
# Note: It is possible for /data to fill up so much that the reboot reason
# can not be updated. In this case the necessary cleanup will still take place on reboot.
readonly fsys_reboot_reason_file="/data/fsys_reboot_reason"
readonly fsys_data_cleanup_percent=90 # do some /data cleanup on reboot
readonly fsys_extra_data_cleanup_percent=95 # do more /data cleanup on reboot in special cases
readonly fsys_max_data_percent=96 # must be large enough to trigger some /data cleanup on reboot
readonly fsys_max_tmp_percent=96

# progress (f/w) monitor specific config
readonly prog_repair_file="$progress_dir/prog_repair"
readonly fw_health_dir="$progress_dir/fw"
readonly fw_nolink_dir="$progress_dir/nolink" # baseband cards with no RF link for 15 minutes
readonly fw_num_radios_file="$progress_dir/fw_num_radios" # discovered via lspci
readonly fw_max_dead_radios=3
readonly fw_init_time_sec=60 # delay progress observation until all baseband cards are initialized
readonly fw_init_time_file="$progress_dir/fw_init_time"
readonly fw_radio_timeout_sec=20
readonly fw_min_dump_intvl_sec=300

# link monitor specific config
readonly link_repair_file="$progress_dir/link_repair"
readonly running_file="$progress_dir/running"
readonly reachable_file="$progress_dir/reachable"
readonly link_monit_reboot_indicator="/data/link_monit_reboot"
readonly data_timeout_sec=60
readonly max_links=6

# pop reachability specific config
#
# Note: max oping execution time = pop_ping_packet_count * pop_ping_reply_wait_sec
#       The max oping execution time must not come close to the wdog test-timeout (presently 60s)
#
readonly pop_repair_file="$progress_dir/pop_repair"
readonly pop_last_ping_reply_file="$progress_dir/pop_last_ping_reply"
readonly pop_last_ping_try_file="$progress_dir/pop_last_ping_try"
readonly pop_reboot_indicator="/data/pop_reboot"
readonly pop_ip_list_file="$progress_dir/pop_ip_list"
readonly pop_ping_timeout_sec=3613
readonly pop_ping_period_sec=61
readonly pop_ping_packet_count=2
readonly pop_ping_interval_sec=0.5
readonly pop_ping_reply_wait_sec=3
readonly pop_traceroute_time_file="$progress_dir/pop_traceroute_time"
readonly pop_traceroute_interval_sec=300
readonly pop_traceroute_timeout_sec=45
readonly pop_traceroute_kill_timeout_sec=5

# gps monitor specific config
readonly gps_good_file="$progress_dir/gps"
readonly gps_good_received_file="${gps_good_file}.rx" # indicates that we have received a gps good message
readonly gps_good_startup_file="${gps_good_file}.start" # copy of the gps_good_file (and hence gps good time) the first time watchdog checks it
readonly gps_timeout_data_file="/data/gps_timeout" # gps timeout left from the previous reboot
readonly gps_timeout_tmp_file="$progress_dir/gps_timeout" # tmp copy of gps_timeout_data_file
readonly gps_timeout_sec=1817 # 17 for best effort avoidance of concurrent timeouts

# vpp cli deadlock monitor config
readonly vpp_cli_check_time_file="$progress_dir/vpp_cli_check_time"
readonly vpp_cli_backoff_file="/data/vpp_cli_backoff" # backoff indicator file
readonly vpp_cli_startup_check_delay_sec=40 # delay after startup for vpp cli checks
readonly vpp_cli_check_period_sec=15
readonly vpp_cli_check_timeout_sec=5
readonly vpp_cli_backoff_check_delay_sec=60 # additional delay when vpp restart does not help

# Shared utility functions that depend on monit_config.sh
# Note: independent utility functions are in monit_utils.sh
#
# shellcheck disable=SC2154
monit_debug()
{
  if [ -e "${monit_log_enable_file}" ] || [ -e "${monit_log_enable_file2}" ];
  then
    echo "$(monotonic-touch -t 2>/dev/null) $2" 2>/dev/null >> "${monit_log_dir}/$1"
  fi
}

# Enable all wdog monitors that can be disabled
# shellcheck disable=SC2154
enable_all_tg_scripts() {
  if [ ! -e /data/wdogtestmode ]; then
    # Until the watchdog is better tested in Puma, the s/w watchdogs
    # can only be enabled in "test mode".
    return
  fi
  rm -f "${monit_disable_prog_file}" "${monit_disable_link_file}" \
        "${monit_disable_pop_file}" \
        "${monit_disable_gps_file}"  \
        "${monit_disable_config_file}" \
        "${monit_disable_vpp_file}" \
        "${persistent_disable_file}" 2>&1
}

# Disable all non-critical wdog monitors
# shellcheck disable=SC2154
disable_all_tg_scripts() {
  touch "${monit_disable_prog_file}" "${monit_disable_link_file}" \
        "${monit_disable_pop_file}" \
        "${monit_disable_gps_file}"  \
        "${monit_disable_config_file}" 2>&1
}

# shellcheck disable=SC2154
cancel_config_fallback() {
  rm -f "${config_fallback_file}" "${config_fallback_timeout_file}" \
        "${config_fallback_do_fallback}" "${config_fallback_reboot_reason_file}" 2>/dev/null
}
