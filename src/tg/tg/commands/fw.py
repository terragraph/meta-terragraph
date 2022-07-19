#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging
import re
import sys

import click
from terragraph_thrift.BWAllocation import ttypes as bwallocationTypes
from terragraph_thrift.Controller import ttypes as ctrlTypes
from terragraph_thrift.DriverMessage import ttypes as drvrTypes
from terragraph_thrift.FwOptParams import ttypes as fwOptParamsTypes
from terragraph_thrift.PassThru import ttypes as passThruTypes
from tg.commands import base, consts


_log = logging.getLogger(__name__)
EMPTY_MAC_ADDRESS = "00:00:00:00:00:00"


def loadThriftFromJson(file_name, thrift_struct):
    with open(file_name, "r") as content:
        thrift_json = content.read()
    thrift_struct.readFromJson(thrift_json)
    return thrift_struct


# Checks if MAC address is in the right format XX:XX:XX:XX:XX:XX
# Returns the MAC address if successful or raises a click error if not


def validate_mac(ctx, param, mac):
    if bool(re.match("^" + "[\:\-]".join(["([0-9a-f]{2})"] * 6) + "$", mac.lower())):
        return mac
    else:
        raise click.BadParameter(
            "Invalid MAC address format should be XX:XX:XX:XX:XX:XX"
        )


