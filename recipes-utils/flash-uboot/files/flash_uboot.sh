#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

dirname=`dirname "$0"`

if [ -f "/usr/sbin/get_hw_info" ]; then
  hw_board_id=$(/usr/sbin/get_hw_info HW_BOARD_ID)
  hw_rev=$(/usr/sbin/get_hw_info HW_REV)
fi

if [ -z "$hw_board_id" ] || [ -z "$hw_rev" ]; then
  # for backward compatability when get_hw_info is missing
  hw_model=$(tr '\0' '\n' < /proc/device-tree/model)
  if echo "$hw_model" | grep -q "Facebook Terragraph Digital Board EVT"; then
    flash_uboot_file="${dirname}"/flash_uboot_mv
  elif echo "$hw_model" | grep -q "NXP TG Board"; then
    flash_uboot_file="${dirname}"/flash_uboot_nxp
  fi
else
  # shellcheck disable=SC2039
  if [[ ( "$hw_board_id" = "MVL_ARMADA39X_P" || "$hw_board_id" = "MVL_ARMADA39X_S" ) && ( "$hw_rev" = "1.2" || "$hw_rev" = "2.0" ) ]]; then
    flash_uboot_file="${dirname}"/flash_uboot_mv
  elif [ "$hw_board_id" = "NXP_LS1012A_BOBCAT" ] || [ "$hw_board_id" = "NXP_LS1048A_JAGUAR" ]; then
    flash_uboot_file="${dirname}"/flash_uboot_nxp
  fi
fi

if [ ! -z "$flash_uboot_file" ]; then
  exec "${flash_uboot_file}" "$@"
else
  echo "Unsupported platform" 1>&2
  exit 1
fi
