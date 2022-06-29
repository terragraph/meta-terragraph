#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import json
import logging
import sys
import time

import zmq
from fbzmq.Monitor import ttypes as monitorTypes
from terragraph_thrift.BWAllocation import ttypes as bwallocationTypes
from terragraph_thrift.Controller import ttypes as ctrlTypes
from terragraph_thrift.DriverMessage import ttypes as drvrTypes
from terragraph_thrift.FwOptParams import ttypes as fwOptParamsTypes
from terragraph_thrift.NodeConfig import ttypes as configTypes
from terragraph_thrift.PassThru import ttypes as passThruTypes
from terragraph_thrift.Topology import ttypes as topologyTypes
from thrift.protocol import fastproto
from thrift.protocol.TSimpleJSONProtocol import TSimpleJSONProtocolFactory
from thrift.transport import TTransport
from thrift.util import Serializer


_log = logging.getLogger(__name__)

EMPTY_MAC_ADDRESS = "00:00:00:00:00:00"
BROADCAST_MAC_ADDRESS = "ff:ff:ff:ff:ff:ff"

# When receiving messages, skip some message types to reduce verbosity
FILTER_RCV_MSG_TYPES = [
    ctrlTypes.MessageType.FW_STATS,
    ctrlTypes.MessageType.FW_ROUTING_INFO,
    ctrlTypes.MessageType.FW_HEALTHY,
]


def deserialize(inByteArray, outThriftStruct):
    fastproto.decode(
        outThriftStruct,
        TTransport.TMemoryBuffer(inByteArray),
        [outThriftStruct.__class__, outThriftStruct.thrift_spec, False],
        utf8strings=0,
        protoid=2,
    )  # compact=2, binary=0


def serialize(inThriftStruct):
    return fastproto.encode(
        inThriftStruct,
        [inThriftStruct.__class__, inThriftStruct.thrift_spec, False],
        utf8strings=0,
        protoid=2,
    )


def loadThriftFromJson(file_name, thrift_struct):
    with open(file_name, "r") as content:
        thrift_json = content.read()
    thrift_struct.readFromJson(thrift_json)
    return thrift_struct


# Backward compatibility:
# Accepts either the old fw_cfg.json or the new node_config.json
def loadRadioFwParamsThriftFromJson(file_name):
    try:
        with open(file_name, "r") as content:
            thrift_json = content.read()
    except Exception:
        print("Error reading config file: " + file_name)
        return

    data = json.loads(thrift_json)
    if "nodeInitOptParams" in data:
        print("Loading legacy fw_cfg file: " + file_name)
        node_fw_params = fwOptParamsTypes.NodeFwParams()
        node_fw_params.readFromJson(thrift_json)
        return node_fw_params.nodeInitOptParams
    else:
        nodeConfig = configTypes.NodeConfig()
        nodeConfig.readFromJson(thrift_json)
        return nodeConfig.radioParamsBase.fwParams


# Backward compatibility:
# Accepts either the old fw_cfg.json or the new node_config.json
def loadLinkFwParamsThriftFromJson(file_name):
    try:
        with open(file_name, "r") as content:
            thrift_json = content.read()
    except Exception:
        print("Error reading config file: " + file_name)
        return

    data = json.loads(thrift_json)
    if "linkOptParams" in data:
        print("Loading legacy fw_cfg file: " + file_name)
        node_fw_params = fwOptParamsTypes.NodeFwParams()
        node_fw_params.readFromJson(thrift_json)
        return node_fw_params.linkOptParams
    else:
        nodeConfig = configTypes.NodeConfig()
        nodeConfig.readFromJson(thrift_json)
        return nodeConfig.linkParamsBase.fwParams


def msgToStr(msgType):
    return ctrlTypes.MessageType._VALUES_TO_NAMES.get(msgType, "UNKNOWN")


