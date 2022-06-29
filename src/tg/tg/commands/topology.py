#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import itertools
import json
import logging
import operator

import click
import tabulate
from terragraph_thrift.Controller import ttypes as ctrlTypes
from terragraph_thrift.Topology import ttypes as topoTypes
from tg.commands import base, consts


_log = logging.getLogger(__name__)


# save Topology to file in json format
def saveTopologyToJson(topology, topology_file):
    try:
        topology_json = base.serialize_to_json(topology).decode("utf-8")
        with open(topology_file, "w") as f:
            print(topology_json, file=f)
    except Exception as e:
        print("Writing to file {} failed: {} ({})".format(topology_file, e, type(e)))


class TopologyCli(object):
    def __init__(self):
        self.topology.add_command(self._ls, name="ls")
        self.topology.add_command(self._validate, name="validate")
        self.topology.add_command(self._sanitize, name="sanitize")
        self.topology.add_command(self._reset, name="reset")
        self.topology.add_command(self._set_name, name="set_name")
        self.topology.add_command(self._time, name="time")

        self.topology.add_command(self._prefixes, name="prefixes")
        self._prefixes.add_command(self._get_prefixes, name="get")
        self._prefixes.add_command(self._allocate_prefixes, name="allocate")

    @click.group()
    def topology():
        """ View/Validate/Sanitize/Manipulate topology """
        pass

    @click.command()
    @click.pass_obj
    @click.option(
        "--raw", is_flag=True, help="don't format, print the raw thrift structures"
    )
    @click.option(
        "--include_wired", is_flag=True, help="include information about wired links"
    )
    @click.option(
        "--json",
        type=str,
        default="",
        help="dump current topology into json file as specified",
    )
    def _ls(cli_opts, raw, include_wired, json):
        """ View current topology """
        TopoListCmd(cli_opts, raw, include_wired, json).run()

    @click.command()
    @click.pass_obj
    @click.option(
        "--link_state_db",
        type=click.File("r"),
        default="lsdb.json",
        required=True,
        help="Link State Db file (breeze decision adj --json)",
    )
    def _validate(cli_opts, link_state_db):
        """ Validate E2E topology against OpenR Link State Db """
        ValidateCmd(cli_opts, link_state_db).run()

    @click.command()
    @click.pass_obj
    @click.option(
        "--topology_file",
        type=click.Path(exists=True),
        required=True,
        help="E2E topology file needed by controller",
    )
    def _sanitize(cli_opts, topology_file):
        """ Sanitize E2E topology file and write the sanitized
            topology in a new file postfixed by '.sanitized'.
            Fow now, we support
            1) order nodes in each link lexically if they are not ordered yet
            2) add wire links among all nodes at the same site
            """
        SanitizeCmd(cli_opts, topology_file).run()

    @click.command()
    @click.option(
        "--linkup_attempts", is_flag=True, help="Reset linkup attempts counter"
    )
    @click.pass_obj
    def _reset(cli_opts, linkup_attempts):
        """ Reset dynamic information in Topology
            e.g. linkup_attempts
        """
        ResetCmd(cli_opts, linkup_attempts).run()

    @click.command()
    @click.option("--name", "-n", type=str, required=True, help="New topology name")
    @click.pass_obj
    def _set_name(cli_opts, name):
        """ Set topology name """
        SetTopologyNameCmd(cli_opts, name).run()

    @click.command()
    @click.pass_obj
    def _time(cli_opts):
        """ Get controller UNIX and GPS timestamps"""
        GetTime(cli_opts).run()

    @click.group()
    @click.pass_obj
    def _prefixes(cli_opts):
        """ View/Modify topology prefix information"""
        pass

    @click.command()
    @click.option(
        "--type",
        "-t",
        default="node",
        type=click.Choice(["node", "zone"]),
        help="Zone or Node prefixes",
    )
    @click.pass_obj
    def _get_prefixes(cli_opts, type):
        """Get prefixes assigned to nodes/zones"""
        PrefixesCmd(cli_opts).get_prefixes(type)

    @click.command()
    @click.pass_obj
    def _allocate_prefixes(cli_opts):
        """Reallocate prefixes assigned to nodes/zones"""
        PrefixesCmd(cli_opts).allocate()


