#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging

import click
from terragraph_thrift.Controller import ttypes as ctrlTypes
from terragraph_thrift.DriverMessage import ttypes as drvrTypes
from terragraph_thrift.Topology import ttypes as topoTypes
from tg.commands import base, consts


_log = logging.getLogger(__name__)


DriverLinkStatusTypes = [
    x.lower() for x in drvrTypes.DriverLinkStatusType._NAMES_TO_VALUES.keys()
]
LinkDownCauses = [x.lower() for x in drvrTypes.LinkDownCause._NAMES_TO_VALUES.keys()]
NodeTypes = [x.lower() for x in topoTypes.NodeType._NAMES_TO_VALUES.keys()]


class LinkCli(object):
    def __init__(self):
        self.link.add_command(self._list, name="ls")
        self.link.add_command(self._up, name="up")
        self.link.add_command(self._down, name="down")
        self.link.add_command(self._add, name="add")
        self.link.add_command(self._del, name="del")
        self.link.add_command(
            self._send_fake_driver_status, name="send_fake_driver_status"
        )

    @click.group()
    def link():
        """View/Add/Modify/Delete/Assoc/Dissoc Links"""
        pass

    @click.command()
    @click.pass_obj
    @click.option(
        "--include_wired", is_flag=True, help="include information about wired links"
    )
    def _list(cli_opts, include_wired):
        """List all the links"""
        LinkListCmd(cli_opts, include_wired).run()

    @click.command()
    @click.option(
        "--initiator_node", "-i", type=str, required=True, help="initiator node name"
    )
    @click.option(
        "--responder_node", "-r", type=str, required=True, help="responder node name"
    )
    @click.pass_obj
    def _up(cli_opts, initiator_node, responder_node):
        """Bring up a link"""
        LinkStatusCmd(
            cli_opts, initiator_node, responder_node, ctrlTypes.LinkActionType.LINK_UP
        ).run()

    @click.command()
    @click.option(
        "--initiator_node", "-i", type=str, required=True, help="initiator node name"
    )
    @click.option(
        "--responder_node", "-r", type=str, required=True, help="responder node name"
    )
    @click.pass_obj
    def _down(cli_opts, initiator_node, responder_node):
        """Bring down a link"""
        LinkStatusCmd(
            cli_opts, initiator_node, responder_node, ctrlTypes.LinkActionType.LINK_DOWN
        ).run()

    @click.command()
    @click.option(
        "--a_node_name",
        "-a",
        type=str,
        required=True,
        help="node name of the smaller end of the link",
    )
    @click.option(
        "--a_node_mac",
        type=str,
        default="",
        required=False,
        help="wlan mac of the smaller end of the link " "(uses node's mac by default)",
    )
    @click.option(
        "--z_node_name",
        "-z",
        type=str,
        required=True,
        help="node name of the larger end of the link",
    )
    @click.option(
        "--z_node_mac",
        type=str,
        default="",
        required=False,
        help="wlan mac of the larger end of the link " "(uses node's mac by default)",
    )
    @click.option(
        "--wired/--wireless", default=None, required=True, help="wired or wireless link"
    )
    @click.option(
        "--backup_cn_link", is_flag=True, help="whether this is a backup DN-to-CN link"
    )
    @click.pass_obj
    def _add(
        cli_opts,
        a_node_name,
        a_node_mac,
        z_node_name,
        z_node_mac,
        wired,
        backup_cn_link,
    ):
        """Add a link"""
        if a_node_name >= z_node_name:
            raise click.UsageError(
                "Error: %s is not lexicographically "
                "smaller than %s - swap" % (a_node_name, z_node_name)
            )
        AddLinkCmd(
            cli_opts,
            a_node_name,
            a_node_mac,
            z_node_name,
            z_node_mac,
            wired,
            backup_cn_link,
        ).run()

    @click.command()
    @click.option(
        "--a_node_name",
        "-a",
        type=str,
        required=True,
        help="node name of the smaller end of the link",
    )
    @click.option(
        "--z_node_name",
        "-z",
        type=str,
        required=True,
        help="node name of the larger end of the link",
    )
    @click.option(
        "--force",
        is_flag=True,
        help="force to delete a link regardless of its aliveness",
    )
    @click.pass_obj
    def _del(cli_opts, a_node_name, z_node_name, force):
        """Delete a link"""
        if a_node_name >= z_node_name:
            raise click.UsageError(
                "Error: %s is not lexicographically "
                "smaller than %s - swap" % (a_node_name, z_node_name)
            )
        DelLinkCmd(cli_opts, a_node_name, z_node_name, force).run()

    @click.command()
    @click.pass_obj
    @click.option(
        "--node_mac", "-m", type=str, required=True, help="target node ID (MAC address)"
    )
    @click.option(
        "--radio_mac",
        "-r",
        type=str,
        help="target node radio MAC (defaults to --node_mac)",
    )
    @click.option(
        "--peer_mac",
        "-n",
        type=str,
        required=True,
        help="peer (responder) node radio MAC",
    )
    @click.option(
        "--link_status",
        "-s",
        type=click.Choice(DriverLinkStatusTypes),
        required=True,
        help="link status type",
    )
    @click.option(
        "--link_down_cause",
        "-d",
        type=click.Choice(LinkDownCauses),
        help="link down cause (optional)",
    )
    @click.option("--ifname", "-i", type=str, required=True, help="link interface name")
    @click.option(
        "--node_type",
        type=click.Choice(NodeTypes),
        help="target node type (defaults to DN)",
    )
    @click.option(
        "--peer_node_type",
        type=click.Choice(NodeTypes),
        help="peer (responder) node type (defaults to DN)",
    )
    def _send_fake_driver_status(
        cli_opts,
        node_mac,
        radio_mac,
        peer_mac,
        link_status,
        link_down_cause,
        ifname,
        node_type,
        peer_node_type,
    ):
        """Send fake DriverLinkStatus to a minion"""
        SendFakeDriverStatusCmd(
            cli_opts,
            node_mac,
            radio_mac,
            peer_mac,
            link_status,
            link_down_cause,
            ifname,
            node_type,
            peer_node_type,
        ).run()


