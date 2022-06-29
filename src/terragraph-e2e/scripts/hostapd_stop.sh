#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Stop hostapd on a given interface, first trying to 'kill' using a pidfile,
# but falling back on 'pkill' if that fails.

if [ -z "$1" ] || [ "$1" = "--help" ]; then
  echo "Usage: $0 <interface>"
  exit 1
fi

IFACE="$1"
POST_KILL_DELAY="0.2"  # IMPORTANT: adjust this as needed for your platform!

# see e2e_minion's IgnitionApp
PIDFILE="/var/run/hostapd/hostapd_${IFACE}.pid"
CTRL_SOCK="/var/run/hostapd_${IFACE}"

# Try reading pidfile
if [ -f "$PIDFILE" ]; then
  read -r PID < "$PIDFILE"
  if [ -n "$PID" ] && [ "$PID" -gt 1 ]; then
    if kill -9 "$PID"; then
      /bin/sleep "$POST_KILL_DELAY"
      /bin/rm -f "$PIDFILE"
      exit 0
    fi
    # else fall through to 'pkill' (since signal was not sent)
  fi
  /bin/rm -f "$PIDFILE"
fi

# Verify hostapd is running
if ! /usr/sbin/hostapd_cli -p "$CTRL_SOCK" quit >/dev/null 2>&1; then
  exit 1
fi
/usr/bin/pkill -9 -f "hostapd.*${IFACE}\\.conf"
