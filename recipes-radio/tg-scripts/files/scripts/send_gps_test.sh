#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Testing script for send_gps_time.py

# start gpsd
gpsd /dev/ttyS1

# load driver
driver_if_start.sh &
sleep 2

# logging w/qc driver
mount -o remount,rw /
3pp_host_manager_11ad &
sleep 2
shell_11ad log_collector set_config CpuType=fw PollingIntervalMs=5 MaxNumberOfLogFiles=10
shell_11ad log_collector set_verbosity cputype=fw DefaultVerbosity=E Module15=VIEW
shell_11ad log_collector start_recording recordingtype=txt

# start GPS on node & start sending time
r2d2 node_init
r2d2 fw_set_params forceGpsDisable 0
send_gps_time.py >/tmp/send_gps_time.txt 2>&1 &
r2d2 gps_enable

# ... now r2d2 assoc -m other_mac ...