class FwCli(object):
    def __init__(self):
        self.fw.add_command(self._node, name="node")
        self.fw.add_command(self._network, name="network")
        self._node.add_command(self._stats_config, name="stats_config")
        self._network.add_command(self._stats_config, name="stats_config")
        self._node.add_command(self._set_fw_params, name="set_fw_params")
        self._node.add_command(self._get_fw_params, name="get_fw_params")
        self._node.add_command(self._airtime_alloc, name="airtime_alloc")
        self._network.add_command(self._airtime_alloc, name="airtime_alloc")
        self._node.add_command(self._debug, name="debug")
        self._network.add_command(self._debug, name="debug")
        self._node.add_command(self._bf_resp_scan, name="bf_resp_scan")
        self._node.add_command(self._set_log_config, name="set_log_config")
        self._network.add_command(self._set_log_config, name="set_log_config")

    def _show_fw_list(cli_opts, param, value):
        if not value:
            return
        print("List of fw parameters that can be set at run-time: ")
        print("================================================== ")
        # these are the fw parameters that can be configured at run-time

        print("\nNode specific :")
        print("---------------")
        print("\n".join(SetFwParamsHelper.nodeParamsList))
        print('\nLink specific ("--responder_node" option required) :')
        print("-----------------------------------------")
        print("\n".join(SetFwParamsHelper.linkParamsList))
        sys.exit(0)

    @click.group()
    @click.pass_obj
    def fw(cli_opts):
        """Enable/disable firmware stats types"""
        pass

    @click.group()
    @click.option(
        "--names",
        "-n",
        default="",
        type=str,
        required=True,
        help="Node name list (separated by ,)",
    )
    @click.pass_obj
    def _node(cli_opts, names):
        """Firmware commands applied to nodes"""
        cli_opts.cmd = FwNodeCmd
        cli_opts.names = [n.strip() for n in names.split(",")]

    @click.group()
    @click.option(
        "--exclude",
        "-e",
        default="",
        type=str,
        help="Exclude node name list (separated by ,)",
    )
    @click.pass_obj
    def _network(cli_opts, exclude):
        """Firmware commands applied to network"""
        cli_opts.cmd = FwNetworkCmd
        cli_opts.exclude = [n.strip() for n in exclude.split(",")]

    # stats config
    @click.command()
    @click.option(
        "--types", "-t", default="", type=str, help="stat types separated by comma"
    )
    @click.option(
        "--enable/--disable", default=True, help="enable or disable firmware stats"
    )
    @click.option(
        "--on_duration",
        "-o",
        default="",
        type=int,
        help="number of bwgd for which stats would be ON",
    )
    @click.option(
        "--period",
        "-p",
        default="",
        type=int,
        help="period (in num bwgd) for stats ON-OFF cycle",
    )
    @click.pass_obj
    def _stats_config(cli_opts, types, enable, on_duration, period):
        """Enable/disable firmware stats"""
        cli_opts.cmd(cli_opts).stats_config(types, enable, on_duration, period)

    # log config
    @click.command()
    @click.option(
        "--module",
        "-m",
        type=str,
        multiple=True,
        help="Module name."
        " Can be used more than once for multiple modules."
        " To use all modules skip this option",
    )
    @click.option("--level", "-l", default="", type=str, help="logging level")
    @click.option(
        "--show_list",
        "-s",
        is_flag=True,
        help="Show list of all the modules and levels",
    )
    @click.pass_obj
    def _set_log_config(cli_opts, module, level, show_list):
        """Set firmware logging level for specified modules"""
        cli_opts.cmd(cli_opts).set_log_config(module, level, show_list)

    # runtime set fw params
    @click.command()
    @click.option(
        "--show_list",
        "-s",
        is_flag=True,
        callback=_show_fw_list,
        expose_value=False,
        is_eager=True,
        help="Show list of fw parameters that can be set at runtime",
    )
    @click.argument("parameters", nargs=-1, metavar="[paramName] [paramValue] ..")
    @click.option(
        "--responder_node", "-r", type=str, default=" ", help="responder node name"
    )
    @click.option(
        "--bwgdIdx",
        "-b",
        type=int,
        help="BWGD index of execution start."
        + " If not set the command will be executed immediately",
    )
    @click.pass_obj
    def _set_fw_params(cli_opts, parameters, responder_node, bwgdidx):
        """Setting runtime firmware params"""
        cli_opts.cmd(cli_opts).set_fw_params(parameters, responder_node, bwgdidx)

    @click.group()
    def _get_fw_params():
        """Getting runtime firmware parameters"""
        pass

    _node_params = [
        "nodeFwCfg",
        # example_1 , example_2 , etc..
    ]

    _link_params = [
        "linkFwCfg",
        # 'example_1', 'example_2'
    ]

    @click.command()
    @click.option(
        "--param_type",
        "-t",
        type=click.Choice(_node_params),
        default="nodeFwCfg",
        required=True,
        help="Type of Node parameters to get",
    )
    @click.pass_obj
    def _fw_get_node_params(cli_opts, param_type):
        """Get FW Node parameters"""
        cli_opts.cmd(cli_opts).get_fw_params("node", param_type, "")

    @click.command()
    @click.option(
        "--param_type",
        "-t",
        type=click.Choice(_link_params),
        default="linkFwCfg",
        required=True,
        help="Type of Link parameters to get",
    )
    @click.option(
        "--responder_node", "-r", type=str, required=True, help="Responder node name"
    )
    @click.pass_obj
    def _fw_get_link_params(cli_opts, param_type, responder_node):
        """Get FW Link parameters"""
        cli_opts.cmd(cli_opts).get_fw_params("link", param_type, responder_node)

    _get_fw_params.add_command(_fw_get_node_params, name="nodeParams")
    _get_fw_params.add_command(_fw_get_link_params, name="linkParams")

    # airtime allocation
    @click.command()
    @click.option(
        "--airtime_file",
        "-f",
        default="",
        type=click.Path(exists=True),
        required=True,
        help="airtime allocation configuration file",
    )
    @click.pass_obj
    def _airtime_alloc(cli_opts, airtime_file):
        """Allocate airtime in a dynamic manner"""
        cli_opts.cmd(cli_opts).airtime_alloc(airtime_file)

    # debug command
    @click.command()
    @click.option("--command", "-c", type=str, help="Debug Command")
    @click.option("--value", "-v", type=int, help="Command value")
    @click.pass_obj
    def _debug(cli_opts, command, value):
        """push debug command"""
        cli_opts.cmd(cli_opts).debug(command, value)

    # bf responder scan command
    @click.command()
    @click.option("--cfg", "-c", type=bool, help="Enable/disable bf responder scan")
    @click.pass_obj
    def _bf_resp_scan(cli_opts, cfg):
        """Enable/disable bf responder scan"""
        cli_opts.cmd(cli_opts).bf_resp_scan(cfg)


