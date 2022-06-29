#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# P2MP Random Assoc Dissoc After Broadcast BF
# Description:
# This script does broadcast BF to collect responders macs and does
# random assoc/dissoc with one of them.
# All nodes have to be initiated and enabled with topology_scan before start
# Topology scan is launched using tg command at the controller

import datetime
import json
import os
import re
import sys
import time
from random import randint

from p2mp_random_assoc import Get_mac


class Topo_scan_assoc(object):
    def __init__(self):
        # the peer mac addresses
        self.peer_macs = []
        # the number of peers
        self.num_peer = 0
        # count of association attempts for each responder
        self.resp_assoc_attempts = []
        # count of association failures for each responder
        self.resp_assoc_fails = []
        # After link is up, openr adj will be checked to see whether the
        # new node is there. This counts such check failed.
        self.resp_link_fails = []
        # the count that each responder responded in topo scan
        self.n_topo_resp = []
        # the distribution for the # of different Tx beams responded
        self.resp_sweep_stats = []
        # scan duration, with around 1.5s redundancy
        self.SCAN_DURATION = 3
        # object to check openr adj
        self.get = Get_mac()

    def get_adjs(self, kvname):
        try:
            return self.get.get_mac(kvname)
        except BaseException:
            return []

    def add_macs(self, macs):
        # adding macs to peer_macs
        for mac in macs:
            if mac not in self.peer_macs:
                self.peer_macs.append(mac)
                self.num_peer = len(self.peer_macs)
                self.resp_assoc_attempts.append(0)
                self.resp_assoc_fails.append(0)
                self.resp_link_fails.append(0)
                self.n_topo_resp.append(0)
                self.resp_sweep_stats.append({})

    def inc_topo_res(self, scan_results):
        # increment the topology scan response count
        for resp in scan_results:
            mac = resp["addr"]
            if mac in self.peer_macs:
                peer_id = self.peer_macs.index(mac)
                self.n_topo_resp[peer_id] += 1
                n_tx = len(resp["itorLqmMat"])
                if n_tx not in self.resp_sweep_stats[peer_id]:
                    self.resp_sweep_stats[peer_id][n_tx] = 0
                self.resp_sweep_stats[peer_id][n_tx] += 1

    def scan(self, delay, node_name):
        print("Launching topology scan")
        print(datetime.datetime.now())
        # run topology scan
        scan_result = os.popen(
            "tg scan start -t topo --tx {} --rx all -d {}".format(node_name, delay)
        ).read()
        if "ScanStartCmd succeeded" in scan_result:
            # parse the scan id
            match_scan_id = re.search(r"scan (\d+)", scan_result)
            if match_scan_id is None:
                print("Failed to parse scan id")
                return None
            scan_id = int(match_scan_id.group(1))
            time.sleep(delay + self.SCAN_DURATION)
            try:
                return self.load_topo_scan_resp(scan_id, node_name)
            except Exception as e:
                print(e)
                return None
        else:
            # the topology scan failed to start
            print("Failed to launch topology scan")
            return None

    def load_topo_scan_resp(self, scan_id, node_name):
        scan_status = os.popen("tg scan status -t {} -f json".format(scan_id)).read()
        struct = json.loads(scan_status)
        scan_resp = struct["scans"][str(scan_id)]["responses"][node_name]

        if scan_resp["status"] != 0:
            return None

        if "topoResps" not in scan_resp:
            return []

        topo_resps = scan_resp["topoResps"]
        scan_results = list(topo_resps.values())
        # print the results
        for resp in scan_results:
            mac = resp["addr"]
            if "pos" in resp:
                loc = [
                    resp["pos"]["latitude"],
                    resp["pos"]["longitude"],
                    resp["pos"]["altitude"],
                    resp["pos"]["accuracy"],
                ]
                print("The position of {} is {}".format(mac, loc))
            if "adjs" in resp:
                print("The local adjs of {} are {}".format(mac, resp["adjs"]))
            # calculate # of differnt TX beams the responder responded
            n_tx = len(resp["itorLqmMat"])
            print(
                "Collected the channel info to {} for {} different TX "
                "beams".format(mac, n_tx)
            )

        return scan_results


