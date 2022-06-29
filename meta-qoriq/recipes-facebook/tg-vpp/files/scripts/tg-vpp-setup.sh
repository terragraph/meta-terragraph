#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

############################################################################
# Globals
############################################################################
DEBUG=0
INFO=1

VPP_DPRC="dprc.2"

############################################################################
# Logging Routines
############################################################################
log_debug()
{
	if [ $DEBUG -eq 1 ]
	then
		echo "$@"
	fi
}

log_info()
{
	if [ $INFO -eq 1 ]
	then
		echo "$@"
	fi
}

log_error()
{
	echo "$@"
	exit 1
}

############################################################################
# Helper routines
############################################################################
check_error_and_exit()
{
	if [ "$1" -ne 0 ]
	then
		log_error "Failure to execute last command. Unable to continue!"
	fi
}

perform_hugepage_mount()
{
	HUGE=$(grep -E '/mnt/\<hugepages\>.*hugetlbfs' /proc/mounts)
	if [ -z "$HUGE" ]
	then
		mkdir -p /mnt/hugepages
		mount -t hugetlbfs none /mnt/hugepages
	else
		echo
		echo
		echo "Already mounted : $HUGE"
		echo
	fi
}

perform_vfio_mapping()
{
	# Assuming argument has DPRC to bind
	log_info "Performing vfio mapping for $1"
	if [ "$1" = "" ]
	then
		log_debug "Incorrect usage: pass DPRC to bind"
	else
		if [ -e /sys/module/vfio_iommu_type1 ]
		then
			echo 1 > /sys/module/vfio_iommu_type1/parameters/allow_unsafe_interrupts
		else
			log_error "NO VFIO Support available"
		fi
		echo vfio-fsl-mc > "/sys/bus/fsl-mc/devices/$1/driver_override"
		# shellcheck disable=SC2181
		if [ $? -ne 0 ]
		then
			log_debug "No such DPRC (/sys/bus/fsl-mc/devices/ \
					$1/driver_override) exists."
			return
		fi
		echo "$1" > /sys/bus/fsl-mc/drivers/vfio-fsl-mc/bind
	fi
}

