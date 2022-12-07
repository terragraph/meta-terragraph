#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Change NXP core volrage from 1.05 to 1.025
# Need to run one time.

exec 2> /dev/null

i2cset -y 1 0x10 0x10 0x00 b

i2cset -y 1 0x10 0x33 0xFB52 w

i2cset -y 1 0x10 0x35 0xCB40 w

i2cset -y 1 0x10 0x36 0xCB00 w

i2cset -y 1 0x10 0x55 0xD3E0 w

i2cset -y 1 0x10 0x58 0xCB26 w

i2cset -y 1 0x10 0x5D 0xCA00 w

i2cset -y 1 0x10 0x7C 0xff b

i2cset -y 1 0x10 0x7E 0xff b

i2cset -y 1 0x10 0xB0 0x0000 w

i2cset -y 1 0x10 0xB2 0x0000 w

i2cset -y 1 0x10 0xB4 0x0000 w

i2cset -y 1 0x10 0xD1 0x69 b

i2cset -y 1 0x10 0xE6 0x4F b

i2cset -y 1 0x10 0xE8 0xD280 w

i2cset -y 1 0x10 0xF5 0x10 b

i2cset -y 1 0x10 0xF7 0x0BE8 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x01 0x80 b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x02 0x1E b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x21 0x1066 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x24 0x120A w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x25 0x1138 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x26 0x0F94 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x27 0xAA00 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x38 0xC31A w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x40 0x120A w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x41 0xB8 b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x42 0x11A1 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x43 0x0F2B w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x44 0x0EC2 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x45 0xB8 b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x46 0xDA00 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x47 0x00 b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x4A 0xD320 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x4F 0xEB20 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x50 0xB8 b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x51 0xEAA8 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x53 0xE580 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x54 0xB8 b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x56 0xB8 b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x60 0x8000 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x61 0xCA80 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x62 0xDA80 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x63 0xB8 b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x64 0x8000 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x65 0xCA80 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x66 0xF320 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x7A 0xff b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x7B 0xff b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x7D 0xff b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0x80 0xff b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0xB1 0xADF1 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0xB3 0x0000 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0xD0 0x1D b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0xD2 0x6993 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0xD3 0x70 b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0xD4 0x47 b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0xD5 0xC0 b

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0xDB 0xFABC w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0xDC 0xFBE8 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0xF6 0x0F3C w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0xF8 0x4000 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0xF9 0x8000 w

i2cset -y 1 0x10 0 0 b
i2cset -y 1 0x10 0xFA 0x31 b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x01 0x80 b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x02 0x1E b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x21 0x1066 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x24 0x120A w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x25 0x1138 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x26 0x0F94 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x27 0xAA00 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x38 0xC31A w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x40 0x120A w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x41 0xB8 b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x42 0x11A1 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x43 0x0F2B w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x44 0x0EC2 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x45 0xB8 b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x46 0xDA00 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x47 0x00 b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x4A 0xD320 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x4F 0xEB20 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x50 0xB8 b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x51 0xEAA8 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x53 0xE580 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x54 0xB8 b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x56 0xB8 b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x60 0x8000 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x61 0xCA80 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x62 0xDA80 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x63 0xB8 b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x64 0x8000 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x65 0xCA80 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x66 0xF320 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x7A 0xff b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x7B 0xff b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x7D 0xff b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0x80 0xff b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0xB1 0x986A w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0xB3 0x0000 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0xD0 0x1D b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0xD2 0x6993 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0xD3 0x70 b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0xD4 0x47 b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0xD5 0xC0 b

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0xDB 0xFABC w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0xDC 0xFBE8 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0xF6 0x0F3C w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0xF8 0x4000 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0xF9 0x8000 w

i2cset -y 1 0x10 0 1 b
i2cset -y 1 0x10 0xFA 0x31 b

i2cset -y 1 0x10 0x15
