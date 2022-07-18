#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging
import time

import click
from terragraph_thrift.Controller import ttypes as ctrlTypes
from terragraph_thrift.Topology import ttypes as topoTypes
from tg.commands import base, consts


_log = logging.getLogger(__name__)


class IgnitionCli(object):
    def __init__(self):
        self.ignition.add_command(self._auto, name="auto")
        self.ignition.add_command(self._link, name="link")
        self.ignition.add_command(self._state, name="state")
        self.ignition.add_command(self._extinguish, name="extinguish")

    @click.group()
    def ignition():
        """Modify/Query ignition parameters"""
        pass

    @click.command()
    @click.option(
        "--enable/--disable",
        default=None,
        help="enable auto-ignition at the controller",
    )
    @click.option(
        "--linkup_interval",
        default=None,
        type=int,
        help="linkup timeout interval to control ignition frequency",
    )
    @click.option(
        "--linkup_dampen_interval",
        default=None,
        type=int,
        help="dampen interval to control ignition frequency on same link",
    )
    @click.option(
        "--bf_timeout",
        default=None,
        type=int,
        help="ignition timeout on each minion (in seconds)",
    )
    @click.pass_obj
    def _auto(cli_opts, enable, linkup_interval, linkup_dampen_interval, bf_timeout):
        """Change network auto-ignition parameters"""
        if all(
            op is None
            for op in [enable, linkup_interval, linkup_dampen_interval, bf_timeout]
        ):
            raise click.UsageError("Please use one of the options")
        IgnitionAutoCmd(
            cli_opts, enable, linkup_interval, linkup_dampen_interval, bf_timeout
        ).run()

    @click.command()
    @click.option("--name", "-n", type=str, required=True, help="link name")
    @click.option(
        "--enable/--disable",
        default=None,
        help="enable link auto-ignition at the controller",
    )
    @click.pass_obj
    def _link(cli_opts, name, enable):
        """Change link auto-ignition state"""
        if enable is None:
            IgnitionStateCmd(cli_opts, name).run()
        else:
            IgnitionLinkCmd(cli_opts, name, enable).run()

    @click.command()
    @click.pass_obj
    def _state(cli_opts):
        """Dump ignition state at the controller"""
        IgnitionStateCmd(cli_opts, None).run()

    @click.command()
    @click.option(
        "--link_down_interval",
        default=5,
        type=int,
        help="seconds between successive link downs",
    )
    @click.pass_obj
    def _extinguish(cli_opts, link_down_interval):
        """bring all wireless links down, must disable auto-ignition before"""
        ExtinguishCmd(cli_opts, link_down_interval).run()