class BaseCmd:
    def __init__(self, cli_opts):
        # Some socket constants.
        self._driver_if_host = cli_opts.driver_if_host
        self._driver_if_pair_port = cli_opts.driver_if_pair_port
        self._timeout = cli_opts.timeout
        self._radio_mac = cli_opts.radio_mac
        self._context = zmq.Context()
        self._socket = self._context.socket(zmq.PAIR)
        # node config takes up to 30s with gps
        self._socket.SNDTIMEO = 4000  # ms
        self._socket.linger = 250  # ms
        # initialize poll set
        self._poller = zmq.Poller()
        self._poller.register(self._socket, zmq.POLLIN)

    def _connect_to_driver_if(self):
        print("... connecting to DriverIf")
        # Enable IPv6 on the socket
        try:
            self._socket.set(zmq.IPV6, 1)
        except Exception as ex:
            self._socket.close()
            self._my_exit(False, "Error creating socket. %s" % ex)

        # Connect to the pair socket
        try:
            pair_sock_url = "tcp://%s:%s" % (
                self._driver_if_host,
                self._driver_if_pair_port,
            )
            self._socket.connect(pair_sock_url)
        except Exception as ex:
            self._socket.close()
            self._my_exit(False, "Error connecting to DriverIf. %s" % ex)

    # util function used by recv_msg, recv_all_msgs
    # it receives a message -> deserialize to ctrlTypes.Message()
    # returns a ctrlTypes.Message() struct
    def __recv_from_driver_if(self):
        # receive message
        self._socket.RCVTIMEO = 1000 * self._timeout
        try:
            ser_msg = self._socket.recv()
        except Exception as ex:
            _log.error("Error receiving response: %s (%s)" % (ex, type(ex)))
            _log.info(
                "Note: r2d2 commands will fail if e2e_minion or another "
                + "r2d2 instance is running."
            )
            self._my_exit(False)

        # deserialize message
        deser_msg = ctrlTypes.Message()
        try:
            deserialize(ser_msg, deser_msg)
        except Exception as ex:
            self._my_exit(False, "Error receiving response: %s (%s)" % (ex, type(ex)))

        return deser_msg

    # deserialize msg (Message.value) -> DriverMessage, then fills msg_data by
    # deserializing DriverMessage.value and returns DriverMessage.radioMac
    def _deserialize_driver_msg(self, data, msg_data):
        dr_msg = drvrTypes.DriverMessage()
        deserialize(data, dr_msg)
        deserialize(dr_msg.value, msg_data)
        return dr_msg.radioMac.decode("utf-8")

    def _send_msg(self, msg_type, msg_data):
        msg_type_str = msgToStr(msg_type)
        print("... sending %s to DriverIf" % (msg_type_str))

        # prepare message
        dr_msg = drvrTypes.DriverMessage()
        dr_msg.radioMac = self._radio_mac
        if msg_data is not None:
            dr_msg.value = serialize(msg_data)
        msg = ctrlTypes.Message(msg_type, serialize(dr_msg))

        # send message
        try:
            self._socket.send(serialize(msg))
        except Exception as ex:
            self._my_exit(
                False, "Error sending %s to DriverIf. %s" % (msg_type_str, ex)
            )

    # receive one expect_msg_type message and fill out msg_data
    def _recv_msg(self, expect_msg_type, msg_data):
        msg_type_str = msgToStr(expect_msg_type)
        print("... receiving %s from DriverIf" % (msg_type_str))
        end_time = time.time() + self._timeout

        while time.time() <= end_time:
            deser_msg = self.__recv_from_driver_if()

            # skip some messages to reduce verbosity
            if (
                expect_msg_type not in FILTER_RCV_MSG_TYPES
                and deser_msg.mType in FILTER_RCV_MSG_TYPES
            ):
                continue

            try:
                radio_mac = self._deserialize_driver_msg(deser_msg.value, msg_data)
            except Exception as ex:
                self._my_exit(False, "Error reading response. %s" % ex)

            # check if received radio MAC is expected
            if self._radio_mac and radio_mac != self._radio_mac:
                print(
                    "Skipping {} in _recv_msg from unexpected radio MAC {}"
                    ", waiting for {}".format(
                        msgToStr(deser_msg.mType), radio_mac, self._radio_mac
                    )
                )
                continue

            # check if received message type is expected
            if deser_msg.mType != expect_msg_type:
                print(
                    "Skipping received message type {}, waiting for {}".format(
                        msgToStr(deser_msg.mType), msgToStr(expect_msg_type)
                    )
                )
                continue

            return

        self._my_exit(False, "Error: Command Time Out!")

    def _recv_link_msg(self, expect_msg_type, expect_mac_addr, msg_data):
        end_time = time.time() + self._timeout
        while time.time() <= end_time:
            self._recv_msg(expect_msg_type, msg_data)
            # In Python 3 this can be bytes - so decode if so
            if isinstance(msg_data.macAddr, bytes):
                msg_data.macAddr = msg_data.macAddr.decode("utf-8")
            if msg_data.macAddr != expect_mac_addr:
                print(
                    "Skipping message in _recv_link_msg for unexpected "
                    + "link-mac {} waiting for link-mac {}".format(
                        msg_data.macAddr, expect_mac_addr
                    )
                )
                continue
            return
        self._my_exit(False, "Error: Command Time Out!")

    # this takes in a list of expected message types (can be duplicate)
    # returns a list of received messages (not necessarily in same input order)
    def _recv_all_msgs(self, expect_msg_types):
        print(
            "... receiving %s from DriverIf" % ([msgToStr(x) for x in expect_msg_types])
        )
        msgs = []
        end_time = time.time() + self._timeout

        while time.time() <= end_time:
            deser_msg = self.__recv_from_driver_if()

            # skip some messages to reduce verbosity
            if (
                deser_msg.mType not in expect_msg_types
                and deser_msg.mType in FILTER_RCV_MSG_TYPES
            ):
                continue

            dr_msg = drvrTypes.DriverMessage()
            try:
                deserialize(deser_msg.value, dr_msg)
            except Exception as ex:
                self._my_exit(False, "Error reading response. %s" % ex)

            # In Python 3 this can be bytes - so decode if so
            if isinstance(dr_msg.radioMac, bytes):
                dr_msg.radioMac = dr_msg.radioMac.decode("utf-8")

            # check if received radio MAC is expected
            if self._radio_mac and dr_msg.radioMac != self._radio_mac:
                print(
                    "Skipping {} in _recv_all_msgs from unexpected radio MAC {}"
                    ", waiting for {}".format(
                        msgToStr(deser_msg.mType), dr_msg.radioMac, self._radio_mac
                    )
                )
                continue

            # check if received message type is expected
            if deser_msg.mType not in expect_msg_types:
                print("Unexpected message type {}".format(msgToStr(deser_msg.mType)))
                continue

            expect_msg_types.remove(deser_msg.mType)
            msgs.append(deser_msg)
            print("received %s." % (msgToStr(deser_msg.mType)))

            if len(expect_msg_types) == 0:
                return msgs

        self._my_exit(False, "Error: Command Time Out!")

    def _my_exit(self, status, error_msg="", operation=None, ifname=None):
        if not operation:
            operation = type(self).__name__
        if status:
            if ifname is not None:
                print("%s succeeded on %s" % (operation, ifname))
            else:
                print("%s succeeded." % operation)
            sys.exit(0)
        else:
            print("%s failed. %s" % (operation, error_msg))
            sys.exit(1)


