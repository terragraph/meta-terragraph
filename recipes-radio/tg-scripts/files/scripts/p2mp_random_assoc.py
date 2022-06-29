#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# P2MP Random Assoc Dissoc with terra and 1-hop lo Pings
# Description:
# This script should be used to do random assoc/dissoc with r2d2 in p2mp setups
# Should be run at DN-initiator.
# All links have to be associated before starting the script.

import datetime
import json
import sys
import time
from random import randint
from subprocess import PIPE, Popen, call


# Function to execute a system command
# - Module level to be used in both classes
def execute(cmd_list, shell=False):
    if shell:
        cmd = " ".join(cmd_list)
    else:
        cmd = cmd_list

    proc = Popen(cmd, shell=shell, stderr=PIPE, stdout=PIPE, universal_newlines=True)

    output, errors = proc.communicate()
    return output


class RandomAssocDisassoc:
    # Init Function
    def __init__(self, peer_macs):
        self.peer_macs = peer_macs
        self.num_peer = len(self.peer_macs)
        self.is_aso = [0 for _x in range(1, self.num_peer + 1)]
        self.aso_fail = [0 for _x in range(1, self.num_peer + 1)]
        self.dso_fail = [0 for _x in range(1, self.num_peer + 1)]
        self.get_intf = [0 for _x in range(1, self.num_peer + 1)]
        self.terra_fail = [0 for _x in range(1, self.num_peer + 1)]
        self.lo_fail = [0 for _x in range(1, self.num_peer + 1)]
        self.is_pass = [0 for _x in range(1, self.num_peer + 1)]
        self.total_aso_dso = [0 for _x in range(1, self.num_peer + 1)]

    # Association Function
    def assoc(self, peer_id):
        print("Association attempt for {}:".format(self.peer_macs[peer_id]))
        print(datetime.datetime.now())
        aso_result = execute(
            [
                "r2d2",
                "assoc",
                "--config_file",
                "/data/cfg/node_config.json",
                "--responder_mac={}".format(self.peer_macs[peer_id]),
            ]
        )

        if "LinkAssocCmd failed" in aso_result:
            print("ERROR: Association failed for {}".format(self.peer_macs[peer_id]))
            self.is_aso[peer_id] = 0
            self.aso_fail[peer_id] = self.aso_fail[peer_id] + 1
            return
        elif "LinkAssocCmd succeeded" in aso_result:
            self.is_aso[peer_id] = 1
            print("Association Succeeded")

    # Dissociation Function
    def dissoc(self, peer_id):
        print(datetime.datetime.now())
        print("Dissociation attempt for {}:".format(self.peer_macs[peer_id]))
        dissoc_result = execute(
            ["r2d2", "dissoc", "--responder_mac={}".format(self.peer_macs[peer_id])]
        )

        if "LinkDissocCmd failed" in dissoc_result:
            self.dso_fail[peer_id] = self.dso_fail[peer_id] + 1
            print("ERROR: Dissociation failed for {}".format(self.peer_macs[peer_id]))
            return
        elif "LinkDissocCmd succeeded" in dissoc_result:
            self.is_aso[peer_id] = 0
            print("Dissociation Succeeded")

    # Function to find interface for the associated link
    def mac_to_intf(self, peer_id):
        print("Finding interface for {}".format(self.peer_macs[peer_id]))
        link_state = []
        for intf in range(0, self.num_peer):
            out_link_state = execute(
                ["cat", "/sys/kernel/debug/terra/terra{}/info".format(intf)]
            )
            link_state.insert(
                intf,
                out_link_state[
                    out_link_state.index("Link state")
                    + 18 : out_link_state.index("\nLink Count") : 1
                ],
            )
            if link_state[intf] == "3":
                intf_mac = out_link_state[
                    out_link_state.index("Sta addr")
                    + 18 : out_link_state.index("\nLink state") : 1
                ]
                if intf_mac == self.peer_macs[peer_id]:
                    print(
                        "Interface: terra{} matches {}".format(
                            intf, self.peer_macs[peer_id]
                        )
                    )
                    return intf
        print("ERROR: Did not find interface for {}".format(self.peer_macs[peer_id]))
        self.get_intf[peer_id] = self.get_intf[peer_id] + 1
        return -1

    # Function to get lo ip address of the responder
    def get_lo(self, peer_id):
        print("Getting lo ip address for {}".format(self.peer_macs[peer_id]))
        print(datetime.datetime.now())
        iteration_num = 0
        max_iteration_num = 50
        while iteration_num != max_iteration_num:
            stri = execute(
                ["breeze", "kvstore", "prefixes", "--nodes", "all", "--json"]
            )
            struct = json.loads(stri)
            len_struct = len(struct)

            node_names = []
            raw_node_names = list(struct.keys())
            node_vals = 0
            while node_vals < len_struct:
                value = raw_node_names[node_vals]
                values = str(value[value.index("-") + 1 :]).replace(".", ":")
                node_names.append(values)
                node_vals += 1

            if self.peer_macs[peer_id] in node_names:
                node_pos = node_names.index(self.peer_macs[peer_id])
                raw_peer_lo = struct[raw_node_names[node_pos]]["prefixEntries"][0][
                    "prefix"
                ]
                peer_lo = str(raw_peer_lo[: raw_peer_lo.index("/")]) + "1"
                print(
                    "lo ip address for {} is {}".format(
                        self.peer_macs[peer_id], peer_lo
                    )
                )
                return peer_lo
            else:
                iteration_num += 1
                print(
                    "Failed to get lo ip. "
                    + "Retrying: {}/{}".format(iteration_num, max_iteration_num)
                )
                time.sleep(1)
        print("ERROR: lo ip address not found after {} attempts!".format(iteration_num))
        return -1

    # Function to ping terra interface
    def ping_terra(self, peer_id, interface):
        print(
            "Starting Ping for {} over terra{} interface".format(
                self.peer_macs[peer_id], interface
            )
        )
        found = execute(
            [
                "ping6",
                "-c2",
                "ff02::1%terra{}".format(interface),
                "|",
                "grep",
                "DUP",
                "|",
                "sort",
            ],
            shell=True,
        ).strip()
        if found == "":
            print("ERROR: Terra Ping failed for {}".format(self.peer_macs[peer_id]))
            self.terra_fail[peer_id] = self.terra_fail[peer_id] + 1
            call(["ifconfig", "terra" + str(interface)])
            time.sleep(2)
            return
        else:
            print("Terra Ping found for interface {}: {}".format(interface, found))

    # Function to ping lo interface
    def ping_lo(self, peer_id, peer_lo):
        print("Starting Ping for {} over lo interface".format(self.peer_macs[peer_id]))
        iteration_num = 0
        max_iteration_num = 50
        while iteration_num != max_iteration_num:
            pkt_loss = int(
                execute(
                    [
                        "ping6",
                        str(peer_lo),
                        "-c5.0",
                        "-W",
                        "15",
                        "-i0.2",
                        "-s64",
                        "|",
                        "grep",
                        "-o",
                        "'[0-9]*% packet loss'",
                        "|",
                        "cut",
                        "-d'%'",
                        "-f1",
                    ],
                    shell=True,
                ).strip()
            )
            if pkt_loss > 30:
                print("lo Ping failed with {}% packet loss".format(pkt_loss))
                iteration_num += 1
                print("Retrying: {}/{}".format(iteration_num, max_iteration_num))
                time.sleep(1)
            else:
                print("lo Ping passes with {}% packet loss".format(pkt_loss))
                self.is_pass[peer_id] = self.is_pass[peer_id] + 1
                return
        print("ERROR: lo ip Ping failed after {} attempts!".format(iteration_num))
        self.lo_fail[peer_id] = self.lo_fail[peer_id] + 1

    # Function to toggle the state of a link
    def toggle_link(self, peer_id):
        if self.is_aso[peer_id] == 1:
            print(
                "Link for {} is Associated. Starting Dissociation".format(
                    self.peer_macs[peer_id]
                )
            )
            self.dissoc(peer_id)
        else:
            print(
                "Link for {} is Dissociated. Starting Association".format(
                    self.peer_macs[peer_id]
                )
            )
            self.assoc(peer_id)
            if self.is_aso[peer_id] == 0:
                return
            else:
                time.sleep(4)
                interface = self.mac_to_intf(peer_id)
                if interface == -1:
                    print(
                        "ERROR: Link not up for mac {}".format(self.peer_macs[peer_id])
                    )

                    return
                else:
                    self.ping_terra(peer_id, interface)
                    peer_lo = self.get_lo(peer_id)
                    if peer_lo == -1:
                        return
                    else:
                        self.ping_lo(peer_id, peer_lo)

    # Function to print the status for all links
    def print_status(self):
        self.total_aso_dso[peer_id] = self.total_aso_dso[peer_id] + 1
        print("\n=====================================================")
        print(datetime.datetime.now())
        for i in range(0, self.num_peer):
            print(
                (
                    "Peer: {} IsAso: {} "
                    "AsoFails: {} DsoFails: {} "
                    "GetIntfFails: {} TerraPingFails: {} "
                    "LoPingFails: {} AsoPingPass = {} TotAsoDso: {}"
                ).format(
                    self.peer_macs[i],
                    self.is_aso[i],
                    self.aso_fail[i],
                    self.dso_fail[i],
                    self.get_intf[i],
                    self.terra_fail[i],
                    self.lo_fail[i],
                    self.is_pass[i],
                    self.total_aso_dso[i],
                )
            )
        print("=====================================================\n")