class Topo_wrapper(object):
    def __init__(self):
        self.clear_topo_data()

    def get_polarity(self, node):
        data = self.get_topology()
        for node_meta in data["nodes"]:
            if node_meta["name"] == node:
                return node_meta["polarity"]

    def clear_topo_data(self):
        # clear the data structure to maintain topology information
        self.site_to_node = {}  # site -> nodes
        self.node_to_site = {}  # node -> site
        self.mac_to_node = {}  # mac -> node
        self.node_to_mac = {}  # node -> mac

    def add_ignite_link(self, a_node, z_node):
        # add the node to the topology, then ignite the link
        if a_node not in self.node_to_site or z_node not in self.node_to_site:
            print("Invalid nodes, Abort")
            return False
        # add link
        if not self.add_link(a_node, z_node):
            return False
        # ignite the link
        if not self.ignite_link(a_node, z_node):
            return False

        return True

    def del_link(self, a_node, z_node):
        while self.check_link_status(a_node, z_node) is not None:
            print(os.popen("tg link down -i {} -r {}".format(a_node, z_node)).read())
            time.sleep(1)
            print(os.popen("tg link del -a {} -z {}".format(a_node, z_node)).read())
            time.sleep(1)

    def del_nodes(self, nodes):
        # remove nodes and sites, if necessary, from topology
        sites_to_delete = []
        for node in nodes:
            print(os.popen("tg node del -n {} --force".format(node)).read())
            site = self.del_node_data(node)
            if len(self.site_to_node[site]) == 0:
                sites_to_delete.append(site)
            time.sleep(0.1)
        for site in sites_to_delete:
            print(os.popen("tg site del -n {}".format(site)).read())
            self.site_to_node.pop(site)
            time.sleep(0.1)

    def check_link_status(self, a_node, z_node):
        # check whether link is alive
        data = self.get_topology()
        for link in data["links"]:
            if link["a_node_name"] == a_node and link["z_node_name"] == z_node:
                return link["is_alive"]

        return None

    def check_node_online_initiator(self, node):
        # check whether node is ONLINE_INITIATOR(3)
        data = self.get_topology()
        if data is None:
            return False

        for node_meta in data["nodes"]:
            if node == node_meta["name"] and node_meta["status"] == 3:
                return True
        return False

    def get_topology(self):
        data = None
        try:
            os.popen("tg topology ls --json /tmp/topo.json").read()
            with open("/tmp/topo.json") as f:
                data = json.load(f)
        except Exception:
            print("Unable to read topology")
        os.remove("/tmp/topo.json")
        return data

    def add_site(self, name, pos):
        site_result = os.popen(
            "tg site add -n {} --lat {} --lon {} --alt {} --acc {}".format(
                name,
                pos["latitude"],
                pos["longitude"],
                pos["altitude"],
                pos["accuracy"],
            )
        ).read()
        if "SiteCmd succeeded" not in site_result:
            print(site_result)
            print("Unable to add site, Abort")
            return False
        return True

    def add_node(self, name, mac, site, dn, polariy):
        node_result = os.popen(
            "tg node add -n {} -m {} -s {} --node_type {} -p {}".format(
                name, mac, site, dn, polariy
            )
        ).read()
        if "NodeCmd succeeded" not in node_result:
            print(node_result)
            print("Unable to add node, Abort")
            return False
        return True

    def add_link(self, a_node, z_node):
        link_result = os.popen(
            "tg link add -a {} -z {} --wireless".format(a_node, z_node)
        ).read()
        if "AddLinkCmd succeeded" not in link_result:
            print(link_result)
            print("Unable to add link, Abort")
            return False
        return True

    def ignite_link(self, a_node, z_node):
        ig_result = os.popen("tg link up -i {} -r {}".format(a_node, z_node)).read()
        if "LinkStatusCmd succeeded" not in ig_result:
            print(ig_result)
            print("Unable to ignite the link, Abort")
            return False
        return True

    def add_node_data(self, node, mac, site):
        self.del_node_data(node)
        self.site_to_node[site].add(node)
        self.node_to_site[node] = site
        self.mac_to_node[mac] = node
        self.node_to_mac[node] = mac

    def del_node_data(self, node):
        if node not in self.node_to_site:
            return None
        site = self.node_to_site[node]
        mac = self.node_to_mac[node]
        self.site_to_node[site].remove(node)
        self.node_to_site.pop(node)
        self.mac_to_node.pop(mac)
        self.node_to_mac.pop(node)
        # return the site that the node was in
        return site

    def load_topology(self):
        data = self.get_topology()
        if data is None:
            return False
        # load all the sites and nodes
        # clear the existing data
        self.clear_topo_data()
        for site_meta in data["sites"]:
            self.site_to_node[site_meta["name"]] = set()
        for node_meta in data["nodes"]:
            self.add_node_data(
                node_meta["name"], node_meta["mac_addr"], node_meta["site_name"]
            )

        return True

    def add_responders(self, scan_results, polarity):
        # add the responders into the topology
        # the newly added sites and nodes are named as "site-#" and "node-#"
        site_id = 1
        node_id = 1
        nodes_added = []
        for resp in scan_results:
            if "addr" not in resp:
                # skip the invalid responder
                continue
            # the mac list of responder + adjacencies
            mac_list = [resp["addr"]]
            if "adjs" in resp:
                mac_list.extend(resp["adjs"])
            # check whether any one in the mac_list is known
            site_name = None
            for mac in mac_list:
                if mac in self.mac_to_node:
                    site_name = self.node_to_site[self.mac_to_node[mac]]
                    break
            if site_name is None:
                # add new site
                if "pos" not in resp:
                    # position information missing, skip the responder
                    continue
                # find unused site name
                while "site-" + str(site_id) in self.site_to_node:
                    site_id += 1
                site_name = "site-" + str(site_id)
                if not self.add_site(site_name, resp["pos"]):
                    continue
                self.site_to_node[site_name] = set()
            # add the nodes in mac_list
            for mac in mac_list:
                if mac in self.mac_to_node:
                    # if the node is already in the topology, skip
                    continue
                # find unused node name
                while "node-" + str(node_id) in self.node_to_site:
                    node_id += 1
                node_name = "node-" + str(node_id)
                # add the node onto site site_name
                if self.add_node(node_name, mac, site_name, "dn", polarity):
                    self.add_node_data(node_name, mac, site_name)
                    nodes_added.append(node_name)
        # return the newly added nodes
        return nodes_added


