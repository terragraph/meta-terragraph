#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging

import click
import tabulate
from fbzmq.Monitor import ttypes as monitorTypes
from tg.commands import base

# fastproto is not used directly, but required by Thrift to accelerate
# encoding/decoding
# @lint-ignore FLAKE8 F401
from thrift.protocol import fastproto  # noqa: F401
from thrift.protocol.TCompactProtocol import TCompactProtocolAcceleratedFactory
from thrift.util import Serializer


_log = logging.getLogger(__name__)


class CountersCli(object):
    def __init__(self):
        self.counters.add_command(self._minion, name="minion")
        self.counters.add_command(self._driverif, name="driverif")
        self.counters.add_command(self._ctrl, name="ctrl")

    @click.group()
    def counters():
        """ Counters from controller/minion/driver/fw """
        pass

    @click.command()
    @click.option(
        "--minion_monitor_port",
        "-mp",
        default=17007,
        type=int,
        help="minion monitor port to talk to (default 17007)",
    )
    @click.option(
        "--name",
        "-n",
        type=str,
        multiple=True,
        help="get specific counter(s) by name "
        "(requests full counter dump by default)",
    )
    @click.pass_obj
    def _minion(cli_opts, minion_monitor_port, name):
        """ Show counters from e2e minions """
        CountersCmd(cli_opts, minion_monitor_port, name).minionCounters()

    @click.command()
    @click.option(
        "--driver_monitor_port",
        "-mp",
        default=17008,
        type=int,
        help="driver monitor port to talk to (default 17008)",
    )
    @click.option(
        "--name",
        "-n",
        type=str,
        multiple=True,
        help="get specific counter(s) by name "
        "(requests full counter dump by default)",
    )
    @click.pass_obj
    def _driverif(cli_opts, driver_monitor_port, name):
        """ Show counters from DriverIf """
        CountersCmd(cli_opts, driver_monitor_port, name).driverIfCounters()

    @click.command()
    @click.option(
        "--ctrl_monitor_port",
        "-mp",
        default=27007,
        type=int,
        help="ctrl monitor port to talk to (default 27007)",
    )
    @click.option(
        "--name",
        "-n",
        type=str,
        multiple=True,
        help="get specific counter(s) by name "
        "(requests full counter dump by default)",
    )
    @click.pass_obj
    def _ctrl(cli_opts, ctrl_monitor_port, name):
        """ Show counters from e2e controller """
        CountersCmd(cli_opts, ctrl_monitor_port, name).ctrlCounters()


class CountersCmd(base.BaseCmd):
    def __init__(self, cli_opts, monitor_port, name):
        base.BaseCmd.__init__(self, cli_opts)
        self._monitor_port = monitor_port
        self._name = name

    def _get_counter(self, monitor_host, monitor_port, name):
        # Send get-counter request for stat(s)
        req = monitorTypes.MonitorRequest()
        req.cmd = monitorTypes.MonitorCommand.GET_COUNTER_VALUES
        req.counterGetParams = monitorTypes.CounterGetParams()
        req.counterGetParams.counterNames = name
        return self._send_fbzmq_monitor_req(monitor_host, monitor_port, req)

    def _dump_all_counter_data(self, monitor_host, monitor_port):
        # Send dump-all-counter-data request for all stats
        req = monitorTypes.MonitorRequest()
        req.cmd = monitorTypes.MonitorCommand.DUMP_ALL_COUNTER_DATA
        return self._send_fbzmq_monitor_req(monitor_host, monitor_port, req)

    def _send_fbzmq_monitor_req(self, monitor_host, monitor_port, req):
        # Create a dealer socket
        monitor_sock = self._connect_to_router(monitor_host, monitor_port)

        # Send counter request
        data = Serializer.serialize(TCompactProtocolAcceleratedFactory(), req)
        try:
            monitor_sock.send(data)
        except Exception as ex:
            _log.error("Error sending request to Monitor: %s" % ex)
            monitor_sock.close()
            raise

        # Receive counter response
        try:
            message = monitor_sock.recv()
            monitor_sock.close()
        except Exception as ex:
            _log.error("Error receiving response from Monitor: %s" % ex)
            monitor_sock.close()
            raise

        # Parse counter response
        response = monitorTypes.CounterValuesResponse()
        try:
            Serializer.deserialize(
                TCompactProtocolAcceleratedFactory(), message, response
            )
        except Exception as ex:
            _log.error("Error parsing response: %s" % ex)
            raise

        # Return parsed counter response
        return response

    def _get_node_counters(self, monitor_name, monitor_port):
        self._connect_to_controller()
        status_dump = self._get_status_dump()

        _log.info(
            "Received status about %d %s(s)\n",
            len(status_dump.statusReports),
            monitor_name,
        )
        for node, status_report in status_dump.statusReports.items():
            ipv6_address = status_report.ipv6Address
            if not ipv6_address:
                _log.error(
                    "%s on %s has no reachable ipv6 address - skip.", monitor_name, node
                )
                continue
            counter_resp = {}
            try:
                counter_resp = self._dump_all_counter_data(
                    "[{}]".format(ipv6_address), monitor_port
                )
            except Exception as ex:
                _log.error(
                    "%s on %s is not responding with its counters - %s",
                    monitor_name,
                    node,
                    ex,
                )
                continue

            # display stats
            _log.info(
                "Counters from %s %s [%s]:%d",
                monitor_name,
                node,
                ipv6_address,
                monitor_port,
            )
            self._display_node_counters(node, counter_resp.counters)

    def _display_node_counters(self, node, counters):
        headers = ["Node", "Counter", "Value", "TimeStamp"]
        table = []
        for name, counter in counters.items():
            # _name is a tuple containing multiple names so check every name
            if self._name and any((_name not in name) for _name in self._name):
                continue
            table.append([node, name, int(counter.value), counter.timestamp])
        print(tabulate.tabulate(table, headers))
        print("")

    def _display_ctrl_counters(self, counters):
        headers = ["Counter", "Value", "TimeStamp"]
        table = []
        for name, counter in counters.items():
            # _name is a tuple containing multiple names so check every name
            if self._name and any((_name not in name) for _name in self._name):
                continue
            table.append([name, int(counter.value), counter.timestamp])
        print(tabulate.tabulate(table, headers))
        print("")

    def ctrlCounters(self):
        # get all counters from controller monitor
        try:
            counter_resp = self._dump_all_counter_data(
                self._ctrl_host, self._monitor_port
            )
        except Exception as ex:
            self._my_exit(
                False,
                "cannot get counters from %s:%d - %s"
                % (self._ctrl_host, self._monitor_port, ex),
            )
        _log.info("Counters from controller %s:%d", self._ctrl_host, self._monitor_port)
        self._display_ctrl_counters(counter_resp.counters)

    def minionCounters(self):
        self._get_node_counters("minion", self._monitor_port)

    def driverIfCounters(self):
        self._get_node_counters("driverIf", self._monitor_port)
