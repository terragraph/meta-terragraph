#! /bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Invoke iperf using vppcom library to bypass slowpath
#
VCL_CFG=/etc/vpp/vcl.conf

# Parse VPP version string:
#   vpp v20.12-LSDK~g6e1bc...
VPP_VERSION=$( vppctl show version | sed -e 's/vpp v\([0-9.]\+\).*$/\1/' )
LDP_PATH=/usr/lib/libvcl_ldpreload.so.${VPP_VERSION}
taskset --cpu-list 0 sh -c "LD_PRELOAD=$LDP_PATH VCL_CONFIG=$VCL_CFG iperf3 $*"