class LinkListCmd(base.BaseCmd):
    def __init__(self, cli_opts, include_wired):
        base.BaseCmd.__init__(self, cli_opts)
        self._include_wired = include_wired

    def run(self):
        self._connect_to_controller()
        topology = self._get_topology()
        self._display_links(topology.links, self._include_wired)


class LinkStatusCmd(base.BaseCmd):
    def __init__(self, cli_opts, initiator_node, responder_node, action):
        base.BaseCmd.__init__(self, cli_opts)
        self._initiator_node = initiator_node
        self._responder_node = responder_node
        self._action = action

    def run(self):
        self._connect_to_controller()

        set_link_status_req = ctrlTypes.SetLinkStatusReq(
            self._action, self._initiator_node, self._responder_node
        )
        self._send_to_ctrl(
            ctrlTypes.MessageType.SET_LINK_STATUS_REQ,
            set_link_status_req,
            consts.IGNITION_APP_CTRL_ID,
        )

        e2e_ack = self._recv_e2e_ack(consts.IGNITION_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class AddLinkCmd(base.BaseCmd):
    def __init__(
        self,
        cli_opts,
        a_node_name,
        a_node_mac,
        z_node_name,
        z_node_mac,
        wired,
        backup_cn_link,
    ):
        base.BaseCmd.__init__(self, cli_opts)
        self._a_node_name = a_node_name
        self._a_node_mac = a_node_mac
        self._z_node_name = z_node_name
        self._z_node_mac = z_node_mac
        self._wired = wired
        self._backup_cn_link = backup_cn_link

    def run(self):
        self._connect_to_controller()

        link_to_add = topoTypes.Link()
        link_to_add.a_node_name = self._a_node_name
        link_to_add.a_node_mac = self._a_node_mac
        link_to_add.z_node_name = self._z_node_name
        link_to_add.z_node_mac = self._z_node_mac
        link_to_add.is_backup_cn_link = self._backup_cn_link
        if self._wired:
            link_to_add.link_type = topoTypes.LinkType.ETHERNET
            # always mark a wired link alive
            link_to_add.is_alive = True
        else:
            link_to_add.link_type = topoTypes.LinkType.WIRELESS
            link_to_add.is_alive = False
        self._send_to_ctrl(
            ctrlTypes.MessageType.ADD_LINK,
            ctrlTypes.AddLink(link_to_add),
            consts.TOPOLOGY_APP_CTRL_ID,
        )

        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class DelLinkCmd(base.BaseCmd):
    def __init__(self, cli_opts, a_node_name, z_node_name, force):
        base.BaseCmd.__init__(self, cli_opts)
        self._a_node_name = a_node_name
        self._z_node_name = z_node_name
        self._force = force

    def run(self):
        self._connect_to_controller()
        if self._force:
            _log.warn(
                "You are deleting a link in controller which could be alive "
                + "in the network. This might leave our view of topology in "
                + "an inconsistent state."
            )
        self._send_to_ctrl(
            ctrlTypes.MessageType.DEL_LINK,
            ctrlTypes.DelLink(self._a_node_name, self._z_node_name, self._force),
            consts.TOPOLOGY_APP_CTRL_ID,
        )

        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class SendFakeDriverStatusCmd(base.BaseCmd):
    def __init__(
        self,
        cli_opts,
        node_mac,
        radio_mac,
        peer_mac,
        link_status,
        link_down_cause,
        ifname,
        node_type,
        peer_node_type,
    ):
        base.BaseCmd.__init__(self, cli_opts)
        self._node_mac = node_mac
        self._radio_mac = radio_mac if radio_mac else node_mac
        self._msg = drvrTypes.DriverLinkStatus()
        self._msg.valid = True
        self._msg.macAddr = peer_mac
        self._msg.drLinkStatusType = drvrTypes.DriverLinkStatusType._NAMES_TO_VALUES[
            link_status.upper()
        ]
        self._msg.linkDownCause = (
            drvrTypes.LinkDownCause._NAMES_TO_VALUES[link_down_cause.upper()]
            if link_down_cause
            else drvrTypes.LinkDownCause.NOT_APPLICABLE
        )
        self._msg.ifname = ifname
        self._msg.selfNodeType = (
            topoTypes.NodeType._NAMES_TO_VALUES[node_type.upper()]
            if node_type
            else topoTypes.NodeType.DN
        )
        self._msg.peerNodeType = (
            topoTypes.NodeType._NAMES_TO_VALUES[peer_node_type.upper()]
            if peer_node_type
            else topoTypes.NodeType.DN
        )

    def run(self):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.DR_LINK_STATUS,
            self._serialize_driver_msg(self._msg, self._radio_mac),
            consts.IGNITION_APP_MINION_ID,
            self._node_mac,
        )