def topo_ignite_test():

    MAX_TOPO_TRIES = 30
    SCAN_DELAY = 3
    SCAN_COOLDOWN = 1
    IGNITION_DELAY = 6

    topo_scan_count = 0
    topo_scan = Topo_scan_assoc()
    topo_wrapper = Topo_wrapper()

    pop_name = "node-1"
    print("\nSTART TOPOLOGY SCAN IGNITION SCRIPT\n")

    # check if pop node is online
    if not topo_wrapper.check_node_online_initiator(pop_name):
        print("pop node not online, abort")
        exit(1)

    # load the topology
    if topo_wrapper.load_topology():
        print(topo_wrapper.site_to_node)
        print(topo_wrapper.node_to_site)
        print(topo_wrapper.mac_to_node)
    else:
        print("Error loading topology")

    # Turn off the auto ignition at the controller
    ig_result = os.popen("tg ignition auto --disable").read()
    if "IgnitionAutoCmd succeeded" not in ig_result:
        print("Unable to turn off auto ignition... Abort")
        exit(1)

    # find pop polarity
    pop_polarity = topo_wrapper.get_polarity(pop_name)
    new_polarity = "even" if pop_polarity == 1 else "odd"

    # find the kvstore name of the pop node
    pop_mac = topo_wrapper.node_to_mac[pop_name]
    pop_kvname = "node-" + pop_mac.replace(":", ".")

    # get the pre-associated peers
    pre_macs = topo_scan.get_adjs(pop_kvname)
    topo_scan.add_macs(pre_macs)

    for i in range(MAX_TOPO_TRIES):
        # check if pop node is still online
        if not topo_wrapper.check_node_online_initiator(pop_name):
            print("pop node not online, abort")
            break

        # check the pre assocs in kvstore
        print("Check pre assocs...")
        adj_macs = topo_scan.get_adjs(pop_kvname)
        for mac in pre_macs:
            peer_id = topo_scan.peer_macs.index(mac)
            if mac not in adj_macs:
                topo_scan.resp_link_fails[peer_id] += 1
                print("Link of pre assoc {} is down".format(mac))

        print("Topology scan round {}/{}".format(i + 1, MAX_TOPO_TRIES))
        # launch topology scan and load the results
        scan_results = topo_scan.scan(SCAN_DELAY, pop_name)
        time.sleep(SCAN_COOLDOWN)
        if scan_results is None:
            print("Invalid scan, continue...")
            continue
        topo_scan_count += 1
        if len(scan_results) == 0:
            print("No responder discovered in topology scan, continue...")
            continue
        resp_macs = [resp["addr"] for resp in scan_results]
        print("Responders:")
        print(resp_macs)
        topo_scan.add_macs(resp_macs)
        topo_scan.inc_topo_res(scan_results)
        # add all the nodes into topology
        nodes_added = topo_wrapper.add_responders(scan_results, new_polarity)
        # randomly pick one to assoc
        resp_idx = randint(0, len(resp_macs) - 1)
        resp_mac = resp_macs[resp_idx]
        print("Picking responder {}".format(resp_mac))
        if resp_mac in topo_wrapper.mac_to_node:
            # add and ignite the link
            a_node_name = pop_name
            z_node_name = topo_wrapper.mac_to_node[resp_mac]
            peer_id = topo_scan.peer_macs.index(resp_mac)
            topo_scan.resp_assoc_attempts[peer_id] += 1
            topo_scan.resp_assoc_fails[peer_id] += 1
            if topo_wrapper.add_ignite_link(a_node_name, z_node_name):
                topo_scan.resp_assoc_fails[peer_id] -= 1
                time.sleep(IGNITION_DELAY)
                # check whether the node is in the adjacency of pop node
                adj_macs = topo_scan.get_adjs(pop_kvname)
                if resp_mac not in adj_macs:
                    topo_scan.resp_link_fails[peer_id] += 1
                    print("link is not set up successfully")
                else:
                    while (
                        topo_wrapper.check_link_status(a_node_name, z_node_name)
                        is not True
                    ):
                        # wait until topology is updated
                        time.sleep(1)
            else:
                print("link not ignited successfully")
            # del the link
            topo_wrapper.del_link(a_node_name, z_node_name)
        else:
            print("Error: Selected node is not added in topology")

        # remove the node_added
        topo_wrapper.del_nodes(nodes_added)
        time.sleep(IGNITION_DELAY)

    # print out the results
    print("Number of valid topology scans: {}".format(topo_scan_count))
    for mac in topo_scan.peer_macs:
        peer_id = topo_scan.peer_macs.index(mac)
        n_resp = topo_scan.n_topo_resp[peer_id]
        resp_assoc_fails = topo_scan.resp_assoc_fails[peer_id]
        resp_assoc_attempts = topo_scan.resp_assoc_attempts[peer_id]
        resp_link_fails = topo_scan.resp_link_fails[peer_id]
        print(
            "Responder: {}, responded in {} rounds, "
            "failed/total assocs: {}/{}, unexpected link down: {}".format(
                mac, n_resp, resp_assoc_fails, resp_assoc_attempts, resp_link_fails
            )
        )
        for n_tx in topo_scan.resp_sweep_stats[peer_id]:
            print(
                "# of rounds responded to {} different tx beams: {}".format(
                    n_tx, topo_scan.resp_sweep_stats[peer_id][n_tx]
                )
            )

    for mac in pre_macs:
        peer_id = topo_scan.peer_macs.index(mac)
        resp_link_fails = topo_scan.resp_link_fails[peer_id]
        print("Pre assoc: {}, link down: {}".format(mac, resp_link_fails))


if __name__ == "__main__":
    if len(sys.argv) != 1:
        print("This script does broadcast BF to collect responders macs and does")
        print("random assoc/dissoc with one of them.")
        print("All nodes have to be initiated and enabled with topology_scan")
        print("before starting this script.")
        exit(0)

    topo_ignite_test()
