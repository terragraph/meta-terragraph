#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import json
import logging
from io import StringIO

import click
from terragraph_thrift.Aggregator import ttypes as aggrTypes
from terragraph_thrift.Controller import ttypes as ctrlTypes
from tg.commands import base, consts


_log = logging.getLogger(__name__)


class ConfigCli(object):
    def __init__(self):
        self.config.add_command(self._get, name="get")
        self.config.add_command(self._set, name="set")
        self.config.add_command(self._modify, name="modify")
        self.config.add_command(self._metadata, name="metadata")
        self._get.add_command(self._getNode, name="node")
        self._get.add_command(self._getNetwork, name="network")
        self._get.add_command(self._getBase, name="base")
        self._get.add_command(self._getFirmwareBase, name="fw")
        self._get.add_command(self._getHardwareBase, name="hw")
        self._get.add_command(self._getController, name="controller")
        self._get.add_command(self._getAggregator, name="aggregator")
        self._getNode.add_command(self._getNodeFull, name="full")
        self._getNode.add_command(self._getNodeOverrides, name="overrides")
        self._getNode.add_command(self._getAutoNodeOverrides, name="automated")
        self._getNetwork.add_command(self._getNetworkOverrides, name="overrides")
        self._set.add_command(self._setNode, name="node")
        self._set.add_command(self._setNetwork, name="network")
        self._setNode.add_command(self._setNodeOverrides, name="overrides")
        self._setNetwork.add_command(self._setNetworkOverrides, name="overrides")
        self._modify.add_command(self._modifyNode, name="node")
        self._modify.add_command(self._modifyNetwork, name="network")
        self._modify.add_command(self._modifyController, name="controller")
        self._modify.add_command(self._modifyAggregator, name="aggregator")
        self._metadata.add_command(self._metadataNode, name="node")
        self._metadata.add_command(self._metadataController, name="controller")
        self._metadata.add_command(self._metadataAggregator, name="aggregator")

    @click.group()
    def config():
        """Config management CLI"""
        pass

    @click.group()
    @click.pass_obj
    def _get(cli_opts):
        """Get node(s)/network config"""
        pass

    @click.group()
    @click.pass_obj
    def _set(cli_opts):
        """Set node(s)/network config"""
        pass

    @click.group()
    @click.option(
        "--nodes",
        "-n",
        default="",
        type=str,
        required=False,
        help="Node name list (separated by ,)",
    )
    @click.option(
        "--macs",
        "-m",
        default="",
        type=str,
        required=False,
        help="Node MAC list (separated by ,) " "[MAY BE REMOVED IN A FUTURE RELEASE]",
    )
    @click.pass_obj
    def _getNode(cli_opts, nodes, macs):
        """Get node(s) config"""
        cli_opts.nodes = [n.strip() for n in nodes.split(",") if n.strip()]
        cli_opts.macs = [m.strip() for m in macs.split(",") if m.strip()]

    @click.group()
    @click.pass_obj
    def _getNetwork(cli_opts):
        """Get network config"""
        pass

    @click.command()
    @click.option(
        "--versions",
        "-v",
        default="",
        type=str,
        required=False,
        help="Software versions for base (separated by ,)",
    )
    @click.pass_obj
    def _getBase(cli_opts, versions):
        """Get base configs"""
        sw = versions.split(",") if versions else []
        ConfigCmd(cli_opts)._getBaseCmd(sw)

    @click.command()
    @click.option(
        "--versions",
        "-v",
        default="",
        type=str,
        required=False,
        help="Firmware versions for firmware base (separated by ,)",
    )
    @click.pass_obj
    def _getFirmwareBase(cli_opts, versions):
        """Get firmware base configs"""
        fw = versions.split(",") if versions else []
        ConfigCmd(cli_opts)._getFirmwareBaseCmd(fw)

    @click.command()
    @click.option(
        "--hardware",
        "-h",
        default="",
        type=str,
        required=False,
        help="Hardware board IDs for hardware base (separated by ,)",
    )
    @click.option(
        "--versions",
        "-v",
        default="",
        type=str,
        required=False,
        help="Software versions for hardware base (separated by ,)",
    )
    @click.pass_obj
    def _getHardwareBase(cli_opts, hardware, versions):
        """Get hardware base configs"""
        hw = hardware.split(",") if hardware else []
        sw = versions.split(",") if versions else []
        ConfigCmd(cli_opts)._getHardwareBaseCmd(hw, sw)

    @click.command()
    @click.pass_obj
    def _getController(cli_opts):
        """Get controller config"""
        ConfigCmd(cli_opts)._getControllerConfigCmd()

    @click.command()
    @click.pass_obj
    def _getAggregator(cli_opts):
        """Get aggregator config"""
        ConfigCmd(cli_opts, connect_to_ctrl=False)._getAggregatorConfigCmd()

    @click.command()
    @click.option(
        "--version", "-v", type=str, required=False, help="Base software version to use"
    )
    @click.option(
        "--hardware", "-h", type=str, required=False, help="Hardware board ID to use"
    )
    @click.option(
        "--firmware", "-f", type=str, required=False, help="Firmware version to use"
    )
    @click.option(
        "--output",
        "-o",
        default="",
        type=str,
        required=False,
        help="Output file path to write the config JSON",
    )
    @click.pass_obj
    def _getNodeFull(cli_opts, version, hardware, firmware, output):
        """Get node(s) full config"""
        ConfigCmd(cli_opts)._getNodeFullCmd(version, hardware, firmware, output)

    @click.command()
    @click.pass_obj
    def _getNodeOverrides(cli_opts):
        """Get node(s) config overrides"""
        ConfigCmd(cli_opts)._getNodeOverridesCmd()

    @click.command()
    @click.pass_obj
    def _getAutoNodeOverrides(cli_opts):
        """Get node(s) automated config overrides"""
        ConfigCmd(cli_opts)._getAutoNodeOverridesCmd()

    @click.command()
    @click.pass_obj
    def _getNetworkOverrides(cli_opts):
        """Get network config overrides"""
        ConfigCmd(cli_opts)._getNetworkOverridesCmd()

    @click.group()
    @click.pass_obj
    def _setNode(cli_opts):
        """Set node config"""
        pass

    @click.command()
    @click.pass_obj
    @click.option(
        "--overrides_file",
        type=click.Path(exists=True),
        required=True,
        help="Node config overrides json file",
    )
    def _setNodeOverrides(cli_opts, overrides_file):
        """Set node config overrides"""
        ConfigCmd(cli_opts)._setNodeOverridesCmd(overrides_file)

    @click.group()
    @click.pass_obj
    def _setNetwork(cli_opts):
        """Set network config"""
        pass

    @click.command()
    @click.pass_obj
    @click.option(
        "--overrides_file",
        type=click.Path(exists=True),
        required=True,
        help="Network config overrides json file",
    )
    def _setNetworkOverrides(cli_opts, overrides_file):
        """Set network config overrides"""
        ConfigCmd(cli_opts)._setNetworkOverridesCmd(overrides_file)

    @click.group()
    @click.pass_obj
    def _modify(cli_opts):
        """Modify node/network config overrides"""
        pass

    @click.command()
    @click.pass_obj
    @click.option(
        "--node", "-n", type=str, default="", required=False, help="Node name"
    )
    @click.option(
        "--mac",
        "-m",
        type=str,
        default="",
        required=False,
        help="Node MAC [MAY BE REMOVED IN A FUTURE RELEASE]",
    )
    @click.option(
        "--add_int_val",
        "-i",
        type=(str, int),
        multiple=True,
        help="Add integer key-value pairs",
    )
    @click.option(
        "--add_float_val",
        "-f",
        type=(str, float),
        multiple=True,
        help="Add floating-point key-value pairs",
    )
    @click.option(
        "--add_str_val",
        "-s",
        type=(str, str),
        multiple=True,
        help="Add string key-value pairs",
    )
    @click.option(
        "--add_bool_val",
        "-b",
        type=(str, bool),
        multiple=True,
        help="Add boolean key-value pairs",
    )
    @click.option(
        "--delete_key", "-d", type=str, multiple=True, help="Delete existing key"
    )
    def _modifyNode(
        cli_opts,
        node,
        mac,
        add_int_val,
        add_float_val,
        add_str_val,
        add_bool_val,
        delete_key,
    ):
        """Modify node config overrides.

        The config key name is a text traversal of the JSON tree with dot
        delimiters (e.g. radioParamsBase.fwParams.wsecEnable).
        """
        ConfigCmd(cli_opts)._modifyNodeOverridesCmd(
            node, mac, add_int_val, add_float_val, add_str_val, add_bool_val, delete_key
        )

    @click.command()
    @click.pass_obj
    @click.option(
        "--add_int_val",
        "-i",
        type=(str, int),
        multiple=True,
        help="Add integer key-value pairs",
    )
    @click.option(
        "--add_float_val",
        "-f",
        type=(str, float),
        multiple=True,
        help="Add floating-point key-value pairs",
    )
    @click.option(
        "--add_str_val",
        "-s",
        type=(str, str),
        multiple=True,
        help="Add string key-value pairs",
    )
    @click.option(
        "--add_bool_val",
        "-b",
        type=(str, bool),
        multiple=True,
        help="Add boolean key-value pairs",
    )
    @click.option(
        "--delete_key", "-d", type=str, multiple=True, help="Delete existing key"
    )
    def _modifyNetwork(
        cli_opts, add_int_val, add_float_val, add_str_val, add_bool_val, delete_key
    ):
        """Modify network config overrides.

        The config key name is a text traversal of the JSON tree with dot
        delimiters (e.g. radioParamsBase.fwParams.wsecEnable).
        """
        ConfigCmd(cli_opts)._modifyNetworkOverridesCmd(
            add_int_val, add_float_val, add_str_val, add_bool_val, delete_key
        )

    @click.command()
    @click.pass_obj
    @click.option(
        "--add_int_val",
        "-i",
        type=(str, int),
        multiple=True,
        help="Add integer key-value pairs",
    )
    @click.option(
        "--add_float_val",
        "-f",
        type=(str, float),
        multiple=True,
        help="Add floating-point key-value pairs",
    )
    @click.option(
        "--add_str_val",
        "-s",
        type=(str, str),
        multiple=True,
        help="Add string key-value pairs",
    )
    @click.option(
        "--add_bool_val",
        "-b",
        type=(str, bool),
        multiple=True,
        help="Add boolean key-value pairs",
    )
    @click.option(
        "--delete_key", "-d", type=str, multiple=True, help="Delete existing key"
    )
    def _modifyController(
        cli_opts, add_int_val, add_float_val, add_str_val, add_bool_val, delete_key
    ):
        """Modify controller config.

        The config key name is a text traversal of the JSON tree with dot
        delimiters (e.g. flags.v).
        """
        ConfigCmd(cli_opts)._modifyControllerConfigCmd(
            add_int_val, add_float_val, add_str_val, add_bool_val, delete_key
        )

    @click.command()
    @click.pass_obj
    @click.option(
        "--add_int_val",
        "-i",
        type=(str, int),
        multiple=True,
        help="Add integer key-value pairs",
    )
    @click.option(
        "--add_float_val",
        "-f",
        type=(str, float),
        multiple=True,
        help="Add floating-point key-value pairs",
    )
    @click.option(
        "--add_str_val",
        "-s",
        type=(str, str),
        multiple=True,
        help="Add string key-value pairs",
    )
    @click.option(
        "--add_bool_val",
        "-b",
        type=(str, bool),
        multiple=True,
        help="Add boolean key-value pairs",
    )
    @click.option(
        "--delete_key", "-d", type=str, multiple=True, help="Delete existing key"
    )
    def _modifyAggregator(
        cli_opts, add_int_val, add_float_val, add_str_val, add_bool_val, delete_key
    ):
        """Modify aggregator config.

        The config key name is a text traversal of the JSON tree with dot
        delimiters (e.g. flags.v).
        """
        ConfigCmd(cli_opts, connect_to_ctrl=False)._modifyAggregatorConfigCmd(
            add_int_val, add_float_val, add_str_val, add_bool_val, delete_key
        )

    @click.group()
    @click.option(
        "--format",
        "-f",
        default="json",
        type=click.Choice(["json", "html"]),
        help="Output format",
    )
    @click.option(
        "--output",
        "-o",
        type=str,
        required=True,
        help="Output file path to write the config metadata",
    )
    @click.pass_obj
    def _metadata(cli_opts, format, output):
        """Get config metadata"""
        cli_opts.format = format
        cli_opts.output = output

    @click.command()
    @click.pass_obj
    def _metadataNode(cli_opts):
        """Get node config metadata"""
        ConfigMetadataCmd(cli_opts)._getNodeConfigMetadataCmd()

    @click.command()
    @click.pass_obj
    def _metadataController(cli_opts):
        """Get controller config metadata"""
        ConfigMetadataCmd(cli_opts)._getControllerConfigMetadataCmd()

    @click.command()
    @click.pass_obj
    def _metadataAggregator(cli_opts):
        """Get aggregator config metadata"""
        ConfigMetadataCmd(
            cli_opts, connect_to_ctrl=False
        )._getAggregatorConfigMetadataCmd()


