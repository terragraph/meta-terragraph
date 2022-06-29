#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cleanup()
{
  echo "Removing mtdhack"
  rmmod mtdhack
  echo "done"
}

if [ "$#" -lt 1 -o ! -f "$1" ]; then
        echo "Usage: $0 uboot-image.bin [force]"
        exit 64
fi

grep recovery /proc/mtd >/dev/null 2>&1
if [ $? -ne 0 ]; then
	echo "Unexpected partitioning scheme!"
	exit 64
fi

ubootimg=$1

if [ "$2" != "force" ]; then
	echo "Updating uboot with ${ubootimg}.  Are you sure (y/n)?"
	read answer

	if [ "$answer" != "y" ]; then
		exit 0
	fi
fi

mtd0size=$(cat /sys/class/mtd/mtd0/size)
erasesize=$(cat /sys/class/mtd/mtd0/erasesize)
mtd0blocks="$((mtd0size / erasesize))"
imagesize="$(stat -c %s -L "${ubootimg}" 2>/dev/null)"
imageblocks="$((imagesize / erasesize))"
if [ "$((imagesize % erasesize))" -ne 0 ];
then
  imageblocks="$((imageblocks + 1))"
fi
start=0
maxstart=$(((mtd0blocks - imageblocks) * erasesize))

ret=3 # assume failure
modprobe mtdhack
trap cleanup EXIT

while [ ${start} -le ${maxstart} ];
do
  echo "Flashing contiguous image @ $(printf '0x%x' "${start}")"
  flash_erase -q -N /dev/mtd0 "${start}" "${imageblocks}" 2>/dev/null
  nandwrite -N -p -q -s ${start} /dev/mtd0 "${ubootimg}"

  echo "verifying ${ubootimg} @ $(printf '0x%x' "${start}")"
  nanddump --bb=dumpbad -q -s "${start}" -l "${imagesize}" /dev/mtd0 | head -c "${imagesize}" | cmp -s "${ubootimg}"
  if [ $? -eq 0 ]; then
    echo "verify ok"
    fw_setenv tg_ubifs_init # Force TG boot environment reinitialization on the next reboot
    ret=0
    break
  fi
  echo "verify mismatch"

  echo "Invalidating first block of bad image @ $(printf '0x%x' "${start}")"
  flash_erase -N /dev/mtd0 "${start}" 1 2>/dev/null
  dd status=none bs="${erasesize}" count=1 if=/dev/zero | nandwrite -N -p -q -s "${start}"  /dev/mtd0 -

  # Try writing a contigous image starting at the next eraseblock
  start=$((start + erasesize))
done

exit ${ret}