#
# Core command to interface with restool
# has following format:
# restool_cmd <cmd line, without 'restool'> <return variable | None> <target dprc | None>
#  - cmd line should be without restool command itself. This is to make it flexible for
#    testing purpose (replace restool without 'echo', for e.g.)
#  - return variable is a pass by reference of a global which contains return value of
#    restool execution, for example, dprc.1. This can be useful if the caller needs the
#    object created for some future command
#  - target dprc, when passed, would assign the object created to that dprc
restool_cmd()
{
	if [ $# -ne 3 ]
	then
		# Wrong usage
		log_info "Wrong usage: <$*> : Missing args"
		log_error "Should be: restool_cmd <cmd line> <return | None> <target dprc | None>"
	fi

	_var=''
	_object=''
	_cmdline=$1
	_container=dprc.1

	if [ "$3" != "None" ]
	then
		eval _container="\$${3}"
		_cmdline="$_cmdline --container=$_container"
	fi

	log_debug "Executing: $_cmdline"
	#shellcheck disable=SC2086
	_var=$(restool -s $_cmdline)
	check_error_and_exit $?

	# Assigning to passed variable
	_object=$(echo "${_var}" | head -1 | cut -d ' ' -f 1)
	if [ "$2" != "None" ]
	then
		eval "$2=${_object}"
		log_debug "Created Object: $_object"
	fi

	if [ "$3" != "None" ]
	then
		restool -s dprc assign "$_container" --object="$_object" --plugged=1
		log_debug "Plugged $_object in $_container"
		check_error_and_exit $?
	fi
}

create_vpp_container()
{
	# Check if target container is present already, refuse to
	# run if that is the case
	if [ -f /sys/bus/fsl-mc/devices/${VPP_DPRC} ]
	then
		log_error "The ${VPP_DPRC} container already exists"
	fi

	log_debug "Creating APP container"
	vpp_app_dprc=
	restool_cmd "dprc create dprc.1 --label=\"VPP\" --options=DPRC_CFG_OPT_SPAWN_ALLOWED,DPRC_CFG_OPT_ALLOC_ALLOWED,DPRC_CFG_OPT_IRQ_CFG_ALLOWED,DPRC_CFG_OPT_OBJ_CREATE_ALLOWED" vpp_app_dprc None
	log_info "Created VPP Container: $vpp_app_dprc"

	log_debug "Creating DPMCP"
	restool_cmd "dpmcp create" None vpp_app_dprc

	# One per-CPU general purpose DPIO and one dedicated
	# per-CPU DPIO for Ethernet TX/RX. Allocate for all
	# possible cores installed in system to be on the safe
	# side.
	NUM_DPIO=$(($(nproc --all) * 2))
	# One DPCON per Ethernet port (eventdev config only)
	NUM_DPCON="${NUM_DPMAC}"
	# One global buffer poool
	NUM_DPBP=1

	log_debug "Creating DPBP Objects"
	for i in $(seq 1 $NUM_DPBP); do
		restool_cmd "dpbp create" None vpp_app_dprc
	done
	log_debug "Created ${i} DPBP Objects"

	DPIO_PRIORITIES=2
	DPCON_PRIORITIES=2

	log_debug "Creating DPCON Objects"
	# shellcheck disable=SC2086
	for i in $(seq 1 $NUM_DPCON); do
		restool_cmd "dpcon create --num-priorities=$DPCON_PRIORITIES" None vpp_app_dprc
	done

	log_debug "Creating DPIO Objects"
	# shellcheck disable=SC2086
	for i in $(seq 1 $NUM_DPIO); do
		restool_cmd "dpio create --channel-mode=DPIO_LOCAL_CHANNEL --num-priorities=$DPIO_PRIORITIES" None vpp_app_dprc
	done
}

disable_dprc_rescan()
{
	if [ -f /sys/bus/fsl-mc/disable_rescan ]; then
		echo 0 > /sys/bus/fsl-mc/drivers_autoprobe
		echo 1 > /sys/bus/fsl-mc/disable_rescan
		export RESTOOL_NO_RESCAN=1
	fi
}

force_dprc_rescan()
{
	if [ -f /sys/bus/fsl-mc/disable_rescan ]; then
		log_info "Rescanning FSL MC bus"
		echo 0 > /sys/bus/fsl-mc/disable_rescan
		echo 1 > /sys/bus/fsl-mc/drivers_autoprobe
		echo 1 > /sys/bus/fsl-mc/rescan
		unset RESTOOL_NO_RESCAN
	fi
}

############################################################################
# Main logic
############################################################################

probe_config()
{
	# shellcheck source=/dev/null
	. /usr/sbin/config_get_env.sh

	# Obtain DPMAC interconnection info and cache it to local variable
	_ls_listni=$(ls-listni)

	# Build list of DPMACs to be handled by VPP
	# shellcheck disable=SC2016
	_parse_command='
	NF==6 && /^dprc.1\/dpni/ {
	  sub(/,/, "", $3)
	  sub(/)/, "", $6)
	  OFS=":"; print substr($1, 8), $3, $6
	}
	'
	_dpmac_list=$(echo "${_ls_listni}" | awk "${_parse_command}")

	# Exclude OOB_INTERFACE, if OOB is enabled
	if [ "${OOB_NETNS}" -ne 0 ]
	then
		_dpmac_list=$(echo "${_dpmac_list}" | grep -v "${OOB_INTERFACE}")
	fi

	# Count DPMACs
	NUM_DPMAC=0
	for rec in ${_dpmac_list}
	do
		NUM_DPMAC=$((NUM_DPMAC+1))
	done
}

