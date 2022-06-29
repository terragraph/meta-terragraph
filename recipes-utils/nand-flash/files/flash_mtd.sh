#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

### BEGIN INIT INFO
# Provides:          flash_mtd
# Required-Start:
# Required-Stop:
# Default-Start:     1 2 3 4 5
# Default-Stop:
# Short-Description: Flash and mount the data MTD.
# Description: Flash, attach, format, and create volume for the data/config mtd
### END INIT INFO

# shellcheck disable=1091
. /etc/monit_config.sh # FIXME: this is a bash script

DATA_MOUNT_POINT="/data"
TMP_DIR="/var/volatile"
CONFIG_DIR="${DATA_MOUNT_POINT}/cfg"
NTP_CONFIG_DIR="${DATA_MOUNT_POINT}/etc"
CONFIG_FILE_NAME="node_config.json"
CONFIG_FILE="${CONFIG_DIR}/${CONFIG_FILE_NAME}"
TMP_CONFIG_FILE="${TMP_DIR}/${CONFIG_FILE_NAME}"
DERIVED_CONFIG_ENV_FILE="${DATA_MOUNT_POINT}/cfg/config"
NTP_CONFIG_FILE_NAME="ntp.conf"
NTP_CONFIG_FILE="${NTP_CONFIG_DIR}/${NTP_CONFIG_FILE_NAME}"
TMP_NTP_CONFIG_FILE="${TMP_DIR}/${NTP_CONFIG_FILE_NAME}"
VERSION_FILE="/etc/tgversion"
HOME_ROOT=/etc/skel
DATA_ROOT=/data/root
MOUNTOPTS=rw,noatime

check_and_create_data_root () {
	# We have root on /data instead of /home
	# copy dot files from /home unless "sticky" file created to prevent this
	if [ ! -d $DATA_ROOT ] || [ ! -e ${DATA_ROOT}/sticky ]; then
		echo "Updating $DATA_ROOT with dot files from $HOME_ROOT"
		/bin/mkdir -p $DATA_ROOT
		/bin/cp -a $HOME_ROOT/. $DATA_ROOT
	fi

	/bin/mkdir -p "${CONFIG_DIR}"
	# shellcheck disable=SC2154
	/bin/mkdir -p "${preloaded_config_dir}"
}

