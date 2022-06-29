#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging
import unittest

from terragraph_thrift.Topology import ttypes as topoTypes
from tg.commands.ignition import ExtinguishCmd
from tg.tg import CliOptions


_log = logging.getLogger(__name__)


class ExtinguishTest(unittest.TestCase):
    def setUp(self):
        self.cli_opts = CliOptions(
            controller_host="localhost",
            controller_port=17077,
            aggregator_host="localhost",
            aggregator_port=18100,
            agent_host="localhost",
            agent_port=4231,
            minion_host="localhost",
            minion_port=17177,
            verbosity=2,
        )
        self.cli_opts.exclude = ""
        self._extinguish_cmd = ExtinguishCmd(self.cli_opts, 0)

    def tearDown(self):
        print("... cleaning up")

    def _create_nodes(self, num_nodes, pop_nodes):
        nodes = []
        for idx in range(num_nodes):
            node = topoTypes.Node()
            node.name = str("node-%s" % idx)
            if idx in pop_nodes:
                node.pop_node = True
            else:
                node.pop_node = False
            nodes.append(node)
        return nodes

    def _create_link(self, a_node, z_node):
        link = topoTypes.Link()
        link.name = str("%s-%s" % (a_node.name, z_node.name))
        link.a_node_name = a_node.name
        link.z_node_name = z_node.name
        return link

    def _create_topology(self, nodes, links):
        # generates topology for self._extinguish_cmd which needs
        # topology: links, nodes
        # node: name, pop_node
        # link: name, a_node_name, z_node_name
        topology = topoTypes.Topology()
        topology.nodes = nodes
        topology.links = links
        return topology


# 0 (pop) -- 1
class p2p(ExtinguishTest):
    def runTest(self):
        nodes = self._create_nodes(2, [0])
        links = [self._create_link(nodes[0], nodes[1])]
        topology = self._create_topology(nodes, links)
        bfs_initiators, bfs_links = self._extinguish_cmd._get_bfs_links(topology)
        self.assertEqual(1, len(bfs_links))
        self.assertEqual("node-0-node-1", bfs_links[0])
        exp_bfs_initiators = ["node-0"]
        self.assertEqual(len(exp_bfs_initiators), len(bfs_initiators))
        for idx in range(len(bfs_initiators)):
            self.assertEqual(exp_bfs_initiators[idx], bfs_initiators[idx])


# 0 (pop) -- 1
#         \_ 2
class p2mp(ExtinguishTest):
    def runTest(self):
        nodes = self._create_nodes(3, [0])
        links = [
            self._create_link(nodes[0], nodes[1]),
            self._create_link(nodes[0], nodes[2]),
        ]
        topology = self._create_topology(nodes, links)
        bfs_initiators, bfs_links = self._extinguish_cmd._get_bfs_links(topology)
        self.assertEqual(2, len(bfs_links))
        self.assertEqual("node-0-node-1", bfs_links[0])
        self.assertEqual("node-0-node-2", bfs_links[1])
        exp_bfs_initiators = ["node-0", "node-0"]
        self.assertEqual(len(exp_bfs_initiators), len(bfs_initiators))
        for idx in range(len(bfs_initiators)):
            self.assertEqual(exp_bfs_initiators[idx], bfs_initiators[idx])


# Figure 8 topology
#
#   === node0(pop) -------- node5 ==== node4 -------- node10 ===
#  ||                              ||                          ||
#  node1                          node6                      node11
#  |                                |                           |
#  |                                |                           |
#  |                                |                           |
#  node3                          node9                      node13
#  ||                               ||                         ||
#   === node2 -------------- node8 ==== node7 ------ node12 ====
class figure8(ExtinguishTest):
    def runTest(self):
        nodes = self._create_nodes(14, [0])
        links = [
            self._create_link(nodes[1], nodes[3]),
            self._create_link(nodes[0], nodes[5]),
            self._create_link(nodes[2], nodes[8]),
            self._create_link(nodes[6], nodes[9]),
            self._create_link(nodes[10], nodes[4]),
            self._create_link(nodes[12], nodes[7]),
            self._create_link(nodes[11], nodes[13]),
            self._create_link(nodes[0], nodes[1]),
            self._create_link(nodes[2], nodes[3]),
            self._create_link(nodes[10], nodes[11]),
            self._create_link(nodes[12], nodes[13]),
            self._create_link(nodes[4], nodes[6]),
            self._create_link(nodes[5], nodes[6]),
            self._create_link(nodes[8], nodes[9]),
            self._create_link(nodes[7], nodes[9]),
        ]
        topology = self._create_topology(nodes, links)
        bfs_initiators, bfs_links = self._extinguish_cmd._get_bfs_links(topology)
        self.assertEqual(15, len(bfs_links))
        # 1 hop: 5, 1
        self.assertEqual("node-0-node-5", bfs_links[0])
        self.assertEqual("node-0-node-1", bfs_links[1])
        # 2 hop: 6, 3
        self.assertEqual("node-5-node-6", bfs_links[2])
        self.assertEqual("node-1-node-3", bfs_links[3])
        # 3 hop: 9, 4, 2
        self.assertEqual("node-6-node-9", bfs_links[4])
        self.assertEqual("node-4-node-6", bfs_links[5])
        self.assertEqual("node-2-node-3", bfs_links[6])
        # 4 hop: 8, 7, 10
        self.assertEqual("node-8-node-9", bfs_links[7])
        self.assertEqual("node-7-node-9", bfs_links[8])
        self.assertEqual("node-10-node-4", bfs_links[9])
        self.assertEqual("node-2-node-8", bfs_links[10])
        # 5 hop: 12, 11
        self.assertEqual("node-12-node-7", bfs_links[11])
        self.assertEqual("node-10-node-11", bfs_links[12])
        # 6 hop: 13
        self.assertEqual("node-12-node-13", bfs_links[13])
        self.assertEqual("node-11-node-13", bfs_links[14])
        exp_bfs_initiators = [
            "node-0",
            "node-0",
            "node-5",
            "node-1",
            "node-6",
            "node-6",
            "node-3",
            "node-9",
            "node-9",
            "node-4",
            "node-2",
            "node-7",
            "node-10",
            "node-12",
            "node-11",
        ]
        self.assertEqual(len(exp_bfs_initiators), len(bfs_initiators))
        for idx in range(len(bfs_initiators)):
            self.assertEqual(exp_bfs_initiators[idx], bfs_initiators[idx])