class Get_mac:

    # Init Function
    def __init__(self):
        self.dicts = {}

    # Function to get mac addr of all assoc links at the start of the script
    def get_mac(self, node=None):
        print("Looking for peers...")
        if node is not None:
            adj_cmd = ["breeze", "kvstore", "adj", "--nodes", node, "--json"]
        else:
            adj_cmd = ["breeze", "kvstore", "adj", "--json"]

        stri = execute(adj_cmd)
        struct = json.loads(stri)
        len_struct = len(struct[list(struct)[0]]["adjacencies"])

        peer_macs = []
        keys = range(len_struct)
        j = 0
        for i in keys:
            if (struct[list(struct)[0]]["adjacencies"][i]["ifName"]) != "nic2" and (
                struct[list(struct)[0]]["adjacencies"][i]["ifName"]
            ) != "nic1":
                self.dicts[j] = struct[list(struct)[0]]["adjacencies"][i][
                    "otherNodeName"
                ]
                j += 1
        if len(self.dicts) == 0:
            print(
                "No Associated Peers found!! "
                + "Please associate all links and retry.\n"
            )
            exit(-1)
        dict_keys = 0
        while dict_keys < len(self.dicts):
            value = self.dicts[dict_keys]
            values = str(value[value.index("-") + 1 :]).replace(".", ":")
            print("Found Peer: {}".format(values))
            peer_macs.append(values)
            dict_keys += 1
        print("\n")
        return peer_macs


