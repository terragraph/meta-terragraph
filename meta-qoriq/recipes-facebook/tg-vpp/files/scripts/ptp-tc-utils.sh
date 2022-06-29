#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

#######################################################################################
# Utilities for ptp transparent clock on RDB
# source it to use:
# . ptp-tc-utils.sh
# 1) setup_netns Creates network namespaces and routing between them.
# 2) Other utilities include: silent_console, set_tc_delay, run_ping, show_ip_route
# 3) Placeholder for anything else needed later
#######################################################################################

silent_console()
{
  /etc/init.d/watchdog.sh disable_tgscripts
  sv stop e2e_minion
  rmmod ublox_gps
}

# +----------------+                                    +----------------+
# |                +----------+              +----------+                +----------+     +----------+
# |                | dpmac.3  |              | dpmac.4  |                |          |     |          |
# |    ptp_src     | nic1     +<--Ethernet-->+ nic2     | ptp_tc_ingress | veth0    +<--->+ veth1    |
# |                | 30::10   |              | 30::11   |                | 31::10   |     | 31::11   |
# |                +----------+              +----------+                +----------+     +----------+-----+
# +----------------+                                    +----------------+                |                |
#                                                                                         |                |
#                                                                                         |  ptp_tc_delay  |
#                                                                                         |                |
# +----------------+                                    +----------------+                |                |
# |                +----------+              +----------+                +----------+     +----------+-----+
# |                | dpmac.7  |              | dpmac.6  |                |          |     |          |
# |    ptp_dst     | nic4     +<--Ethernet-->+ nic3     | ptp_tc_egress  | veth3    +<--->+ veth2    |
# |                | 33::11   |              | 33::10   |                | 32::11   |     | 32::10   |
# |                +----------+              +----------+                +----------+     +----------+
# +----------------+                                    +----------------+
#
# ptp_src:
# Netns running ptp packet generator.
# This can be replaced by external device like Calnex
#
# ptp_tc_ingress:
# Netns running VPP node for ingress packet processing
# Copies 8 bytes of timestamp  from frame annotation to 4 bytes field in packet
#
# ptp_tc_delay:
# Netns running netem on veth1 and veth2 to add delay
# this will emulate time a packet would spend in terragraph network
#
# ptp_tc_egress:
# Netns running vpp node for egress packet processing
# Copies 4 bytes of timestamp from packet to 8 bytes in frame annotation
#
# ptp_tc_dst:
# Netns running ptp packet inspector/logger
# This can be replaced by the other port of external device like Calnex
setup_netns()
{
  PREFIX=fc00:cafe:babe
  LEN=64
  # create network namespaces
  net_ns_names=( ptp_src ptp_tc_ingress ptp_tc_delay ptp_tc_egress ptp_dst )
  for name in "${net_ns_names[@]}"; do ip netns add $name; done
  # bring up network interfaces, assumes nic1 --> dpmac.3 etc.
  ni_names=( dpmac.3 dpmac.4 dpmac.6 dpmac.7 )
  for name in "${ni_names[@]}"; do ls-addni $name; done
  # bring up virtual network interfaces
  ip link add name veth0 type veth peer name veth1
  ip link add name veth2 type veth peer name veth3
  # bring up network interfaces in corresponding network namespaces
  eth_to_ns=( "nic1,ptp_src" "nic2,ptp_tc_ingress" "veth0,ptp_tc_ingress" "veth1,ptp_tc_delay" "veth2,ptp_tc_delay" "veth3,ptp_tc_egress" "nic3,ptp_tc_egress" "nic4,ptp_dst" )
  for pair in "${eth_to_ns[@]}"; do eth_ns=(${pair//,/ }); ip link set dev ${eth_ns[0]} up netns ${eth_ns[1]}; done
  # set static ip addresses
  ip netns exec ptp_src ip -6 addr add $PREFIX:30::10/$LEN dev nic1
  ip netns exec ptp_tc_ingress ip -6 addr add $PREFIX:30::11/$LEN dev nic2
  ip netns exec ptp_tc_ingress ip -6 addr add $PREFIX:31::10/$LEN dev veth0
  ip netns exec ptp_tc_delay ip -6 addr add $PREFIX:31::11/$LEN dev veth1
  ip netns exec ptp_tc_delay ip -6 addr add $PREFIX:32::10/$LEN dev veth2
  ip netns exec ptp_tc_egress ip -6 addr add $PREFIX:32::11/$LEN dev veth3
  ip netns exec ptp_tc_egress ip -6 addr add $PREFIX:33::10/$LEN dev nic3
  ip netns exec ptp_dst ip -6 addr add $PREFIX:33::11/$LEN dev nic4
  # enable routing
  echo 1 > /tmp/one
  for name in "${net_ns_names[@]}"; do ip netns exec $name dd if=/tmp/one  of=/proc/sys/net/ipv6/conf/all/forwarding; done
  # set static routing table
  ip netns exec ptp_src ip -6 route add $PREFIX:31::/$LEN via $PREFIX:30::11 dev nic1
  ip netns exec ptp_src ip -6 route add $PREFIX:32::/$LEN via $PREFIX:30::11 dev nic1
  ip netns exec ptp_src ip -6 route add $PREFIX:33::/$LEN via $PREFIX:30::11 dev nic1
  ip netns exec ptp_tc_ingress ip -6 route add $PREFIX:32::/$LEN via $PREFIX:31::11 dev veth0
  ip netns exec ptp_tc_ingress ip -6 route add $PREFIX:33::/$LEN via $PREFIX:31::11 dev veth0
  ip netns exec ptp_tc_delay ip -6 route add $PREFIX:30::/$LEN via $PREFIX:31::10 dev veth1
  ip netns exec ptp_tc_delay ip -6 route add $PREFIX:33::/$LEN via $PREFIX:32::11 dev veth2
  ip netns exec ptp_tc_egress ip -6 route add $PREFIX:30::/$LEN via $PREFIX:32::10 dev veth3
  ip netns exec ptp_tc_egress ip -6 route add $PREFIX:31::/$LEN via $PREFIX:32::10 dev veth3
  ip netns exec ptp_dst ip -6 route add $PREFIX:30::/$LEN via $PREFIX:33::10 dev nic4
  ip netns exec ptp_dst ip -6 route add $PREFIX:31::/$LEN via $PREFIX:33::10 dev nic4
  ip netns exec ptp_dst ip -6 route add $PREFIX:32::/$LEN via $PREFIX:33::10 dev nic4
  # add delay 0.1ms
  ip netns exec ptp_tc_delay tc qdisc add dev veth1 root netem limit 100 delay 100
  ip netns exec ptp_tc_delay tc qdisc add dev veth2 root netem limit 100 delay 100
}

# copy /etc/vpp/startup.conf to $1.conf with different socket and log file
create_startup_conf()
{
  CONF_FILE=startup_$1.conf
  rm -f $CONF_FILE
  cp -v /etc/vpp/startup.conf $CONF_FILE
  sed -i.bak "s/vpp.log/vpp_$1.log/g" $CONF_FILE
  sed -i.bak "s/cli.sock/cli_$1.sock/g" $CONF_FILE
  rm -f $CONF_FILE.bak
}

# +----------------+                                    +----------------+
# |                +----------+              +----------+                +----------+     +----------+
# |                | dpmac.3  |              | dpmac.4  |                |          |     |          |
# |    ptp_src     | nic1     +<--Ethernet-->+ nic2 et0 | ptp_tc_ingress | h-veth0  +<--->+ veth1    |
# |                | 30::10   |              | 30::11   |     dprc.2     | 31::10   |     | 31::11   |
# |                +----------+              +----------+                +----------+     +----------+-----+
# +----------------+                                    +----------------+                |                |
#                                                                                         |                |
#                                                                                         |  ptp_tc_delay  |
#                                                                                         |                |
# +----------------+                                    +----------------+                |                |
# |                +----------+              +----------+                +----------+     +----------+-----+
# |                | dpmac.7  |              | dpmac.6  |                |          |     |          |
# |    ptp_dst     | nic4     +<--Ethernet-->+ nic3 et0 | ptp_tc_egress  | h-veth3  +<--->+ veth2    |
# |                | 33::11   |              | 33::10   |     dprc.3     | 32::11   |     | 32::10   |
# |                +----------+              +----------+                +----------+     +----------+
# +----------------+                                    +----------------+
#
# similar to setup_netns, except for:
# it does not create netns ptp_tc_ingress/egress namespaces
# it does not bring-up nic2, nic3,
# uses vpp routing for ingress dprc.2 and egress dprc.3
alias ivppctl="vppctl -s /run/vpp/cli_ingress.sock"
alias evppctl="vppctl -s /run/vpp/cli_egress.sock"
setup_dprc_netns()
{
  PREFIX=fc00:cafe:babe
  LEN=64

  ## Linux ##

  # create network namespaces
  net_ns_names=( ptp_src ptp_tc_delay ptp_dst )
  for name in "${net_ns_names[@]}"; do ip netns add $name; done
  # bring up network interfaces, assumes nic1 --> dpmac.3 etc.
  ni_names=( dpmac.3 dpmac.4 dpmac.6 dpmac.7 )
  for name in "${ni_names[@]}"; do ls-addni $name; done
  # bring up virtual network interfaces
  ip link add name veth0 type veth peer name veth1
  ip link add name veth2 type veth peer name veth3
  # bring up network interfaces in corresponding network namespaces
  eth_to_ns=( "nic1,ptp_src" "veth1,ptp_tc_delay" "veth2,ptp_tc_delay" "nic4,ptp_dst" )
  for pair in "${eth_to_ns[@]}"; do eth_ns=(${pair//,/ }); ip link set dev ${eth_ns[0]} up netns ${eth_ns[1]}; done
  # set static ip addresses
  ip netns exec ptp_src ip -6 addr add $PREFIX:30::10/$LEN dev nic1
  ip netns exec ptp_tc_delay ip -6 addr add $PREFIX:31::11/$LEN dev veth1
  ip netns exec ptp_tc_delay ip -6 addr add $PREFIX:32::10/$LEN dev veth2
  ip netns exec ptp_dst ip -6 addr add $PREFIX:33::11/$LEN dev nic4
  # enable routing
  echo 1 > /tmp/one
  for name in "${net_ns_names[@]}"; do ip netns exec $name dd if=/tmp/one  of=/proc/sys/net/ipv6/conf/all/forwarding; done
  # set static routing table
  ip netns exec ptp_src ip -6 route add $PREFIX:31::/$LEN via $PREFIX:30::11 dev nic1
  ip netns exec ptp_src ip -6 route add $PREFIX:32::/$LEN via $PREFIX:30::11 dev nic1
  ip netns exec ptp_src ip -6 route add $PREFIX:33::/$LEN via $PREFIX:30::11 dev nic1
  ip netns exec ptp_tc_delay ip -6 route add $PREFIX:30::/$LEN via $PREFIX:31::10 dev veth1
  ip netns exec ptp_tc_delay ip -6 route add $PREFIX:33::/$LEN via $PREFIX:32::11 dev veth2
  ip netns exec ptp_dst ip -6 route add $PREFIX:30::/$LEN via $PREFIX:33::10 dev nic4
  ip netns exec ptp_dst ip -6 route add $PREFIX:31::/$LEN via $PREFIX:33::10 dev nic4
  ip netns exec ptp_dst ip -6 route add $PREFIX:32::/$LEN via $PREFIX:33::10 dev nic4
  # add delay 0.1ms
  ip netns exec ptp_tc_delay tc qdisc add dev veth1 root netem limit 100 delay 100
  ip netns exec ptp_tc_delay tc qdisc add dev veth2 root netem limit 100 delay 100

  ## VPP ##

  # create dprc.2, dprc.3
  restool dprc disconnect dprc.1 --endpoint dpmac.4
  restool dprc disconnect dprc.1 --endpoint dpmac.6
  cd /tmp; DPSECI_COUNT=0 /usr/bin/dpdk-extras/dynamic_dpl.sh dpmac.4; cd -; # dprc.2
  cd /tmp; DPSECI_COUNT=0 /usr/bin/dpdk-extras/dynamic_dpl.sh dpmac.6; cd -; # dprc.3
  create_startup_conf ingress
  create_startup_conf egress
  export DPRC=dprc.2
  /usr/bin/vpp -c startup_ingress.conf
  sleep 3
  export DPRC=dprc.3
  /usr/bin/vpp -c startup_egress.conf
  sleep 3

  # Ingress vpp interface addresses and routing
  SOCK=/run/vpp/cli_ingress.sock
  vppctl -s $SOCK create host-interface name veth0
  vppctl -s $SOCK show hardware-interfaces
  vppctl -s $SOCK set interface ip address TenGigabitEthernet0 $PREFIX:30::11/$LEN
  vppctl -s $SOCK set interface ip address host-veth0 $PREFIX:31::10/$LEN
  vppctl -s $SOCK set interface state TenGigabitEthernet0 up
  vppctl -s $SOCK set interface state host-veth0 up
  vppctl -s $SOCK ip route add $PREFIX:33::/$LEN via $PREFIX:31::11 host-veth0
  # enable transparent clock
  vppctl -s $SOCK ptptc-input enable-disable TenGigabitEthernet0

  # Egress vpp interface address and routing
  SOCK=/run/vpp/cli_egress.sock
  vppctl -s $SOCK create host-interface name veth3
  vppctl -s $SOCK show hardware-interfaces
  vppctl -s $SOCK set interface ip address TenGigabitEthernet0 $PREFIX:33::10/$LEN
  vppctl -s $SOCK set interface ip address host-veth3 $PREFIX:32::11/$LEN
  vppctl -s $SOCK set interface state TenGigabitEthernet0 up
  vppctl -s $SOCK set interface state host-veth3 up
  vppctl -s $SOCK ip route add $PREFIX:30::/$LEN via $PREFIX:32::10 host-veth3
  # enable transparent clock
  vppctl -s $SOCK ptptc-output enable-disable TenGigabitEthernet0
  vppctl -s $SOCK ptptc-output port 6
  # apply fix for uncalibrated calnex, cables, RDB crystal offset, etc.
  # based on these measurements, with and without 400ms fwd delay
  # 14999914.722x + y = -7758.315
  # 406375537.542x + y = -8317.983
  # x = -0.00000143, y = -7736.865
  evppctl ptptc-output offset 7737
  evppctl ptptc-output clk_offset -1430

}

set_tc_delay()
{
  echo "forward delay ms, " $2 ", reverse delay ms, " $1
  ip netns exec ptp_tc_delay tc qdisc replace dev veth1 root netem limit 100 delay $(($1 * 1000))
  ip netns exec ptp_tc_delay tc qdisc replace dev veth2 root netem limit 100 delay $(($2 * 1000))
}

periodic_random_delay()
{
  while :
  do
    DELAY=$(od -An -N1 -i /dev/random)
    set_tc_delay 0 $DELAY
    sleep 10
  done
}

run_ping()
{
  ip netns exec ptp_src ping6 -c 5 fc00:cafe:babe:33::11
  ip netns exec ptp_dst ping6 -c 5 fc00:cafe:babe:30::10
}

run_traceroute()
{
  ip netns exec ptp_src traceroute -6 fc00:cafe:babe:33::11
  ip netns exec ptp_dst traceroute -6 fc00:cafe:babe:30::10
}

show_ip_route()
{
  net_ns_names=( ptp_src ptp_tc_ingress ptp_tc_delay ptp_tc_egress ptp_dst )
  for name in "${net_ns_names[@]}"; do echo ----; echo $name; ip netns exec $name ip addr list; done
  for name in "${net_ns_names[@]}"; do echo ----; echo $name; ip netns exec $name ip -6 route show; done
}