class NodeInitCmd(BaseCmd):
    def __init__(self, cli_opts, config_file):
        BaseCmd.__init__(self, cli_opts)
        self._config_file = config_file

    def run(self):
        self._connect_to_driver_if()

        radio_fw_params = loadRadioFwParamsThriftFromJson(self._config_file)
        self._send_msg(
            ctrlTypes.MessageType.NODE_INIT,
            drvrTypes.DriverNodeInitReq(radio_fw_params),
        )

        node_init_notif = drvrTypes.DriverNodeInitNotif()
        self._recv_msg(ctrlTypes.MessageType.NODE_INIT_NOTIFY, node_init_notif)
        self._my_exit(node_init_notif.success)


class LinkAssocCmd(BaseCmd):
    def __init__(self, cli_opts, config_file, responder_mac, backwards_compatible):
        BaseCmd.__init__(self, cli_opts)
        self._responder_mac = responder_mac
        self._config_file = config_file
        self._backwards_compatible = backwards_compatible

    def run(self):
        self._connect_to_driver_if()

        self._send_msg(
            ctrlTypes.MessageType.DR_DEV_ALLOC_REQ,
            drvrTypes.DriverDevAllocReq(self._responder_mac),
        )

        dev_alloc_res = drvrTypes.DriverDevAllocRes()
        self._recv_link_msg(
            ctrlTypes.MessageType.DR_DEV_ALLOC_RES, self._responder_mac, dev_alloc_res
        )

        if not dev_alloc_res.success:
            self._my_exit(False, "Unable to reserve terragraph interface")

        dr_link_status = self.send_assoc()

        # make assoc TAS compatible
        if (
            self._backwards_compatible
            and dr_link_status.linkDownCause
            == drvrTypes.LinkDownCause.CHANNEL_NOT_CONFIGURED
        ):
            print("Channel is not configured, configuring to default channel")
            channelCfgMsg = passThruTypes.PassThruMsg()
            channelCfgMsg.msgType = passThruTypes.PtMsgTypes.SB_CHANNEL_CONFIG
            channelCfgMsg.dest = passThruTypes.PtMsgDest.SB
            channelCfgMsg.channelCfg = passThruTypes.ChannelConfig()
            channelCfgMsg.channelCfg.channel = 2
            self._send_msg(
                ctrlTypes.MessageType.FW_SET_NODE_PARAMS,
                drvrTypes.FwSetNodeParams([channelCfgMsg]),
            )
            fw_ack = drvrTypes.FwAck()
            self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
            time.sleep(1)
            print("retrying assoc")
            dr_link_status = self.send_assoc()

        if dr_link_status.valid:
            self._my_exit(
                dr_link_status.drLinkStatusType
                == drvrTypes.DriverLinkStatusType.LINK_UP,
                ifname=dr_link_status.ifname.decode(),
            )
        else:
            self._my_exit(False, "Received invalid driver link status")

    def send_assoc(self):
        link_fw_params = loadLinkFwParamsThriftFromJson(self._config_file)
        self._send_msg(
            ctrlTypes.MessageType.DR_SET_LINK_STATUS,
            drvrTypes.DriverSetLinkStatus(True, self._responder_mac, link_fw_params),
        )

        dr_link_status = drvrTypes.DriverLinkStatus()
        self._recv_link_msg(
            ctrlTypes.MessageType.DR_LINK_STATUS, self._responder_mac, dr_link_status
        )

        return dr_link_status


class LinkDissocCmd(BaseCmd):
    def __init__(self, cli_opts, responder_mac):
        BaseCmd.__init__(self, cli_opts)
        self._responder_mac = responder_mac

    def run(self):
        self._connect_to_driver_if()

        self._send_msg(
            ctrlTypes.MessageType.DR_SET_LINK_STATUS,
            drvrTypes.DriverSetLinkStatus(
                False, self._responder_mac, fwOptParamsTypes.FwOptParams()
            ),
        )

        # two cases:
        # 1. dissoc on unignited link: FW_ACK, LINK_PAUSE
        # 2. dissoc on ignited link: FW_ACK, LINK_PAUSE, LINK_DOWN

        dr_link_status = drvrTypes.DriverLinkStatus()
        self._recv_link_msg(
            ctrlTypes.MessageType.DR_LINK_STATUS, self._responder_mac, dr_link_status
        )

        if dr_link_status.valid:
            print(
                "Received driver link status: "
                + drvrTypes.DriverLinkStatusType._VALUES_TO_NAMES.get(
                    dr_link_status.drLinkStatusType, "UNKNOWN"
                )
            )
        else:
            self._my_exit(False, "Received invalid driver link status")

        # case 1
        if dr_link_status.drLinkStatusType == drvrTypes.DriverLinkStatusType.LINK_DOWN:
            self._my_exit(True)

        # case 2: expect one more LINK_DOWN
        self._recv_link_msg(
            ctrlTypes.MessageType.DR_LINK_STATUS, self._responder_mac, dr_link_status
        )
        if dr_link_status.valid:
            self._my_exit(
                dr_link_status.drLinkStatusType
                == drvrTypes.DriverLinkStatusType.LINK_DOWN
            )
        else:
            self._my_exit(False, "Received invalid driver link status")


