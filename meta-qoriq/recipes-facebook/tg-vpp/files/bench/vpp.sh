#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

if [ $# -eq 0 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
  exec 1>&2
  echo "Usage: $0 <mode> [setup|num-cores]"
  echo "Modes:"
  echo "  10g - Route between dpmac.1<->dpmac.2"
  echo "  1g - Route between dpmac.8<->dpmac.9"
  echo
  echo "Example: \$ $0 10g setup # Run dynamic_dpl.sh"
  echo "         \$ export DPRC=dprc.X (X=output from 'setup')"
  echo "         \$ $0 10g # Use 2 cores by default"
  echo "         \$ $0 10g 1 # Use only 1 core"
  exit 1
fi

# shellcheck disable=SC1091
. $(dirname -- "$0")/common

setup_10g()
{
  dynamic_dpl dpmac.1 dpmac.2
}

run_10g()
{
  [ "$cpus" = "1" ] && coremask="0x6" || coremask="0x7"
  cat > /tmp/vpp.conf <<EOF
unix {
  log /tmp/vpp.log
  cli-listen /run/vpp/cli.sock
}

dpdk {
  # number of memory channels. We only have 1
  nchannels 1
  coremask  $coremask
  huge-dir   /mnt/hugepages
  socket-mem 1024
  num-mbufs 16384
}
EOF

  [ "$cpus" = "2" ] && cat >> /tmp/vpp.conf <<EOF
cpu {
  main-core 0
  corelist-workers 1,2
}
EOF

  # (Re)start vpp
  killall -9 vpp 2> /dev/null
  vpp -c /tmp/vpp.conf

  # Wait till vpp is up
  i=0
  until vppctl show hardware-interfaces >/dev/null 2>&1; do
    i="$((i+1))"
    if [ "$i" -ge 5 ]; then
      echo "ERROR: Timed out waiting for VPP to start, check /tmp/vpp.log" 1>&2
      exit 1
    fi
    sleep 1
  done

  # Setup interface IPs, MACs, and neighbor ARP entries
  # TenGigabitEthernet0 is dpmac.1
  # Network is 10.0.0.0/24
  # 10.0.0.1/00:00:00:00:00:01 is us, 10.0.0.2/00:00:00:00:01:01 is neighbor
  vppctl set interface mac address TenGigabitEthernet0 00:00:00:00:00:01
  vppctl set interface ip address TenGigabitEthernet0 10.0.0.1/24
  vppctl set interface state TenGigabitEthernet0 up
  vppctl set ip arp TenGigabitEthernet0 10.0.0.2 00:00:00:00:01:01 static

  # TenGigabitEthernet1 is dpmac.2
  # Network is 10.0.1.0/24
  # 10.0.1.1/00:00:00:00:00:02 is us, 10.0.1.2/00:00:00:00:01:02 is neighbor
  vppctl set interface mac address TenGigabitEthernet1 00:00:00:00:00:02
  vppctl set interface ip address TenGigabitEthernet1 10.0.1.1/24
  vppctl set interface state TenGigabitEthernet1 up
  vppctl set ip arp TenGigabitEthernet1 10.0.1.2 00:00:00:00:01:02 static

  # Enable routing
  vppctl ip route add 10.0.0.2/24 via 10.0.0.2 TenGigabitEthernet0
  vppctl ip route add 10.0.1.2/24 via 10.0.1.2 TenGigabitEthernet1

  # Show configured state
  vppctl show hardware-interfaces
}

setup_1g()
{
  dynamic_dpl dpmac.8 dpmac.9
}

run_1g()
{
  run_10g
}

mode="$1"
setup=""
cpus="2"

case "$2" in
"") ;;
setup) setup="1" ;;
1) cpus="1" ;;
2) cpus="2" ;;
*)
  echo "Bad value for second argument '$2'. Should be setup, 1 or 2." 1>&2
  exit 1
esac

case "$mode" in
10g|1g)
  if [ -n "$setup" ]; then
    "setup_$mode"
  else
    if [ -z "$DPRC" ]; then
      echo "WARNING: DPRC not set, setting to dprc.2" 1>&2
      export DPRC="dprc.2"
    fi
    "run_$mode"
  fi
;;
*)
  echo "Unknown mode '$mode'" 1>&2
  exit 1
esac
