#!/bin/sh

grep -q u-boot-env /proc/mtd 2> /dev/null
if [ $? -eq 0 ]; then
	# env in mtd.  Look for mtd partition named u-boot-env
	mtd_part=$(grep u-boot-env /proc/mtd | cut -f1 -d:)
	size=0x$(grep "${mtd_part}" /proc/mtd | awk '{print $2}')
	erasesize=0x$(grep "${mtd_part}" /proc/mtd | awk '{print $3}')
	sectors=$((size/erasesize))
	hexsectors=0x$(echo "obase=16 ; $sectors" | bc)
	echo  "#Device         Offset   Size   Sector_Size  Sectors" > /etc/fw_env.config
	echo  "/dev/$mtd_part   0x0   $size  $erasesize  $hexsectors" >> /etc/fw_env.config
	exit 0
fi

MMCBLK="mmcblk0"
SECTOR_SIZE=512
gdisk -l /dev/"${MMCBLK}" | grep -q u-boot-env 2> /dev/null
if [ $? -eq 0 ]; then
	# env on mmc.  Look for gpt partition named u-boot-env
	#   7            6144            6159   8.0 KiB     8300  u-boot-env
	part=$(gdisk -l /dev/"${MMCBLK}" | grep u-boot-env | awk '{print $1}')
	sectors=$(cat /sys/class/block/"${MMCBLK}"p"${part}"/size)
	size=$((sectors * SECTOR_SIZE))
	hexsize=0x$(echo "obase=16 ; $size" | bc)
	echo  "#Device         Offset   Size   Sector_Size  Sectors" > /etc/fw_env.config
	echo  "/dev/${MMCBLK}p${part}  0x0  $hexsize  $SECTOR_SIZE  $sectors" >> /etc/fw_env.config
	exit 0
fi

echo "ERROR:  UBOOT-ENV PARTITION NOT FOUND"
exit 1
