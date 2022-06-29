#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

export LC_ALL=en_US
POP_DEFAULT_PREFIX_LEN="64"

vpp_pop_config() {
  if [ -z "$VPP_ADDR" ] || [ -z "$POP_ADDR" ]; then
    echo "VPP_ADDR and POP_ADDR need to be specified for VPP POP routing."
    exit 10
  fi

  /usr/sbin/run_vpp_chaperone_and_monitor.sh

  if [ "$POP_STATIC_ROUTING" -eq "1" ]; then
    echo "Applying POP static route config to VPP"
    breeze prefixmgr advertise ::/0
  elif [ "$POP_BGP_ROUTING" -eq "1" ]; then
    /usr/sbin/bgp_wrapper.sh restart
  fi
}

linux_pop_config() {
  if [ "$OPENR_USE_FIB_NSS" -eq "1" ]; then
    echo "Waiting for fib_nss to be running ..."
    until pgrep 'fib_nss' >/dev/null 2>&1; do sleep 1; done
  fi

  if [ -n "$POP_ADDR" ] && [ -n "$POP_IFACE" ]; then
    # use default prefix length if none specified
    if [ -z "$POP_ADDR_PREFIX_LEN" ]; then
      POP_ADDR_PREFIX_LEN="$POP_DEFAULT_PREFIX_LEN"
    fi
    ip -6 address add "${POP_ADDR}/${POP_ADDR_PREFIX_LEN}" dev "${POP_IFACE}"
  fi

  # Static routing
  if [ "$POP_STATIC_ROUTING" -eq "1" ]; then
    echo "Applying POP static route config"
    ip link set "${POP_IFACE}" up
    until ip -6 neighbor show | grep -i "${GW_ADDR}.*REACHABLE"; do
      # link-local addrs need the interface specified, globals do not
      if echo "$GW_ADDR" | grep -qiE '^fe80'; then
        ping6 -c 1 "${GW_ADDR}%${POP_IFACE}"
      else
        ping6 -c 1 "${GW_ADDR}"
      fi
      sleep 5
    done
    breeze fib --client-id 64 add ::/0 "${GW_ADDR}@${POP_IFACE}"
    breeze prefixmgr advertise ::/0
  elif [ "$POP_BGP_ROUTING" -eq "1" ]; then
    /usr/sbin/bgp_wrapper.sh restart
    ip link set "${POP_IFACE}" up
  fi
}

linux_nat64_config() {
  if [ -z "$NAT64_POP_ENABLED" ] || [ -z "$NAT64_IPV4_ADDR" ] || [ -z "$NAT64_IPV6_PREFIX" ] ; then
    echo "NAT64_POP_ENABLED, NAT64_IPV4_ADDR and NAT64_IPV6_PREFIX must be configured for NAT64."
    return
  fi

  if [ "$NAT64_POP_ENABLED" -ne "1" ]; then
    echo "NAT64 is not enabled."
    return
  fi
  echo "Configuring NAT64 with Jool"
  # Configure Jool NAT64 module with NAT64 prefix
  jool instance add "tg_nat64" --netfilter --pool6 "$NAT64_IPV6_PREFIX"

  # Program IPv4 Address on NAT64 interface. We also need a IPv6 address but that should
  # be assigned already to POP interface. If not POP (atypical), NAT64 interface should be
  # assigned a IPv6 prefix manually.
  ip addr add "$NAT64_IPV4_ADDR" dev "$POP_IFACE"

  # enable IPv4 forwarding
  sysctl -w net.ipv4.conf.all.forwarding=1

  # advertise NAT64 prefix to other nodes. Not needed if NAT64 is POP as default route
  # will cover routing for NAT64 prefixed addresses.
  breeze prefixmgr advertise "$NAT64_IPV6_PREFIX"
}

# load tg config environment variables
# shellcheck disable=1091
. /usr/sbin/config_get_env.sh

# Open/R initialization
echo "Waiting for openr binary to be running ..."
until pgrep 'openr' >/dev/null 2>&1; do sleep 1; done
# Inject all configured kvstore pairs
/usr/sbin/config_set_kvstore
# openr remembers advertised routes in persistent store. Withdraw default at startup.
breeze prefixmgr withdraw ::/0

# Config logic checks
if [ -z "$POP_STATIC_ROUTING" ] || [ -z "$POP_BGP_ROUTING" ] || \
    [ "$POP_STATIC_ROUTING" -eq "0" ] && [ "$POP_BGP_ROUTING" -eq "0" ]; then
  echo "No POP config applicable"
  exit 1
fi
if [ "$POP_STATIC_ROUTING" -eq "1" ] && [ "$POP_BGP_ROUTING" -eq "1" ]; then
  echo "Both static and BGP routing are enabled. Only one is allowed."
  exit 2
fi
if [ "$POP_STATIC_ROUTING" -eq "1" ]; then
  if [ -z "$GW_ADDR" ] || [ -z "$POP_IFACE" ]; then
    echo "GW_ADDR and POP_IFACE need to be specified for static routing."
    exit 3
  fi
fi

# Apply platform-dependent POP config
if [ "$DPDK_ENABLED" -eq "1" ]; then
  if [ "$OPENR_USE_FIB_VPP" -eq "1" ]; then
    vpp_pop_config
  else
    echo "POP config is not supported for DPDK_ENABLED=1 without OPENR_USE_FIB_VPP"
  fi
else
  linux_pop_config
  linux_nat64_config
fi
