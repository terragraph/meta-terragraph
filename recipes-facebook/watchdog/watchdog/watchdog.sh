#! /bin/bash
export LC_ALL=en_US # enable python3 scripts to be called from wdog test/repair scripts

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

### BEGIN INIT INFO
# Provides:        WATCHDOG
# Required-Start:
# Required-Stop:
# Default-Start:   2 3 4 5
# Default-Stop:
# Short-Description: Start WATCHDOG daemon
### END INIT INFO

# shellcheck source=/dev/null
. /etc/monit_config.sh

PATH=/sbin:/bin:/usr/bin:/usr/sbin

DAEMON=/usr/sbin/watchdog
DAEMON_DEV_FILE=/dev/watchdog

test -x $DAEMON -a -r $DAEMON_DEV_FILE || exit 0

# load the function library
# shellcheck source=/dev/null
. /etc/init.d/functions

# shellcheck disable=SC2154
lock_config_fallback()
{
  eval "exec ${config_fallback_lock_fd}>${config_fallback_lock_file}"
  flock -w "${wdog_cli_lock_wait_sec}" "${config_fallback_lock_fd}"
  if [ $? -ne 0 ]; then
    echo "Could not lock ${config_fallback_lock_file}"
    exit 1
  fi
}

# shellcheck disable=SC2154
unlock_config_fallback()
{
  flock -u "${config_fallback_lock_fd}"
}

# shellcheck disable=SC2154
do_prevent_config_fallback_on_reboot() {
  lock_config_fallback
  rm -f "${config_fallback_do_fallback}" 2>/dev/null
  if [ $? -ne 0 ]; then
    echo "Failed to delete ${config_fallback_do_fallback}"
    exit 1
  fi
  unlock_config_fallback
}

# shellcheck disable=SC2154
do_set_config_fallback_file() {
  if [ -z "$1" ]; then
    echo "Missing fallback config file parameter"
    exit 1
  fi
  if [ ! -f "$1" ]; then
    echo "Requested fallback config file does not exist"
    exit 1
  fi
  lock_config_fallback
  mkdir -p "$config_fallback_dir" 2>/dev/null
  cancel_config_fallback
  cp "$1" "${config_fallback_tmp_file}" 2>/dev/null
  if [ $? -ne 0 ]; then
    echo "Failed to copy $1 to ${config_fallback_tmp_file}"
    exit 1
  fi
  sync
  unlock_config_fallback
}

# shellcheck disable=SC2154
do_start_config_fallback_monitor() {
  lock_config_fallback
  if [ ! -f "${config_fallback_tmp_file}" ]; then
    echo "Can't find fallback config file: ${config_fallback_tmp_file}"
    exit 1
  fi
  cancel_config_fallback
  mv "${config_fallback_tmp_file}" "${config_fallback_file}"
  monotonic-touch -o "${config_fallback_timeout_sec}" "${config_fallback_timeout_file}"
  # Assume that we will be rebooted before minion connects with the new config
  # _and_ before the config wdog times out. We allow one reboot with the unverified
  # new config in case the reboot was done specifically to apply it.
  echo -n "unverified-new-config" > "${config_fallback_reboot_reason_file}" 2>/dev/null
  sync
  unlock_config_fallback
}

# shellcheck disable=SC2154
timed_disable_tgscripts() {
  local minutes
  minutes="$1"

  if [ "${minutes}" -eq "${minutes}" ] 2>/dev/null && \
      [ "${minutes}" -gt 0 ] && \
      [ "${minutes}" -le "${monit_disable_max_minutes}" ]; then
    eval "exec ${monit_enable_lock_fd}>${monit_enable_lock_file}"
    flock -w "${wdog_cli_lock_wait_sec}" "${monit_enable_lock_fd}"
    if [ $? -ne 0 ]; then
      echo "Could not lock ${monit_enable_lock_file}"
      exit 1
    fi
    disable_all_tg_scripts
    # The -x option prevents the disable period in effect (if any) from being shortened.
    # Wdog suppression can be cancelled at any time with one of the "enable" cli options,
    # but there is no seamless way of shortening the disable period.
    monotonic-touch -x -o "$((60 * minutes))" "${monit_enable_time_file}"
    flock -u "${monit_enable_lock_fd}"
  else
    echo "Invalid disable time. Range: 1 - ${monit_disable_max_minutes} minutes."
    exit 1
  fi
}

# Disable tg wdog scripts until reboot or until enabled by another command,
# cancel pending timed enable (if any), and restart the wdog daemon.
# shellcheck disable=SC2154
indefinitely_disable_tgscripts() {
  local persist="persist"
  if [ -n "$1" ] && [ "$1" != "${persist}" ]; then
    echo "Unknown option \"$1\""
    exit 1
  fi
  eval "exec ${monit_enable_lock_fd}>${monit_enable_lock_file}"
  flock -w "${wdog_cli_lock_wait_sec}" "${monit_enable_lock_fd}"
  if [ $? -ne 0 ]; then
    echo "Could not lock ${monit_enable_lock_file}"
    exit 1
  fi
  stopdaemon
  rm -f "${monit_enable_time_file}" 2>/dev/null # cancel pending timed enable
  disable_all_tg_scripts
  if [ "$1" = "${persist}" ]; then
    touch "${persistent_disable_file}"
  fi
  startdaemon
  flock -u "${monit_enable_lock_fd}"
}