class IgnitionAutoCmd(base.BaseCmd):
    def __init__(
        self, cli_opts, enable, linkup_interval, linkup_dampen_interval, bf_timeout
    ):
        base.BaseCmd.__init__(self, cli_opts)
        self._enable = enable
        self._linkup_interval = linkup_interval
        self._linkup_dampen_interval = linkup_dampen_interval
        self._bf_timeout = bf_timeout

    def run(self):
        self._connect_to_controller()

        self._send_to_ctrl(
            ctrlTypes.MessageType.SET_IGNITION_PARAMS,
            ctrlTypes.IgnitionParams(
                self._enable,
                self._linkup_interval,
                self._linkup_dampen_interval,
                None,
                self._bf_timeout,
            ),
            consts.IGNITION_APP_CTRL_ID,
        )

        e2e_ack = self._recv_e2e_ack(consts.IGNITION_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class IgnitionLinkCmd(base.BaseCmd):
    def __init__(self, cli_opts, name, enable):
        base.BaseCmd.__init__(self, cli_opts)
        self._name = name
        self._enable = enable

    def run(self):
        self._connect_to_controller()

        self._send_to_ctrl(
            ctrlTypes.MessageType.SET_IGNITION_PARAMS,
            ctrlTypes.IgnitionParams(None, None, None, {self._name: self._enable}),
            consts.IGNITION_APP_CTRL_ID,
        )

        e2e_ack = self._recv_e2e_ack(consts.IGNITION_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class IgnitionStateCmd(base.BaseCmd):
    def __init__(self, cli_opts, link_name):
        base.BaseCmd.__init__(self, cli_opts)
        self._link_name = link_name

    def run(self):
        self._connect_to_controller()
        ignition_state = self._get_ignition_state()

        if self._link_name is not None:
            _log.info(
                "Network-wide auto ignition is: %s",
                "ON" if ignition_state.igParams.enable else "OFF",
            )
            if self._link_name in ignition_state.igParams.linkAutoIgnite.keys():
                _log.info("link auto ignition is OFF")
            else:
                _log.info("link auto ignition is ON")
        else:
            _log.info(ignition_state)


class ExtinguishCmd(base.BaseCmd):
    def __init__(self, cli_opts, link_down_interval):
        base.BaseCmd.__init__(self, cli_opts)
        self._link_down_interval = link_down_interval

    def run(self):
        self._connect_to_controller()
        topology = self._get_topology()
        name_2_link = {link.name: link for link in topology.links}
        name_2_node = {node.name: node for node in topology.nodes}
        rev_bfs_initiators, rev_bfs_links = self._get_bfs_links(topology)
        rev_bfs_links.reverse()
        rev_bfs_initiators.reverse()
        for idx in range(len(rev_bfs_links)):
            # figure out initiator and responder
            # skip link_down for non-wireless links
            # skip link_down if already down
            # use rev_bfs_initiators as initiator and the other as responder
            # try link_down even if just initiator is offline
            link = name_2_link[rev_bfs_links[idx]]
            if link.link_type != topoTypes.LinkType.WIRELESS:
                continue
            if not link.is_alive:
                _log.info("link %s is not alive, don't send link_down", link.name)
                continue
            initiator = name_2_node[rev_bfs_initiators[idx]]
            if initiator.name == link.a_node_name:
                responder = name_2_node[link.z_node_name]
            else:
                responder = name_2_node[link.a_node_name]
            if initiator.status == topoTypes.NodeStatusType.OFFLINE:
                _log.info("initiator %s is offline", initiator.name)
                if (
                    responder.status == topoTypes.NodeStatusType.OFFLINE
                    or responder.nodeType == topoTypes.NodeType.CN
                ):
                    _log.info(
                        "responder %s, is either offline or cn, "
                        "so don't send link_down for link %s",
                        responder.name,
                        link.name,
                    )
                    continue
                else:
                    _log.info("swap initiator/responder before sending link_down")
                    responder, initiator = initiator, responder
            set_link_status_req = ctrlTypes.SetLinkStatusReq(
                ctrlTypes.LinkActionType.LINK_DOWN, initiator.name, responder.name
            )
            self._send_to_ctrl(
                ctrlTypes.MessageType.SET_LINK_STATUS_REQ,
                set_link_status_req,
                consts.IGNITION_APP_CTRL_ID,
            )
            e2e_ack = self._recv_e2e_ack(consts.IGNITION_APP_CTRL_ID)
            _log.info("%s: %s", link.name, e2e_ack.message)
            time.sleep(self._link_down_interval)

    def _get_bfs_links(self, topology):
        # bfs_links is list of all links in bfs order
        # bfs_initiators is first bfs node for each link
        bfs_links = []
        bfs_initiators = []
        # start with anchor_nodes = pop nodes
        # move anchor_nodes --> bfs_initiators/links
        anchor_nodes = [node.name for node in topology.nodes if node.pop_node]
        while len(anchor_nodes) > 0:
            node = anchor_nodes.pop(0)
            n_nodes, n_links = self._get_neighbors(topology, node)
            anchor_nodes.extend(
                [neighbor for neighbor in n_nodes if neighbor not in bfs_initiators]
            )
            new_links = [link for link in n_links if link not in bfs_links]
            bfs_links.extend(new_links)
            bfs_initiators.extend([node] * len(new_links))
        return bfs_initiators, bfs_links

    def _get_neighbors(self, topology, node_name):
        neighbors = []
        links = []
        for link in topology.links:
            if link.a_node_name == node_name:
                neighbors.append(link.z_node_name)
                links.append(link.name)
            if link.z_node_name == node_name:
                neighbors.append(link.a_node_name)
                links.append(link.name)
        return neighbors, links