class FwNodeCmd(base.BaseCmd):
    def __init__(self, cli_opts):
        base.BaseCmd.__init__(self, cli_opts)
        self._node_names = cli_opts.names
        self._connect_to_controller()

    def _nodes_action(self, func):
        topology = self._get_topology()
        name_to_mac = {
            n.name: n.mac_addr for n in topology.nodes if n.name in self._node_names
        }
        status_reports = self._get_status_dump().statusReports
        for name, mac in name_to_mac.items():
            if not mac:
                _log.error("%s : Does not have a valid mac address", name)
                continue
            if mac not in status_reports:
                _log.error("%s : No status report in controller", name)
                continue
            func(mac, name)

    def _get_node_mac(self, node_name):
        if not node_name:
            return ""
        topology = self._get_topology()
        name_to_mac = {
            n.name: n.mac_addr for n in topology.nodes if n.name in node_name
        }
        status_reports = self._get_status_dump().statusReports
        for name, mac in name_to_mac.items():
            if not mac:
                print("ERROR:" + name + " : Does not have a valid mac address")
                return ""
            if mac not in status_reports:
                print("ERROR: No status report in controller found for ", name)
                return ""
            return mac

    def stats_config(self, types, enable, on_duration, period):
        helper = StatsConfigHelper(self, types, enable, on_duration, period)
        self._nodes_action(helper.do_stats_config)

    def set_log_config(self, module, level, show_list):
        helper = SetLogConfigHelper(self, module, level, show_list)
        self._nodes_action(helper.do_set_log_config)

    def set_fw_params(self, parameters, responder_node, bwgdIdx):
        # TODO Doesn't support multi-radio nodes, assumes node MAC == radio MAC
        responder_mac = self._get_node_mac(responder_node)
        if not responder_mac:
            responder_mac = EMPTY_MAC_ADDRESS
        helper = SetFwParamsHelper(self, parameters, responder_mac, bwgdIdx)
        self._nodes_action(helper.do_set_fw_params)

    def get_fw_params(self, cmd_type, param_type, responder_node):
        # TODO Doesn't support multi-radio nodes, assumes node MAC == radio MAC
        responder_mac = self._get_node_mac(responder_node)
        if cmd_type == "link":
            if not responder_mac:
                print(
                    "ERROR: get_fw_params failed"
                    + ", no valid MAC address for responder "
                    + responder_node
                )
                return
        else:
            responder_mac = EMPTY_MAC_ADDRESS
        helper = GetFwParamsHelper(self, cmd_type, param_type, responder_mac)
        self._nodes_action(helper.do_get_fw_params)

    def airtime_alloc(self, airtime_alloc_file):
        helper = AirtimeAllocHelper(self, airtime_alloc_file)
        self._nodes_action(helper.do_airtime_alloc)

    def debug(self, command, value):
        helper = DebugHelper(self, command, value)
        self._nodes_action(helper.do_debug)

    def bf_resp_scan(self, cfg):
        helper = BfRespScanHelper(self, cfg)
        self._nodes_action(helper.do_bf_resp_scan)


class FwNetworkCmd(base.BaseCmd):
    def __init__(self, cli_opts):
        base.BaseCmd.__init__(self, cli_opts)
        self._exclude_nodes = cli_opts.exclude
        self._connect_to_controller()

    def _network_action(self, func):
        topology = self._get_topology()
        name_to_mac = {
            n.name: n.mac_addr
            for n in topology.nodes
            if n.name not in self._exclude_nodes
        }
        status_reports = self._get_status_dump().statusReports
        for name, mac in name_to_mac.items():
            if not mac:
                _log.error("%s : Does not have a valid mac address", name)
                continue
            if mac not in status_reports:
                _log.error("%s : No status report in controller", name)
                continue
            func(mac, name)

    def stats_config(self, types, enable, on_duration, period):
        helper = StatsConfigHelper(self, types, enable, on_duration, period)
        self._network_action(helper.do_stats_config)

    def set_log_config(self, module, level, show_list):
        helper = SetLogConfigHelper(self, module, level, show_list)
        self._network_action(helper.do_set_log_config)

    def airtime_alloc(self, airtime_alloc_file):
        AirtimeAllocHelper(self, airtime_alloc_file).do_airtime_alloc_network()

    def debug(self, command, value):
        helper = DebugHelper(self, command, value)
        self._network_action(helper.do_debug)


