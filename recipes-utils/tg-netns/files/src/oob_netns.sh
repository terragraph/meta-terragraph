#!/bin/bash

nic0_default_pid="/var/run/dhclient.nic0.pid"

# sanity check on input
if [ $# -eq 0 ]; then
  echo "Please specify oob interface name"
  exit 1
fi

interfaces=($(ls /sys/class/net))

if [[ ! ${interfaces[@]} =~ $1 ]]; then
  echo "Invalid interface name, exiting..."
  exit 1
fi

# create oob namespace
ip netns add oob

oob_if=$1
hw_board_id=$(/usr/sbin/get_hw_info HW_BOARD_ID)
if [ "${hw_board_id}" = "NXP_LS1012A_BOBCAT" ]; then
  echo "Setup OOB on interface $oob_if, with VLAN 100"

  oob_if="${oob_if}.100"
  ip link add link "$1" name "$oob_if" type vlan id 100
fi

# Kill default netns dhclient
if [ -s "${nic0_default_pid}" ]; then
  kill "$(cat ${nic0_default_pid})"
fi

echo "Move interface $oob_if to OOB"
ip link set dev "$oob_if" netns oob

# bring interface up
ip netns exec oob ip link set lo up
ip netns exec oob ip link set "$oob_if" up

# run dhclient for legacy ip
dh_pid="/var/run/dhclient_oob.${oob_if}.pid"
ip netns exec oob /sbin/dhclient -nw -pf "$dh_pid" "$oob_if"

# mount default namespace profile so we can switch back to default
ln -s /proc/1/ns/net /var/run/netns/default
