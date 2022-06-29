#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

is_in_qemu() {
    ls /dev/disk/by-id/*QEMU* > /dev/null 2>&1
    return $?
}

qemu_set_bb_macs() {
    TG_NODE_INFO_FILE=/var/run/node_info
    # load node info file
    if [ -f "$TG_NODE_INFO_FILE" ]; then
        # shellcheck disable=SC1090
        . $TG_NODE_INFO_FILE
    fi

    # set the MAC address for each virtual baseband to its address in node_info
    for ifpath in /sys/class/net/wlan*
    do
        [ -d "${ifpath}" ] || continue
        ifname=$(basename "${ifpath}")
        # extract number from ifname (eg. wlan3 -> 3)
        ifnum=$(echo "$ifname" | grep -Po "\\d+")

        mac="\$MAC_${ifnum}"
        mac_addr=$(eval "echo ${mac}")

        ip link set dev "${ifname}" address "${mac_addr}"
    done
}

qemu_bring_virtual_eth_up() {
    # set all virtual ethernet devices that correspond to a virtual baseband up
    for ethifpath in /sys/class/net/eth*
    do
        [ -d "${ethifpath}" ] || continue
        ethif_mac=$(cat "${ethifpath}/address")
        # change from QEMU netdev MAC prefix to wlan/terradev MAC prefix
        expected_wlan_mac=$(echo "$ethif_mac" | sed "s/52:56/52:55/")
        ethif_mac_matches=false
        for wlanifpath in /sys/class/net/wlan*
        do
            [ -d "${wlanifpath}" ] || continue
            wlanif_mac=$(cat "${wlanifpath}/address")
            if [ "${expected_wlan_mac}" = "${wlanif_mac}" ]; then
                ethif_mac_matches=true
            fi
        done

        if $ethif_mac_matches; then
            ethifname=$(basename "${ethifpath}")
            ip link set "${ethifname}" up
            # delete all default routes for ethX so it doesn't interfere.
            # this would fail if e2e minion was restarted because the routes
            # didn't exist, so just make it succeed every time.
            ip -6 route | grep "${ethifname}" | xargs -l ip -6 route del || true
        fi
    done
}