class ConfigCmd(base.BaseCmd):
    def __init__(self, cli_opts, connect_to_ctrl=True):
        base.BaseCmd.__init__(self, cli_opts)
        self._nodes = getattr(cli_opts, "nodes", [])  # For NODE
        if connect_to_ctrl:
            self._connect_to_controller()
        else:
            self._connect_to_aggregator()

        # translate MAC addresses to node names
        macs = getattr(cli_opts, "macs", [])
        if macs:
            if self._nodes:
                self._my_exit(False, "Only one of --nodes or --macs is allowed.")

            # get MAC -> node name mapping from topology
            topology = self._get_topology()
            mac_to_node_name = {n.mac_addr.lower(): n.name for n in topology.nodes}

            # convert MAC addresses
            nodes = {}
            for mac in macs:
                mac = mac.lower()
                if mac not in mac_to_node_name:
                    self._my_exit(False, "MAC " + mac + " not found in topology.")
                name = mac_to_node_name[mac]
                if name in nodes:
                    self._my_exit(
                        False,
                        "Multiple MACs belong to the same node "
                        + name
                        + ": "
                        + nodes[name]
                        + ", "
                        + mac,
                    )
                nodes[name] = mac
            self._nodes.extend(list(nodes.keys()))

    def _logJsonObj(self, obj):
        _log.info(json.dumps(json.loads(obj), indent=4, sort_keys=True))

    def _modifyConfig(
        self, obj, add_int_val, add_float_val, add_str_val, add_bool_val, delete_key
    ):
        if not modify_config(
            obj, add_int_val, add_float_val, add_str_val, add_bool_val, delete_key
        ):
            self._my_exit(False, "No option specified. Type --help for usage.")

    def _receiveCtrlGetConfigResp(self, resp_msg, resp_type):
        response1 = resp_msg
        response2 = ctrlTypes.E2EAck()
        recv_type = self._recv_multi_from_ctrl(
            [(resp_type, response1), (ctrlTypes.MessageType.E2E_ACK, response2)],
            consts.CONFIG_APP_CTRL_ID,
        )

        if recv_type == resp_type:
            return response1
        else:
            self._my_exit(False, response2.message)

    def _getAutoNodeOverrides(self, nodes):
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_REQ,
            ctrlTypes.GetCtrlConfigAutoNodeOverridesReq(nodes),
            consts.CONFIG_APP_CTRL_ID,
        )
        response = self._receiveCtrlGetConfigResp(
            ctrlTypes.GetCtrlConfigAutoNodeOverridesResp(),
            ctrlTypes.MessageType.GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_RESP,
        )
        return response.overrides

    def _getNodeOverrides(self, nodes):
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_CTRL_CONFIG_NODE_OVERRIDES_REQ,
            ctrlTypes.GetCtrlConfigNodeOverridesReq(nodes),
            consts.CONFIG_APP_CTRL_ID,
        )
        response = self._receiveCtrlGetConfigResp(
            ctrlTypes.GetCtrlConfigNodeOverridesResp(),
            ctrlTypes.MessageType.GET_CTRL_CONFIG_NODE_OVERRIDES_RESP,
        )
        return response.overrides

    def _getNetworkOverrides(self):
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_CTRL_CONFIG_NETWORK_OVERRIDES_REQ,
            ctrlTypes.GetCtrlConfigNetworkOverridesReq(),
            consts.CONFIG_APP_CTRL_ID,
        )
        response = self._receiveCtrlGetConfigResp(
            ctrlTypes.GetCtrlConfigNetworkOverridesResp(),
            ctrlTypes.MessageType.GET_CTRL_CONFIG_NETWORK_OVERRIDES_RESP,
        )
        return response.overrides

    def _getControllerConfig(self):
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_CTRL_CONFIG_CONTROLLER_REQ,
            ctrlTypes.GetCtrlControllerConfigReq(),
            consts.CONFIG_APP_CTRL_ID,
        )
        response = self._receiveCtrlGetConfigResp(
            ctrlTypes.GetCtrlControllerConfigResp(),
            ctrlTypes.MessageType.GET_CTRL_CONFIG_CONTROLLER_RESP,
        )
        return response.config if response.config else "{}"

    def _getAggregatorConfig(self):
        self._send_to_aggr(
            aggrTypes.AggrMessageType.GET_AGGR_CONFIG_REQ,
            aggrTypes.AggrGetConfigReq(),
            consts.CONFIG_APP_AGGR_ID,
        )
        response = aggrTypes.AggrGetConfigResp()
        self._recv_from_aggr(
            aggrTypes.AggrMessageType.GET_AGGR_CONFIG_RESP,
            response,
            consts.CONFIG_APP_AGGR_ID,
        )
        return response.config if response.config else "{}"

    def _setNodeOverrides(self, jsonString):
        self._send_to_ctrl(
            ctrlTypes.MessageType.SET_CTRL_CONFIG_NODE_OVERRIDES_REQ,
            ctrlTypes.SetCtrlConfigNodeOverridesReq(jsonString),
            consts.CONFIG_APP_CTRL_ID,
        )

        e2e_ack = self._recv_e2e_ack(consts.CONFIG_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _setNetworkOverrides(self, jsonString):
        self._send_to_ctrl(
            ctrlTypes.MessageType.SET_CTRL_CONFIG_NETWORK_OVERRIDES_REQ,
            ctrlTypes.SetCtrlConfigNetworkOverridesReq(jsonString),
            consts.CONFIG_APP_CTRL_ID,
        )

        e2e_ack = self._recv_e2e_ack(consts.CONFIG_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _setControllerConfig(self, jsonString):
        self._send_to_ctrl(
            ctrlTypes.MessageType.SET_CTRL_CONFIG_CONTROLLER_REQ,
            ctrlTypes.SetCtrlControllerConfigReq(jsonString),
            consts.CONFIG_APP_CTRL_ID,
        )

        e2e_ack = self._recv_e2e_ack(consts.CONFIG_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _setAggregatorConfig(self, jsonString):
        self._send_to_aggr(
            aggrTypes.AggrMessageType.SET_AGGR_CONFIG_REQ,
            aggrTypes.AggrSetConfigReq(jsonString),
            consts.CONFIG_APP_AGGR_ID,
        )

        aggr_ack = self._recv_aggr_ack(consts.CONFIG_APP_AGGR_ID)
        self._my_exit(aggr_ack.success, aggr_ack.message)

    def _getNodeFullCmd(self, version, hardware, firmware, output):
        if len(self._nodes) < 1:
            self._my_exit(False, "Please specify a node name.")
            return
        if len(self._nodes) > 1:
            self._my_exit(False, "Only one node at a time supported.")
            return

        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_CTRL_CONFIG_REQ,
            ctrlTypes.GetCtrlConfigReq(self._nodes[0], version, hardware, firmware),
            consts.CONFIG_APP_CTRL_ID,
        )
        response = self._receiveCtrlGetConfigResp(
            ctrlTypes.GetCtrlConfigResp(), ctrlTypes.MessageType.GET_CTRL_CONFIG_RESP
        )

        if output:
            with open(output, "w") as outfile:
                json.dump(
                    json.loads(response.config),
                    outfile,
                    indent=4,
                    sort_keys=True,
                    ensure_ascii=False,
                )
            _log.info("Wrote output to " + output)
        else:
            self._logJsonObj(response.config)
        self._my_exit(True, "Success")

    def _getAutoNodeOverridesCmd(self):
        overrides = self._getAutoNodeOverrides(self._nodes)
        self._logJsonObj(overrides)
        self._my_exit(True, "Success")

    def _getNodeOverridesCmd(self):
        overrides = self._getNodeOverrides(self._nodes)
        self._logJsonObj(overrides)
        self._my_exit(True, "Success")

    def _getNetworkOverridesCmd(self):
        overrides = self._getNetworkOverrides()
        self._logJsonObj(overrides)
        self._my_exit(True, "Success")

    def _getBaseCmd(self, versions):
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_CTRL_CONFIG_BASE_REQ,
            ctrlTypes.GetCtrlConfigBaseReq(versions),
            consts.CONFIG_APP_CTRL_ID,
        )
        response = self._receiveCtrlGetConfigResp(
            ctrlTypes.GetCtrlConfigBaseResp(),
            ctrlTypes.MessageType.GET_CTRL_CONFIG_BASE_RESP,
        )

        self._logJsonObj(response.config)
        self._my_exit(True, "Success")

    def _getFirmwareBaseCmd(self, versions):
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_CTRL_CONFIG_FIRMWARE_BASE_REQ,
            ctrlTypes.GetCtrlConfigFirmwareBaseReq(versions),
            consts.CONFIG_APP_CTRL_ID,
        )
        response = self._receiveCtrlGetConfigResp(
            ctrlTypes.GetCtrlConfigFirmwareBaseResp(),
            ctrlTypes.MessageType.GET_CTRL_CONFIG_FIRMWARE_BASE_RESP,
        )

        self._logJsonObj(response.config)
        self._my_exit(True, "Success")

    def _getHardwareBaseCmd(self, hardware, versions):
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_CTRL_CONFIG_HARDWARE_BASE_REQ,
            ctrlTypes.GetCtrlConfigHardwareBaseReq(hardware, versions),
            consts.CONFIG_APP_CTRL_ID,
        )
        response = self._receiveCtrlGetConfigResp(
            ctrlTypes.GetCtrlConfigHardwareBaseResp(),
            ctrlTypes.MessageType.GET_CTRL_CONFIG_HARDWARE_BASE_RESP,
        )

        self._logJsonObj(response.config)
        self._my_exit(True, "Success")

    def _getControllerConfigCmd(self):
        ctrl_config = self._getControllerConfig()
        self._logJsonObj(ctrl_config)
        self._my_exit(True, "Success")

    def _getAggregatorConfigCmd(self):
        aggr_config = self._getAggregatorConfig()
        self._logJsonObj(aggr_config)
        self._my_exit(True, "Success")

    def _setNodeOverridesCmd(self, overrides_file):
        try:
            with open(overrides_file, "r") as f:
                json_string = f.read()
        except Exception:
            print("Empty file in {}".format(overrides_file))
            return
        self._setNodeOverrides(json_string)

    def _setNetworkOverridesCmd(self, overrides_file):
        try:
            with open(overrides_file, "r") as f:
                json_string = f.read()
        except Exception:
            print("Empty file in {}".format(overrides_file))
            return

        self._setNetworkOverrides(json_string)

    def _modifyNodeOverridesCmd(
        self,
        node,
        mac,
        add_int_val,
        add_float_val,
        add_str_val,
        add_bool_val,
        delete_key,
    ):
        if not node and not mac:
            self._my_exit(False, "Must specify --node or --mac.")
        if node and mac:
            self._my_exit(False, "Only one of --node or --mac is allowed.")

        # Translate MAC address to node name
        if mac:
            mac = mac.lower()
            topology = self._get_topology()
            for n in topology.nodes:
                if mac == n.mac_addr.lower():
                    node = n.name
                    break
            if not node:
                self._my_exit(False, "MAC " + mac + " not found in topology.")

        # Get node overrides from controller
        overrides = self._getNodeOverrides([node])

        # Modify node overrides
        overrides_obj = json.loads(overrides)
        if not isinstance(overrides_obj, dict) or not isinstance(
            overrides_obj.get(node, None), dict
        ):
            overrides_obj = {node: {}}
        node_overrides_obj = overrides_obj[node]
        self._modifyConfig(
            node_overrides_obj,
            add_int_val,
            add_float_val,
            add_str_val,
            add_bool_val,
            delete_key,
        )
        _log.info(json.dumps(node_overrides_obj, indent=4, sort_keys=True))

        # Set node overrides
        self._setNodeOverrides(json.dumps(overrides_obj))

    def _modifyNetworkOverridesCmd(
        self, add_int_val, add_float_val, add_str_val, add_bool_val, delete_key
    ):
        # Get network overrides from controller
        overrides = self._getNetworkOverrides()

        # Modify network overrides
        overrides_obj = json.loads(overrides)
        if not isinstance(overrides_obj, dict):
            overrides_obj = {}
        self._modifyConfig(
            overrides_obj,
            add_int_val,
            add_float_val,
            add_str_val,
            add_bool_val,
            delete_key,
        )
        _log.info(json.dumps(overrides_obj, indent=4, sort_keys=True))

        # Set network overrides
        self._setNetworkOverrides(json.dumps(overrides_obj))

    def _modifyControllerConfigCmd(
        self, add_int_val, add_float_val, add_str_val, add_bool_val, delete_key
    ):
        # Get controller config
        ctrl_config = self._getControllerConfig()

        # Modify controller config
        config_obj = json.loads(ctrl_config)
        if not isinstance(config_obj, dict):
            config_obj = {}
        self._modifyConfig(
            config_obj,
            add_int_val,
            add_float_val,
            add_str_val,
            add_bool_val,
            delete_key,
        )
        _log.info(json.dumps(config_obj, indent=4, sort_keys=True))

        # Set controller config
        self._setControllerConfig(json.dumps(config_obj))

    def _modifyAggregatorConfigCmd(
        self, add_int_val, add_float_val, add_str_val, add_bool_val, delete_key
    ):
        # Get aggregator config
        aggr_config = self._getAggregatorConfig()

        # Modify aggregator config
        config_obj = json.loads(aggr_config)
        if not isinstance(config_obj, dict):
            config_obj = {}
        self._modifyConfig(
            config_obj,
            add_int_val,
            add_float_val,
            add_str_val,
            add_bool_val,
            delete_key,
        )
        _log.info(json.dumps(config_obj, indent=4, sort_keys=True))

        # Set aggregator config
        self._setAggregatorConfig(json.dumps(config_obj))


