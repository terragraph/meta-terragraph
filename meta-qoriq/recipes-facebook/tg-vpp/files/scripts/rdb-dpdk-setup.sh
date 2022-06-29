#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

#######################################################################################
# This script initializes dpdk for vpp.
# It should only be run once after the node boots up.
# It configures the nic interfaces and assigns them for dpdk.
#######################################################################################
dpdk_setup()
{
  ###### disconnect nic1 (dpmac7) and nic2 (dpmac8) from the macs.
  restool dprc disconnect dprc.1 --endpoint dpmac.7
  restool dprc disconnect dprc.1 --endpoint dpmac.8
  # dynamic_dpl.sh script will read/write from the current directory during the run.
  CWD="$(pwd)"
  cd /tmp/ || return 1
  DPSECI_COUNT=0 /usr/bin/dpdk-extras/dynamic_dpl.sh dpmac.7 dpmac.8
  cd "${CWD}" || return 1
}

nic_init()
{
  ls-addni  dpmac.7
  ifconfig nic1 up
  ls-addni  dpmac.8
  ifconfig nic2 up
}

nic_init
dpdk_setup

