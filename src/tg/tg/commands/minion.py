#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import json

import click
import zmq
from terragraph_thrift.Controller import ttypes as ctrlTypes
from terragraph_thrift.PassThru import ttypes as passThruTypes
from terragraph_thrift.Topology import ttypes as topoTypes
from tg.commands import base, consts
from tg.commands.config import modify_config


NodeTypes = [x.lower() for x in topoTypes.NodeType._NAMES_TO_VALUES.keys()]
PolarityTypes = [x.lower() for x in topoTypes.PolarityType._NAMES_TO_VALUES.keys()]
LogModules = [x.lower() for x in ctrlTypes.LogModule._NAMES_TO_VALUES.keys()]
LogLevels = [x.lower() for x in ctrlTypes.LogLevel._NAMES_TO_VALUES.keys()]


class MinionCli(object):
    def __init__(self):
        self.minion.add_command(self._sub, name="sub")
        self.minion.add_command(self._assoc, name="assoc")
        self.minion.add_command(self._dissoc, name="dissoc")
        self.minion.add_command(self._get_link_status, name="get_link_status")
        self.minion.add_command(self._get_gps_pos, name="get_gps_pos")
        self.minion.add_command(self._gps_enable, name="gps_enable")
        self.minion.add_command(self._set_params, name="set_params")
        self.minion.add_command(self._fw_set_log_config, name="fw_set_log_config")
        self.minion.add_command(self._fw_stats_config, name="fw_stats_config")
        self.minion.add_command(self._nbr, name="nbr")
        self.minion.add_command(self._set_node_config, name="set_node_config")
        self.minion.add_command(self._modify_node_config, name="modify_node_config")

    @click.group()
    def minion():
        """ Interface with the E2E minion directly (FOR DEVELOPMENT ONLY) """
        pass

    @click.command()
    @click.option(
        "--minion_pub_port",
        "-p",
        default=17277,
        type=int,
        help="""minion broadcast pub port to connect to (default=17277)""",
    )
    @click.pass_obj
    def _sub(cli_opts, minion_pub_port):
        """ Subscribe to broadcast messages """
        MinionCmd(cli_opts).sub(minion_pub_port)

    @click.command()
    @click.option(
        "--responder_mac", "-m", type=str, help="responder MAC address", required=True
    )
    @click.option("--initiator_mac", "-i", type=str, help="initiator MAC address")
    @click.option(
        "--responder_node_type",
        "-n",
        type=click.Choice(NodeTypes),
        help="responder node type",
    )
    @click.option("--tx_golay", "-t", type=int, help="responder TX golay")
    @click.option("--rx_golay", "-r", type=int, help="responder RX golay")
    @click.option(
        "--control_superframe", "-s", type=int, help="control superframe for the link"
    )
    @click.option(
        "--responder_polarity",
        "-p",
        type=click.Choice(PolarityTypes),
        help="responder polarity",
    )
    @click.pass_obj
    def _assoc(
        cli_opts,
        responder_mac,
        initiator_mac,
        responder_node_type,
        tx_golay,
        rx_golay,
        control_superframe,
        responder_polarity,
    ):
        """ Associate a link """
        MinionCmd(cli_opts).assoc(
            responder_mac,
            initiator_mac,
            responder_node_type,
            tx_golay,
            rx_golay,
            control_superframe,
            responder_polarity,
        )

    @click.command()
    @click.option(
        "--responder_mac", "-m", type=str, help="responder MAC address", required=True
    )
    @click.option("--initiator_mac", "-i", type=str, help="initiator MAC address")
    @click.pass_obj
    def _dissoc(cli_opts, responder_mac, initiator_mac):
        """ Dissociate a link """
        MinionCmd(cli_opts).dissoc(responder_mac, initiator_mac)

    @click.command()
    @click.option(
        "--responder_mac", "-m", type=str, help="responder MAC address", required=True
    )
    @click.pass_obj
    def _get_link_status(cli_opts, responder_mac):
        """ Get link status """
        MinionCmd(cli_opts).get_link_status(responder_mac)

    @click.command()
    @click.pass_obj
    def _get_gps_pos(cli_opts):
        """ Get GPS position """
        MinionCmd(cli_opts).get_gps_pos()

    @click.command()
    @click.option(
        "--radio_mac",
        "-m",
        type=str,
        help="radio MAC address (if empty, send for all MACs)",
    )
    @click.pass_obj
    def _gps_enable(cli_opts, radio_mac):
        """ Enable GPS sync mode """
        MinionCmd(cli_opts).gps_enable(radio_mac)

    @click.command()
    @click.option(
        "--radio_mac",
        "-m",
        type=str,
        help="radio MAC address (if empty, send for all MACs)",
    )
    @click.option("--channel", "-c", type=int, help="radio channel")
    @click.option(
        "--polarity", "-p", type=click.Choice(PolarityTypes), help="radio polarity"
    )
    @click.pass_obj
    def _set_params(cli_opts, radio_mac, channel, polarity):
        """ Set node parameters """
        MinionCmd(cli_opts).set_params(radio_mac, channel, polarity)

    @click.command()
    @click.option("--radio_mac", type=str, default="", help="radio MAC address")
    @click.option(
        "--module",
        "-m",
        type=click.Choice(LogModules),
        multiple=True,
        help="module name (supports multiple modules, empty = all modules)",
    )
    @click.option(
        "--level",
        "-l",
        type=click.Choice(LogLevels),
        help="logging level",
        required=True,
    )
    @click.pass_obj
    def _fw_set_log_config(cli_opts, radio_mac, module, level):
        """ Set firmware verbosity logging level for specified modules """
        MinionCmd(cli_opts).fw_set_log_config(radio_mac, module, level)

    @click.command()
    @click.option("--radio_mac", type=str, default="", help="radio MAC address")
    @click.option(
        "--enable",
        "-y",
        type=str,
        multiple=True,
        help="stats type to enable (eTgfStatsType)",
    )
    @click.option(
        "--disable",
        "-n",
        type=str,
        multiple=True,
        help="stats type to disable (eTgfStatsType)",
    )
    @click.pass_obj
    def _fw_stats_config(cli_opts, radio_mac, enable, disable):
        """ Set firmware stats config """
        MinionCmd(cli_opts).fw_stats_config(radio_mac, enable, disable)

    @click.command()
    @click.option(
        "--device", "-d", type=str, multiple=True, help="network device(s) to query"
    )
    @click.pass_obj
    def _nbr(cli_opts, device):
        """ Get neighbors on network device(s) """
        MinionCmd(cli_opts).nbr(device)

    @click.command()
    @click.option(
        "--node_config_file",
        "-f",
        type=str,
        default="/data/cfg/node_config.json",
        help="path to node config file",
    )
    @click.pass_obj
    def _set_node_config(cli_opts, node_config_file):
        """ Set node config and apply associated actions """
        MinionCmd(cli_opts).set_node_config(node_config_file)

    @click.command()
    @click.option(
        "--node_config_file",
        "-f",
        type=str,
        default="/data/cfg/node_config.json",
        help="path to node config file",
    )
    @click.option(
        "--add_int_val",
        "-i",
        type=(str, int),
        multiple=True,
        help="add integer key-value pairs",
    )
    @click.option(
        "--add_float_val",
        "-f",
        type=(str, float),
        multiple=True,
        help="add floating-point key-value pairs",
    )
    @click.option(
        "--add_str_val",
        "-s",
        type=(str, str),
        multiple=True,
        help="add string key-value pairs",
    )
    @click.option(
        "--add_bool_val",
        "-b",
        type=(str, bool),
        multiple=True,
        help="add boolean key-value pairs",
    )
    @click.option(
        "--delete_key", "-d", type=str, multiple=True, help="delete existing key"
    )
    @click.pass_obj
    def _modify_node_config(
        cli_opts,
        node_config_file,
        add_int_val,
        add_float_val,
        add_str_val,
        add_bool_val,
        delete_key,
    ):
        """ Modify the local node config file.

            The config key name is a text traversal of the JSON tree with dot
            delimiters (e.g. radioParamsBase.fwParams.wsecEnable).
        """
        MinionCmd(cli_opts).modify_node_config(
            node_config_file,
            add_int_val,
            add_float_val,
            add_str_val,
            add_bool_val,
            delete_key,
        )


