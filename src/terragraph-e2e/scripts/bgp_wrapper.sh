#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

run_action() {
  if [ -f /etc/sv/quagga/run ]; then
    # Invoke quagga bgpd + zebra (via quagga_wrapper), used on Rev5
    echo "== Using Quagga for BGP config =="
    if [ "$1" = "start" ] || [ "$1" = "restart" ]; then
      sv once quagga
    else
      sv "$1" quagga
    fi
  elif [ -f /etc/sv/exabgp/run ]; then
    # Invoke exabgp, used on Puma
    echo "== Using ExaBGP for BGP config =="
    sv "$1" exabgp
  elif [ -f /etc/sv/bgpd/run ] && [ -f /etc/sv/zebra/run ]; then
    # Invoke FRR bgpd + zebra + frr_openr_sync + frr_bgp_healthcheck,
    # used on Puma
    echo "== Using FRR for BGP config =="
    if [ "$1" = "status" ]; then
      sv status bgpd
    else
      sv "$1" zebra
      sv "$1" bgpd
      sv "$1" frr_openr_sync
      sv "$1" frr_bgp_healthcheck
    fi
  else
    echo "ERROR: No BGP daemon found on this platform!" >&2
    exit 1
  fi
}

# invoked by /etc/init.d/bgp_softshut
do_softshut() {
  if [ -f /etc/sv/quagga/run ]; then
    sv stop quagga
  elif [ -f /etc/sv/exabgp/run ]; then
    sv stop exabgp
  elif [ -f /etc/sv/bgpd/run ] && [ -f /etc/sv/zebra/run ]; then
    sv stop frr_bgp_healthcheck  # healthcheck would reload it
    sv stop bgpd
  else
    echo "ERROR: No BGP daemon found on this platform!" >&2
    exit 1
  fi
}

case "$1" in
  start|stop|status|restart)
    run_action "$1"
    ;;
  softshut)
    do_softshut
    ;;
  *)
    echo "Usage: $0 {start|stop|status|restart|softshut}" >&2
    exit 1
    ;;
esac