class StatsConfigHelper(object):
    def __init__(self, cmd, types, enable, on_duration, period):
        self._cmd = cmd
        self._types = types
        self._enable = enable
        self._on_duration = on_duration
        self._period = period

    def do_stats_config(self, node_mac, node_name=None):
        stats_configure = passThruTypes.StatsConfigure()
        stats_configure.configs = {}

        types = [t.strip() for t in self._types.split(",")]
        for ty in types:
            stats_configure.configs[ty] = self._enable
        stats_configure.onDuration = self._on_duration
        stats_configure.period = self._period

        self._cmd._send_to_ctrl(
            ctrlTypes.MessageType.FW_STATS_CONFIGURE_REQ,
            self._cmd._serialize_driver_msg(stats_configure),
            consts.DRIVER_APP_MINION_ID,
            node_mac,
        )


class SetLogConfigHelper(object):
    def __init__(self, cmd, module, level, show_list):
        self._cmd = cmd
        self._modules = [m.upper() for m in module]
        self._level = level.upper()
        self._show_list = show_list

    def do_set_log_config(self, node_mac, node_name=None):
        all_levels = ctrlTypes.LogLevel._NAMES_TO_VALUES
        all_modules = ctrlTypes.LogModule._NAMES_TO_VALUES

        all_modules_csv = ", ".join(all_modules.keys()).lower()
        all_levels_csv = ", ".join(all_levels.keys()).lower()

        if self._show_list:
            print("modules:", all_modules_csv)
            print("levels:", all_levels_csv)
            sys.exit(0)

        if self._level not in all_levels.keys():
            print("Invalid level, available levels:", all_levels_csv)
            sys.exit(1)

        if not self._modules:
            self._modules = [m for m in all_modules.keys()]
            print("configuring all modules: ", all_modules_csv)

        set_log_config = passThruTypes.SetLogConfig()
        set_log_config.configs = {}
        for module in self._modules:
            try:
                set_log_config.configs[all_modules[module]] = all_levels[self._level]
            except KeyError:
                print("Invalid level: ", module)
                print("available levels:", all_modules_csv)
                sys.exit(1)

        self._cmd._send_to_ctrl(
            ctrlTypes.MessageType.FW_SET_LOG_CONFIG,
            self._cmd._serialize_driver_msg(set_log_config),
            consts.DRIVER_APP_MINION_ID,
            node_mac,
        )


