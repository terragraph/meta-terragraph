#!/bin/bash

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# For boards with multiple base band devices we need a way to uniquely
# identify them, so that the same device gets the same mac
# irrespecitve of what other devices are present.   For this
# reason we use pci bus id as the identification mechanism.  For
# single function devices in a PCIE enviroment the domain and bus
# should be enough to uniquely identify the device.  We store
# mapping of bus number to mac address in the digital board eeprom
# in device tree format.  For e.g. for a board with 4 macs it
# is stored as below.  When this utility is given the bus number
# it returns the mac-address for that device.  For e.g.
#
# node-0e-a3-e5-80-25-f9:~# get_wlanmac 0002:04
# 0e:a3:e5:80:25:fc
#
# Note that bus number is what we use in this case  if more
# detailed info is needed to identify it for e.g. pci device id etc.
# we can use that below.  The only requirement is identifier is unique
# and consistent
#
#/ {
#    board {
#        tg-if2if = <1>;
#        hw-ids {
#            hw-vendor = "FB";
#            hw-board = "NXP_LS1048A_JAGUAR";
#            hw-rev = "1.0";
#            hw-batch = "";
#            hw-sn = "";
#        };
#        ethernet {
#            eth0 {
#                mac-address = "96:ea:51:5c:c0:7e";
#            };
#            eth1 {
#                mac-address = "64:bf:01:aa:fa:3e";
#            };
#        }
#        wlan_macs {
#            count = <0x00000004>;
#            tg3 {
#                bus = "0002:04";
#                mac-address = "0e:a3:e5:80:25:fc";
#                rf-conn-rst-pin = "1:28";
#            };
#            tg2 {
#                bus = "0002:03";
#                mac-address = "0e:a3:e5:80:25:fb";
#                rf-conn-rst-pin = "1:27";
#            };
#            tg1 {
#                bus = "0001:01";
#                mac-address = "0e:a3:e5:80:25:fa";
#                rf-conn-rst-pin = "1:26";
#            };
#            tg0 {
#                bus = "0000:01";
#                mac-address = "0e:a3:e5:80:25:f9";
#                rf-conn-rst-pin = "1:14";
#            };
#        };
#
# Sample commands to generate the above for NXP DN
# fdtget /sys/firmware/fdt /chosen eeprom
# fdtput -p  /sys/bus/nvmem/devices/1-00540/nvmem  -t i /board/wlan_macs count 4
# fdtput -p  /sys/bus/nvmem/devices/1-00540/nvmem  -t s /board/wlan_macs/tg0 mac-address "0e:a3:e5:80:25:f9"
# fdtput -p  /sys/bus/nvmem/devices/1-00540/nvmem  -t s /board/wlan_macs/tg0 bus "0000:01"
# fdtput -p  /sys/bus/nvmem/devices/1-00540/nvmem  -t s /board/wlan_macs/tg0 rf-conn-rst-pin "1:28"
#
##

declare -a mac_array
declare -a bus_array
declare -a gpio_array
declare -a nvram_array
numb_wlanmac=1

# map the gpio given as gpiochip:number to linux exported gpio mapping
map_gpio_num() {
	gpio_chip=$(echo "$1" | cut -d : -f 1)
	gpio_num=$(echo "$1" | cut -d : -f 2)

	gpiodir=""
	for dir in /sys/class/gpio/gpiochip*; do
		if [ -e "$dir"/device/gpiochip"$gpio_chip" ] ; then
			gpiodir="$dir"
			break
		fi
	done
	if [ -z "$gpiodir" ] ; then
		echo -1
	else
		base=$(cat "$gpiodir"/base)
		((gpio=base+gpio_num))
		echo "$gpio"
	fi
}