class PrefixesCmd(base.BaseCmd):
    def __init__(self, cli_opts):
        base.BaseCmd.__init__(self, cli_opts)

    def get_prefixes(self, type):
        self._connect_to_controller()
        if type == "node":
            msg_type = ctrlTypes.MessageType.GET_NODE_PREFIXES
            msg = ctrlTypes.GetNodePrefixes()
            recv_msg_type = ctrlTypes.MessageType.GET_NODE_PREFIXES_RESP
            response = ctrlTypes.GetNodePrefixesResp()
        elif type == "zone":
            msg_type = ctrlTypes.MessageType.GET_ZONE_PREFIXES
            msg = ctrlTypes.GetZonePrefixes()
            recv_msg_type = ctrlTypes.MessageType.GET_ZONE_PREFIXES_RESP
            response = ctrlTypes.GetZonePrefixesResp()

        ackResponse = ctrlTypes.E2EAck()
        self._send_to_ctrl(msg_type, msg, consts.TOPOLOGY_APP_CTRL_ID)
        recv_type = self._recv_multi_from_ctrl(
            [(recv_msg_type, response), (ctrlTypes.MessageType.E2E_ACK, ackResponse)],
            consts.TOPOLOGY_APP_CTRL_ID,
        )

        if recv_type == recv_msg_type:
            return print(response)
        else:
            self._my_exit(False, ackResponse.message)

    def allocate(self):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.ALLOCATE_PREFIXES,
            ctrlTypes.AllocatePrefixes(),
            consts.TOPOLOGY_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class TopoListCmd(base.BaseCmd):
    def __init__(self, cli_opts, raw, include_wired, json):
        base.BaseCmd.__init__(self, cli_opts)
        self._raw = raw
        self._include_wired = include_wired
        self._json = json

    def run(self):
        self._connect_to_controller()
        topology = self._get_topology()
        if self._raw:
            print(topology)
        else:
            self._display_nodes(topology.nodes)
            self._display_links(topology.links, self._include_wired)
            self._display_sites(topology.sites)

        if self._json:
            saveTopologyToJson(topology, self._json)


class ValidateCmd(base.BaseCmd):
    def __init__(self, cli_opts, link_state_db):
        base.BaseCmd.__init__(self, cli_opts)
        self._link_state_db = link_state_db

    def _e2e_link_type_get(self, link_type):
        if link_type == 1:
            return "WIRELESS"
        elif link_type == 2:
            return "ETHERNET"
        else:
            return str(link_type)

    def _openr_link_type_get(self, ifname):
        if ifname.startswith("terra"):
            return "WIRELESS"
        elif ifname.startswith("nic"):
            return "ETHERNET"
        else:
            return ifname

    # node names are node-<mac-addr> but '.' instead of ':'
    def _get_mac(self, node):
        return node.replace("node-", "").replace(".", ":").lower()

    # openr node names are from mac addressess
    def _openr_name_to_e2e_name(self, openr_name):
        if self._get_mac(openr_name) in self._mac_to_e2e_node_name:
            return self._mac_to_e2e_node_name[self._get_mac(openr_name)]
        else:
            return openr_name

    def _get_links_table(self, links_table):
        _links_table = []
        _links_header = ["Source", "Destination", "LinkType"]
        for node, links in links_table.items():
            for link in links:
                _links_table.append([node, link[0], link[1]])
        _links_table = sorted(_links_table, key=operator.itemgetter(0))
        return tabulate.tabulate(_links_table, _links_header)

    def _get_lsdb_table(self, lsdb):
        _lsdb_table = []
        _lsdb_header = []
        for node, data in lsdb.items():
            for adj in data["adjacencies"]:
                if not _lsdb_header:
                    _lsdb_header = ["Source"] + adj.keys()
                _lsdb_table.append([node] + adj.values())
        return tabulate.tabulate(_lsdb_table, _lsdb_header)

    def run(self):
        self._connect_to_controller()
        topology = self._get_topology()
        lsdb = json.loads(self._link_state_db.read())

        _log.debug("Link-State DB")
        _log.debug("\n" + self._get_lsdb_table(lsdb) + "\n")

        self._mac_to_e2e_node_name = {
            node.mac_addr.lower(): node.name for node in topology.nodes
        }

        _log.debug("Mapping of mac-addr to Node Names")
        _log.debug(
            "\n"
            + tabulate.tabulate(
                self._mac_to_e2e_node_name.items(), ["Mac", "Node-Name"]
            )
            + "\n"
        )

        # map(source, set(destination, link-type)
        e2e_links = {}
        for link in topology.links:
            src_node = link.a_node_name
            dst_node = link.z_node_name
            link_type = self._e2e_link_type_get(link.link_type)
            e2e_links.setdefault(src_node, set()).add((dst_node, link_type))
            # Add the reverse direction link
            e2e_links.setdefault(dst_node, set()).add((src_node, link_type))

        # map(source, set(destination, link-type))
        # lsdb is map(source, list(adjacencies))
        # NOTE:  lsdb is bidirectional
        lsdb_links = {}
        for node, val in lsdb.items():
            src_node = self._openr_name_to_e2e_name(node)
            for adj in val["adjacencies"]:
                dst_node = self._openr_name_to_e2e_name(adj["otherNodeName"])
                link_type = self._openr_link_type_get(adj["ifName"])
                lsdb_links.setdefault(src_node, set()).add((dst_node, link_type))

        _log.debug("Links from Topology File")
        _log.debug("\n" + self._get_links_table(e2e_links) + "\n")
        _log.debug("Links from Link State Db")
        _log.debug("\n" + self._get_links_table(lsdb_links) + "\n")

        links_match = True

        not_in_lsdb = set(e2e_links.keys()) - set(lsdb_links.keys())
        not_in_e2e = set(lsdb_links.keys()) - set(e2e_links.keys())
        valid_nodes = set(lsdb_links.keys()).intersection(e2e_links.keys())
        _log.debug("Common nodes in both")
        _log.debug(
            "\n" + tabulate.tabulate(list([_] for _ in valid_nodes), ["Nodes"]) + "\n"
        )

        # Missing source nodes in either DB
        if not_in_lsdb:
            links_match = False
            print()
            print("Missing links in link-state from nodes")
            print(tabulate.tabulate(list([_] for _ in not_in_lsdb), ["Nodes"]))

        if not_in_e2e:
            links_match = False
            print()
            print("Missing links in Topology from nodes")
            print(tabulate.tabulate([list(not_in_e2e)], ["Nodes"]))

        # For intersecting nodes (present in both dbs) check the links
        not_in_e2e_table = {}
        not_in_lsdb_table = {}
        print()
        for node, links in e2e_links.items():
            if node not in valid_nodes:
                continue
            # missing links
            not_in_lsdb = links - lsdb_links[node]
            not_in_e2e = lsdb_links[node] - links

            if not_in_lsdb:
                links_match = False
                not_in_lsdb_table[node] = list(not_in_lsdb)

            if not_in_e2e:
                links_match = False
                not_in_e2e_table[node] = list(not_in_e2e)

        if not_in_lsdb_table:
            print("Missing links in link-state")
            print(self._get_links_table(not_in_lsdb_table))
            print()

        if not_in_e2e_table:
            print("Missing links in Topology")
            print(self._get_links_table(not_in_e2e_table))
            print()

        if links_match:
            print("PASS. E2E and Link-State DB match. Nothing to report")