class AirtimeAllocateCmd(BaseCmd):
    def __init__(self, cli_opts, airtime_alloc_file):
        BaseCmd.__init__(self, cli_opts)
        self._airtime_alloc_file = airtime_alloc_file

    def run(self):
        self._connect_to_driver_if()

        airtime_map = bwallocationTypes.NodeAirtime()
        loadThriftFromJson(self._airtime_alloc_file, airtime_map)

        pass_through_message = passThruTypes.PassThruMsg()
        pass_through_message.msgType = passThruTypes.PtMsgTypes.SB_AIRTIMEALLOC
        pass_through_message.dest = passThruTypes.PtMsgDest.SB
        pass_through_message.airtimeAllocMap = airtime_map

        self._send_msg(
            ctrlTypes.MessageType.FW_SET_NODE_PARAMS,
            drvrTypes.FwSetNodeParams([pass_through_message]),
        )

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class NodeFwStatsCmd(BaseCmd):
    def __init__(self, cli_opts, driver_if_pub_port, radio_mac, poll_time):
        # Some socket constants.
        self._driver_if_host = cli_opts.driver_if_host
        self._driver_if_pub_port = driver_if_pub_port
        self._context = zmq.Context()
        self._socket = self._context.socket(zmq.SUB)
        self._socket.RCVTIMEO = 200000  # ms
        self._socket.SNDTIMEO = 4000  # ms
        self._socket.linger = 250  # ms
        # initialize poll set
        self._poller = zmq.Poller()
        self._poller.register(self._socket, zmq.POLLIN)
        self._radio_mac = radio_mac
        self._poll_time = poll_time

    def _connect_to_driver_if_pub(self):
        # Enable IPv6 on the socket
        try:
            self._socket.set(zmq.IPV6, 1)
        except Exception as ex:
            self._socket.close()
            self._my_exit(False, "Error creating socket. %s" % ex)

        # Connect to the pub socket
        try:
            pub_sock_url = "tcp://%s:%s" % (
                self._driver_if_host,
                self._driver_if_pub_port,
            )
            self._socket.connect(pub_sock_url)
            self._socket.setsockopt_string(zmq.SUBSCRIBE, "")
        except Exception as ex:
            self._socket.close()
            self._my_exit(False, "Error connecting to DriverIf. %s" % ex)

    def run(self):
        print("Connecting to DriverIf publication port")
        self._connect_to_driver_if_pub()

        # loop forever
        start_time = time.monotonic()
        while True:
            now = time.monotonic()
            if self._poll_time and now - start_time >= self._poll_time:
                break

            try:
                if self._poll_time:
                    socks = dict(self._poller.poll(1000))
                else:
                    socks = dict(self._poller.poll())
            except KeyboardInterrupt:
                break

            if self._socket not in socks:
                continue

            try:
                message = self._socket.recv()
            except Exception as ex:
                self._my_exit(False, "Error receiving response. %s" % ex)

            response = monitorTypes.MonitorPub()
            try:
                deserialize(message, response)
            except Exception as ex:
                print("Error parsing response:", ex, file=sys.stderr)
                sys.exit(3)

            if response.pubType != monitorTypes.PubType.COUNTER_PUB:
                continue

            # display stats
            for name, counter in response.counterPub.counters.items():
                name = name.decode("utf-8")
                entity = ""

                if "\0" in name:
                    name, entity = name.split("\0")

                if self._radio_mac and self._radio_mac != entity:
                    continue  # filter out other baseband stats

                if self._radio_mac or not entity:
                    print(counter.timestamp, name, int(counter.value), sep=", ")
                else:
                    print(counter.timestamp, name, int(counter.value), entity, sep=", ")


