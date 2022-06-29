#!/bin/sh
# start/stop gps outage emulation (for testing)
# by setting Minimum Elevation (for a GNSS satellite to be used in NAV)
# to 15 degree or 90 degree in UBX-CFG-NAV5 / minElev
# Usage:
# emulate_gps_outage.sh 1
# emulate_gps_outage.sh 0

if [ "$1" -eq 1 ]; then
    echo "start GPS outage emulation, set mask to 90 degrees"
    echo B5 62 06 24 24 00 02 00 02 03 00 00 00 00 10 27 00 00 5A 00 FA 00 FA 00 64 00 2C 01 00 3C 00 00 00 00 C8 00 03 00 00 00 00 00 > /sys/class/fb_tgd_gps/cmd_cfg
    echo "sleeping for 5 minutes"
    sleep 300
    cat /sys/class/fb_tgd_gps/lat_long
    cat /sys/class/fb_tgd_gps/sat_in_view
elif [ "$1" -eq 0 ]; then
    echo "stop GPS outage emulation, set mask to 15 degrees"
    echo B5 62 06 24 24 00 02 00 02 03 00 00 00 00 10 27 00 00 0F 00 FA 00 FA 00 64 00 2C 01 00 3C 00 00 00 00 C8 00 03 00 00 00 00 00 > /sys/class/fb_tgd_gps/cmd_cfg
    echo "sleeping for 5 minutes"
    sleep 300
    cat /sys/class/fb_tgd_gps/lat_long
    cat /sys/class/fb_tgd_gps/sat_in_view
fi