class MinionCmd(base.BaseCmd):
    def __init__(self, cli_opts):
        base.BaseCmd.__init__(self, cli_opts)
        self._connect_to_minion()

    # ==========================================================================
    # HACK! Using _ctrl_sock and associated functions for minion socket.
    # ==========================================================================

    def _connect_to_minion(self, recv_timeout=2000, send_timeout=4000):
        # HACK! Minion forwards responses when target ZMQ ID has this prefix
        self._myId = consts.MINION_APPS_SOCK_FORWARD_PREFIX + self._myId
        self._ctrl_sock = self._connect_to_router(
            self._minion_host, self._minion_port, recv_timeout, send_timeout
        )

    def _send_to_minion(self, msg_type, msg_data, receiver_app):
        # HACK! Passing dummy "minion" message part (just needs to be non-empty)
        self._send_to_ctrl(msg_type, msg_data, receiver_app, "dummy")

    def _recv_from_minion(self, msg_type, msg_data, sender_app):
        # receive message
        try:
            actual_sender_app = self._ctrl_sock.recv_string()
            ser_msg = self._ctrl_sock.recv()
        except Exception as ex:
            self._my_exit(False, "Error receiving response: %s (%s)" % (ex, type(ex)))
        assert actual_sender_app == sender_app, "Unexpected sender app"

        # deserialize message
        deser_msg = ctrlTypes.Message()
        try:
            base.deserialize(ser_msg, deser_msg)
            assert deser_msg.mType == msg_type, "Unexpected message type"
            base.deserialize(deser_msg.value, msg_data)
        except Exception as ex:
            self._my_exit(False, "Error reading response: %s (%s)" % (ex, type(ex)))

    # ==========================================================================

    def _connect_to_minion_pub(self, minion_pub_port):
        self._context = zmq.Context()
        self._minion_pub_sock = self._context.socket(zmq.SUB)
        self._minion_pub_sock.RCVTIMEO = 200000  # ms
        self._minion_pub_sock.SNDTIMEO = 4000  # ms
        self._minion_pub_sock.linger = 250  # ms
        self._poller = zmq.Poller()
        self._poller.register(self._minion_pub_sock, zmq.POLLIN)

        # Enable IPv6 on the socket
        try:
            self._minion_pub_sock.set(zmq.IPV6, 1)
        except Exception as ex:
            self._minion_pub_sock.close()
            self._my_exit(False, "Error creating socket. %s" % ex)

        # Connect to the pub socket
        try:
            pub_sock_url = "tcp://%s:%s" % (self._minion_host, minion_pub_port)
            self._minion_pub_sock.connect(pub_sock_url)
            self._minion_pub_sock.setsockopt_string(zmq.SUBSCRIBE, "")
        except Exception as ex:
            self._minion_pub_sock.close()
            self._my_exit(False, "Error connecting to minion broadcast socket. %s" % ex)

    def sub(self, minion_pub_port):
        print("Connecting to minion broadcast socket...")
        self._connect_to_minion_pub(minion_pub_port)

        # loop forever
        while True:
            try:
                socks = dict(self._poller.poll())
            except KeyboardInterrupt:
                break
            if self._minion_pub_sock not in socks:
                continue

            try:
                sender_app = self._minion_pub_sock.recv_string()
                ser_msg = self._minion_pub_sock.recv()
            except Exception as ex:
                self._my_exit(False, "Error receiving response. %s" % ex)

            deser_msg = ctrlTypes.Message()
            try:
                base.deserialize(ser_msg, deser_msg)
            except Exception as ex:
                self._my_exit(False, "Error reading response: %s (%s)" % (ex, type(ex)))

            print(
                "Received %s from '%s'"
                % (
                    ctrlTypes.MessageType._VALUES_TO_NAMES.get(
                        deser_msg.mType, deser_msg.mType
                    ),
                    sender_app,
                )
            )

            # try to print to deserialized type
            msg_data = None
            is_driver_msg = False
            if deser_msg.mType == ctrlTypes.MessageType.LINK_STATUS:
                msg_data = ctrlTypes.LinkStatus()
            elif deser_msg.mType == ctrlTypes.MessageType.STATUS_REPORT:
                msg_data = ctrlTypes.StatusReport()
            elif deser_msg.mType == ctrlTypes.MessageType.GPS_GET_POS_RESP:
                msg_data = topoTypes.Location()
                is_driver_msg = True
            elif deser_msg.mType == ctrlTypes.MessageType.SCAN_RESP:
                msg_data = ctrlTypes.ScanResp()
                is_driver_msg = True

            if msg_data is not None:
                if is_driver_msg:
                    self._deserialize_driver_msg(deser_msg.value, msg_data)
                else:
                    base.deserialize(deser_msg.value, msg_data)
            else:
                msg_data = deser_msg
            print(msg_data)

    def assoc(
        self,
        responder_mac,
        initiator_mac,
        responder_node_type,
        tx_golay,
        rx_golay,
        control_superframe,
        responder_polarity,
    ):
        req = ctrlTypes.SetLinkStatus()
        req.linkStatusType = ctrlTypes.LinkStatusType.LINK_UP
        req.responderMac = responder_mac
        req.initiatorMac = initiator_mac
        if responder_node_type is not None:
            req.responderNodeType = topoTypes.NodeType._NAMES_TO_VALUES[
                responder_node_type.upper()
            ]
        if tx_golay is not None or rx_golay is not None:
            req.golayIdx = topoTypes.GolayIdx()
            if tx_golay is not None:
                req.golayIdx.txGolayIdx = tx_golay
            if rx_golay is not None:
                req.golayIdx.rxGolayIdx = rx_golay
        if control_superframe is not None:
            req.controlSuperframe = control_superframe
        if responder_polarity is not None:
            req.responderNodePolarity = topoTypes.PolarityType._NAMES_TO_VALUES[
                responder_polarity.upper()
            ]
        print(req)
        self._send_to_minion(
            ctrlTypes.MessageType.SET_LINK_STATUS, req, consts.IGNITION_APP_MINION_ID
        )
        self._my_exit(True, "Request sent")

    def dissoc(self, responder_mac, initiator_mac):
        req = ctrlTypes.SetLinkStatus()
        req.linkStatusType = ctrlTypes.LinkStatusType.LINK_DOWN
        req.responderMac = responder_mac
        req.initiatorMac = initiator_mac
        print(req)
        self._send_to_minion(
            ctrlTypes.MessageType.SET_LINK_STATUS, req, consts.IGNITION_APP_MINION_ID
        )
        self._my_exit(True, "Request sent")

    def get_link_status(self, responder_mac):
        req = ctrlTypes.GetLinkStatus()
        req.responderMac = responder_mac
        print(req)
        self._send_to_minion(
            ctrlTypes.MessageType.GET_LINK_STATUS, req, consts.IGNITION_APP_MINION_ID
        )
        self._my_exit(True, "Request sent")

    def get_gps_pos(self):
        req = ctrlTypes.Empty()
        self._send_to_minion(
            ctrlTypes.MessageType.GPS_GET_POS_REQ, req, consts.STATUS_APP_MINION_ID
        )
        self._my_exit(True, "Request sent")

    def gps_enable(self, radio_mac):
        req = ctrlTypes.NodeParams()
        req.type = ctrlTypes.NodeParamsType.GPS
        req.enableGps = True
        if radio_mac:
            req.radioMac = radio_mac
        self._send_to_minion(
            ctrlTypes.MessageType.SET_NODE_PARAMS, req, consts.STATUS_APP_MINION_ID
        )
        self._my_exit(True, "Request sent")

    def set_params(self, radio_mac, channel, polarity):
        if channel is None and polarity is None:
            self._my_exit(False, "Provide a channel (-c) or polarity (-p).")

        req = ctrlTypes.NodeParams()
        req.type = ctrlTypes.NodeParamsType.MANUAL
        if channel is not None:
            req.channel = channel
        if polarity is not None:
            req.polarity = topoTypes.PolarityType._NAMES_TO_VALUES[polarity.upper()]
        if radio_mac:
            req.radioMac = radio_mac
        print(req)
        self._send_to_minion(
            ctrlTypes.MessageType.SET_NODE_PARAMS, req, consts.STATUS_APP_MINION_ID
        )
        self._my_exit(True, "Request sent")

    def fw_set_log_config(self, radio_mac, module, level):
        req = passThruTypes.SetLogConfig()
        if not module:
            module = LogModules  # use all modules
        modules = [ctrlTypes.LogModule._NAMES_TO_VALUES[m.upper()] for m in module]
        levelValue = ctrlTypes.LogLevel._NAMES_TO_VALUES[level.upper()]
        req.configs = {}
        for m in modules:
            req.configs[m] = levelValue
        print(req)
        if radio_mac:
            # send to specific baseband directly
            self._send_to_minion(
                ctrlTypes.MessageType.FW_SET_LOG_CONFIG,
                self._serialize_driver_msg(req, radio_mac),
                consts.DRIVER_APP_MINION_ID,
            )
        else:
            # send to all basebands via ConfigApp API
            self._send_to_minion(
                ctrlTypes.MessageType.FW_SET_LOG_CONFIG,
                req,
                consts.CONFIG_APP_MINION_ID,
            )
        self._my_exit(True, "Request sent")

    def fw_stats_config(self, radio_mac, enable, disable):
        if len(enable) == 0 and len(disable) == 0:
            self._my_exit(
                False,
                "Provide a list of stats to enable ('-y TGF_STATS_BF') or "
                + "disable ('-n TGF_STATS_BF').",
            )

        req = passThruTypes.StatsConfigure()
        req.configs = {}
        for key in enable:
            req.configs[key] = True
        for key in disable:
            req.configs[key] = False
        req.onDuration = 1
        req.period = 1
        print(req)
        self._send_to_minion(
            ctrlTypes.MessageType.FW_STATS_CONFIGURE_REQ,
            self._serialize_driver_msg(req, radio_mac),
            consts.DRIVER_APP_MINION_ID,
        )
        self._my_exit(True, "Request sent")

    def nbr(self, device):
        req = ctrlTypes.GetMinionNeighborsReq()
        req.devices = device
        req.reqId = "MinionCli"
        req.senderApp = ""
        self._send_to_minion(
            ctrlTypes.MessageType.GET_MINION_NEIGHBORS_REQ,
            req,
            consts.STATUS_APP_MINION_ID,
        )
        resp = ctrlTypes.GetMinionNeighborsResp()
        self._recv_from_minion(
            ctrlTypes.MessageType.GET_MINION_NEIGHBORS_RESP,
            resp,
            consts.STATUS_APP_MINION_ID,
        )
        print(resp)
        self._my_exit(True, "Success")

    def _read_file(self, file):
        try:
            with open(file, "r") as f:
                return f.read()
        except Exception:
            self._my_exit(False, "Failed to read {}".format(file))

    def set_node_config(self, node_config_file):
        config_str = self._read_file(node_config_file)

        req = ctrlTypes.SetMinionConfigReq()
        req.config = config_str
        self._send_to_minion(
            ctrlTypes.MessageType.SET_MINION_CONFIG_REQ,
            req,
            consts.CONFIG_APP_MINION_ID,
        )
        self._my_exit(True, "Request sent")

    def modify_node_config(
        self,
        node_config_file,
        add_int_val,
        add_float_val,
        add_str_val,
        add_bool_val,
        delete_key,
    ):
        config_str = self._read_file(node_config_file)

        config = json.loads(config_str)
        if not isinstance(config, dict):
            config = {}

        if not modify_config(
            config, add_int_val, add_float_val, add_str_val, add_bool_val, delete_key
        ):
            self._my_exit(False, "No option specified. Type --help for usage.")
        with open(node_config_file, "w") as f:
            f.write(json.dumps(config, indent=2, sort_keys=True))
            print("Wrote new config to " + node_config_file)
