#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

if [ "$#" -lt 1 -o ! -f "$1" ]; then
        echo "Usage: $0 uboot-image.bin [force]"
        exit 64
fi

mtd=`awk -F: '/"u-boot"$/ {print $1}' /proc/mtd`
if [ "x${mtd}" = x ]; then
	echo "u-boot MTD was not found" 1>&2
	exit 1
fi

ubootimg=$1

if [ "$2" != "force" ]; then
	echo "Updating uboot with ${ubootimg}.  Are you sure (y/n)?"
	read answer

	if [ "$answer" != "y" ]; then
		exit 0
	fi
fi

if flashcp -v "${ubootimg}" /dev/"${mtd}"; then
	exec fw_setenv tg_env_init # Force TG boot environment reinitialization on the next reboot
else
	exit 2
fi
