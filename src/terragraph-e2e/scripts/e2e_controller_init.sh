#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Start the e2e_controller on this device, and create (overwrite) a topology
# file with just this node in it.

# Args: <optional topology file>
if [ ! -z "$1" ]; then
  TOPOLOGY_FILE="$1"
else
  TOPOLOGY_FILE="/data/e2e_topology.conf"
fi

# Make the user delete this file manually
if [ -f "$TOPOLOGY_FILE" ]; then
  echo "Topology file $TOPOLOGY_FILE already exists. Please delete or move it."
  exit 1
fi

# Restart e2e_controller and wait
sv restart e2e_controller
while [ -z "$(pidof e2e_controller)" ]; do
  echo "Waiting for e2e_controller to start..."
  sleep 1
done
echo "> e2e_controller is running with new topology file: $TOPOLOGY_FILE"

# Wait for minion to start
sv start e2e_minion
while [ -z "$(pidof e2e_controller)" ]; do
  echo "Waiting for e2e_minion to start..."
  sleep 1
done
NODE_INFO_FILE="/tmp/node_info"
if [ ! -f "$NODE_INFO_FILE" ]; then
  echo "$NODE_INFO_FILE does not exist"
  exit 1
fi
# shellcheck disable=SC1090
. $NODE_INFO_FILE

# Add site, node, and radio MACs
WLAN_MACS=""
for i in $(seq 0 $((NUM_WLAN_MACS - 1))); do
  eval MAC='$MAC_'$i
  WLAN_MACS="${WLAN_MACS}${MAC},"
done
SITE_NAME="POP_SITE"
NODE_NAME="PopNode"
tg site add -n "$SITE_NAME" --lat 0 --lon 0 --alt 0 --acc 4000000
tg node add -n "$NODE_NAME" -m "$NODE_ID" --wlan_mac_addrs "$WLAN_MACS" -s "$SITE_NAME" --node_type dn --pop_node
cat $TOPOLOGY_FILE
echo
echo "> Added this node to the topology file."

# Add controller URL to node config
LOOP_IFACE="lo"
GLOBAL_IPV6=$(ip -6 addr show dev $LOOP_IFACE scope global | awk '/inet6/ {split($2,arr,"/"); print arr[1];}')
if [ ! -z "$GLOBAL_IPV6" ]; then
  echo "> Setting controller IP to $GLOBAL_IPV6 (found on $LOOP_IFACE)."
  CTRL_IPV6="$GLOBAL_IPV6"
else
  echo "> Setting controller IP to localhost (no global IP found on $LOOP_IFACE)."
  CTRL_IPV6="::1"
fi
CTRL_URL="tcp://[$CTRL_IPV6]:7007"
tg config modify node -n "$NODE_NAME" -s "kvstoreParams.e2e-ctrl-url" "$CTRL_URL"
tg minion modify_node_config -s "kvstoreParams.e2e-ctrl-url" "$CTRL_URL"
tg minion set_node_config
echo "> Modified local node configuration and controller's config overrides."

echo "> Done."
