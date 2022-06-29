#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

DIR="$(dirname -- "$0")"

setup()
{
  $DIR/vpp.sh 1g setup
  $DIR/pktgen.sh 1g2w setup
}

run()
{
  DPRC=dprc.2 $DIR/vpp.sh 1g 1 || exit 1
  DPRC=dprc.3 $DIR/pktgen.sh 1g2w
  killall -9 vpp >/dev/null 2>&1
}

if [ "$1" = "setup" ]; then
  setup
elif [ "$1" = "run" ]; then
  run
else
  exec 1>&2
  echo "Usage: $0 {setup|run}"
  echo
  echo "Runs a smoke-test in loopback mode to make sure pktgen and vpp can"
  echo "push and route traffic."
  echo "The board needs to have 2 cables connecting ports 7<->8 and 9<->10."
  echo
  echo "Example: \$ $0 setup"
  echo "         \$ $0 run"
  exit 1
fi