class FwStatsConfigCmd(BaseCmd):
    def __init__(self, cli_opts, config_file):
        BaseCmd.__init__(self, cli_opts)
        self._config_file = config_file

    def run(self):
        self._connect_to_driver_if()

        stats_configure = passThruTypes.StatsConfigure()
        loadThriftFromJson(self._config_file, stats_configure)

        self._send_msg(ctrlTypes.MessageType.FW_STATS_CONFIGURE_REQ, stats_configure)

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class FwSetParamsCmd(BaseCmd):
    def __init__(self, cli_opts, parameters, responder_mac, bwgdIdx, show_list):
        BaseCmd.__init__(self, cli_opts)
        self._parameters = parameters
        self._responder_mac = responder_mac
        self._show_list = show_list
        self._bwgdIdx = bwgdIdx

        # These are the fw parameters that can be configured at run-time
        # List of Link related params that require a responder MAC
        self._linkParamsList = [
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
            "tpcPBEnable",
        ]

        # List of Node related params that don't require a responder MAC
        self._nodeParamsList = [
            "forceGpsDisable",
            "topoScanEnable",
            "crsScale",
            "maxAgcUseSameForAllSta",
            "polarity",
            "mtpoEnabled",
            "htsfMsgInterval",
        ]

    def run(self):
        self._connect_to_driver_if()

        if self._show_list:
            print("f/w parameters that can be set at run-time: ")
            print("=========================================== ")
            print("\nNode specific :")
            print("---------------")
            print("\n".join(self._nodeParamsList))
            print('\nLink specific ("--responder_mac" option required) :')
            print("--------------------------------------------------")
            print("\n".join(self._linkParamsList))
            self._my_exit(1)

        if len(self._parameters) == 0 or len(self._parameters) % 2 != 0:
            print("Error : Invalid arguments list")
            print("Please enter arguments as following: [paramName] [paramValue] ...")
            self._my_exit(0)

        fw_opt_params = fwOptParamsTypes.FwOptParams()

        for i in range(0, len(self._parameters), 2):
            if (
                self._responder_mac == EMPTY_MAC_ADDRESS
                and self._parameters[i] in self._linkParamsList
            ):
                print(
                    'Error : "--responder_mac" option required for setting '
                    + self._parameters[i]
                )
                self._my_exit(0)
            elif hasattr(fw_opt_params, self._parameters[i]):
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
                self._my_exit(0)

        pass_through_message = passThruTypes.PassThruMsg()
        pass_through_message.msgType = passThruTypes.PtMsgTypes.SB_SET_FW_PARAMS
        pass_through_message.dest = passThruTypes.PtMsgDest.SB
        pass_through_message.setfwParamsReq = passThruTypes.SetFwParamsMsg()
        pass_through_message.setfwParamsReq.addr = self._responder_mac
        pass_through_message.setfwParamsReq.optionalParams = fw_opt_params
        pass_through_message.setfwParamsReq.bwgdIdx = self._bwgdIdx

        self._send_msg(
            ctrlTypes.MessageType.FW_CONFIG_REQ,
            drvrTypes.FwConfigParams([pass_through_message]),
        )

        resp = passThruTypes.SetFwParamsResp()
        self._recv_msg(ctrlTypes.MessageType.FW_CONFIG_RESP, resp)
        if resp.status:
            print("Setting parameters succeeded")
        else:
            print("ERROR:Setting one of the requested parameters failed")

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class FwGetParamsCmd(BaseCmd):
    def __init__(self, cli_opts, param_type, responder_mac):
        BaseCmd.__init__(self, cli_opts)
        self._responder_mac = responder_mac
        self._param_type = param_type
        self._fwParamType = 0

    def getNodeParams(self):
        # Map param type option to the enum
        nodeFwParamTypes = {
            "nodeFwCfg": passThruTypes.FwParamsType.FW_PARAMS_NODE_FW_CFG
            # other param type should go here
        }

        self._fwParamType = nodeFwParamTypes[self._param_type]
        print(
            "Getting node parameters of type", self._param_type, ":", self._fwParamType
        )
        self._send_fw_get_params_cmd()

    def getLinkParams(self):
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
        self._send_fw_get_params_cmd()

    def run(self):
        pass

    def _print_opt_params(self, optParams):
        out = ("%s" % optParams).split(",")
        print("\n---start of list ---\n")
        for item in out:
            if "=None" not in item:
                print(item)
        print("\n---end of list ---\n")

    def _send_fw_get_params_cmd(self):

        self._connect_to_driver_if()

        pass_through_message = passThruTypes.PassThruMsg()
        pass_through_message.msgType = passThruTypes.PtMsgTypes.SB_GET_FW_PARAMS
        pass_through_message.dest = passThruTypes.PtMsgDest.SB
        pass_through_message.getFwParamsReq = passThruTypes.GetFwParamsReq()
        pass_through_message.getFwParamsReq.addr = self._responder_mac
        pass_through_message.getFwParamsReq.requestedParamsType = self._fwParamType

        self._send_msg(
            ctrlTypes.MessageType.FW_CONFIG_REQ,
            drvrTypes.FwConfigParams([pass_through_message]),
        )

        # Receive the response from the node
        print("Receiving response ...")
        resp = passThruTypes.GetFwParamsResp()
        self._recv_msg(ctrlTypes.MessageType.FW_CONFIG_RESP, resp)
        print("Received parameters of type : ", resp.fwParamsType)
        print("Current BWGD = ", resp.bwgdIdx)

        if resp.fwParamsType in [
            passThruTypes.FwParamsType.FW_PARAMS_NODE_FW_CFG,
            passThruTypes.FwParamsType.FW_PARAMS_LINK_FW_CFG,
        ]:
            self._print_opt_params(resp.optParams)
        elif resp.fwParamsType is passThruTypes.FwParamsType.FW_PARAMS_INVALID:
            print("ERROR:  Unable to get requested params")

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class PhyLAConfigCmd(BaseCmd):
    def __init__(self, cli_opts, config_file, responder_mac):
        BaseCmd.__init__(self, cli_opts)
        self._config_file = config_file
        self._responder_mac = responder_mac

    def run(self):
        self._connect_to_driver_if()

        tbl_cfg = passThruTypes.PhyLAParams()
        loadThriftFromJson(self._config_file, tbl_cfg)

        pass_through_message = passThruTypes.PassThruMsg()
        pass_through_message.msgType = passThruTypes.PtMsgTypes.SB_PHY_LA_CONFIG
        pass_through_message.dest = passThruTypes.PtMsgDest.SB
        pass_through_message.phyLAConfig = passThruTypes.PhyLAConfig()
        pass_through_message.phyLAConfig.addr = self._responder_mac
        pass_through_message.phyLAConfig.laParams = tbl_cfg.laParams
        pass_through_message.phyLAConfig.laNodeParams = tbl_cfg.laNodeParams

        self._send_msg(
            ctrlTypes.MessageType.FW_CONFIG_REQ,
            drvrTypes.FwConfigParams([pass_through_message]),
        )

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class PhyAgcConfigCmd(BaseCmd):
    def __init__(self, cli_opts, config_file, responder_mac):
        BaseCmd.__init__(self, cli_opts)
        self._config_file = config_file
        self._responder_mac = responder_mac

    def run(self):
        self._connect_to_driver_if()

        tbl_cfg = passThruTypes.PhyAgcParams()
        loadThriftFromJson(self._config_file, tbl_cfg)

        pass_through_message = passThruTypes.PassThruMsg()
        pass_through_message.msgType = passThruTypes.PtMsgTypes.SB_PHY_AGC_CONFIG
        pass_through_message.dest = passThruTypes.PtMsgDest.SB
        pass_through_message.phyAgcConfig = passThruTypes.PhyAgcConfig()
        pass_through_message.phyAgcConfig.addr = self._responder_mac
        pass_through_message.phyAgcConfig.agcNodeParams = tbl_cfg.agcNodeParams
        pass_through_message.phyAgcConfig.agcLinkParams = tbl_cfg.agcLinkParams

        self._send_msg(
            ctrlTypes.MessageType.FW_CONFIG_REQ,
            drvrTypes.FwConfigParams([pass_through_message]),
        )

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class PhyTpcConfigCmd(BaseCmd):
    def __init__(self, cli_opts, config_file, responder_mac):
        BaseCmd.__init__(self, cli_opts)
        self._config_file = config_file
        self._responder_mac = responder_mac

    def run(self):
        self._connect_to_driver_if()

        tbl_cfg = passThruTypes.PhyTpcParams()
        loadThriftFromJson(self._config_file, tbl_cfg)

        phy_tpc_cfg_req = passThruTypes.PhyTpcConfig()
        phy_tpc_cfg_req.addr = self._responder_mac
        phy_tpc_cfg_req.tpcNodeParams = tbl_cfg.tpcNodeParams
        phy_tpc_cfg_req.tpcLinkParams = tbl_cfg.tpcLinkParams

        self._send_msg(ctrlTypes.MessageType.PHY_TPC_CONFIG_REQ, phy_tpc_cfg_req)

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class PhyTpcAdjustmentTblConfigCmd(BaseCmd):
    def __init__(self, cli_opts, config_file):
        BaseCmd.__init__(self, cli_opts)
        self._config_file = config_file

    def run(self):
        self._connect_to_driver_if()

        tbl_cfg = passThruTypes.PhyTpcAdjTblCfg()
        loadThriftFromJson(self._config_file, tbl_cfg)

        self._send_msg(ctrlTypes.MessageType.PHY_TPC_ADJ_TBL_CFG_REQ, tbl_cfg)

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class GetGpsPosCmd(BaseCmd):
    def __init__(self, cli_opts):
        BaseCmd.__init__(self, cli_opts)

    def run(self):
        self._connect_to_driver_if()

        self._send_msg(ctrlTypes.MessageType.GPS_GET_POS_REQ, None)
        location = topologyTypes.Location()
        self._recv_msg(ctrlTypes.MessageType.GPS_GET_POS_RESP, location)
        print(
            "latitude=%f, longitude=%f, height=%f, accuracy=%f"
            % (
                location.latitude,
                location.longitude,
                location.altitude,
                location.accuracy,
            )
        )


