#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# usage: python dump_ublox.py /data/root/ublox_dump.ubx
# use ublox u-center on windows to parse ublox_dump.ubx
# Caveats:
# debug messages cannot be read through this script
# the read frequency is very slow
# it has not been very useful for ublox analysis

import sys
import time


if len(sys.argv) != 2 or sys.argv[1] == "--help":
    print("usage: dump_ublox.py /data/root/ublox_dump.ubx")
    exit(0)

class_id_list = [
    [0x03, 0x0F],  # TRK-SFRBX
    [0x03, 0x10],  # TRK-MEAS
    [0x0C, 0x10],  # DBG-NAV
    [0x0C, 0x31],  # DBG-GNSSDB
    [0x03, 0x09],  # Unknown
    [0x03, 0x11],  # Unknown
    [0x02, 0x57],  # Unknown
    [0x0A, 0x26],  # Unknown
    [0x0A, 0x06],  # MON-MSGPP
    [0x01, 0x22],  # NAV-CLOCK
    [0x01, 0x06],  # NAV-SOL
    [0x01, 0x30],  # NAV-SVINFO
    [0x01, 0x07],  # NAV-PVT
    [0x01, 0x03],  # NAV-STATUS
    [0x0D, 0x12],
]  # TIM-TOS
sync_chars = [0xB5, 0x62]
null_len_chars = [0x00, 0x00]
out_bytes = []

for _x in range(0, 5):
    for class_id in class_id_list:
        cmd_chars = sync_chars + class_id + null_len_chars
        cmd = " " + " ".join([hex(n)[2:] for n in cmd_chars]) + " "
        print(cmd)
        with open("/sys/class/fb_tgd_gps/cmd_cfg", "w") as f:
            f.write(cmd)
        time.sleep(3)
        with open("/sys/class/fb_tgd_gps/resp_cfg", "r") as f:
            resp = f.read()
            print(resp)
            out_bytes += [int(num, 16) for num in resp.split()]
        time.sleep(3)

with open(sys.argv[1], "w") as f:
    f.write(bytearray(out_bytes))