class ConfigMetadataCmd(base.BaseCmd):
    def __init__(self, cli_opts, connect_to_ctrl=True):
        base.BaseCmd.__init__(self, cli_opts)
        self._format = getattr(cli_opts, "format", "")
        self._output = getattr(cli_opts, "output", "")
        if connect_to_ctrl:
            self._connect_to_controller()
        else:
            self._connect_to_aggregator()

    def _getNodeConfigMetadataCmd(self):
        """Retrieve the node config metadata"""
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_CTRL_CONFIG_METADATA_REQ,
            ctrlTypes.GetCtrlConfigMetadata(),
            consts.CONFIG_APP_CTRL_ID,
        )
        response = ctrlTypes.GetCtrlConfigMetadataResp()
        self._recv_from_ctrl(
            ctrlTypes.MessageType.GET_CTRL_CONFIG_METADATA_RESP,
            response,
            consts.CONFIG_APP_CTRL_ID,
        )

        self._write_metadata("Node Config Metadata", json.loads(response.metadata))
        self._my_exit(True, "Success")

    def _getControllerConfigMetadataCmd(self):
        """Retrieve the controller config metadata"""
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_CTRL_CONFIG_CONTROLLER_METADATA_REQ,
            ctrlTypes.GetCtrlControllerConfigMetadata(),
            consts.CONFIG_APP_CTRL_ID,
        )
        response = ctrlTypes.GetCtrlControllerConfigMetadataResp()
        self._recv_from_ctrl(
            ctrlTypes.MessageType.GET_CTRL_CONFIG_CONTROLLER_METADATA_RESP,
            response,
            consts.CONFIG_APP_CTRL_ID,
        )

        self._write_metadata(
            "Controller Config Metadata", json.loads(response.metadata)
        )
        self._my_exit(True, "Success")

    def _getAggregatorConfigMetadataCmd(self):
        """Retrieve the aggregator config metadata"""
        self._send_to_aggr(
            aggrTypes.AggrMessageType.GET_AGGR_CONFIG_METADATA_REQ,
            aggrTypes.AggrGetConfigMetadata(),
            consts.CONFIG_APP_AGGR_ID,
        )
        response = aggrTypes.AggrGetConfigMetadataResp()
        self._recv_from_aggr(
            aggrTypes.AggrMessageType.GET_AGGR_CONFIG_METADATA_RESP,
            response,
            consts.CONFIG_APP_AGGR_ID,
        )

        self._write_metadata(
            "Aggregator Config Metadata", json.loads(response.metadata)
        )
        self._my_exit(True, "Success")

    def _write_metadata(self, title, metadata):
        """Write the given metadata to the output file"""
        with open(self._output, "w") as outfile:
            if self._format == "json":
                self._write_metadata_json(metadata, outfile)
            elif self._format == "html":
                self._write_metadata_html(title, metadata, outfile)
            else:
                self._my_exit(False, "Invalid output format.")
        _log.info("Wrote output to " + self._output)

    def _write_metadata_json(self, metadata, outfile):
        """Write the metadata as JSON to the output file"""
        json.dump(metadata, outfile, indent=4, sort_keys=True, ensure_ascii=False)

    def _write_metadata_html(self, title, metadata, outfile):
        """Write the metadata as HTML to the output file"""
        bootstrap_css_url = (
            "https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css"
        )
        bootstrap_js_url = (
            "https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/js/bootstrap.min.js"
        )
        jquery_url = "https://ajax.googleapis.com/ajax/libs/jquery/1.12.4/jquery.min.js"
        outfile.write(
            "<!doctype html>\n"
            '<html lang="en">\n'
            "<head>\n"
            '\t<meta charset="utf-8">\n'
            '\t<meta http-equiv="X-UA-Compatible" content="IE=edge">\n'
            '\t<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            "\t<title>{}</title>\n"
            '\t<link rel="stylesheet" href="{}">\n'
            "\t<style>\n"
            "\t.panel-heading .panel-toggle:after {{\n"
            '\t  font-family: "Glyphicons Halflings";\n'
            '\t  content: "\\e114";\n'
            "\t  float: right;\n"
            "\t  color: grey;\n"
            "\t}}\n"
            "\t.panel-heading .panel-toggle.collapsed:after {{\n"
            '\t  content: "\\e080";\n'
            "\t}}\n"
            "\t</style>\n"
            "</head>\n"
            '<body class="container">\n'
            "\t<h1>{}</h1>\n"
            "\t<hr>\n"
            "{}"
            "\n"
            '\t<script src="{}"></script>\n'
            '\t<script src="{}"></script>\n'
            "</body>\n"
            "</html>\n".format(
                title,
                bootstrap_css_url,
                title,
                self._get_metadata_html_recursive(metadata, [], 1),
                jquery_url,
                bootstrap_js_url,
            )
        )

    def _get_metadata_html_recursive(self, metadata, keys, t):
        """Recursively parse the metadata and return as HTML"""
        output = StringIO()

        for k, v in sorted(metadata.items()):
            keys.append(k)
            id = "-".join(keys)

            # we identify a metadata block by its required fields
            if "desc" in v and "type" in v and "action" in v:
                content = self._get_metadata_html_obj(v, keys, t + 2)
            else:
                # look one level deeper (recursively)
                content = (
                    self._tabbed('<div class="panel-body">\n', t + 2)
                    + self._get_metadata_html_recursive(v, keys, t + 3)
                    + self._tabbed("</div>\n", t + 2)
                )

            keys.pop()
            output.write(
                self._get_metadata_html_titled_panel(
                    id, k, content, "deprecated" in v, (t == 1), t
                )
            )

        s = output.getvalue()
        output.close()
        return s

    def _get_metadata_html_obj(self, obj, keys, t):
        """Recursively parse the metadata param and return as HTML"""
        items = []

        # parse base properties
        if "desc" in obj:
            items.append(("Description", obj["desc"]))
        if "action" in obj:
            items.append(("Action", "<code>{}</code>".format(obj["action"])))
        if "type" in obj:
            items.append(("Type", obj["type"].title()))
        if "optional" in obj:
            items.append(("Optional", "Yes" if obj["optional"] else "No"))

        # parse type-specific properties
        if "intVal" in obj:
            if "allowedRanges" in obj["intVal"]:
                items.append(
                    (
                        "Allowed ranges",
                        ", ".join(
                            [
                                "<code>{}</code>".format(x)
                                for x in obj["intVal"]["allowedRanges"]
                            ]
                        ),
                    )
                )
            if "allowedValues" in obj["intVal"]:
                items.append(
                    (
                        "Allowed values",
                        ", ".join(
                            [
                                "<code>{}</code>".format(x)
                                for x in obj["intVal"]["allowedValues"]
                            ]
                        ),
                    )
                )
        elif "floatVal" in obj:
            if "allowedRanges" in obj["floatVal"]:
                items.append(
                    (
                        "Allowed ranges",
                        ", ".join(
                            [
                                "<code>{}</code>".format(x)
                                for x in obj["floatVal"]["allowedRanges"]
                            ]
                        ),
                    )
                )
            if "allowedValues" in obj["floatVal"]:
                items.append(
                    (
                        "Allowed values",
                        ", ".join(
                            [
                                "<code>{}</code>".format(x)
                                for x in obj["floatVal"]["allowedValues"]
                            ]
                        ),
                    )
                )
        elif "strVal" in obj:
            if "regexMatches" in obj["strVal"]:
                items.append(
                    (
                        "Regex matches",
                        "<code>{}</code>".format(obj["strVal"]["regexMatches"]),
                    )
                )
            if "intRanges" in obj["strVal"]:
                items.append(
                    (
                        "Allowed integer ranges",
                        ", ".join(
                            [
                                "<code>{}</code>".format(x)
                                for x in obj["strVal"]["intRanges"]
                            ]
                        ),
                    )
                )
            if "floatRanges" in obj["strVal"]:
                items.append(
                    (
                        "Allowed float ranges",
                        ", ".join(
                            [
                                "<code>{}</code>".format(x)
                                for x in obj["strVal"]["floatRanges"]
                            ]
                        ),
                    )
                )
            if "allowedValues" in obj["strVal"]:
                items.append(
                    (
                        "Allowed values",
                        ", ".join(
                            [
                                "<code>{}</code>".format(x)
                                for x in obj["strVal"]["allowedValues"]
                            ]
                        ),
                    )
                )
        elif "boolVal" in obj:
            pass
        elif "objVal" in obj:
            if "properties" in obj["objVal"]:
                props = []
                for k, v in sorted(obj["objVal"]["properties"].items()):
                    keys.append(k)
                    id = "-".join(keys)
                    content = self._get_metadata_html_obj(v, keys, t + 4)
                    keys.pop()
                    props.append(
                        self._get_metadata_html_titled_panel(
                            id, k, content, "deprecated" in v, False, t + 2
                        )
                    )
                items.append(
                    (
                        "Object properties",
                        ""
                        if not props
                        else "\n"
                        + self._tabbed(
                            '<div style="margin-bottom:0.5em"></div>\n', t + 2
                        )
                        + "".join(props)
                        + self._tabbed("", t + 1),
                    )
                )
        elif "mapVal" in obj:
            items.append(
                (
                    "Map value",
                    "\n"
                    + self._tabbed('<div style="margin-top:0.5em"></div>\n', t + 2)
                    + self._tabbed('<div class="panel panel-default">\n', t + 2)
                    + self._get_metadata_html_obj(obj["mapVal"], keys, t + 3)
                    + self._tabbed("</div>\n", t + 2)
                    + self._tabbed("", t + 1),
                )
            )
            pass

        # write items as list-items
        output = StringIO()
        output.write(self._tabbed('<ul class="list-group">\n', t))
        for key, val in items:
            output.write(
                self._tabbed(
                    '<li class="list-group-item"><strong>{}:</strong> {}</li>\n'.format(
                        key, val
                    ),
                    t + 1,
                )
            )
        output.write(self._tabbed("</ul>\n", t))

        s = output.getvalue()
        output.close()
        return s

    def _get_metadata_html_titled_panel(
        self, id, header, content, deprecated, collapsed, t
    ):
        """Return an HTML panel with the given DOM id and panel content"""
        return (
            self._tabbed('<div class="panel panel-default">\n', t)
            + self._tabbed('<div class="panel-heading">\n', t + 1)
            + self._tabbed(
                '<h2 class="panel-title">'
                '<a class="panel-toggle {}" data-toggle="collapse" href="#{}">'
                "<samp>{}</samp>"
                "</a>"
                "{}"
                "</h2>\n".format(
                    "collapsed" if collapsed else "",
                    id,
                    header,
                    '&nbsp;&nbsp;<span class="badge">DEPRECATED</span>'
                    if deprecated
                    else "",
                ),
                t + 2,
            )
            + self._tabbed("</div>\n", t + 1)
            + self._tabbed(
                '<div class="panel-collapse collapse {}" id="{}">\n'.format(
                    "" if collapsed else "in", id
                ),
                t + 1,
            )
            + content
            + self._tabbed("</div>\n", t + 1)
            + self._tabbed("</div>\n", t)
        )

    def _tabbed(self, s, t):
        """Return the string with the given number of tabs prepended"""
        return ("\t" * t) + s


