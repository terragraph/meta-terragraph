#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging
import operator
import random
import sys

import tabulate
import zmq
from terragraph_thrift.Aggregator import ttypes as aggrTypes
from terragraph_thrift.Controller import ttypes as ctrlTypes
from terragraph_thrift.DriverMessage import ttypes as drvrTypes
from terragraph_thrift.Topology import ttypes as topoTypes
from tg.commands import consts

# fastproto is not used directly, but required by Thrift to accelerate
# encoding/decoding
# @lint-ignore FLAKE8 F401
from thrift.protocol import fastproto  # noqa: F401
from thrift.protocol.TCompactProtocol import TCompactProtocolAcceleratedFactory
from thrift.protocol.TSimpleJSONProtocol import TSimpleJSONProtocolFactory
from thrift.util import Serializer


_log = logging.getLogger(__name__)


def deserialize(inByteArray, outThriftStruct):
    Serializer.deserialize(
        TCompactProtocolAcceleratedFactory(), inByteArray, outThriftStruct
    )


def serialize(inThriftStruct):
    return Serializer.serialize(TCompactProtocolAcceleratedFactory(), inThriftStruct)


def serialize_to_json(inThriftStruct):
    return Serializer.serialize(TSimpleJSONProtocolFactory(), inThriftStruct)