# shellcheck disable=SC2154
clean_data_partition () {
	data_usage() {
		data_part_used=$(df --output=pcent ${DATA_MOUNT_POINT} | sed -n '2p')
		data_part_used=${data_part_used%\%}
		echo "$data_part_used"
	}
	if [ "$(data_usage)" -gt "${fsys_data_cleanup_percent}" ]; then
		# Remove logs and any screenlogs in /data
		echo "Cleaning up ${DATA_MOUNT_POINT} "
		/bin/rm -rf ${DATA_MOUNT_POINT}/log/*  2> /dev/null
		/bin/rm -rf ${DATA_MOUNT_POINT}/fwdumps/* 2> /dev/null
		find ${DATA_MOUNT_POINT} -name 'nohup.out' -print0 | xargs -I {} -0 /bin/rm  {} 2> /dev/null
		find ${DATA_MOUNT_POINT} -name 'screenlog*' -print0 | xargs -I {} -0 /bin/rm  {} 2> /dev/null
	fi
	if  [ ! -d ${DATA_MOUNT_POINT}/root ] || [ ! -d ${DATA_MOUNT_POINT}/etc ] ||
		[ ! -d ${DATA_MOUNT_POINT}/etc/ssh ] ||
		[ $(ls -1 ${DATA_MOUNT_POINT}/etc/ssh | wc -l ) -lt 8 ]; then
		#  Can create problems sshing since we
		# have our ssh keys in /data/etc and /data/root is root dir
		# So if data_usage is still high to avoid failing creation of
		# above dirs clean up any big files.  So unexpected files
		# might dissapper but can't do much.  Also make sure there are
		# actually files present in /data/etc/ssh.  We generate 8
		# files so check if we have those many.  Crude test for now
		while [ "$(data_usage)" -gt "${fsys_extra_data_cleanup_percent}" ]; do
			echo "Further cleaning of /data "
			find ${DATA_MOUNT_POINT} -type f -printf '%s\t%p\n' \
				| sort -nr | cut -f2 | head -n1 | xargs /bin/rm -rf
		done
	fi
}

format_and_mount_ubifs () {

	# try to save node config
	if [ -e "${CONFIG_FILE}" ]; then
		/bin/cp "${CONFIG_FILE}" "${TMP_CONFIG_FILE}" 2> /dev/null
	fi
	# try to save ntp config
	if [ -e "${NTP_CONFIG_FILE}" ]; then
		/bin/cp "${NTP_CONFIG_FILE}" "${TMP_NTP_CONFIG_FILE}" 2> /dev/null
	fi
	# unmount in case it was mounted readonly
	umount "${DATA_MOUNT_POINT}" > /dev/null 2>&1
	# detach first incase only the mount command failed (missing volume)
	ubidetach /dev/ubi_ctrl -d 1 > /dev/null 2>&1
	# format the file system
	ubiformat "/dev/${DATA_MTD}" -y && \
	# attach to confirm
	ubiattach -d1 --dev-path "/dev/${DATA_MTD}" && \
	# create ubi volume
	ubimkvol --maxavsize --name data "${DATA_DEV}" &&
	# mount the file system on /data
	mount -t ubifs -o "${MOUNTOPTS}" "${DATA_VOL}" "${DATA_MOUNT_POINT}"
	if [ -e "${TMP_CONFIG_FILE}" ]; then
		echo "Restoring node config"
		/bin/mkdir -p "${CONFIG_DIR}"
		/bin/mv "${TMP_CONFIG_FILE}" "${CONFIG_FILE}"  2> /dev/null
	fi
	if [ -e "${TMP_NTP_CONFIG_FILE}" ]; then
		echo "Restoring ntp config"
		/bin/mkdir -p "${NTP_CONFIG_DIR}"
		/bin/mv "${TMP_NTP_CONFIG_FILE}" "${NTP_CONFIG_FILE}" 2> /dev/null
	fi
	# make sure mounted rw
	grep "${DATA_MOUNT_POINT}" /proc/mounts \
			| awk '{print $4}' | grep rw > /dev/null
}

#
# Figure out where our data lives
#
DATA_MTD=$(grep data -w /proc/mtd|cut -d: -f1)
if [ -n "${DATA_MTD}" ]; then
	DATA_VOL=/dev/ubi1_0
	DATA_DEV=/dev/ubi1
	MOUNTOPTS="${MOUNTOPTS},chk_data_crc"
else
	# Determine the disk we boot from
	kernel_root=$(sed -E 's/^.*[[:space:]]+root=([^[:space:]]+).*/\1/g' /proc/cmdline)
	# Only understand MMC disks
	case "${kernel_root}" in
	/dev/mmcblk*)
		boot_disk=${kernel_root%p*}
		data_part=$(/usr/sbin/sgdisk -p "${boot_disk}" | awk '/^\s+[[:digit:]+]/ { if ($7 == "data") print $1 }')
		;;
	*)
		exit 0
		;;
	esac

	DATA_DEV=
	if [ -e "${boot_disk}p${data_part}" ]; then
		DATA_DEV="${boot_disk}p${data_part}"
		DATA_VOL=${DATA_DEV}
		MOUNTOPTS="${MOUNTOPTS},data=ordered"
	fi
	if [ -z "${DATA_DEV}" ]; then
		exit 0;
	fi
fi

# Check if the data volume has been mounted
grep -qw "$DATA_VOL" /etc/mtab
if [ $? -eq 0 ]; then
	# Already mounted
	check_and_create_data_root
	exit 0
fi

# Deal with mounting of MTD /data
if [ -n "${DATA_MTD}" ]; then

	mount_failed=1 # assume failed

	# verify we've created the ubi by attaching & mounting
	ubiattach -d1 --dev-path "/dev/${DATA_MTD}" && \
	mount -t ubifs -o "${MOUNTOPTS}" "${DATA_VOL}" "${DATA_MOUNT_POINT}"

	if [ $? -eq 0 ]; then
		# make sure mounted as rw
		grep "${DATA_MOUNT_POINT}" /proc/mounts \
			| awk '{print $4}' | grep rw > /dev/null
		mount_failed=$?
	fi

	# if we're unable to attach + mount, then we need to create it first
	if [ $mount_failed -ne 0 ]; then
		format_and_mount_ubifs
		ret=$?
		if [ "$ret" -ne 0 ]; then
			echo Failed mounting ${DATA_MOUNT_POINT} && exit 1
		fi
		# log that we formatted the disk
		echo  "/data formatted at $(date)"   > ${DATA_MOUNT_POINT}/format_data.log
	fi