class SetGpsPosCmd(BaseCmd):
    def __init__(self, cli_opts, latitude, longitude, height, accuracy):
        BaseCmd.__init__(self, cli_opts)
        self._latitude = latitude
        self._longitude = longitude
        self._height = height
        self._accuracy = accuracy

    def run(self):
        self._connect_to_driver_if()

        req = drvrTypes.FwSetNodeParams()
        req.location = topologyTypes.Location()
        req.location.latitude = self._latitude
        req.location.longitude = self._longitude
        req.location.altitude = self._height
        req.location.accuracy = self._accuracy

        self._send_msg(ctrlTypes.MessageType.FW_SET_NODE_PARAMS, req)
        driverAck = drvrTypes.DriverAck()
        self._recv_msg(ctrlTypes.MessageType.DR_ACK, driverAck)
        self._my_exit(driverAck.success)


class GpsEnableCmd(BaseCmd):
    def __init__(self, cli_opts):
        BaseCmd.__init__(self, cli_opts)

    def run(self):
        self._connect_to_driver_if()

        self._send_msg(ctrlTypes.MessageType.GPS_ENABLE_REQ, None)
        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class GpsSendTimeCmd(BaseCmd):
    def __init__(self, cli_opts, gps_time):
        BaseCmd.__init__(self, cli_opts)
        self._gps_time = int(gps_time, 0)

    def run(self):
        self._connect_to_driver_if()
        gpsTimeValue = passThruTypes.GpsTimeValue()
        gpsTimeValue.unixTimeSecs = self._gps_time
        gpsTimeValue.unixTimeNsecs = 0
        self._send_msg(ctrlTypes.MessageType.GPS_SEND_TIME, gpsTimeValue)

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class PolarityConfigCmd(BaseCmd):
    def __init__(self, cli_opts, polarity):
        BaseCmd.__init__(self, cli_opts)
        self._polarity = polarity

    def run(self):
        self._connect_to_driver_if()

        pass_through_message = passThruTypes.PassThruMsg()
        pass_through_message.msgType = passThruTypes.PtMsgTypes.SB_POLARITY
        pass_through_message.dest = passThruTypes.PtMsgDest.SB
        pass_through_message.polarityCfg = passThruTypes.PolarityConfig()
        pass_through_message.polarityCfg.polarity = self._polarity

        self._send_msg(
            ctrlTypes.MessageType.FW_SET_NODE_PARAMS,
            drvrTypes.FwSetNodeParams([pass_through_message]),
        )

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class GolayConfigCmd(BaseCmd):
    def __init__(self, cli_opts, tx_index, rx_index):
        BaseCmd.__init__(self, cli_opts)
        self._tx_index = tx_index
        self._rx_index = rx_index

    def run(self):
        self._connect_to_driver_if()

        pass_through_message = passThruTypes.PassThruMsg()
        pass_through_message.msgType = passThruTypes.PtMsgTypes.SB_GOLAY_INDX
        pass_through_message.dest = passThruTypes.PtMsgDest.SB
        pass_through_message.golayCfg = passThruTypes.GolayConfig()
        pass_through_message.golayCfg.txGolayIndx = self._tx_index
        pass_through_message.golayCfg.rxGolayIndx = self._rx_index

        self._send_msg(
            ctrlTypes.MessageType.FW_SET_NODE_PARAMS,
            drvrTypes.FwSetNodeParams([pass_through_message]),
        )

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class BfSlotExclusionReqCmd(BaseCmd):
    def __init__(self, cli_opts, bwgdIdx):
        BaseCmd.__init__(self, cli_opts)
        self._bwgdIdx = bwgdIdx

    def run(self):
        self._connect_to_driver_if()

        pass_through_message = passThruTypes.PassThruMsg()
        pass_through_message.msgType = passThruTypes.PtMsgTypes.SB_BF_SLOT_EXCLUSION_REQ
        pass_through_message.dest = passThruTypes.PtMsgDest.SB
        pass_through_message.bfSlotExclusionReq = ctrlTypes.BfSlotExclusionReq()
        pass_through_message.bfSlotExclusionReq.startBwgdIdx = self._bwgdIdx

        self._send_msg(
            ctrlTypes.MessageType.FW_SET_NODE_PARAMS,
            drvrTypes.FwSetNodeParams([pass_through_message]),
        )

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class PhyGolaySequenceConfigCmd(BaseCmd):
    def __init__(self, cli_opts, config_file):
        BaseCmd.__init__(self, cli_opts)
        self._config_file = config_file

    def run(self):
        self._connect_to_driver_if()

        golay_seq_cfg = passThruTypes.PhyGolaySequenceConfig()
        loadThriftFromJson(self._config_file, golay_seq_cfg)

        phy_golay_seq_cfg_req = drvrTypes.PhyGolaySequenceConfigReq()
        phy_golay_seq_cfg_req.transmitSequence = golay_seq_cfg.transmitSequence
        phy_golay_seq_cfg_req.receiveSequence = golay_seq_cfg.receiveSequence

        self._send_msg(
            ctrlTypes.MessageType.PHY_GOLAY_SEQUENCE_CONFIG_REQ, phy_golay_seq_cfg_req
        )

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        if fw_ack.success is False:
            self._my_exit(fw_ack.success)