# Main
if __name__ == "__main__":

    if len(sys.argv) != 1:
        print("This script should be used to do random assoc/dissoc with r2d2 in")
        print("p2mp setups")
        print("This should be run at DN-initiator.")
        print("All links have to be associated before starting this script.")
        exit(0)

    MAX_TRIES = 50
    MAX_WAIT_SECS = 2

    print("\nSTART OF RANDOM ASSOC DISSOC SCRIPT\n")
    get = Get_mac()
    peer_macs = get.get_mac()
    rnd = RandomAssocDisassoc(peer_macs)

    count = 1
    while count <= MAX_TRIES:
        print("Iteration {} of {}...".format(count, MAX_TRIES))
        peer_id = randint(0, rnd.num_peer - 1)
        delay = randint(1, MAX_WAIT_SECS)
        print(
            "Toggle link {} and wait {} seconds".format(rnd.peer_macs[peer_id], delay)
        )
        rnd.toggle_link(peer_id)
        rnd.print_status()
        time.sleep(delay)
        count += 1
    if (
        all(val == 0 for val in rnd.aso_fail)
        and all(val == 0 for val in rnd.terra_fail)
        and all(val == 0 for val in rnd.lo_fail)
        and all(val == 0 for val in rnd.dso_fail)
        and all(val == 0 for val in rnd.get_intf)
    ):
        print("\nTest passed with flying colors!\n")
        print("End of Random Assoc Dissoc Script\n")
        exit(0)
    else:
        print("\nTest failed, Oh the shame...\n")
        print("End of Random Assoc Dissoc Script\n")
        exit(-1)