TG_BOARD_MODEL=$( tr -d '\0' < /proc/device-tree/model )
if echo "${TG_BOARD_MODEL}" | grep -q "NXP TG Board" ; then
	# NXP board
	# Use nic0 mac as node id (nic0 might be under oob namespace)
	node_id=$(ip addr show nic0 | awk '/ether/{print $2}' 2>/dev/null)
	if [ -z "$node_id" ]; then
		node_id=$(ip netns exec oob ip addr show nic0 | awk '/ether/{print $2}' 2>/dev/null)
	fi

	# Find location of digital board eeprom
	db_eeprom=$(fdtget /sys/firmware/fdt /chosen eeprom)
	if [ -z "$db_eeprom" ] || [ ! -f "${db_eeprom}" ]; then
		mac_array[0]=$(fw_printenv tg_wlanmac | cut -d'=' -f2 | tr '[:upper:]' '[:lower:]')
		nvram_array[0]="bottom_lvds"  # default
	else
		# Get local copy to speed up operations
		dtbfile=$(mktemp)
		if dtc -I dtb -O dtb -o "${dtbfile}" -Wno-unit_address_vs_reg "${db_eeprom}" ; then
			db_eeprom="${dtbfile}"
		fi
		tg_if2if=$(fdtget "$db_eeprom" "/board" "tg-if2if")
		# Read mac addresses from eeprom
		wlan_mac_dt_node="/board/wlan_macs"
		numb_wlanmac=$(fdtget "$db_eeprom" "$wlan_mac_dt_node"/ count)
		if ! [[ "$numb_wlanmac" =~ ^[0-9]+$ ]]; then
			numb_wlanmac=0
		fi

		# Read HW identifiers from eeprom
		hw_ids_dt_node="/board/hw-ids"
		hw_vendor=$(fdtget "$db_eeprom" "$hw_ids_dt_node"/ hw-vendor)
		hw_board_id=$(fdtget "$db_eeprom" "$hw_ids_dt_node"/ hw-board)
		hw_rev=$(fdtget "$db_eeprom" "$hw_ids_dt_node"/ hw-rev)
		hw_batch=$(fdtget "$db_eeprom" "$hw_ids_dt_node"/ hw-batch)
		hw_sn=$(fdtget "$db_eeprom" "$hw_ids_dt_node"/ hw-sn)

		# Puma Proto 2 does not use '/antenna node' or 'antenna-array' property. Also,
		# RFBB eeprom store a header which precedes dtb data, causing fdtget to
		# scan through the entire eeprom causing 7s-14s delay depending on size
		# of eeprom (64KB or 128KB). Restricting fdtget to JAGUAR and BOBCAT boards.
		read_ant_info=0
		if echo "${hw_board_id}" | grep -q -e "NXP_LS1048A_JAGUAR" -e "NXP_LS1012A_BOBCAT" ; then
			read_ant_info=1
		fi

		i=0
		while [ $i -lt "$numb_wlanmac" ]; do
			instance="tg$i"
			mac_array[$i]=$(fdtget "$db_eeprom" "$wlan_mac_dt_node"/"$instance" mac-address | tr '[:upper:]' '[:lower:]')
			bus_array[$i]=$(fdtget "$db_eeprom" "$wlan_mac_dt_node"/"$instance" bus)
			gpio=$(fdtget "$db_eeprom" "$wlan_mac_dt_node"/"$instance" rf-conn-rst-pin)
			gpio_array[$i]=$(map_gpio_num "$gpio")
			# Antenna array info is on eeprom on the antenna board
			((bbi=i+1))
			antenna_eeprom=$(fdtget /sys/firmware/fdt /chosen bb"${bbi}"_eeprom)
			nvram_array[$i]="bottom_lvds"  # default
			if [ -n "${antenna_eeprom}" ] && [ -f "${antenna_eeprom}" ] && [ "${read_ant_info}" -eq 1 ] ; then
				nvram=$(fdtget "$antenna_eeprom" /antenna antenna-array)
				if [ -n "$nvram" ] ; then
					nvram_array[$i]=$nvram
				fi
			fi
			((i=i+1))
		done

		rm -rf "${dtbfile}"
	fi
	if [ -z "$hw_board_id" ]; then
		if echo "${TG_BOARD_MODEL}" | grep -q "LS1048A" ; then
			hw_board_id="NXP_LS1048A_JAGUAR"
		elif echo "${TG_BOARD_MODEL}" | grep -q "LS1012A" ; then
			hw_board_id="NXP_LS1012A_BOBCAT"
		else
			hw_board_id="NXP_UNKNOWN"
		fi
	fi
