#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging

import click
from terragraph_thrift.Controller import ttypes as ctrlTypes
from terragraph_thrift.Topology import ttypes as topoTypes
from tg.commands import base, consts


_log = logging.getLogger(__name__)


class NodeCli(object):
    def __init__(self):
        self.node.add_command(self._setmac, name="setmac")
        self.node.add_command(self._wlanmac, name="wlanmac")
        self.node.add_command(self._add, name="add")
        self.node.add_command(self._del, name="del")
        self.node.add_command(self._rename, name="rename")
        self.node.add_command(self._reboot, name="reboot")
        self.node.add_command(self._restart_minion, name="restart_minion")
        self._wlanmac.add_command(self._add_wlan_macs, name="add")
        self._wlanmac.add_command(self._del_wlan_macs, name="del")
        self._wlanmac.add_command(self._change_wlan_mac, name="change")

    @click.group()
    def node():
        """Add/Modify/Delete/Reboot Nodes"""
        pass

    @click.command()
    @click.option("--name", "-n", type=str, required=True, help="node name")
    @click.option("--mac", "-m", type=str, required=True, help="node's mac address")
    @click.option(
        "--force", is_flag=True, help="Force MAC change regardless of node state"
    )
    @click.pass_obj
    def _setmac(cli_opts, name, mac, force):
        """Set node mac address"""
        NodeCmd(cli_opts, name, mac_addr=mac, force=force)._setmac()

    @click.group()
    @click.option("--name", "-n", type=str, required=True, help="node name")
    @click.pass_obj
    def _wlanmac(cli_opts, name):
        """Add/Delete/Change node's wlan mac addresses"""
        cli_opts.nodeName = name

    @click.command()
    @click.option(
        "--wlan_macs",
        type=str,
        default="",
        required=True,
        help="node wlan mac addresses (separated by ,)",
    )
    @click.pass_obj
    def _add_wlan_macs(cli_opts, wlan_macs):
        """Add node wlan mac addresses"""
        wlan_mac_addrs = [m.strip() for m in wlan_macs.split(",") if m.strip()]
        NodeCmd(
            cli_opts, cli_opts.nodeName, wlan_mac_addrs=wlan_mac_addrs
        )._add_wlan_macs()

    @click.command()
    @click.option(
        "--wlan_macs",
        type=str,
        default="",
        required=True,
        help="node wlan mac addresses (separated by ,)",
    )
    @click.pass_obj
    def _del_wlan_macs(cli_opts, wlan_macs):
        """Delete node wlan mac addresses"""
        wlan_mac_addrs = [m.strip() for m in wlan_macs.split(",") if m.strip()]
        NodeCmd(
            cli_opts, cli_opts.nodeName, wlan_mac_addrs=wlan_mac_addrs
        )._del_wlan_macs()

    @click.command()
    @click.option("--old_mac", type=str, required=True, help="old wlan mac address")
    @click.option("--new_mac", type=str, required=True, help="new wlan mac address")
    @click.option(
        "--force", is_flag=True, help="Force MAC change regardless of node state"
    )
    @click.pass_obj
    def _change_wlan_mac(cli_opts, old_mac, new_mac, force):
        """Change node wlan mac address"""
        NodeCmd(cli_opts, cli_opts.nodeName, force=force)._change_wlan_mac(
            old_mac, new_mac
        )

    @click.command()
    @click.option("--name", "-n", type=str, required=True, help="node name")
    @click.option("--mac", "-m", type=str, help="node mac address")
    @click.option(
        "--wlan_mac_addrs",
        type=str,
        default="",
        required=False,
        help="node wlan mac addresses (separated by ,)",
    )
    @click.option("--site", "-s", type=str, required=True, help="site name")
    @click.option(
        "--node_type",
        default="dn",
        type=click.Choice(["dn", "cn"]),
        help="node type (default=dn)",
    )
    @click.option(
        "--pop_node",
        default=False,
        is_flag=True,
        help="whether this is a POP node (default=False)",
    )
    @click.pass_obj
    def _add(cli_opts, name, mac, wlan_mac_addrs, site, node_type, pop_node):
        """Add a node"""
        wlan_mac_addrs = [m.strip() for m in wlan_mac_addrs.split(",") if m.strip()]
        NodeCmd(
            cli_opts,
            name,
            None,
            mac,
            wlan_mac_addrs,
            site,
            node_type,
            pop_node,
        )._add_node()

    @click.command()
    @click.option("--name", "-n", type=str, required=True, help="node name")
    @click.option(
        "--force",
        is_flag=True,
        help="force to delete a node regardless of the aliveness of \
                  itself or link associated with it",
    )
    @click.pass_obj
    def _del(cli_opts, name, force):
        """Delete a node"""
        NodeCmd(cli_opts, name, force=force)._del_node()

    @click.command()
    @click.option("--name", "-n", type=str, required=True, help="node name")
    @click.option("--new_name", "-r", type=str, required=True, help="new node name")
    @click.pass_obj
    def _rename(cli_opts, name, new_name):
        """Rename a node"""
        NodeCmd(cli_opts, name, new_name)._rename_node()

    @click.command()
    @click.option(
        "--name",
        "-n",
        default="",
        type=str,
        required=False,
        help="Node name list (separated by ,)",
    )
    @click.option("--force", is_flag=True, help="reboot node with option -f")
    @click.option(
        "--delay",
        default=5,
        nargs=1,
        type=click.IntRange(1, 60),
        required=False,
        help="number of seconds to wait before reboot (default 5). \
                  Valid ranges are [1-60]",
    )
    @click.pass_obj
    def _reboot(cli_opts, name, force, delay):
        """Reboot a node(s). If no names are given, will reboot all nodes"""
        NodeCmd(cli_opts, name.replace(",", " ").split(), force=force)._reboot_node(
            delay
        )

    @click.command()
    @click.option(
        "--name",
        "-n",
        default="",
        type=str,
        required=False,
        help="Node name list (separated by ,)",
    )
    @click.option(
        "--delay",
        default=5,
        nargs=1,
        type=click.IntRange(1, 60),
        required=False,
        help="number of seconds to wait before reboot (default 5). \
                  Valid ranges are [1-60]",
    )
    @click.pass_obj
    def _restart_minion(cli_opts, name, delay):
        """Restart minon(s). If no names are given, will restart all minions"""
        NodeCmd(cli_opts, name.replace(",", " ").split())._restart_minion(delay)