def _addToObject(currentObj, keyVal):
    # use . as delimiter
    keyTokens = keyVal[0].replace(".", " ").split()
    val = keyVal[1]
    nextObject = currentObj
    level = 1
    for token in keyTokens:
        if level >= len(keyTokens):
            nextObject[token] = val
            break
        if token not in nextObject:
            nextObject[token] = {}
        level += 1
        nextObject = nextObject[token]
    return currentObj


def _deleteFromObject(currentObj, key):
    # use . as delimiter
    keyTokens = key.replace(".", " ").split()
    nextObject = currentObj
    level = 1
    for token in keyTokens:
        if token not in nextObject:
            raise Exception("Key does not exist: " + key)
        if level >= len(keyTokens):
            nextObject.pop(token)
            break
        level += 1
        nextObject = nextObject[token]
    return currentObj


def modify_config(
    obj, add_int_val, add_float_val, add_str_val, add_bool_val, delete_key
):
    modified = False
    if delete_key:
        modified = modified or len(delete_key)
        for key in delete_key:
            obj = _deleteFromObject(obj, key)
    if add_int_val:
        modified = modified or len(add_int_val)
        for keyVal in add_int_val:
            obj = _addToObject(obj, keyVal)
    if add_float_val:
        modified = modified or len(add_float_val)
        for keyVal in add_float_val:
            obj = _addToObject(obj, keyVal)
    if add_str_val:
        modified = modified or len(add_str_val)
        for keyVal in add_str_val:
            obj = _addToObject(obj, keyVal)
    if add_bool_val:
        modified = modified or len(add_bool_val)
        for keyVal in add_bool_val:
            obj = _addToObject(obj, keyVal)
    return modified
