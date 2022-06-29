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
from terragraph_thrift.Controller import ttypes as ctrlTypes
from tg.commands import base


_log = logging.getLogger(__name__)


class UpgradeStatusCmd(base.BaseCmd):
    def __init__(self, cli_opts):
        super(UpgradeStatusCmd, self).__init__(cli_opts)

    def _sanitize_string(self, s):
        return str(s).encode("ascii", "ignore")

    def run(self):
        self._connect_to_controller()

        topology = self._get_topology()
        node_mac_to_name = {n.mac_addr: n.name for n in topology.nodes}

        status_reports = self._get_status_dump().statusReports

        headers = [
            "NodeName",
            "NodeId",
            "CurrVersion",
            "IPAddress",
            "UpgradeStatus",
            "Reason",
            "RequestId",
            "NextImageVersion",
            "NextImageMd5",
            "WhenToCommit",
        ]
        table = []
        start_time = time.strftime("%D %H:%M:%S", time.localtime(time.time()))
        _log.info("Current time: %s", start_time)
        for node, status_report in status_reports.items():
            upgrade_status = status_report.upgradeStatus
            row = map(
                self._sanitize_string,
                [
                    node_mac_to_name.get(node, "--"),
                    node,
                    status_report.version,
                    status_report.ipv6Address,
                    ctrlTypes.UpgradeStatusType._VALUES_TO_NAMES.get(
                        upgrade_status.usType, "UNKNOWN"
                    ),
                    upgrade_status.reason,
                    upgrade_status.upgradeReqId,
                    upgrade_status.nextImage.version,
                    upgrade_status.nextImage.md5,
                    ""
                    if upgrade_status.whenToCommit == 0
                    else time.strftime(
                        "%D %H:%M:%S", time.localtime(upgrade_status.whenToCommit)
                    ),
                ],
            )
            table.append(list(row))
        table = sorted(table, key=operator.itemgetter(0))

        click.echo(tabulate.tabulate(table, headers))
        click.echo("")