elif [ "${TG_BOARD_MODEL}" = "LS1088A RDB Board" ]; then
	# TODO hack for QRP
	hw_board_id="NXP_LS1088A_RDB"
fi
if [ "${hw_board_id}" = "NXP_LS1048A_PUMA" ]; then
	pci_order="0000:01:00.0,0001:01:00.0,0002:03:00.0,0002:04:00.0"
else
	pci_order=""
fi

if ls /dev/disk/by-id/*QEMU* &> /dev/null; then
	# generates required values for node_info if running in a QEMU VM

	# get the MAC address of the nic0 interface to use an node_info.
	# in QEMU this script gets called before nic0 is moved to oob namespace
	node_id=$(ip addr show nic0 | awk '/ether/{print $2}' 2>/dev/null)

	# convert node_id MAC into a decimal number so it can be incremented
	mac=$(echo "${node_id}" |  tr "[:lower:]" "[:upper:]" | tr -d ":")
	mac=$(printf "%d\\n" "0x${mac}")

	# offsets the MACs of the basebands so they start with 52:55 and aren't
	# the same as the nic0 netdev's MAC
	mac=$((mac + 0x000100000000))

	# use hwsim's default number of simulated basebands
	numb_wlanmac=4

	# set the MAC addresses for each baseband to be increasing starting from
	# the node_id similar to MAC addresses on some real nodes
	i=0
	while [ $i -lt "$numb_wlanmac" ]; do
		curr_mac=$((mac + i))
		# converts the MAC address back into standard form from decimal
		curr_mac=$(printf "%012x" $curr_mac | sed 's/../&:/g;s/:$//')
		mac_array[$i]=$curr_mac
		((i=i+1))
	done

	# use bus addresses of Puma board
	bus_array=("0000:01" "0001:01" "0002:03" "0002:04")
fi

# Write to file
TG_NODE_INFO_FILE=/var/run/node_info
echo "##### THIS FILE IS AUTO GENERATED. DO NOT EDIT  #####" > "$TG_NODE_INFO_FILE"
echo "NODE_ID=\"${node_id}\"" >> "$TG_NODE_INFO_FILE"
echo "NUM_WLAN_MACS=\"${numb_wlanmac}\"" >> "$TG_NODE_INFO_FILE"
if [ -n "$tg_if2if" ]; then
	echo "TG_IF2IF=\"${tg_if2if}\"" >> "$TG_NODE_INFO_FILE"
fi

i=0
while [ $i -lt "$numb_wlanmac" ]; do
	if [ -n "${mac_array[$i]}" ]; then
		echo "MAC_$i=\"${mac_array[$i]}\"" >> "$TG_NODE_INFO_FILE"
	fi
	if [ -n "${bus_array[$i]}" ]; then
		echo "BUS_$i=\"${bus_array[$i]}\"" >> "$TG_NODE_INFO_FILE"
	fi
	if [ -n "${gpio_array[$i]}" ]; then
		echo "GPIO_$i=\"${gpio_array[$i]}\"" >> "$TG_NODE_INFO_FILE"
	fi
	if [ -n "${nvram_array[$i]}" ]; then
		echo "NVRAM_$i=\"${nvram_array[$i]}\"" >> "$TG_NODE_INFO_FILE"
	fi
	((i=i+1))
done
echo "PCI_ORDER=\"${pci_order}\"" >> "$TG_NODE_INFO_FILE"
echo "HW_MODEL=\"${TG_BOARD_MODEL}\"" >> "$TG_NODE_INFO_FILE"
echo "HW_VENDOR=\"${hw_vendor}\"" >> "$TG_NODE_INFO_FILE"
echo "HW_BOARD_ID=\"${hw_board_id}\"" >> "$TG_NODE_INFO_FILE"
echo "HW_REV=\"${hw_rev}\"" >> "$TG_NODE_INFO_FILE"
echo "HW_BATCH=\"${hw_batch}\"" >> "$TG_NODE_INFO_FILE"
echo "HW_SN=\"${hw_sn}\"" >> "$TG_NODE_INFO_FILE"

exit 0
