#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

if [ $# -eq 0 ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
  exec 1>&2
  echo "Usage: $0 <mode> [setup]"
  echo "Modes:"
  echo "  10g - Send traffic over dpmac.1, receive over dpmac.2"
  echo "  10g2w - Send 2-way traffic over dpmac.1<->dpmac.2"
  echo "  1g2w - Send 2-way traffic over dpmac.7<->dpmac.10"
  echo
  echo "Example: \$ $0 10g setup"
  echo "         \$ export DPRC=dprc.X (X=output from 'setup')"
  echo "         \$ $0 10g"
  exit 1
fi

# shellcheck disable=SC1091
. $(dirname -- "$0")/common

# $1 - core list
# $2 - core to port mapping
run_pktgen()
{
  [ -n "$1" ] && corelist="$1" || corelist="0,1,2"
  [ -n "$2" ] && mapping="$2" || mapping="1.0,2.1"
  dpdk-pktgen -l "$corelist" -n 1 --proc-type auto --file-prefix pg1 --log-level 8 -m 256 -- -P -T -m "$mapping" -f /tmp/pktgen-commands.txt
}

setup_10g()
{
  dynamic_dpl dpmac.1 dpmac.2
}

run_10g()
{
  cat > /tmp/pktgen-commands.txt <<EOF
set 0 src mac 00:00:00:00:01:01
set 0 dst mac 00:00:00:00:00:01
set 0 src ip 10.0.0.2/24
set 0 dst ip 10.0.1.2
set 0 size 500
start 0
EOF

  run_pktgen "$@"
}

setup_10g2w()
{
  dynamic_dpl dpmac.1 dpmac.2
}

run_10g2w()
{
  cat > /tmp/pktgen-commands.txt <<EOF
set 0 src mac 00:00:00:00:01:01
set 0 dst mac 00:00:00:00:00:01
set 0 src ip 10.0.0.2/24
set 0 dst ip 10.0.1.2
set 0 size 500

set 1 src mac 00:00:00:00:01:02
set 1 dst mac 00:00:00:00:00:02
set 1 src ip 10.0.1.2/24
set 1 dst ip 10.0.0.2
set 1 size 500

start 0
start 1
EOF

  run_pktgen "$@"
  rm /tmp/pktgen-commands.txt
}

setup_1g2w()
{
  dynamic_dpl dpmac.7 dpmac.10
}

run_1g2w()
{
  # Use only core 3 for polling
  run_10g2w "0,2" "2.[0-1]"
}

mode="$1"
[ "$2" = "setup" ] && setup="1" || setup=""

case "$mode" in
10g|10g2w|1g2w)
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