class SanitizeCmd(base.BaseCmd):
    def __init__(self, cli_opts, topology_file):
        base.BaseCmd.__init__(self, cli_opts)
        self._topology_file = topology_file

    def run(self):
        try:
            with open(self._topology_file, "r") as f:
                thrift_json = f.read()
            topology = topoTypes.Topology()
            topology.readFromJson(thrift_json)
        except Exception:
            print("Empty file or invalid content in {}".format(self._topology_file))
            return

        # reorder a/z_node of a link if they are out of order
        for link in topology.links:
            # out of order
            if link.a_node_name > link.z_node_name:
                print(
                    "Swapping {} and {} in a link into lexical ordering".format(
                        link.a_node_name, link.z_node_name
                    )
                )
                # swap
                link.a_node_name, link.z_node_name = (
                    link.z_node_name,
                    link.a_node_name,
                )
                link.name = "link-{}-{}".format(link.a_node_name, link.z_node_name)

        # add links between all nodes on a site if they are missing
        link_names = [link.name for link in topology.links]
        for site in topology.sites:
            # get all nodes at this site
            node_names = [
                node.name for node in topology.nodes if node.site_name == site.name
            ]
            # order them
            node_names.sort()
            # get all combo of pair of node names, sorted
            links = itertools.combinations(node_names, 2)
            for link in links:
                a_node_name, z_node_name = link
                assert a_node_name < z_node_name, "a node larger than z node"
                link_name = "link-{}-{}".format(a_node_name, z_node_name)
                # missing?
                if link_name in link_names:
                    continue
                print(
                    "Add missing wired link {} at site {}".format(link_name, site.name)
                )
                link = topoTypes.Link()
                link.name = link_name
                link.a_node_name = a_node_name
                link.z_node_name = z_node_name
                link.link_type = topoTypes.LinkType.ETHERNET
                link.is_alive = True
                topology.links.append(link)

        topology_file_sanitized = self._topology_file + ".sanitized"
        print("Writing sanitized topology to {}".format(topology_file_sanitized))
        saveTopologyToJson(topology, topology_file_sanitized)


class ResetCmd(base.BaseCmd):
    def __init__(self, cli_opts, linkup_attempts):
        base.BaseCmd.__init__(self, cli_opts)
        self._linkup_attempts = linkup_attempts

    def run(self):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.RESET_TOPOLOGY_STATE,
            ctrlTypes.ResetTopologyState(self._linkup_attempts),
            consts.TOPOLOGY_APP_CTRL_ID,
        )


class GetTime(base.BaseCmd):
    def __init__(self, cli_opts):
        base.BaseCmd.__init__(self, cli_opts)

    def run(self):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_GPS_TIME,
            ctrlTypes.GetGpsTime(),
            consts.STATUS_APP_CTRL_ID,
        )
        gpsTime = ctrlTypes.GpsTime()
        self._recv_from_ctrl(
            ctrlTypes.MessageType.GPS_TIME, gpsTime, consts.STATUS_APP_CTRL_ID
        )
        print(gpsTime)


class SetTopologyNameCmd(base.BaseCmd):
    def __init__(self, cli_opts, name):
        base.BaseCmd.__init__(self, cli_opts)
        self._name = name

    def run(self):
        self._connect_to_controller()

        self._send_to_ctrl(
            ctrlTypes.MessageType.SET_TOPOLOGY_NAME,
            ctrlTypes.SetTopologyName(self._name),
            consts.TOPOLOGY_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)
