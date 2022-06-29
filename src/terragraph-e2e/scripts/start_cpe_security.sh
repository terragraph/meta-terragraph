#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# This script manages hostapd instance on wired interface

enable=$1
vppIf="TenGigabitEthernet$2"
tapIf="nic$2"
secret=$3

wired_security_enable() {
    # Turn on EAPOL only mode
    vppctl set interface eapol-only on "${vppIf}"

    # Generate wired config files (hostapd, action file)
    nic_mac=$(ip -o link show "$tapIf" | awk '{print $(NF-2)}')
    if [ -z "$nic_mac" ]; then
        echo "$tapIf: Empty mac address"
        exit 1
    fi
    /usr/sbin/export_security_config conf "$tapIf" "$nic_mac"

    # Start hostapd on wired interface
    hostapd -dd -t "/var/run/hostapd/hostapd_${tapIf}.conf" &>> "/tmp/hostapd_${tapIf}" &
    while :
    do
        hostapd_instance=$(hostapd_cli -p "/var/run/hostapd_${tapIf}" -i "${tapIf}" ping)
        if [ "$hostapd_instance" = "PONG" ]; then
            break
        fi
        sleep 1
    done

    # Configurate action file
    hostapd_cli -p "/var/run/hostapd_${tapIf}" -i "${tapIf}" -a "/var/run/hostapd/hostapd_action_${tapIf}.sh" &
    # Configurate shared secret
    hostapd_cli -p "/var/run/hostapd_${tapIf}" -i "${tapIf}" set auth_server_shared_secret "${secret}"
}

wired_security_disable() {
    # Terminate hostapd instance
    pkill -9 -f "hostapd.*${tapIf}"
    rm -rf "/var/run/hostapd_${tapIf}"

    # Delete source MAC based ACL on VPP interface
    vppctl set interface input acl intfc "${vppIf}" ip6-table 0 del
    vppctl set interface input acl intfc "${vppIf}" ip4-table 0 del
    vppctl set interface input acl intfc "${vppIf}" l2-table 0 del
    vppctl classify table table 0 del
}

if [ "$#" -lt 2 ]; then
    echo "Usage: $0 [enable|disable] ifIdx"
    exit 1
fi

# Check if hostapd instance is running (return "PONG")
hostapd_instance=$(hostapd_cli -p "/var/run/hostapd_${tapIf}" -i "${tapIf}" ping)

if [ "$enable" = "enable" ] && [ "$hostapd_instance" != "PONG" ]; then
    if [ "$#" -ne 3 ]; then
        echo "Usage: $0 enable ifIdx secret"
        exit 1
    fi
    wired_security_enable
elif [ "$enable" = "disable" ] && [ "$hostapd_instance" = "PONG" ]; then
    wired_security_disable
fi
