#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import json

#
# Set encoding to UTF-8 for all modules as it is needed for click in python3
#
import locale
import subprocess
import sys

import click


def getpreferredencoding(do_setlocale=True):
    return "utf-8"


locale.getpreferredencoding = getpreferredencoding


NETWORK_PREFIX_KEY = "e2e-network-prefix"


"""
This script is used to migrate the current allocated prefixes on an existing
network into the e2e_controller topology file. This allows for a smooth
transition between seeded and static prefix allocation since this will ensure
the prefixes won't change after the switch.

Most common usage is likely while chroot'd into the rootfs:
    set_prefixes.py <src_topology_file> <node_overrides_file> -o <dest_topology_file>
"""


def _read_json_file(filename):
    try:
        with open(filename, "r") as f:
            return json.load(f)
    except Exception as ex:
        sys.exit("Error reading file {}: {}".format(filename, ex))


@click.command()
@click.argument("topology_file")
@click.argument("node_overrides_file")
@click.option(
    "--network_prefix",
    help=("Network prefix string " "e.g. 'face:b00c:cafe:ba00::/56,64'"),
)
@click.option(
    "--output", "-o", help=("Write modified topology to OUTPUT " "instead of stdout")
)
def _set_prefixes(topology_file, node_overrides_file, network_prefix, output):
    topo = _read_json_file(topology_file)
    node_overrides = _read_json_file(node_overrides_file)
    alloc_prefix_len = None

    # Find network prefix in the overrides file
    if network_prefix is None:
        for _, overrides in node_overrides.items():
            try:
                network_prefix = overrides["kvstoreParams"][NETWORK_PREFIX_KEY]
            except Exception:
                pass

    # Create prefix_alloc_params and set them in topology
    if network_prefix is None:
        print("Error: seed prefix not found in overrides file", file=sys.stderr)
        sys.exit(1)
    else:
        network_prefix_split = network_prefix.split(",")
        alloc_prefix_len = int(network_prefix_split[1])
        topo["config"]["prefix_alloc_params"] = {
            "seed_prefix": network_prefix_split[0],
            "alloc_prefix_len": alloc_prefix_len,
        }

    # Create nodes map for easy indexing by node name
    nodes = {n["name"]: n for n in topo["nodes"]}
    try:
        # Run `tg status -l`, split by newline and filter out empty strings
        ips = filter(bool, subprocess.check_output(["tg", "status", "-l"]).split("\n"))
    except subprocess.CalledProcessError as e:
        print(e, file=sys.stderr)
        sys.exit(1)

    # Example of ips (output of `tg status -l`):
    # 14:56:11 [INFO]: Received status about 14 minion(s)
    # NodeName                Ipv6Address
    # ----------------------  ---------------------
    # terra111.f5.tb.a404-if  2001:a:b:c000::1
    # terra114.f5.tb.a404-if  2001:a:b:c011::1
    # terra121.f5.tb.a404-if  2001:a:b:c022::1
    # terra123.f5.tb.a404-if  2001:a:b:c033::1
    # terra211.f5.tb.a404-if  2001:a:b:c044::1
    #
    # Simple check to ensure ips is somewhat valid
    if len(ips) == 3:
        print(
            "Error: `tg status -l` returns 0 nodes with allocated ips", file=sys.stderr
        )
        sys.exit(1)
    elif len(ips) < 3:
        print("Error: invalid ips: {}".format(ips), file=sys.stderr)
        sys.exit(1)

    # Parse ips and set the prefixes for all nodes
    # Ignore the first 3 header lines
    for line in ips[3:]:
        # Remove beginning and trailing whitespaces then split on whitespaces
        parse_line = line.strip().split()
        name = parse_line[0]
        ip = parse_line[1]
        # Remove the '1' at the end of the ip and add the prefix length
        prefix = "{}/{}".format(ip[:-1], alloc_prefix_len)
        nodes[name]["prefix"] = prefix

    topo["nodes"] = list(nodes.values())

    # Write to output file if one was provided, otherwise stdout
    if output:
        with open(output, "w") as outfile:
            json.dump(topo, outfile)
    else:
        json.dump(topo, sys.stdout, indent=4)


if __name__ == "__main__":
    _set_prefixes()
