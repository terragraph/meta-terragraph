#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import argparse
import json
import logging
import re
import subprocess
import tempfile

import networkx  # type: ignore


LOG = logging.getLogger(__name__)


class Node:
    __slots__ = ["macs", "ifaces", "neighs"]

    def __init__(self, macs, ifaces, neighs):
        self.macs = macs
        self.ifaces = ifaces
        self.neighs = neighs


class Neigh:
    __slots__ = ["ip", "iface", "name"]

    def __init__(self, ip, iface, name):
        self.ip = ip
        self.iface = iface
        self.name = name


class Ssh:
    def __init__(self):
        self.ssh_config_path = None

    def get_ssh_config_path(self):
        if self.ssh_config_path is not None:
            return self.ssh_config_path
        (fp, path) = tempfile.mkstemp(prefix="hbhssh_config_")
        self.ssh_config_path = path
        return path

    def ssh(self, host, command):
        try:
            LOG.debug("Running ssh {} {}".format(host, command))
            completed_process = subprocess.run(
                ["ssh", "-F", self.get_ssh_config_path(), host, command],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=10,
                encoding="utf-8",
            )
            if completed_process.stderr:
                LOG.debug(
                    "Stderr for 'ssh {} {}': {}".format(
                        host, command, completed_process.stderr
                    )
                )
            return completed_process.stdout
        except subprocess.TimeoutExpired:
            return ""

    def get_macs(self, host):
        result = []
        output = self.ssh(host, "ip link show")
        # 2: nic0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP mode DEFAULT group default qlen 1000
        #     link/ether 00:01:02:03:04:05 brd ff:ff:ff:ff:ff:ff
        lines = output.split("\n")
        for i in range(0, len(lines) - 1, 2):
            line1 = lines[i]
            line2 = lines[i + 1]
            m = re.search(r":\s*([^:]+):", line1)
            if m:
                iface = m.group(1) if "UP" in line1 else None
                m = re.search(r"link/ether\s*(\S+)", line2)
                if m:
                    mac = m.group(1).lower()
                    result.append((mac, iface))
        return result

    def find_neighbor_ips(self, host, iface_list):
        # Send 4 packets (-c), out of which send a burst of 3 without waiting
        # for reply (-l), with the forth packet sent 0.2s after the first 3.
        # Ignore replies from localhost (-L), exit after receiving replies to all
        # 4 packets or at most after 1s (-w).
        # The goal is to have ping wait for all reachable neighbors to reply.
        # Run pings on all interfaces in parallel, and wait for all to complete
        cmds = [
            "ping6 -c4 -l3 -L -i0.2 -w1 ff02::1%{}".format(iface)
            for iface in iface_list
        ]
        cmds.append("wait")
        output = self.ssh(host, " & ".join(cmds))
        # 64 bytes from fe80::c4b:7aff:fe2c:ae57%nic1: icmp_seq=3 ttl=64 time=0.129 ms (DUP!)
        ips_and_ifaces = {
            m.group(1) for m in re.finditer(r"bytes from ([^%]+%[^:]+)", output)
        }
        return [tuple(entry.split("%")) for entry in ips_and_ifaces]