# Figure 8 topology with 2 pops
#
#   === node0(pop) -------- node5 ==== node4 -------- node10 ===
#  ||                              ||                          ||
#  node1                          node6                      node11
#  |                                |                           |
#  |                                |                           |
#  |                                |                           |
#  node3                          node9                      node13
#  ||                               ||                         ||
#   === node2 -------------- node8 ==== node7 ------ node12 (pop)
class figure8Pop2(ExtinguishTest):
    def runTest(self):
        nodes = self._create_nodes(14, [0, 12])
        links = [
            self._create_link(nodes[1], nodes[3]),
            self._create_link(nodes[0], nodes[5]),
            self._create_link(nodes[2], nodes[8]),
            self._create_link(nodes[6], nodes[9]),
            self._create_link(nodes[10], nodes[4]),
            self._create_link(nodes[12], nodes[7]),
            self._create_link(nodes[11], nodes[13]),
            self._create_link(nodes[0], nodes[1]),
            self._create_link(nodes[2], nodes[3]),
            self._create_link(nodes[10], nodes[11]),
            self._create_link(nodes[12], nodes[13]),
            self._create_link(nodes[4], nodes[6]),
            self._create_link(nodes[5], nodes[6]),
            self._create_link(nodes[8], nodes[9]),
            self._create_link(nodes[7], nodes[9]),
        ]
        topology = self._create_topology(nodes, links)
        bfs_initiators, bfs_links = self._extinguish_cmd._get_bfs_links(topology)
        self.assertEqual(15, len(bfs_links))
        # 0 hop: 0, 12
        # 1 hop: 5, 1, 7, 13
        self.assertEqual("node-0-node-5", bfs_links[0])
        self.assertEqual("node-0-node-1", bfs_links[1])
        self.assertEqual("node-12-node-7", bfs_links[2])
        self.assertEqual("node-12-node-13", bfs_links[3])
        # 2 hops: 6, 3, 9, 11
        self.assertEqual("node-5-node-6", bfs_links[4])
        self.assertEqual("node-1-node-3", bfs_links[5])
        self.assertEqual("node-7-node-9", bfs_links[6])
        self.assertEqual("node-11-node-13", bfs_links[7])
        # 3 hops: 9, 4, 2, 8, 10
        self.assertEqual("node-6-node-9", bfs_links[8])
        self.assertEqual("node-4-node-6", bfs_links[9])
        self.assertEqual("node-2-node-3", bfs_links[10])
        self.assertEqual("node-8-node-9", bfs_links[11])
        self.assertEqual("node-10-node-11", bfs_links[12])
        # 4 hops: 10, 8
        self.assertEqual("node-10-node-4", bfs_links[13])
        self.assertEqual("node-2-node-8", bfs_links[14])
        exp_bfs_initiators = [
            "node-0",
            "node-0",
            "node-12",
            "node-12",
            "node-5",
            "node-1",
            "node-7",
            "node-13",
            "node-6",
            "node-6",
            "node-3",
            "node-9",
            "node-11",
            "node-4",
            "node-2",
        ]
        self.assertEqual(len(exp_bfs_initiators), len(bfs_initiators))
        for idx in range(len(bfs_initiators)):
            self.assertEqual(exp_bfs_initiators[idx], bfs_initiators[idx])


if __name__ == "__main__":
    unittest.main()
