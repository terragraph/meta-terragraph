#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging
import time

import click
import tabulate
from terragraph_thrift.Controller import ttypes as ctrlTypes
from tg.commands import consts
from tg.commands.upgrade import upgrade_base


_log = logging.getLogger(__name__)


class UpgradeStateCli(object):
    def __init__(self):
        self.state.add_command(self._pending_reqs, name="pending_requests")
        self.state.add_command(self._pending_batches, name="pending_batches")
        self.state.add_command(self._current_request, name="current_request")
        self.state.add_command(self._current_batch, name="current_batch")
        self.state.add_command(self._all, name="all")

    @click.group()
    @click.pass_obj
    def state(cli_opts):
        """ Print information about the controller UpgradeApp state """
        pass

    @click.command()
    @click.pass_obj
    def _pending_reqs(cli_opts):
        """ Print information about each pending request """
        UpgradeStateCmd(cli_opts).pending_reqs()

    @click.command()
    @click.pass_obj
    def _pending_batches(cli_opts):
        """ Print the nodes in each of the pending batches """
        UpgradeStateCmd(cli_opts).pending_batches()

    @click.command()
    @click.pass_obj
    def _current_request(cli_opts):
        """ Print information about the current request """
        UpgradeStateCmd(cli_opts).current_request()

    @click.command()
    @click.option(
        "--refresh",
        "-r",
        default=None,
        type=int,
        help="Refresh the output every few seconds. (Use this"
        + "instead of watch since it lets your scroll)",
    )
    @click.pass_obj
    def _current_batch(cli_opts, refresh):
        """ Print information about each node in the current batch """
        if refresh:
            cmd = UpgradeStateCmd(cli_opts)
            while True:
                click.clear()
                cmd.current_batch(True)
                time.sleep(refresh)
        else:
            UpgradeStateCmd(cli_opts).current_batch()

    @click.command()
    @click.pass_obj
    def _all(cli_opts):
        """ Dump the controller UpgradeApp state """
        state_cmd = UpgradeStateCmd(cli_opts)
        state_cmd.current_batch()
        state_cmd.pending_batches()
        state_cmd.pending_reqs()


class UpgradeStateCmd(upgrade_base.UpgradeBaseCmd):
    def __init__(self, cli_opts):
        super(UpgradeStateCmd, self).__init__(cli_opts)
        self._connect_to_controller()
        self._status_dump = None

    def _get_state_dump(self, force=False):
        # Avoid fetching the status dump more than once (unless force is set)
        if (not self._status_dump) or force:
            self._send_to_ctrl(
                ctrlTypes.MessageType.UPGRADE_STATE_REQ,
                ctrlTypes.UpgradeStateReq(),
                consts.UPGRADE_APP_CTRL_ID,
            )

            self._status_dump = ctrlTypes.UpgradeStateDump()
            self._recv_from_ctrl(
                ctrlTypes.MessageType.UPGRADE_STATE_DUMP,
                self._status_dump,
                consts.UPGRADE_APP_CTRL_ID,
            )

        return self._status_dump

    def _encode(self, s):
        # Avoid weird errors in tabulate
        return s.encode("ascii", "ignore")

    def _get_req_info(self, req):
        req_id = req.urReq.upgradeReqId
        req_type = ctrlTypes.UpgradeReqType._VALUES_TO_NAMES.get(
            req.urReq.urType, "UNKNOWN"
        )
        req_level = ctrlTypes.UpgradeGroupType._VALUES_TO_NAMES.get(
            req.ugType, "UNKNOWN"
        )

        return map(self._encode, [req_id, req_type, req_level])

    def pending_reqs(self):
        stateDump = self._get_state_dump()

        if not stateDump.pendingReqs:
            click.echo(click.style("There are no pending requests", fg="red"))
            return

        table = [self._get_req_info(req) for req in stateDump.pendingReqs]
        click.echo(click.style("\nPending Requests\n", bold=True, fg="green"))
        click.echo(tabulate.tabulate(table, ["ReqId", "ReqType", "ReqLevel"], "grid"))

    def current_batch(self, force=False):
        stateDump = self._get_state_dump(force)
        self.current_request(stateDump)

        if not stateDump.curBatch:
            click.echo(click.style("The current batch is empty", fg="red"))
            return

        table = []

        status_reports = self._get_status_dump().statusReports
        topology = self._get_topology()
        node_name_to_mac = {n.name: n.mac_addr for n in topology.nodes}

        for node in stateDump.curBatch:
            # Safe dictionary access
            status_report = status_reports.get(node_name_to_mac.get(node, None), None)

            if not status_report:
                table.append([node] + ["UNKNOWN"] * 3)
                continue

            row = [node]
            # Upgrade Status
            row.append(
                ctrlTypes.UpgradeStatusType._VALUES_TO_NAMES.get(
                    status_report.upgradeStatus.usType
                )
            )
            # Last seen
            row.append(
                time.strftime("%D %H:%M:%S", time.localtime(status_report.timeStamp))
            )
            # Download progress
            row.append(status_report.upgradeStatus.reason)
            table.append(map(self._encode, row))

        click.echo(click.style("\nPending nodes in current batch:\n", fg="green"))
        click.echo(
            tabulate.tabulate(
                table,
                ["NodeName", "UpgradeStatus", "LastSeen", "DownloadStatus"],
                "grid",
            )
        )

    def current_request(self, stateDump=None):
        stateDump = stateDump or self._get_state_dump()

        if stateDump.curBatch or stateDump.pendingBatches:
            req_id, req_type, req_level = self._get_req_info(stateDump.curReq)
            click.echo(
                click.style(
                    "\nIn Progress : Upgrade %s %s (Req Id : %s)\n"
                    % (req_level, req_type, req_id),
                    fg="green",
                )
            )
        else:
            click.echo(click.style("\nThere are no pending requests\n", fg="red"))

    def pending_batches(self):
        stateDump = self._get_state_dump()

        if stateDump.pendingBatches:
            table = [[", ".join(batch)] for batch in stateDump.pendingBatches]
            click.echo("\nPending Batches\n")
            click.echo(tabulate.tabulate(table, ["NodeNames"], "grid"))
        else:
            click.echo(click.style("\nThere are no pending batches\n", fg="red"))