class NodeCmd(base.BaseCmd):
    def __init__(
        self,
        cli_opts,
        node_name,
        new_node_name=None,
        mac_addr=None,
        wlan_mac_addrs=None,
        site_name=None,
        node_type=None,
        pop_node=None,
        force=False,
    ):
        base.BaseCmd.__init__(self, cli_opts)
        self._node_name = node_name
        self._new_node_name = new_node_name
        self._site_name = site_name
        self._mac_addr = mac_addr
        self._wlan_mac_addrs = wlan_mac_addrs
        self._pop_node = pop_node
        self._force = force

        if node_type:
            node_type = node_type.upper()
            if node_type not in topoTypes.NodeType._NAMES_TO_VALUES:
                self._my_exit(False, "Invalid node type specified")
            self._node_type = topoTypes.NodeType._NAMES_TO_VALUES[node_type]
        else:
            self._node_type = None

        self._connect_to_controller()

    def _setmac(self):
        set_node_mac = ctrlTypes.SetNodeMac()
        set_node_mac.nodeName = self._node_name
        set_node_mac.nodeMac = self._mac_addr
        set_node_mac.force = self._force
        self._send_to_ctrl(
            ctrlTypes.MessageType.SET_NODE_MAC,
            set_node_mac,
            consts.TOPOLOGY_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _add_wlan_macs(self):
        add_node_wlan_macs = ctrlTypes.AddNodeWlanMacs()
        add_node_wlan_macs.nodeName = self._node_name
        add_node_wlan_macs.wlanMacs = self._wlan_mac_addrs
        self._send_to_ctrl(
            ctrlTypes.MessageType.ADD_NODE_WLAN_MACS,
            add_node_wlan_macs,
            consts.TOPOLOGY_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _del_wlan_macs(self):
        del_node_wlan_macs = ctrlTypes.DelNodeWlanMacs()
        del_node_wlan_macs.nodeName = self._node_name
        del_node_wlan_macs.wlanMacs = self._wlan_mac_addrs
        self._send_to_ctrl(
            ctrlTypes.MessageType.DEL_NODE_WLAN_MACS,
            del_node_wlan_macs,
            consts.TOPOLOGY_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _change_wlan_mac(self, old_wlan_mac, new_wlan_mac):
        change_node_wlan_mac = ctrlTypes.ChangeNodeWlanMac()
        change_node_wlan_mac.nodeName = self._node_name
        change_node_wlan_mac.oldWlanMac = old_wlan_mac
        change_node_wlan_mac.newWlanMac = new_wlan_mac
        change_node_wlan_mac.force = self._force
        self._send_to_ctrl(
            ctrlTypes.MessageType.CHANGE_NODE_WLAN_MAC,
            change_node_wlan_mac,
            consts.TOPOLOGY_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _add_node(self):
        node_to_add = topoTypes.Node()
        node_to_add.name = self._node_name
        node_to_add.mac_addr = self._mac_addr
        node_to_add.wlan_mac_addrs = self._wlan_mac_addrs
        node_to_add.site_name = self._site_name
        node_to_add.node_type = self._node_type
        node_to_add.pop_node = self._pop_node
        self._send_to_ctrl(
            ctrlTypes.MessageType.ADD_NODE,
            ctrlTypes.AddNode(node_to_add),
            consts.TOPOLOGY_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _del_node(self):
        if self._force:
            _log.warn(
                "You are deleting a node in controller which could have "
                + "alive links associated with it in the network. This might "
                + "leave our view of topology in an inconsistent state."
            )
        self._send_to_ctrl(
            ctrlTypes.MessageType.DEL_NODE,
            ctrlTypes.DelNode(self._node_name, self._force),
            consts.TOPOLOGY_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _rename_node(self):
        # Get existing node
        topology = self._get_topology()
        new_node = None
        for n in topology.nodes:
            if n.name == self._node_name:
                new_node = n
                break
        if not new_node:
            self._my_exit(False, "Node not present in topology")

        # Send request with new node name
        new_node.name = self._new_node_name
        self._send_to_ctrl(
            ctrlTypes.MessageType.EDIT_NODE,
            ctrlTypes.EditNode(self._node_name, new_node),
            consts.TOPOLOGY_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _reboot_node(self, delay):
        self._send_to_ctrl(
            ctrlTypes.MessageType.REBOOT_REQUEST,
            ctrlTypes.RebootReq(self._node_name, self._force, delay),
            consts.STATUS_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.STATUS_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _restart_minion(self, delay):
        self._send_to_ctrl(
            ctrlTypes.MessageType.RESTART_MINION_REQUEST,
            ctrlTypes.RestartMinionRequest(self._node_name, delay),
            consts.STATUS_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.STATUS_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)
