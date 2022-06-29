#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import argparse
from ipaddress import IPv6Address, IPv6Network, ip_address, ip_network

import vpp_utils
from vpp_papi import VPP


DEFAULT_V6_ADDR = ip_address("::")


def add_del_prefix(
    vpp: VPP,
    prefix: IPv6Network,
    nexthop: IPv6Address,
    interface_idx: int,
    drop: bool = False,
    table_id: int = 0,
) -> int:

    func_args = {
        "is_ipv6": 1,
        "next_hop_sw_if_index": interface_idx,
        "table_id": table_id,
        "dst_address": prefix.network_address.packed,
        "dst_address_length": prefix.prefixlen,
        "next_hop_address": nexthop.packed,
    }

    if drop:
        func_args["is_drop"] = 1
    else:
        func_args["is_add"] = 1

    print(vpp.api.ip_add_del_route(**func_args))
    return 0


def print_ip6_fib(vpp: VPP) -> None:
    current_fib = vpp.api.ip6_fib_dump()
    for e in current_fib:
        prefix = ip_address(e.address)
        next_hop = ip_address(e.path[0].next_hop)
        print(f"{prefix}/{e.address_length} via {next_hop}")


def talk_to_vpp(vpp: VPP, args: argparse.Namespace) -> int:
    print("--> Before Add FIB Entry")
    print_ip6_fib(vpp)

    prefix_to_add = ip_network(args.dst_prefix)
    nexthop = ip_address(args.nexthop)

    print("\n--> Adding FIB Entry")
    add_del_prefix(vpp, prefix_to_add, nexthop, args.interface_idx)

    print("\n--> After Add FIB Entry")
    print_ip6_fib(vpp)

    print("\n--> Dropping FIB Entry")
    add_del_prefix(vpp, prefix_to_add, nexthop, args.interface_idx, drop=True)

    print("\n--> After Drop FIB Entry")
    print_ip6_fib(vpp)

    return 0


def main() -> int:
    ret_val = 0
    jsonfiles = []

    parser = argparse.ArgumentParser()
    parser.add_argument("dst_prefix")
    parser.add_argument("nexthop")
    parser.add_argument("interface_idx", type=int)
    args = parser.parse_args()

    vpp_utils.load_api_json(jsonfiles)
    if not jsonfiles:
        print("Error: no json api files found")
        return -1

    vpp = VPP(jsonfiles)
    try:
        vpp_con_success = vpp.connect("fibvpp")
        if vpp_con_success:
            print(f"Failed to connect to VPP: {vpp_con_success}")
            return 1

        ret_val = talk_to_vpp(vpp, args)
    finally:
        disconnect_error = vpp.disconnect()
        if disconnect_error:
            print(f"Unable to disconnet from VPP: {disconnect_error}")
            return 1

    return ret_val


if __name__ == "__main__":
    exit(main())