else # Deal with data on MMC
	# try mounting the FS
	mount -t ext4 -o "${MOUNTOPTS}" "${DATA_VOL}" "${DATA_MOUNT_POINT}"
	# if we're unable to mount, then we need to create the FS it first
	if [ $? -ne 0 ]; then
		mkfs.ext4 -F -q "${DATA_DEV}"
		mount -t ext4 -o "${MOUNTOPTS}" "${DATA_VOL}" "${DATA_MOUNT_POINT}"
		[ $? -ne 0 ] && echo Failed mounting ${DATA_MOUNT_POINT} && exit 1
	fi
fi

# clear /data in case it is full for any reason
clean_data_partition

check_and_create_data_root

# this logfile is only for preload related messages
touch "/data/cfg/preload/log"
# shellcheck disable=SC2154
if [ -f "${preload_fallback_file}" ]; then
  # Testcode failed to commit. Fallback to pre-upgrade config file
  echo "$(date) Falling back" >> "/data/cfg/preload/log" 2>/dev/null
  mv -f "${preload_fallback_file}" "${CONFIG_FILE}"
  rm -f "${DERIVED_CONFIG_ENV_FILE}" # force rebuild of the derived config env
elif [ -f "${preloaded_config}" ]; then
  echo "$(date) Preloaded config exists" >> "/data/cfg/preload/log" 2>/dev/null
  if cmp -s "${VERSION_FILE}" "${preloaded_version_file}"; then
    echo "$(date) Version file matches" >> "/data/cfg/preload/log" 2>/dev/null
    if [ -f "${config_fallback_file}" ]; then
      # Don't apply preloaded config if a config fallback exists
      echo "$(date) Config fallback exists!" >> "/data/cfg/preload/log" 2>/dev/null
    else
      echo "$(date) Loading preloaded config" >> "/data/cfg/preload/log" 2>/dev/null
      mv -f "${CONFIG_FILE}" "${preload_fallback_file}" 2>/dev/null
      mv -f "${preloaded_config}" "${CONFIG_FILE}" 2>/dev/null
      rm -f "${preloaded_version_file}"
      sync
    fi
  else
    # Remove preloaded config because the corresponding image failed to boot
    echo "Version file mismatch" >> "/data/cfg/preload/log" 2>/dev/null
    rm -f "${preloaded_config}"
    rm -f "${preloaded_version_file}"
  fi
fi

# shellcheck disable=SC2154
if [ -f "${config_fallback_file}" ]; then
	mkdir -p "$progress_dir"
	if [ -f "${config_fallback_do_fallback}" ]; then
		# Perform the config fallback.
		rm -f "${config_fallback_do_fallback}"
		mv -f "${CONFIG_FILE}" "${config_fallback_bad_config_file}" 2>/dev/null
		mv -f "${config_fallback_file}" "${CONFIG_FILE}" 2>/dev/null
		rm -f "${DERIVED_CONFIG_ENV_FILE}" # force rebuild of the derived config env
		if [ ! -f "${config_fallback_reboot_reason_file}" ]; then
			# shellcheck disable=SC2039
			echo -n "fallback" > "${config_fallback_reboot_reason_file}" 2>/dev/null
		fi
		sync
	else
		# Start fallback monitoring. This was a spurious reboot before config
		# wdog timed out, or we were rebooted in order to apply the new config.
		monotonic-touch -o "${config_fallback_timeout_sec}" "${config_fallback_timeout_file}"
		# Force config fallback on the next startup. This protects us from a
		# reboot loop triggered by the new config that is shorter than the config
		# wdog timeout. See cancel_config_fallback for cleanup actions when
		# the config wdog is happy with the new config.
		touch "${config_fallback_do_fallback}"
	fi
fi

# Restore the event cache
/etc/init.d/persist_event_cache.sh load

# Was previous reboot clean or unclean?  REBOOT_MODE_FILE
# needs to be removed during clean shutdown.  If it is still present it
# indicates previous reboot was unclean say because kernel crashed
# and watchdog reset the board.
#
# Touch a file in /var/volatile indicating unclean reboot which
# can then be used by any utility that wants to know this
if [ -e "${reboot_mode_file:?}"  ] ; then
	touch "${prev_reboot_dirty_indicator:?}"
fi
touch "${reboot_mode_file}"

exit 0
