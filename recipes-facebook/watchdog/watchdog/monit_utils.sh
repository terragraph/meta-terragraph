#!/bin/bash

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# $1 - lock file path
# $2 - [optional] file descriptor for lockfile
# Reference: http://www.kfirlavi.com/blog/2012/11/06/elegant-locking-of-bash-program/
function lock() {
    local defaultFd=100

    if [ x"$1" = x ]; then
        # Pretend that the lock was acquired if lock file path not given.
        return 0
    fi

    if [ x"$2" = x ]; then
        local fd=${2:-$defaultFd}
    else
        local fd=${2:-$2}
    fi

    # create lock file
    eval "exec $fd>$1"

    # acquire the lock, fail if we can't
    flock -n "$fd" && return 0 || return 1
}

function check_config_enabled()
{
    # load tg config environment variables
    . /usr/sbin/config_get_env.sh >/dev/null 2>&1

    local res
    res=0

    if [ "${E2E_ENABLED}" -eq "1" ]; then
        res=1
    fi

    if [ "${DPDK_ENABLED}" -eq "1" ]; then
        res="$((res + 2))"
    fi

    echo "${res}"
}

function check_e2e_minion_enabled()
{
    local res
    res="$(check_config_enabled)"
    echo $((res & 1))
}

function update_wdog_repair_history()
{
    # Update the wdog repair logs
    local hist="/var/log/wdog_repair_history"
    local hist2="${hist}".2 # previous wdog repair history
    local maxLines=50
    local doWriteLog
    doWriteLog=1
    touch "${hist}"
    numLines=$(wc -l "${hist}" | cut -d " " -f 1 2>/dev/null)
    if [ "${numLines}" -ge "${maxLines}" ];
    then
        mv -f "${hist}" "${hist2}"
        if [ $? -ne 0 ];
        then
            doWriteLog=0 # failed to rotate wdog repair history
        fi
    fi
    if [ "${doWriteLog}" -eq 1 ]; then
      echo "$(date +%s 2>/dev/null) $(date -R 2>/dev/null) $1" 2>/dev/null >> "${hist}"
    fi

    # Report the wdog repair log as an event
    local event_timeout_sec
    local event_kill_timeout_sec
    event_timeout_sec=9
    event_kill_timeout_sec=1
    timeout -k "${event_kill_timeout_sec}" "${event_timeout_sec}" \
      tg2 event --category WATCHDOG --id "$2" --reason "$1" >/dev/null 2>&1
}
