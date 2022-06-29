#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import unittest
from unittest import mock

import hbhssh


class HbhTest(unittest.TestCase):
    def testGetMacs(self):
        ip_link_result = """\
1: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP mode DEFAULT group default qlen 1000
    link/ether 00:01:02:03:04:04 brd ff:ff:ff:ff:ff:ff
2: nic0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP mode DEFAULT group default qlen 1000
    link/ether 00:01:02:03:04:05 brd ff:ff:ff:ff:ff:ff
2: nic1: <BROADCAST,MULTICAST> mtu 1500 qdisc mq state mode DEFAULT group default qlen 1000
    link/ether 00:01:02:03:04:06 brd ff:ff:ff:ff:ff:ff
"""
        ssh = hbhssh.Ssh()
        with mock.patch.object(ssh, "ssh") as mock_ssh:
            mock_ssh.return_value = ip_link_result
            self.assertEqual(
                ssh.get_macs("hostname"),
                [
                    ("00:01:02:03:04:04", "eth0"),
                    ("00:01:02:03:04:05", "nic0"),
                    ("00:01:02:03:04:06", None),
                ],
            )

    def testFindNeighborIps(self):
        ping_result = """\
64 bytes from fe80::c4b:7aff:fe2c:ae57%nic1: icmp_seq=3 ttl=64 time=0.129 ms (DUP!)
64 bytes from fe80::c4b:7aff:fe2c:ae58%nic1: icmp_seq=3 ttl=64 time=0.129 ms (DUP!)
64 bytes from fe80::c4b:7aff:fe2c:ae59%nic2: icmp_seq=3 ttl=64 time=0.129 ms (DUP!)
"""
        ssh = hbhssh.Ssh()
        with mock.patch.object(ssh, "ssh") as mock_ssh:
            mock_ssh.return_value = ping_result
            self.assertEqual(
                sorted(ssh.find_neighbor_ips("hostname", [])),
                sorted(
                    [
                        ("fe80::c4b:7aff:fe2c:ae57", "nic1"),
                        ("fe80::c4b:7aff:fe2c:ae58", "nic1"),
                        ("fe80::c4b:7aff:fe2c:ae59", "nic2"),
                    ]
                ),
            )

    # Triangle topology with 3 nodes. We're looking for a path from A to C.
    # Shortest path is A->C. This is the result if linkDown is False.
    # If linkDown is True, we pretend A's link to C is down, and so
    # the shortest path would be A->B->C.
    def testTriangleTopo(self, linkDown=False):
        topo = {
            "nodes": [
                {"name": "A", "mac_addr": "mac:a"},
                {"name": "B", "mac_addr": "mac:b"},
                {"name": "C", "mac_addr": "mac:c"},
            ],
            "links": [
                {"a_node_name": "A", "z_node_name": "B"},
                {"a_node_name": "B", "z_node_name": "C"},
                {"a_node_name": "A", "z_node_name": "C"},
            ],
        }
        macs_and_ifaces = {
            "A": [("mac:a", "terra0"), ("mac:a2", "terra1")],
            "B": [("mac:b", "terra0"), ("mac:b2", "terra1")],
            "C": [("mac:c", "terra0"), ("mac:c2", "terra1")],
        }
        neighbor_ips = {
            "A": [("fe80::1", "terra0", "B"), ("fe80::2", "terra1", "C")],
            "B": [("fe80::1", "terra0", "C"), ("fe80::2", "terra1", "A")],
            "C": [("fe80::1", "terra0", "A"), ("fe80::2", "terra1", "B")],
        }

        if linkDown:
            # Mark link A->C as down
            neighbor_ips["A"].pop()

        with mock.patch.object(
            hbhssh.Ssh, "get_macs"
        ) as mock_get_macs, mock.patch.object(
            hbhssh.Ssh, "find_neighbor_ips"
        ) as mock_find_neighbors:

            last_find_neighbors_host = [None]

            def my_get_macs(host):
                if host.startswith("__neigh"):
                    idx = int(host[7:]) - 1
                    node_name = neighbor_ips[last_find_neighbors_host[0]][idx][2]
                    return macs_and_ifaces[node_name]
                return macs_and_ifaces[host]

            def my_find_neighbors(host, iface_list):
                last_find_neighbors_host[0] = host
                return [(ip, iface) for (ip, iface, node) in neighbor_ips[host]]

            mock_get_macs.side_effect = my_get_macs
            mock_find_neighbors.side_effect = my_find_neighbors

            hbh = hbhssh.HBH(topo, "C", "A")
            path = hbh.find_paths()
            if linkDown:
                self.assertEqual(path, ["A", "B", "C"])
            else:
                self.assertEqual(path, ["A", "C"])

    def testTriangleTopoWithLinkDown(self):
        return self.testTriangleTopo(linkDown=True)


if __name__ == "__main__":
    unittest.main()
