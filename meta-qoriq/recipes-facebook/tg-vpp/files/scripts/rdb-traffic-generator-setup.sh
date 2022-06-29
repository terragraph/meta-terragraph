#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

######################################################################################
# This script sets up the RDB as a traffic generator.
# The 2 interfaces nic1 and nic2 (by default) have to be connected to the DUT's ethernet
# interfaces.
# This script sets up the network namespace rdb2 for nic2, and brings up nic1/nic2 to talk to
# peer node.
# We have 2 networks:
# network1 (nic2 in rdb2 namespace is part of this subnet): fc00:caff:babb:babe:babe::/90
# network2 (nic1 is part of this network): fc00:caff:babb:babb:babb::/90
# Modify scripts accordingly if the network interfaces connected to the peer are not nic1 and nic2.
######################################################################################
NW0="fc00:caff:babb:babb:babb::"
NW1="fc00:caff:babb:babe:babe::"
SUBNET="/90"
IP0="${NW0}54"
PEER_IP0="${NW0}59"
IP1="${NW1}58"
PEER_IP1="${NW1}60"
NAMESPACE1="rdb2"
# nic used by namespace
NS_NIC="nic2"
NIC1="nic1"

nic_init()
{
  NIC1="$1"
  NIC2="$2"
  ls-addni  dpmac.7
  ifconfig "${NIC1}" up
  ls-addni  dpmac.8
  ifconfig "${NIC2}" up
}

nic_init "${NIC1}" "${NS_NIC}"

# Add namespace called NAMCESPACE1 and bring it up as part of network1.
# Enable ipv6 forwarding on nic2
echo 1 > /proc/sys/net/ipv6/conf/"${NS_NIC}"/forwarding
ip netns add "${NAMESPACE1}"
ip link set dev "${NS_NIC}" netns "${NAMESPACE1}"
ip netns exec "${NAMESPACE1}" ifconfig "${NS_NIC}" up
ip netns exec "${NAMESPACE1}" ip -6 addr add "${PEER_IP1}${SUBNET}" dev "${NS_NIC}"
# Add route from nic2 to the peer network2.
ip netns exec "${NAMESPACE1}" ip -6 route add "${NW0}${SUBNET}" via "${IP1}" dev "${NS_NIC}"

# Add and bring up "${NIC1}" on NW0
ip -6 addr add "${PEER_IP0}${SUBNET}" dev "${NIC1}"
# Add route via "${NIC1}" to NW1.
ip -6 route add "${NW1}${SUBNET}" via "${IP0}" dev "${NIC1}"