dpni_driver_unbind()
{
	_dprc=$1
	_dpni=$2
	_dpmac=$3

	log_debug "Unbinding existing DPAA2 Ethernet driver from $_dpni"
	if [ -e "/sys/bus/fsl-mc/devices/$_dprc/$_dpni/driver" ]
	then
		echo "$_dpni" > "/sys/bus/fsl-mc/devices/$_dprc/$_dpni/driver/unbind"
		#shellcheck disable=SC2181
		if [ "$?" != "0" ]
	 	then
			log_error "Failed to unbind kernel driver from $_dpni"
		fi
	fi

	log_debug "Unplugging DPNI $_dpni"
	if ! restool -s dprc assign "$_dprc" --object="$_dpni" --plugged=0
	then
		log_error "Failed to unplug DPNI $_dpni"
	fi

	log_debug "Disconnecting DPNI $_dpni from DPMAC $_dpmac"
	if ! restool -s dprc disconnect "$_dprc" --endpoint "$_dpmac"
	then
		log_error "Failed to disconnect DPNI $_dpni from DPMAC $_dpmac"
	fi
}

disconnect_existing_dpmac_connections()
{
	_dprc=${vpp_app_dprc}
	for rec in ${_dpmac_list}
	do
		IFS=: read -r _dpni _nic _dpmac <<EOF
$rec
EOF
		dpni_driver_unbind dprc.1 "$_dpni" "$_dpmac"
	done
}

create_new_dpnis()
{
	_dprc=${vpp_app_dprc}

	# DPNI configuration
	NUM_QUEUES=8
	DPNI_OPTIONS="DPNI_OPT_NO_MAC_FILTER"
	NUM_TCS=8
	MAC_FILTER_ENTRIES=16
	VLAN_FILTER_ENTRIES=16
	FS_ENTRIES=1
	QOS_ENTRIES=1

	for rec in ${_dpmac_list}
	do
		# shellcheck disable=SC2034
		IFS=: read -r _dpni _nic _dpmac <<EOF
$rec
EOF
		# Extract MAC address from existing DPNI
		MAC_ADDRESS=$(restool dpni info "$_dpni" | awk '/mac address:/ { print $3 }')

		log_debug "Creating DPNI with MAC $MAC_ADDRESS"
		dpni=
		restool_cmd "dpni create --num-queues=$NUM_QUEUES \
			--options=$DPNI_OPTIONS \
			--num-tcs=$NUM_TCS \
			--fs-entries=$FS_ENTRIES \
			--qos-entries=$QOS_ENTRIES \
			--mac-filter-entries=$MAC_FILTER_ENTRIES \
			--vlan-filter-entries=$VLAN_FILTER_ENTRIES \
			" dpni _dprc
		restool_cmd "dpni update $dpni --mac-addr=$MAC_ADDRESS" None None
		log_info "Created $dpni MAC $MAC_ADDRESS"
	done
}

make_dpni_dpmac_connections()
{
	_dprc=${vpp_app_dprc}

	# Get list of all DPNIs created
	# shellcheck disable=SC2046
	set -- $(restool dprc show "$_dprc" | awk '/^dpni/ { print $1 }')

	for rec in ${_dpmac_list}
	do
		# shellcheck disable=SC2034
		IFS=: read -r _dpni _nic _dpmac <<EOF
$rec
EOF
		_dpni="$1"
		shift

		if [ -z "$_dpni" ]
		then
			log_error "No unused DNPI for $_dpmac in $_dprc"
		fi

		# Hardcoded dprc.1 as it owns DPMAC and can setup and break connections
		restool_cmd "dprc connect dprc.1 --endpoint1=$_dpmac --endpoint2=$_dpni" \
			    None None
	done
}

main()
{
	log_info "Probing DPAA2 connections"
	probe_config

	log_info "Disable MC bus rescans"
	disable_dprc_rescan

	log_info "Create VPP Application Container"
	create_vpp_container

	log_info "Create DPNI objects"
	create_new_dpnis

	log_info "Disconnecting existing DPNIs from DPMACs in Kernel Container"
	disconnect_existing_dpmac_connections

	log_info "Connecting new DPNIs to DPMACs"
	make_dpni_dpmac_connections

	log_info "Force MC bus rescan"
	force_dprc_rescan

	log_info "Mounting hugetlbfs system"
	perform_hugepage_mount

	perform_vfio_mapping $vpp_app_dprc
}

# Redirect output
main 2>&1 | tee -a /var/log/tg-vpp-setup.log
