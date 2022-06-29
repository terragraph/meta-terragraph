#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# vppctl fails if we double quote the ${VPPSOCK} variable. So ignore this error in this file.
# shellcheck disable=SC2086
#######################################################################################
# This script sets up VPP with 2 DPDK ethernet interfaces for IPV6.
# It enables basic connection between 2 LS1088ARDBs via DPDK and VPP.
# The /data/startup/startup.sh is run during startup, and it adds dpmac.7 and dpmac.8
# as nic1 and nic2 interfaces. These are then exported as TenGigabitEthernet0 and
# TenGigabitEthernet1 to VPP.
# TenGigabitEthernet0 is on network1 and is connected to dpmac.7  on the peer rdb
# TenGigabitEthernet1 is on network2 and is connected to dpmac.8 on the peer rdb
#######################################################################################
NW0="fc00:caff:babb:babb:babb::"
NW1="fc00:caff:babb:babe:babe::"
SUBNET="/90"
IP0="${NW0}54"
PEER_IP0="${NW0}59"
IP1="${NW1}58"
PEER_IP1="${NW1}60"
MAC0="00:00:00:00:00:07"
MAC1="00:00:00:00:00:08"

vpp_init()
{
	VPP_CONF="$1"

	# Kill previous vpp instances
	killall -9 vpp 2> /dev/null
	# DPDK init.
	export DPRC=dprc.2
	# Bring up VPP.
	/usr/bin/vpp -c "${VPP_CONF}"
	sleep 3
}

# Sample function to configure the classifier and policer for VPP for IPV6
vpp_policer_ip6_configure()
{
	INTF="$1"
	# Configure policer for TC0 (AF4x) for 300Mbps cir and 500Mbps eir to drop packets exceeding eir.
	vppctl configure policer name policer-tc0 cir 300000 eir 500000 cb 39321600 eb 65536000 rate kbps round up type 2r3c-4115 conform-action mark-and-transmit AF41 exceed-action mark-and-transmit AF42 violate-action drop
	# Add classification table ipv6 to match of traffic class value in IPV6 header
	vppctl classify table mask l3 ip6 traffic-class skip 0 match 1

	# NOTE: Only adding 3 sessions here for sample code. For the final TG product, we will need to add 1 session for each TrafficClass value,
	# as the classifier does a exact match with the traffic class mask of 8 bits.
	# We cannot do a sub-mask on just the dscp value 6 bits, or a partial dscp value.
	# classifier session for AF41 (AF4 with green) 0x22 dscp value (6 bits) = 0x88 TC value (8 bits) = 136d
	vppctl classify session policer-hit-next policer-tc0 table-index 0 match l3 ip6 traffic_class 136
	# classifier for AF4x (AF4 with no color set) 0x20 dscp value (6 bits) = 0x80 TC value (8 bits) = 128d
	vppctl classify session policer-hit-next policer-tc0 table-index 0 match l3 ip6 traffic_class 128
	# classifier for AF42 (AF4 with yellow) 0x24 dscp value (6 bits) = 0x90 TC value (8 bits) = 144
	vppctl classify session policer-hit-next policer-tc0 table-index 0 match l3 ip6 traffic_class 144

	# Set the interface on which we want the classification and policing.
	vppctl set policer classify interface "${INTF}" ip6-table 0
	#vppctl show classify table verbose
	#vppctl show policer
}

vpp_rdb_setup()
{
	# run vppctl to setup the IP interfaces:
	vppctl show hardware-interfaces
	vppctl set interface ip address TenGigabitEthernet0 "${IP0}${SUBNET}"
	vppctl set interface ip address TenGigabitEthernet1 "${IP1}${SUBNET}"
	# Enable sample policer for TenGigabitEthernet0 interface.
	#vpp_policer_ip6_configure TenGigabitEthernet0
	vppctl set interface state TenGigabitEthernet0 up
	vppctl set interface state TenGigabitEthernet1 up

	# Add the routes in VPP
	vppctl ip route add "${PEER_IP1}${SUBNET}" via "${IP1}" TenGigabitEthernet1
	vppctl ip route add "${PEER_IP0}${SUBNET}" via "${IP0}" TenGigabitEthernet0

	#### add static neighbors for NDP in VPP ###########
	vppctl set ip6 neighbor TenGigabitEthernet1 "${IP1}" "${MAC1}" static
	vppctl set ip6 neighbor TenGigabitEthernet0 "${IP0}" "${MAC0}" static
}

vpp_init "/etc/vpp/startup.conf"
vpp_rdb_setup