class BaseCmd:
    def __init__(self, cli_opts):
        self._ctrl_host = cli_opts.controller_host
        self._ctrl_port = cli_opts.controller_port
        self._aggr_host = cli_opts.aggregator_host
        self._aggr_port = cli_opts.aggregator_port
        self._agent_host = cli_opts.agent_host
        self._agent_port = cli_opts.agent_port
        self._minion_host = cli_opts.minion_host
        self._minion_port = cli_opts.minion_port
        self._myId = consts.TG_CLI_APP_ID + "_" + str(random.randint(0, 65535))
        self._context = zmq.Context()
        self._ctrl_sock = None
        self._aggr_sock = None

    def _connect_to_controller(self, recv_timeout=2000, send_timeout=4000):
        _log.debug(
            "... connecting to controller on [%s]:%d"
            % (self._ctrl_host, self._ctrl_port)
        )
        self._ctrl_sock = self._connect_to_router(
            self._ctrl_host, self._ctrl_port, recv_timeout, send_timeout
        )

    def _connect_to_aggregator(self, recv_timeout=2000, send_timeout=4000):
        _log.debug(
            "... connecting to aggregator on [%s]:%d"
            % (self._aggr_host, self._aggr_port)
        )
        self._aggr_sock = self._connect_to_router(
            self._aggr_host, self._aggr_port, recv_timeout, send_timeout
        )

    def _connect_to_router(self, host, port, recv_timeout=2000, send_timeout=4000):
        # prepare socket
        socket = self._context.socket(zmq.DEALER)
        socket.RCVTIMEO = recv_timeout  # ms
        socket.SNDTIMEO = send_timeout  # ms
        socket.linger = 250  # ms

        # Enable IPv6 on the socket
        try:
            socket.set(zmq.IPV6, 1)
        except Exception as ex:
            self._my_exit(
                False, "Error enabling ipv6 on socket: {} ({})".format(ex, type(ex))
            )

        # Set ZMQ_IDENTITY on the socket
        try:
            socket.setsockopt_string(zmq.IDENTITY, self._myId)
        except Exception as ex:
            self._my_exit(
                False, "Error setting zmq id on socket: {} ({})".format(ex, type(ex))
            )

        # Connect to the router socket
        try:
            router_sock_url = "tcp://%s:%s" % (host, port)
            socket.connect(router_sock_url)
        except Exception as ex:
            self._my_exit(
                False, "Error connecting to router socket: {} ({})".format(ex, type(ex))
            )

        return socket

    # serialize msg -> DriverMessage.value
    def _serialize_driver_msg(self, data, radio_mac=None):
        dr_msg = drvrTypes.DriverMessage()
        dr_msg.radioMac = radio_mac
        dr_msg.value = serialize(data)
        return dr_msg

    # deserialize msg (Message.value) -> DriverMessage, then fills msg_data by
    # deserializing DriverMessage.value and returns DriverMessage.radioMac
    def _deserialize_driver_msg(self, data, msg_data):
        dr_msg = drvrTypes.DriverMessage()
        deserialize(data, dr_msg)
        deserialize(dr_msg.value, msg_data)
        return dr_msg.radioMac

    def _send_to_ctrl(self, msg_type, msg_data, receiver_app, minion=""):
        msg_type_str = ctrlTypes.MessageType._VALUES_TO_NAMES.get(msg_type, "UNKNOWN")
        _log.debug("... sending %s to %s:%s" % (msg_type_str, minion, receiver_app))

        # prepare message
        data = serialize(ctrlTypes.Message(msg_type, serialize(msg_data)))

        # send message
        try:
            self._ctrl_sock.send_string(str(minion), zmq.SNDMORE)
            self._ctrl_sock.send_string(str(receiver_app), zmq.SNDMORE)
            self._ctrl_sock.send_string(str(self._myId), zmq.SNDMORE)
            self._ctrl_sock.send(data)
        except Exception as ex:
            self._my_exit(
                False,
                "Failed to send %s to %s:%s; %s"
                % (msg_type_str, minion, receiver_app, ex),
            )

    def _recv_from_ctrl(self, msg_type, msg_data, sender_app):
        msg_type_str = ctrlTypes.MessageType._VALUES_TO_NAMES.get(msg_type, "UNKNOWN")
        _log.debug("... receiving %s from :%s" % (msg_type_str, sender_app))

        # receive message
        try:
            self._ctrl_sock.recv()  # sender_minion
            actual_sender_app = self._ctrl_sock.recv_string()
            ser_msg = self._ctrl_sock.recv()
        except Exception as ex:
            _log.error("Error receiving response: %s (%s)" % (ex, type(ex)))
            _log.info(
                "Note: Specify correct controller host and port with "
                + "-c/--controller_host and -p/--controller_port options, and "
                + "make sure that Controller is running on the host or ports "
                + "are open on that server for network communication."
            )
            self._my_exit(False)
        assert actual_sender_app == sender_app, "Unexpected sender app"

        # deserialize message
        deser_msg = ctrlTypes.Message()
        try:
            deserialize(ser_msg, deser_msg)
            assert deser_msg.mType == msg_type, "Unexpected message type"
            deserialize(deser_msg.value, msg_data)
        except Exception as ex:
            _log.error("Error reading response: %s (%s)" % (ex, type(ex)))
            _log.info(
                "Note: Specify correct controller host and port with "
                + "-c/--controller_host and -p/--controller_port options, and "
                + "make sure that Controller is running on the host or ports "
                + "are open on that server for network communication."
            )
            self._my_exit(False)

    # Receive one of multiple messages from sender_app
    def _recv_multi_from_ctrl(self, type_data_tuples, sender_app):
        # receive message
        deser_msg = ctrlTypes.Message()
        try:
            self._ctrl_sock.recv()  # sender_minion
            actual_sender_app = self._ctrl_sock.recv_string()
            ser_msg = self._ctrl_sock.recv()
            deserialize(ser_msg, deser_msg)
        except Exception as ex:
            _log.error("Error receiving response: %s" % ex)
            _log.info(
                "Note: Specify correct controller host and port with "
                + "-c/--controller_host and -p/--controller_port options, and "
                + "make sure that Controller is running on the host or ports "
                + "are open on that server for network communication."
            )
            self._my_exit(False)
        assert actual_sender_app == sender_app, "Unexpected sender app"

        for (msg_type, msg_data) in type_data_tuples:
            if deser_msg.mType == msg_type:
                try:
                    deserialize(deser_msg.value, msg_data)
                    return msg_type
                except Exception as ex:
                    _log.error("Error deserializing msg: %s" % ex)
                    self._my_exit(False)

        assert False, "Unexpected message type"

    def _send_to_aggr(self, msg_type, msg_data, receiver_app, agent=""):
        msg_type_str = aggrTypes.AggrMessageType._VALUES_TO_NAMES.get(
            msg_type, "UNKNOWN"
        )
        _log.debug("... sending %s to %s:%s" % (msg_type_str, agent, receiver_app))

        # prepare message
        data = serialize(aggrTypes.AggrMessage(msg_type, serialize(msg_data)))

        # send message
        try:
            self._aggr_sock.send_string(str(agent), zmq.SNDMORE)
            self._aggr_sock.send_string(str(receiver_app), zmq.SNDMORE)
            self._aggr_sock.send_string(str(self._myId), zmq.SNDMORE)
            self._aggr_sock.send(data)
        except Exception as ex:
            self._my_exit(
                False,
                "Failed to send %s to %s:%s; %s"
                % (msg_type_str, agent, receiver_app, ex),
            )

    def _recv_from_aggr(self, msg_type, msg_data, sender_app):
        msg_type_str = aggrTypes.AggrMessageType._VALUES_TO_NAMES.get(
            msg_type, "UNKNOWN"
        )
        _log.debug("... receiving %s from :%s" % (msg_type_str, sender_app))

        # receive message
        try:
            self._aggr_sock.recv()  # sender_agent
            actual_sender_app = self._aggr_sock.recv_string()
            ser_msg = self._aggr_sock.recv()
        except Exception as ex:
            _log.error("Error receiving response: %s" % ex)
            _log.info(
                "Note: Specify correct aggregator host and port with "
                + "--aggregator_host and --aggregator_port options, and "
                + "make sure that Aggregator is running on the host or ports "
                + "are open on that server for network communication."
            )
            self._my_exit(False)
        assert actual_sender_app == sender_app, "Unexpected sender app"

        # deserialize message
        deser_msg = aggrTypes.AggrMessage()
        try:
            deserialize(ser_msg, deser_msg)
            assert deser_msg.mType == msg_type, "Unexpected message type"
            deserialize(deser_msg.value, msg_data)
        except Exception as ex:
            _log.error("Error reading response: %s" % ex)
            _log.info(
                "Note: Specify correct aggregator host and port with "
                + "--aggregator_host and --aggregator_port options, and "
                + "make sure that Aggregator is running on the host or ports "
                + "are open on that server for network communication."
            )
            self._my_exit(False)

    def _get_aggr_status_dump(self):
        self._send_to_aggr(
            aggrTypes.AggrMessageType.GET_STATUS_DUMP,
            aggrTypes.AggrGetStatusDump(),
            consts.STATUS_APP_AGGR_ID,
        )
        status_dump = aggrTypes.AggrStatusDump()
        self._recv_from_aggr(
            aggrTypes.AggrMessageType.STATUS_DUMP,
            status_dump,
            consts.STATUS_APP_AGGR_ID,
        )
        return status_dump

    def _get_status_dump(self):
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_STATUS_DUMP,
            ctrlTypes.GetStatusDump(),
            consts.STATUS_APP_CTRL_ID,
        )
        status_dump = ctrlTypes.StatusDump()
        self._recv_from_ctrl(
            ctrlTypes.MessageType.STATUS_DUMP, status_dump, consts.STATUS_APP_CTRL_ID
        )
        return status_dump

    def _get_topology(self):
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_TOPOLOGY,
            ctrlTypes.GetTopology(),
            consts.TOPOLOGY_APP_CTRL_ID,
        )
        topology = topoTypes.Topology()
        self._recv_from_ctrl(
            ctrlTypes.MessageType.TOPOLOGY, topology, consts.TOPOLOGY_APP_CTRL_ID
        )
        return topology

    def _get_ignition_state(self):
        self._send_to_ctrl(
            ctrlTypes.MessageType.GET_IGNITION_STATE,
            ctrlTypes.GetIgnitionState(),
            consts.IGNITION_APP_CTRL_ID,
        )
        ignition_state = ctrlTypes.IgnitionState()
        self._recv_from_ctrl(
            ctrlTypes.MessageType.IGNITION_STATE,
            ignition_state,
            consts.IGNITION_APP_CTRL_ID,
        )
        return ignition_state

    def _get_bstar_state(self):
        self._send_to_ctrl(
            ctrlTypes.MessageType.BSTAR_GET_STATE,
            ctrlTypes.BinaryStarGetState(),
            consts.BINARYSTAR_APP_CTRL_ID,
        )
        fsm = ctrlTypes.BinaryStar()
        self._recv_from_ctrl(
            ctrlTypes.MessageType.BSTAR_FSM, fsm, consts.BINARYSTAR_APP_CTRL_ID
        )
        return fsm

    def _my_exit(self, success, error_msg="", operation=None):
        # log before exit
        if not operation:
            operation = type(self).__name__
        if success:
            _log.info("%s succeeded. %s" % (operation, error_msg))
            sys.exit(0)
        else:
            _log.error("%s failed. %s" % (operation, error_msg))
            sys.exit(1)

    def _display_nodes(self, nodes):
        headers = [
            "NodeName",
            "MacAddr",
            "PopNode",
            "NodeType",
            "Status",
            "SiteName",
        ]
        table = []
        for node in nodes:
            table.append(
                [
                    node.name,
                    node.mac_addr,
                    str(node.pop_node),
                    topoTypes.NodeType._VALUES_TO_NAMES.get(node.node_type, "UNKNOWN"),
                    topoTypes.NodeStatusType._VALUES_TO_NAMES.get(
                        node.status, "UNKNOWN"
                    ),
                    node.site_name,
                ]
            )
        print(tabulate.tabulate(table, headers))
        print("")

    def _display_links(self, links, include_wired):
        headers = [
            "LinkName",
            "ANodeName",
            "ZNodeName",
            "Alive",
            "LinkType",
            "LinkupAttempts",
        ]
        table = []
        for link in links:
            if not include_wired and link.link_type == topoTypes.LinkType.ETHERNET:
                continue
            table.append(
                [
                    link.name,
                    link.a_node_name,
                    link.z_node_name,
                    str(link.is_alive),
                    topoTypes.LinkType._VALUES_TO_NAMES.get(link.link_type, "UNKNOWN"),
                    link.linkup_attempts,
                ]
            )
        table = sorted(table, key=operator.itemgetter(0))
        print(tabulate.tabulate(table, headers))
        print("")

    def _display_sites(self, sites):
        headers = ["SiteName", "Latitude", "Longitude", "Altitude", "Accuracy"]
        table = []
        for site in sites:
            table.append(
                [
                    site.name,
                    site.location.latitude,
                    site.location.longitude,
                    site.location.altitude,
                    site.location.accuracy,
                ]
            )
        print(tabulate.tabulate(table, headers))
        print("")

    def _recv_e2e_ack(self, sender_id):
        e2e_ack = ctrlTypes.E2EAck()
        self._recv_from_ctrl(ctrlTypes.MessageType.E2E_ACK, e2e_ack, sender_id)
        return e2e_ack

    def _recv_aggr_ack(self, sender_id):
        aggr_ack = aggrTypes.AggrAck()
        self._recv_from_aggr(aggrTypes.AggrMessageType.AGGR_ACK, aggr_ack, sender_id)
        return aggr_ack
