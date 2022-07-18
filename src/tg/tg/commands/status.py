#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging
import operator
import time

import click
import tabulate
from tg.commands import base


_log = logging.getLogger(__name__)


class StatusCli(object):
    @click.command()
    @click.option("--loopback", "-l", is_flag=True, help="Show loopback address")
    @click.option("--mac", "-m", is_flag=True, help="Show mac address")
    @click.option("--version", "-v", is_flag=True, help="Show minion version")
    @click.option("--uboot", "-u", is_flag=True, help="Show uboot version")
    @click.option(
        "--timestamp", "-t", is_flag=True, help="Show time stamp of status report"
    )
    @click.option(
        "--openr_name", "-o", is_flag=True, help="Show node name used in opner"
    )
    @click.option("--hw_id", "-h", is_flag=True, help="Show hardware board ID")
    @click.option("--raw", is_flag=True, help="Dump raw output only")
    @click.pass_obj
    def status(
        cli_opts, loopback, mac, version, uboot, timestamp, openr_name, hw_id, raw
    ):
        """Show status of e2e minions:
        by default status shows all information except openr name
        """
        StatusCmd(
            cli_opts, loopback, mac, version, uboot, timestamp, openr_name, hw_id, raw
        ).run()


class StatusCmd(base.BaseCmd):
    def __init__(
        self, cli_opts, loopback, mac, version, uboot, timestamp, openr_name, hw_id, raw
    ):
        base.BaseCmd.__init__(self, cli_opts)
        self._loopback = loopback
        self._mac = mac
        self._version = version
        self._uboot = uboot
        self._timestamp = timestamp
        self._openr_name = openr_name
        self._hw_id = hw_id
        self._raw = raw

    def run(self):
        self._connect_to_controller()
        topology = self._get_topology()
        self._status_dump = self._get_status_dump()
        if self._raw:
            print(self._status_dump)
            return
        _log.info(
            "Received status about %d minion(s)" % len(self._status_dump.statusReports)
        )

        # set all minion related flag true
        if not any(
            [
                self._loopback,
                self._mac,
                self._version,
                self._uboot,
                self._timestamp,
                self._openr_name,
                self._hw_id,
            ]
        ):
            self._loopback = True
            self._mac = True
            self._version = True
            self._timestamp = True
            self._hw_id = True

        self._mac_to_name = {n.mac_addr: n.name for n in topology.nodes}
        headers = self._get_header()
        table = self._get_table()
        print(tabulate.tabulate(table, headers))

    def _get_header(self):
        headers = ["Node Name"]
        if self._loopback:
            headers.append("IPv6 Address")
        if self._mac:
            headers.append("MAC Address")
        if self._version:
            headers.append("Image Version")
        if self._uboot:
            headers.append("Uboot Version")
        if self._openr_name:
            headers.append("Open/R Name")
        if self._hw_id:
            headers.append("HW Board ID")
        if self._timestamp:
            headers.append("Timestamp")
        return headers

    def _get_table(self):
        table = []
        for minion, status_report in self._status_dump.statusReports.items():
            row = [
                "--"
                if self._mac_to_name.get(minion) is None
                else self._mac_to_name[minion]
            ]
            if self._loopback:
                row.append(status_report.ipv6Address)
            if self._mac:
                row.append(minion)
            if self._version:
                row.append(status_report.version)
            if self._uboot:
                row.append(status_report.ubootVersion)
            if self._openr_name:
                name = "node-{}".format(minion.replace(":", ".").lower())
                row.append(name)
            if self._hw_id:
                row.append(status_report.hardwareBoardId)
            if self._timestamp:
                row.append(
                    time.strftime(
                        "%D %H:%M:%S", time.localtime(status_report.timeStamp)
                    )
                )
            table.append(row)
        return sorted(table, key=operator.itemgetter(0))
