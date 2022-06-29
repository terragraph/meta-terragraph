#!/bin/sh

# shellcheck disable=SC1091
. tg.env

wil_setup_iface()
{
	WIL_IF="$1"

	# Get the mac Address for this interface
	BUSID="$(ethtool -i "${WIL_IF}" 2>/dev/null |grep 'bus-info:'|cut -d':' -f2-3|tr -d ' \r\t')"
	if [ -z "${BUSID}" ]; then
		echo "Error: Unable to get Bus ID $BUSID for intf $WIL_IF"
		exit 1
	fi

	MAC_ADDR=$(/usr/sbin/get_wlanmac "${BUSID}")
	if [ -z "${MAC_ADDR}" ]; then
		echo "Error: Unable to get MAC address for busID $BUSID for intf $WIL_IF"
		exit 1
	fi

	# Setup expected MAC address
	if ! ifconfig "${WIL_IF}" hw ether "${MAC_ADDR}"
	then
		echo "Error! Unable to set device MAC address."
		exit 1
	fi
}

fb_get_wil_if_list()
{
	if_busid="${1}"
	if_list=

	# shellcheck disable=SC2013
	for i in $(grep -v 'Inter\|face' /proc/net/dev | cut -d':' -f1)
	do
		modname=$(ethtool -i "${i}" 2>/dev/null |grep 'driver:'|cut -d':' -f2|tr -d ' \t\n\r')
		if [ "${modname}" = "${WL_MOD_NAME}" ]; then
			# wifi Mac interface
			if [ -z "${if_busid}" ]; then
				if_list="${if_list} $i"
			else
				BUSID="$(ethtool -i "${i}" 2>/dev/null |grep 'bus-info:'|cut -d':' -f2-3|tr -d ' \r\t')"
				if [ "${BUSID}" = "${if_busid}" ]; then
					if_list="$i"
					break
				fi
			fi
		fi
	done

	if [ -z "${if_list}" ]
	then
		exit 1
	fi
	# shellcheck disable=SC2086
	echo ${if_list}
}

fb_unbind_other_drivers()
{
	# Make sure devices are setup to be used with the kernel
	# driver. Unbind existing driver if it happens to be anything
	# but the only one we expect. Do this without depending on
	# any external script, namely, from DPDK
	lspci -Dvmmnnk -d 17cb:1201 |
        awk '/^Slot:/ { slot=$2 } /^Driver:/ { print slot, $2 }' |
	while read -r pciid driver
	do
		if [ -n "${driver}" ] && [ "${driver}" != "${WL_MOD_NAME}" ]
		then
			echo "Unbinding ${pciid} from ${driver}"
			echo "${pciid}" > "/sys/bus/pci/drivers/${driver}/unbind"
		fi
	done
}

fb_load_bh_driver()
{
	# Unused FW_USE_IF2IF="$1"
	FW_BUSID="$2"
	HAS_DVPP="$3"

	if [ -z "${FW_BUSID}" ]; then
		# Make proper driver handle the device
		fb_unbind_other_drivers
		# Load wireless driver and download firmware
		# shellcheck disable=SC2086
		insmod "${DRV_PATH}/${WL_MOD_FILE}" ${WL_MOD_ARGS} ${HAS_DVPP}
		# Wait for devices to start up
		sleep 2.0
	fi

	if_list=$(fb_get_wil_if_list "${FW_BUSID}")
	if [ -z "${if_list}" ]; then
		echo "Error: No fw interfaces found. BusId is ${FW_BUSID:-any}"
		exit 1
	fi

	for i in ${if_list}
	do
		if ! wil_setup_iface "$i"
		then
			echo "Error! Unable to initialize the firmware on ${i}."
			exit 1
		fi
	done
	return 0
}

# Translate generic verbosity levels to driver-specific equivalents
# and set driver verbosity
fb_set_bh_verbose() {
	# Unused HMAC_VERBOSE="${1}"
	KMOD_VERBOSE="${2}"
	FW_BUSID="${3}"

	# Check if debugfs is mounted somewhere
	debugfs=$(awk '/^debugfs\s+/ { print $2; exit }' /proc/mounts)
	if [ -z "${debugfs}" ]
	then
		return 1;
	fi

	# Check if dynamic debug printfs can be controlled
	debugfs="${debugfs}/dynamic_debug/control"
	if [ ! -f "${debugfs}" ]
	then
		return 1;
	fi

	if [ "${KMOD_VERBOSE}" -lt 1 ]
	then
		echo "module ${WL_MOD_NAME} -p" > "${debugfs}"
	else
		echo "module ${WL_MOD_NAME} +p" > "${debugfs}"
	fi

	if [ "${KMOD_VERBOSE}" -lt 3 ]
	then
		echo "module ${WL_MOD_NAME} format 'DBG[ IRQ]' -p" > "${debugfs}"
		echo "module ${WL_MOD_NAME} format 'DBG[TXRX]' -p" > "${debugfs}"
		echo "file ioctl.c -p" > "${debugfs}"
	fi

	if [ "${KMOD_VERBOSE}" -lt 2 ]
	then
		echo "module ${WL_MOD_NAME} format 'DBG[ WMI]' -p" > "${debugfs}"
	fi
}

fb_wait_for_dev_ready()
{
	DEV_TIMEOUT_S="$1"

	BB_COUNT=$(lspci -d 17cb:1201 | wc -l)
	TIME_ELAPSED=0
	DEV_SLEEP_TIME_S=2
	while true; do
		DEVS_UP=$(find /sys/class/net/ -name "wlan*" | wc -l)
		if [ "$DEVS_UP" -lt "$BB_COUNT" ]; then
			if [ "$DEVS_UP" -gt 0 ] && [ "$TIME_ELAPSED" -ge "$DEV_TIMEOUT_S" ]; then
				echo "Proceeding without all wlan devices (counted $DEVS_UP of $BB_COUNT after $TIME_ELAPSED seconds)"
				break
			fi
			echo "Waiting for wlan devices (counted $DEVS_UP of $BB_COUNT)"
			sleep "$DEV_SLEEP_TIME_S"
			if [ "$DEVS_UP" -gt 0 ]; then
				TIME_ELAPSED=$((TIME_ELAPSED + DEV_SLEEP_TIME_S))
			fi
		else
			break
		fi
	done
}