class PhyAntWgtCodeBookConfigCmd(BaseCmd):
    def __init__(self, cli_opts, config_file):
        BaseCmd.__init__(self, cli_opts)
        self._config_file = config_file

    def run(self):
        self._connect_to_driver_if()

        tbl_cfg = passThruTypes.PhyAntWgtCodeBookConfig()
        loadThriftFromJson(self._config_file, tbl_cfg)

        self._send_msg(ctrlTypes.MessageType.FW_SET_CODEBOOK, tbl_cfg)

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class DebugCmd(BaseCmd):
    def __init__(self, cli_opts, command, value):
        BaseCmd.__init__(self, cli_opts)
        self._command = command
        self._value = value

    def run(self):
        self._connect_to_driver_if()

        debug = passThruTypes.Debug()
        debug.cmdStr = self._command
        debug.value = self._value
        self._send_msg(ctrlTypes.MessageType.FW_DEBUG_REQ, debug)

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class ScanCmd(BaseCmd):
    def __init__(
        self,
        cli_opts,
        token,
        scanType,
        scanMode,
        bwgdIdx,
        tx,
        bfScanInvertPolarity,
        beams,
        apply,
        subType,
        bwgdLen,
        txPowerIndex,
        peer,
        nullAngle,
        cbfBeamIdx,
        aggressor,
    ):
        BaseCmd.__init__(self, cli_opts)
        self._token = token
        self._scanType = ctrlTypes.ScanType._NAMES_TO_VALUES[scanType.upper()]
        self._scanMode = ctrlTypes.ScanMode._NAMES_TO_VALUES[scanMode.upper()]
        self._bwgdIdx = bwgdIdx
        self._isTx = tx
        self._bfScanInvertPolarity = bfScanInvertPolarity
        self._beams = beams
        self._apply = apply
        self._subType = ctrlTypes.ScanSubType._NAMES_TO_VALUES[subType.upper()]
        self._bwgdLen = bwgdLen
        self._txPowerIndex = txPowerIndex
        self._peer = peer
        self._nullAngle = nullAngle
        self._cbfBeamIdx = cbfBeamIdx
        self._aggressor = aggressor

    def run(self):
        self._connect_to_driver_if()

        req = ctrlTypes.ScanReq()
        req.token = self._token
        req.scanType = self._scanType
        if self._scanMode == ctrlTypes.ScanMode.SELECTIVE and not self._beams:
            self._my_exit(False, "Selective scan mode requires beam indices (--beams)")
        if self._scanType != ctrlTypes.ScanType.TOPO:
            req.apply = self._apply
            req.subType = self._subType
            req.scanMode = self._scanMode
            req.bwgdLen = self._bwgdLen
        req.startBwgdIdx = self._bwgdIdx
        req.bfScanInvertPolarity = self._bfScanInvertPolarity

        if self._beams:
            beams = ctrlTypes.BeamIndices()
            beams.low = self._beams[0]
            beams.high = self._beams[1]
            req.beams = beams

        req.txPwrIndex = self._txPowerIndex
        if self._nullAngle:
            req.nullAngle = self._nullAngle
        if self._cbfBeamIdx:
            req.cbfBeamIdx = self._cbfBeamIdx
        if self._aggressor:
            req.isAggressor = self._aggressor

        if (req.txPwrIndex < 0) or (req.txPwrIndex > 31):
            self._my_exit(
                False, "txPowerIndex is optional, should be within the limit of [0, 31]"
            )

        if self._scanType == ctrlTypes.ScanType.TOPO:
            req.rxNodeMac = BROADCAST_MAC_ADDRESS
        elif self._isTx:
            req.rxNodeMac = self._peer
        else:
            req.txNodeMac = self._peer

        # send request
        self._send_msg(ctrlTypes.MessageType.SCAN_REQ, req)
        print(Serializer.serialize(TSimpleJSONProtocolFactory(), req).decode("utf-8"))

        # wait for responses
        resps = self._recv_all_msgs(
            [ctrlTypes.MessageType.FW_ACK, ctrlTypes.MessageType.SCAN_RESP]
        )
        fw_ack = drvrTypes.FwAck()
        self._deserialize_driver_msg(
            next(x for x in resps if x.mType == ctrlTypes.MessageType.FW_ACK).value,
            fw_ack,
        )
        scan_resp = ctrlTypes.ScanResp()
        self._deserialize_driver_msg(
            next(x for x in resps if x.mType == ctrlTypes.MessageType.SCAN_RESP).value,
            scan_resp,
        )
        print(
            Serializer.serialize(TSimpleJSONProtocolFactory(), scan_resp).decode(
                "utf-8"
            )
        )

        self._my_exit(fw_ack.success)