class HBH:
    def __init__(self, topology, target_node, initial_node_ip):
        self.ssh = Ssh()
        self.mac2node = {}
        self.nodes = {}
        self.network = networkx.Graph()
        for node in topology["nodes"]:
            macs = (
                node["wlan_mac_addrs"]
                if "wlan_mac_addrs" in node
                else [node["mac_addr"]]
            )
            macs = [m.lower() for m in macs]
            for m in macs:
                self.mac2node[m] = node["name"]
            self.network.add_node(node["name"])
        for edge in topology["links"]:
            self.network.add_edge(edge["a_node_name"], edge["z_node_name"])
        self.target_node = target_node
        if target_node not in (node["name"] for node in topology["nodes"]):
            LOG.error("Can't find target node {}".format(target_node))
            exit(1)
        self.initial_node_ip = initial_node_ip

    def list_of_macs_to_node(self, list_of_macs):
        for (m, _) in list_of_macs:
            node = self.mac2node.get(m)
            if node:
                return node
        return None

    # SSH has the -J option, which is equivalent to ProxyJump, but it
    # doesn't like link-local address specification, so we write a temporary
    # ssh_config file instead
    def write_ssh_config_file(self, path, neighbor_ips):
        GLOBAL_TEMPLATE = """\
Host *
    StrictHostKeyChecking no
    UserKnownHostsFile /dev/null
    User root
    ForwardAgent yes
    BatchMode yes
"""
        INITIAL_HOST_TEMPLATE = """\
Host {name}
    Hostname {ip}
"""
        PER_HOST_TEMPLATE = """\
Host {name}
    Hostname [{ip}%%{iface}]
    ProxyJump {path}
"""
        with open(self.ssh.get_ssh_config_path(), "w") as f:
            print(GLOBAL_TEMPLATE, file=f)
            if not path:
                return
            print(
                INITIAL_HOST_TEMPLATE.format(name=path[0], ip=self.initial_node_ip),
                file=f,
            )

            for i in range(1, len(path)):
                prev_node = self.nodes[path[i - 1]]
                prev_node_neigh = [n for n in prev_node.neighs if n.name == path[i]]
                if len(prev_node_neigh) != 1:
                    LOG.error(
                        "Node {} has {} neighbors named {} (need 1), aborting".format(
                            path[i - 1], len(prev_node_neigh), path[i]
                        )
                    )
                    exit(1)
                prev_node_neigh = prev_node_neigh[0]
                print(
                    PER_HOST_TEMPLATE.format(
                        name=path[i],
                        ip=prev_node_neigh.ip,
                        iface=prev_node_neigh.iface,
                        path=",".join(path[:i]),
                    ),
                    file=f,
                )

            for (idx, (ip, iface)) in enumerate(neighbor_ips):
                print(
                    PER_HOST_TEMPLATE.format(
                        name="__neigh{}".format(idx + 1),
                        ip=ip,
                        iface=iface,
                        path=",".join(path),
                    ),
                    file=f,
                )

    # Given a path from the initial node to the current node, discover all
    # neighbors of the current node
    def discover_neighbors(self, path):
        node_name = path[-1]
        node = self.nodes[node_name]
        if node.neighs is not None:
            return
        self.write_ssh_config_file(path, [])
        neighbor_ips = self.ssh.find_neighbor_ips(node_name, node.ifaces)
        self.write_ssh_config_file(path, neighbor_ips)
        neigh_names = set()
        node.neighs = []
        for (index, (ip, iface)) in enumerate(neighbor_ips):
            neigh_macs_and_ifaces = self.ssh.get_macs("__neigh{}".format(index + 1))
            neigh_node_name = self.list_of_macs_to_node(neigh_macs_and_ifaces)
            if neigh_node_name:
                LOG.info("   Found node {}".format(neigh_node_name))
            # Ignore unknown nodes and second (and up) link leading to same neighbor
            if not neigh_node_name or neigh_node_name in neigh_names:
                continue
            neigh_names.add(neigh_node_name)
            if neigh_node_name not in self.nodes:
                self.nodes[neigh_node_name] = Node(
                    macs={mac for (mac, _) in neigh_macs_and_ifaces},
                    ifaces={
                        iface
                        for (_, iface) in neigh_macs_and_ifaces
                        if iface is not None
                    },
                    neighs=None,
                )
            node.neighs.append(Neigh(ip, iface, neigh_node_name))

        # All links that should be there according to the topology but
        # were not discovered above are assumed to be down
        unreachable_neighs = set(self.network.neighbors(node_name)) - neigh_names
        for n in unreachable_neighs:
            LOG.info("  Link {} - {} down".format(node_name, n))
            # Remove the link from the graph, so later paths don't use it
            self.network.remove_edge(node_name, n)

    def find_paths(self):
        self.write_ssh_config_file([], [])
        initial_node_macs = self.ssh.get_macs(self.initial_node_ip)
        initial_node = self.list_of_macs_to_node(initial_node_macs)
        if not initial_node:
            LOG.error("Can't find initial node name")
            exit(1)
        self.nodes[initial_node] = Node(
            macs={mac for (mac, _) in initial_node_macs},
            ifaces={iface for (_, iface) in initial_node_macs if iface is not None},
            neighs=None,
        )
        self.write_ssh_config_file([initial_node], [])

        # Iterate until we find a path to the target node
        num_edges = -1
        while True:
            new_num_edges = self.network.number_of_edges()
            if num_edges == new_num_edges:
                LOG.error("#edges={} hasn't decreased, aborting".format(num_edges))
                break
            num_edges = new_num_edges
            # Find a path from the initial to the target node using links
            # from the topology that are not known to be down
            try:
                path = networkx.shortest_path(
                    self.network, initial_node, self.target_node
                )
            except networkx.NetworkXNoPath:
                LOG.error(
                    "No path between {} and {}, aborting".format(
                        initial_node, self.target_node
                    )
                )
                return

            LOG.info("Trying path {}".format(" -> ".join(path)))
            for cur_node in range(len(path)):
                LOG.info(
                    "  Trying node {}".format(
                        " -> ".join(
                            path[:cur_node]
                            + ["**" + path[cur_node] + "**"]
                            + path[cur_node + 1 :]
                        )
                    )
                )
                if cur_node > 0:
                    # The link from the previous to the current node is down,
                    # so stop working on this path and find a new one
                    if not any(
                        True
                        for n in self.nodes[path[cur_node - 1]].neighs
                        if n.name == path[cur_node]
                    ):
                        LOG.info(
                            "  No link from {} to {}, trying next path".format(
                                path[cur_node - 1], path[cur_node]
                            )
                        )
                        break
                if cur_node < len(path) - 1:
                    # The main step along the path: discover neighbors of
                    # the current node
                    self.discover_neighbors(path[: cur_node + 1])
                else:
                    # Success! We made it to the target node!
                    self.write_ssh_config_file(path, [])
                    LOG.info(
                        "Success! Use 'ssh -F {} {}' to connect".format(
                            self.ssh.get_ssh_config_path(), self.target_node
                        )
                    )
                    return path


def main():
    parser = argparse.ArgumentParser(description="Hop-by-hop SSH")
    parser.add_argument("target_node", help="Name of the target node")
    parser.add_argument("-t", "--topology", required=True, help="Path to topology file")
    parser.add_argument(
        "-i", "--initial-node", required=True, help="IP address of first SSH hop"
    )
    parser.add_argument("-v", "--verbose", action="count", help="Show verbose messages")

    args = parser.parse_args()

    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(
        format="[%(asctime)s] %(levelname)s: %(message)s", level=log_level
    )

    try:
        with open(args.topology) as topofile:
            topology = json.load(topofile)
    except IOError:
        LOG.exception("Can't open file {}".format(args.topology))
        exit(1)
    except json.JSONDecodeError:
        LOG.exception("Can't decode json from {}".format(args.topology))
        exit(1)

    hbh = HBH(topology, args.target_node, args.initial_node)
    hbh.find_paths()


if __name__ == "__main__":
    main()