class SetFwParamsHelper(object):

    # These are the fw parameters that can be configured at run-time
    # List of Link related params that require a responder MAC
    linkParamsList = [
        "txPower",
        "txBeamIndex",
        "rxBeamIndex",
        "maxTxPower",
        "minTxPower",
        "maxAgcRfGainHiLo",
        "maxAgcTrackingEnabled",
        "maxAgcTrackingMargindB",
        "linkAgc",
        "mcs",
        "measSlotEnable",
        "measSlotOffset",
        "laMaxMcs",
        "laMinMcs",
        "tpcEnable",
        "txGolayIdx",
        "rxGolayIdx",
        "restrictToSfParity",
        "linkImpairmentDetectionEnable",
        "latpcLinkImpairConfig",
        "latpc100PercentPERDrop",
    ]
    # List of Node related params that don't require a responder MAC
    nodeParamsList = [
        "forceGpsDisable",
        "topoScanEnable",
        "crsScale",
        "maxAgcUseSameForAllSta",
        "polarity",
        "mtpoEnabled",
        "htsfMsgInterval",
    ]

    def __init__(self, cmd, parameters, responder_mac, bwgdIdx):
        self._cmd = cmd
        self._parameters = parameters
        self._responder_mac = responder_mac
        self._bwgdIdx = bwgdIdx

    def do_set_fw_params(self, node_mac, node_name=None):

        if len(self._parameters) % 2 != 0:
            print("Error : Invalid arguments list")
            print("Please enter arguments as following: [paramName] [paramValue] ...")
            return

        fw_opt_params = fwOptParamsTypes.FwOptParams()

        for i in range(0, len(self._parameters), 2):
            if (
                self._responder_mac == EMPTY_MAC_ADDRESS
                and self._parameters[i] in SetFwParamsHelper.linkParamsList
            ):
                print(
                    'Error : "--responder_node" option required for setting '
                    + self._parameters[i]
                )
                return
            if hasattr(fw_opt_params, self._parameters[i]):
                setattr(
                    fw_opt_params, self._parameters[i], int(self._parameters[i + 1])
                )
                print("Setting", self._parameters[i], "to", self._parameters[i + 1])
            else:
                print(
                    "Error: Attribute ",
                    self._parameters[i],
                    " does not exist in FwOptParams",
                )
                return

        pass_through_message = passThruTypes.PassThruMsg()
        pass_through_message.msgType = passThruTypes.PtMsgTypes.SB_SET_FW_PARAMS
        pass_through_message.dest = passThruTypes.PtMsgDest.SB
        pass_through_message.setfwParamsReq = passThruTypes.SetFwParamsMsg()
        pass_through_message.setfwParamsReq.bwgdIdx = self._bwgdIdx
        pass_through_message.setfwParamsReq.addr = self._responder_mac
        pass_through_message.setfwParamsReq.optionalParams = fw_opt_params

        self._cmd._send_to_ctrl(
            ctrlTypes.MessageType.FW_CONFIG_REQ,
            drvrTypes.FwConfigParams([pass_through_message]),
            consts.STATUS_APP_MINION_ID,
            node_mac,
        )

        # Receive the response from the node
        print("Waiting for response ...")

        msg = drvrTypes.FwConfigResp()
        self._cmd._recv_from_ctrl(
            ctrlTypes.MessageType.FW_CONFIG_RESP, msg, consts.STATUS_APP_MINION_ID
        )

        status = msg.setFwConfigResp.status
        if status:
            print("Setting parameters succeeded")
        else:
            print("ERROR:Setting one of the requested parameters failed")


class GetFwParamsHelper(object):
    def __init__(self, cmd, cmd_type, param_type, responder_mac):
        self._cmd = cmd
        self._cmd_type = cmd_type
        self._responder_mac = responder_mac
        self._param_type = param_type
        self._fwParamType = 0
        self._node_mac = 0

    def do_get_fw_params(self, node_mac, node_name=None):
        self._node_mac = node_mac
        if self._cmd_type == "node":
            self._get_node_params()
        elif self._cmd_type == "link":
            self._get_link_params()

    def _get_node_params(self):
        # Map param type option to the enum
        nodeFwParamTypes = {
            "nodeFwCfg": passThruTypes.FwParamsType.FW_PARAMS_NODE_FW_CFG
            # other param type should go here
        }

        self._fwParamType = nodeFwParamTypes[self._param_type]
        print(
            "Getting node parameters of type", self._param_type, ":", self._fwParamType
        )
        self._send_get_fw_params_cmd()

    def _get_link_params(self):
        # Map param type option to the enum
        linkFwParamTypes = {
            "linkFwCfg": passThruTypes.FwParamsType.FW_PARAMS_LINK_FW_CFG
            # other param type should go here
        }
        self._fwParamType = linkFwParamTypes[self._param_type]
        print(
            "Getting link parameters of type",
            self._param_type,
            ":",
            self._fwParamType,
            "for responder:",
            self._responder_mac,
        )
        self._send_get_fw_params_cmd()

    def _print_opt_params(self, optParams):
        out = ("%s" % optParams).split(",")
        print("\n---start of list ---\n")
        for item in out:
            if "=None" not in item:
                print(item)
        print("\n---end of list ---\n")

    def _send_get_fw_params_cmd(self):

        pass_through_message = passThruTypes.PassThruMsg()
        pass_through_message.msgType = passThruTypes.PtMsgTypes.SB_GET_FW_PARAMS
        pass_through_message.dest = passThruTypes.PtMsgDest.SB
        pass_through_message.getFwParamsReq = passThruTypes.GetFwParamsReq()
        pass_through_message.getFwParamsReq.addr = self._responder_mac
        pass_through_message.getFwParamsReq.requestedParamsType = self._fwParamType

        self._cmd._send_to_ctrl(
            ctrlTypes.MessageType.FW_CONFIG_REQ,
            drvrTypes.FwConfigParams([pass_through_message]),
            consts.STATUS_APP_MINION_ID,
            self._node_mac,
        )

        # Receive the response from the node
        print("Waiting for response ...")

        msg = drvrTypes.FwConfigResp()
        self._cmd._recv_from_ctrl(
            ctrlTypes.MessageType.FW_CONFIG_RESP, msg, consts.STATUS_APP_MINION_ID
        )

        resp = passThruTypes.GetFwParamsResp()

        resp = msg.getFwConfigResp

        print("Received parameters of type : ", resp.fwParamsType)
        print("Current BWGD = ", resp.bwgdIdx)

        if resp.fwParamsType in [
            passThruTypes.FwParamsType.FW_PARAMS_NODE_FW_CFG,
            passThruTypes.FwParamsType.FW_PARAMS_LINK_FW_CFG,
        ]:
            self._print_opt_params(resp.optParams)
        elif resp.fwParamsType is passThruTypes.FwParamsType.FW_PARAMS_INVALID:
            print("ERROR:  Failed to get requested params")