# Enable all tg scripts and cancel pending timed enable.
enable_tg_scripts() {
  local restart_daemon
  restart_daemon="$1"
  eval "exec ${monit_enable_lock_fd}>${monit_enable_lock_file}"
  flock -w "${wdog_cli_lock_wait_sec}" "${monit_enable_lock_fd}"
  if [ $? -ne 0 ]; then
    echo "Could not lock ${monit_enable_lock_file}"
    exit 1
  fi
  if [ "${restart_daemon}" = "1" ]; then
    stopdaemon
  fi
  rm -f "${monit_enable_time_file}" 2>/dev/null # cancel pending timed enable
  enable_all_tg_scripts
  if [ "${restart_daemon}" = "1" ]; then
    startdaemon
  fi
  flock -u "${monit_enable_lock_fd}"
}

startdaemon(){
  echo -n "Initializing the monitoring environment: "
  /etc/monit_init.sh all > /dev/null 2>&1
  echo "done"
  echo -n "Starting WATCHDOG. "
  cpumask=$((1 << ($(nproc)-1)))
  /usr/bin/taskset ${cpumask} start-stop-daemon --start --quiet --oknodo --startas "${DAEMON}" -- "${DAEMON_ARGS}"
  echo "done"
  if [ -e "${persistent_disable_file}" ]; then
    echo "Warning: software watchdog scripts are persistently disabled!"
  fi
}

stopdaemon(){
  echo -n "Stopping WATCHDOG: "
  start-stop-daemon --stop --retry --quiet --oknodo -x $DAEMON
   while [[ $(status $DAEMON) =~ .*running.* ]]; do
                sleep 0.5
        done
  echo "done"
}

case "$1" in
  start)
  stopdaemon # Required for safe "monit_init.sh all"
  startdaemon
  ;;
  stop)
  stopdaemon
  /bin/rm -f "${reboot_mode_file:?}"
  sync
  # During reboot process watchdog daemon is stopped which
  # then causes hardware watchdog to shut off.  So after
  # that time any system hang/crash will cause the board
  # to hang forever.
  #
  # So once daemon is stopped we start it again as below.
  # Since watchdog daemon is not running we have 30
  # seconds to cleanup.  If not watchdog will reboot the
  # system
  touch /dev/watchdog
  ;;
  force-reload)
  stopdaemon
  startdaemon
  ;;
  restart)
  # Don't reset the tick here
  stopdaemon
  startdaemon
  ;;
  reload)
  # Must do this by hand, but don't do -g
  stopdaemon
  startdaemon
  ;;
  dis_prog)
  touch "${monit_disable_prog_file}"
  ;;
  dis_link)
  touch "${monit_disable_link_file}"
  ;;
  dis_pop)
  touch "${monit_disable_pop_file}"
  ;;
  dis_gps)
  touch "${monit_disable_gps_file}"
  ;;
  dis_config)
  touch "${monit_disable_config_file}"
  ;;
  dis_vpp)
  touch "${monit_disable_vpp_file}"
  ;;
  en_vpp)
  rm -f "${monit_disable_vpp_file}"
  ;;
  dis)
  timed_disable_tgscripts "$2" # $2 is the number of minutes
  ;;
  disable_tgscripts)
  indefinitely_disable_tgscripts "$2" # $2 is optional "persist"
  ;;
  set_config_fallback_file)
  do_set_config_fallback_file "$2" # $2 is the config file to fall back to
  ;;
  start_config_fallback_monitor)
  do_start_config_fallback_monitor
  ;;
  prevent_config_fallback_on_reboot)
  do_prevent_config_fallback_on_reboot
  ;;
  en)
  enable_tg_scripts 0 # 0 means "don't restart the wdog daemon"
  ;;
  enable_tgscripts)
  enable_tg_scripts 1 # 1 means "restart the wdog daemon"
  ;;
  en_logs)
  # shellcheck disable=SC2154
  touch "${monit_log_enable_file}"
  ;;
  dis_logs)
  rm -f "${monit_log_enable_file}"
  ;;
  status)
  status $DAEMON
  exit $?
  ;;
  *)
  echo "Usage: watchdog [ start | stop | status | restart | reload |" >&2
  echo "       dis <minutes> | en | dis_prog | dis_link | dis_pop |" >&2
  echo "       dis_gps | dis_config | dis_vpp | en_vpp |" >&2
  echo "       disable_tgscripts [persist] | enable_tgscripts |" >&2
  echo "       en_logs | dis_logs | set_config_fallback_file <file> |" >&2
  echo "       start_config_fallback_monitor | prevent_config_fallback_on_reboot ]" >&2
  exit 1
  ;;
esac

exit 0
