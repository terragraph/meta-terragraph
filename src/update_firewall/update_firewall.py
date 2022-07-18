#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import argparse
import json
import sys
from pathlib import Path
from subprocess import run
from typing import Any, List, Optional

import iptc  # type: ignore
from terragraph_thrift.NodeConfig import ttypes as configTypes  # type: ignore


NODE_CONFIG_FILE = "/data/cfg/node_config.json"
INPUT_CHAIN = "INPUT"
FORWARD_CHAIN = "FORWARD"


# Using Any as Thrift `py` has not type information
def get_firewall_config(node_config: Path) -> Any:
    """Read Node Config and get firewall configs, CPE and POP interfaces."""
    if not node_config.exists() or not node_config.is_file():
        print(f"{node_config} does not exist")
        print("Please specify a valid node config file")
        return None

    try:
        with node_config.open("r") as f:
            node_config_json = f.read()
        config = configTypes.NodeConfig()
        config.readFromJson(node_config_json)
    except OSError as ex:
        sys.exit(f"Error reading node config file {node_config}: {ex}")
    except json.decoder.JSONDecodeError as ex:
        sys.exit(f"Error node config is not a valid json {node_config}: {ex}")

    if not config.firewallConfig:
        print("firewallConfig not found in node config, not configuring")
        return None

    return config.firewallConfig


class FirewallRules:
    """Generate firewall rules and config for a firewall chain
    - Only support default chains"""

    def __init__(self, chain: str, default_policy=iptc.Policy.ACCEPT) -> None:
        table = iptc.Table6(iptc.Table.FILTER)
        self.chain = iptc.Chain(table, chain)
        self.default_policy = default_policy
        self.rules: List[iptc.Rule6] = []

    def allow_established(self) -> None:
        """Allow the TCP connections which are already established"""
        # TODO - Fix me!
        # This is broken as of python3-iptables v1.0.0 + iptables v1.8.3
        # python3 interpreter aborts with:
        #   python-iptables: match "state" already registered
        #
        # ----------------------------------------------------------------------
        # rule = iptc.Rule6()
        # rule.protocol = "tcp"
        # match = rule.create_match("state")
        # match.state = "RELATED,ESTABLISHED"
        # rule.target = iptc.Target(rule, iptc.Policy.ACCEPT)
        # self.rules.append(rule)
        # ----------------------------------------------------------------------
        print("IMPORTANT: 'allowEstablished' is currently broken!")

    def allow_icmpv6(self) -> None:
        rule = iptc.Rule6()
        rule.protocol = "icmpv6"
        rule.target = iptc.Target(rule, iptc.Policy.ACCEPT)
        self.rules.append(rule)

    def allow_link_local(self) -> None:
        """Allow any traffic from fe80::/10"""
        rule = iptc.Rule6()
        rule.src = "fe80::/10"
        rule.target = iptc.Target(rule, iptc.Policy.ACCEPT)
        self.rules.append(rule)

    def allow_loopback(self) -> None:
        """Allow all communication unintrupted on loopbak interface"""
        rule = iptc.Rule6()
        rule.in_interface = "lo"
        rule.target = iptc.Target(rule, iptc.Policy.ACCEPT)
        self.rules.append(rule)

    def _modify_port(
        self,
        port: int,
        target: iptc.Policy,
        protocol: str,
        interface: Optional[str] = None,
    ) -> None:
        port_str = str(port)  # iptc needs a string
        rule = iptc.Rule6()
        rule.protocol = protocol
        if interface is not None:
            rule.in_interface = interface
        match = rule.create_match(protocol)
        match.dport = port_str
        rule.target = iptc.Target(rule, target)
        self.rules.append(rule)

    def modify_tcp(
        self, port: int, target: iptc.Policy, interface: Optional[str] = None
    ) -> None:
        """Create a rule for provided tcp port for specified target"""
        self._modify_port(port, target, "tcp", interface)

    def modify_udp(
        self, port: int, target: iptc.Policy, interface: Optional[str] = None
    ) -> None:
        """Create a rule for provided udp port for specified target"""
        self._modify_port(port, target, "udp", interface)

    def set_interface_target(
        self, interface: str, target: iptc.Policy, destination: Optional[str] = None
    ) -> None:
        """
        Create rule for all packets coming from an interfce to a target, with
        optional destination address.
        """
        rule = iptc.Rule6()
        rule.in_interface = interface
        if destination is not None:
            rule.dst = destination
        rule.target = iptc.Target(rule, target)
        self.rules.append(rule)

    def _set_default_policy(self) -> None:
        """Set default policy as specified in the target"""
        policy = iptc.Policy(self.default_policy)
        self.chain.set_policy(policy)

    def apply_changes(self) -> None:
        """Flush the chain and apply default policy + rules"""
        self._set_default_policy()
        self.chain.flush()
        # Add Rules to chain
        for rule in self.rules:
            self.chain.append_rule(rule)


def generate_rules(table_name: str, firewall_cfg: Any) -> FirewallRules:
    """Process rules for a table"""
    default_policy = iptc.Policy.ACCEPT
    rule_target = iptc.Policy.DROP
    if firewall_cfg.defaultPolicy.upper() != "ACCEPT":
        default_policy = iptc.Policy.DROP
        rule_target = iptc.Policy.ACCEPT

    table = FirewallRules(table_name, default_policy=default_policy)

    if firewall_cfg and firewall_cfg.allowEstablished:
        table.allow_established()

    if firewall_cfg and firewall_cfg.allowICMPv6:
        table.allow_icmpv6()

    if firewall_cfg and firewall_cfg.allowLinkLocal:
        table.allow_link_local()

    if firewall_cfg and firewall_cfg.allowLoopback:
        table.allow_loopback()

    # Modify TCP ports
    if firewall_cfg.tcpPorts:
        ports = firewall_cfg.tcpPorts.split(",")
        for port in ports:
            table.modify_tcp(int(port), rule_target)

    # Modify UDP ports
    if firewall_cfg.udpPorts:
        ports = firewall_cfg.udpPorts.split(",")
        for port in ports:
            table.modify_udp(int(port), rule_target)

    return table


def apply_rules(node_config_file: str) -> int:
    """creates/updates firewall rules"""
    firewall_cfg = get_firewall_config(Path(node_config_file))
    if not firewall_cfg:
        input_rules = FirewallRules(INPUT_CHAIN)
        input_rules.apply_changes()

        # Unload kernel modules
        print("No firewall config, unloading kernel modules")
        run(["modprobe", "-r", "ip6table_filter"])
        return 0

    input_chain = generate_rules(INPUT_CHAIN, firewall_cfg)
    input_chain.apply_changes()

    # TODO: Once no more REV5 - Drop all forward traffic on Linux
    # Today we default to accept all so things keep working
    forward_chain = FirewallRules(FORWARD_CHAIN)
    forward_chain.apply_changes()

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Called by minion to update the firewall. "
            + "Not designed to be invoked manually."
        ),
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "-c",
        "--node-config",
        default=NODE_CONFIG_FILE,
        help=f"Path to node config JSON",
    )
    args = parser.parse_args()
    return apply_rules(args.node_config)


if __name__ == "__main__":
    sys.exit(main())