class ChannelConfigCmd(BaseCmd):
    def __init__(self, cli_opts, channel):
        BaseCmd.__init__(self, cli_opts)
        self._channel = channel

    def run(self):
        self._connect_to_driver_if()

        channelCfgMsg = passThruTypes.PassThruMsg()
        channelCfgMsg.msgType = passThruTypes.PtMsgTypes.SB_CHANNEL_CONFIG
        channelCfgMsg.dest = passThruTypes.PtMsgDest.SB
        channelCfgMsg.channelCfg = passThruTypes.ChannelConfig()
        channelCfgMsg.channelCfg.channel = self._channel
        self._send_msg(
            ctrlTypes.MessageType.FW_SET_NODE_PARAMS,
            drvrTypes.FwSetNodeParams([channelCfgMsg]),
        )

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class BfRespScanConfigCmd(BaseCmd):
    def __init__(self, cli_opts, cfg):
        BaseCmd.__init__(self, cli_opts)
        self._cfg = cfg

    def run(self):
        self._connect_to_driver_if()

        bfRespScanCfg = passThruTypes.BfRespScanConfig()
        bfRespScanCfg.cfg = self._cfg
        self._send_msg(ctrlTypes.MessageType.FW_BF_RESP_SCAN, bfRespScanCfg)

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)


class FwSetLogConfigCmd(BaseCmd):
    def __init__(self, cli_opts, module, level, show_list):
        BaseCmd.__init__(self, cli_opts)
        self._modules = [m.upper() for m in module]
        self._level = level.upper()
        self._show_list = show_list

    def run(self):
        self._connect_to_driver_if()

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
            self._my_exit(False)

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
                self._my_exit(False)

        self._send_msg(ctrlTypes.MessageType.FW_SET_LOG_CONFIG, set_log_config)

        fw_ack = drvrTypes.FwAck()
        self._recv_msg(ctrlTypes.MessageType.FW_ACK, fw_ack)
        self._my_exit(fw_ack.success)