class AirtimeAllocHelper(object):
    def __init__(self, cmd, airtime_alloc_file):
        self._cmd = cmd
        self._airtime_map = bwallocationTypes.NetworkAirtime()
        loadThriftFromJson(airtime_alloc_file, self._airtime_map)

    def do_airtime_alloc(self, node_mac, node_name):
        if node_name not in self._airtime_map.nodeAirtimeMap:
            _log.info("%s does not exist in airtime alloc map, skipping...", node_name)
            return

        set_node_param_req = ctrlTypes.SetNodeParamsReq()
        set_node_param_req.nodeMac = node_mac
        set_node_param_req.nodeAirtime = self._airtime_map.nodeAirtimeMap[node_name]

        self._cmd._send_to_ctrl(
            ctrlTypes.MessageType.SET_NODE_PARAMS_REQ,
            set_node_param_req,
            consts.TOPOLOGY_APP_CTRL_ID,
        )

    def do_airtime_alloc_network(self):
        set_network_param_req = ctrlTypes.SetNetworkParamsReq()
        set_network_param_req.networkAirtime = self._airtime_map

        self._cmd._send_to_ctrl(
            ctrlTypes.MessageType.SET_NETWORK_PARAMS_REQ,
            set_network_param_req,
            consts.TOPOLOGY_APP_CTRL_ID,
        )


class BfRespScanHelper(object):
    def __init__(self, cmd, cfg):
        self._cmd = cmd
        self._cfg = cfg

    def do_bf_resp_scan(self, node_mac, node_name=None):

        bfRespScanCfg = passThruTypes.BfRespScanConfig()
        bfRespScanCfg.cfg = self._cfg

        self._cmd._send_to_ctrl(
            ctrlTypes.MessageType.FW_BF_RESP_SCAN,
            self._cmd._serialize_driver_msg(bfRespScanCfg),
            consts.DRIVER_APP_MINION_ID,
            node_mac,
        )


class DebugHelper(object):
    def __init__(self, cmd, command, value):
        self._cmd = cmd
        self._cmd_str = command
        self._value = value

    def do_debug(self, node_mac, node_name=None):

        debug = passThruTypes.Debug()
        debug.cmdStr = self._cmd_str
        debug.value = self._value

        self._cmd._send_to_ctrl(
            ctrlTypes.MessageType.FW_DEBUG_REQ,
            self._cmd._serialize_driver_msg(debug),
            consts.DRIVER_APP_MINION_ID,
            node_mac,
        )
