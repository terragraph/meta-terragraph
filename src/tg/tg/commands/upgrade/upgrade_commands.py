#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging
import os

import click
from terragraph_thrift.Controller import ttypes as ctrlTypes
from tg.commands import consts
from tg.commands.upgrade import upgrade_base


_log = logging.getLogger(__name__)


class UpgradeLaunchCmd(upgrade_base.UpgradeBaseCmd):
    def __init__(self, cli_opts):
        super(UpgradeLaunchCmd, self).__init__(cli_opts)

    def launch_server(self, image, server_port, global_v6_interface):
        # Get containing directory
        path = os.path.dirname(image)
        # Validate interface
        ip = self._get_global_ipv6(global_v6_interface)
        # Validate port
        self._validate_port(ip, server_port, "httpd")
        # Run the httpd service
        addr = "[{}]:{}".format(ip, server_port)
        try:
            # Run the httpd server
            self._run_sys_cmd(["httpd", "-p", addr, "-h", path])
        except Exception as ex:
            self._my_exit(False, ex)

        return "http://{}/{}".format(addr, os.path.basename(image))

    # Wrapper for click
    def run(self, *args):
        url = self.launch_server(*args)
        self._my_exit(True, "Image file is accessible at %s" % url)


class UpgradePrepareCmd(upgrade_base.UpgradeBaseCmd):
    def __init__(self, cli_opts):
        super(UpgradePrepareCmd, self).__init__(cli_opts)
        self._group_type = cli_opts.group_type
        self._names = getattr(cli_opts, "names", [])  # For NODE
        self._exclude = getattr(cli_opts, "exclude", [])  # For NETWORK
        self._connect_to_controller()

    def send_prepare_msg(self, image, timeout, download_attempts, limit, retries):
        image_meta = self._get_meta_from_url(image)

        upgrade_request = ctrlTypes.UpgradeReq(
            ctrlTypes.UpgradeReqType.PREPARE_UPGRADE,
            self._upgrade_req_id,  # request timestamp
            image_meta["md5"],
            image,
            0,  # scheduleToCommit (not required for prepare)
            download_attempts,
            None,  # torrentParams
            None,  # nextNodeConfigJson
            image_meta.get("hardwareBoardIds"),
        )

        self._send_to_ctrl(
            ctrlTypes.MessageType.UPGRADE_GROUP_REQ,  # Message type
            ctrlTypes.UpgradeGroupReq(  # Message data
                self._group_type,  # Group type
                self._names,  # Node names
                self._exclude,  # Exclude names
                upgrade_request,  # UpgradeReq
                timeout,  # timeout
                True,  # skipFailure
                True,  # skipPopFailure
                image_meta["version"],  # image version
                [],  # skipLinks
                limit,  # batch limit
                retries,
            ),  # retry limit
            consts.UPGRADE_APP_CTRL_ID,
        )  # Receiver app id

        return self._recv_e2e_ack(consts.UPGRADE_APP_CTRL_ID)

    # Wrapper for click
    def run(self, *args):
        _log.info("Sending upgrade prepare request")
        e2e_ack = self.send_prepare_msg(*args)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class UpgradePrepareTorrentCmd(upgrade_base.UpgradeBaseCmd):
    def __init__(self, cli_opts):
        super(UpgradePrepareTorrentCmd, self).__init__(cli_opts)
        self._group_type = cli_opts.group_type
        self._names = getattr(cli_opts, "names", [])  # For NODE
        self._exclude = getattr(cli_opts, "exclude", [])  # For NETWORK

        self._connect_to_controller()

    def run(
        self,
        magnet,
        md5,
        limit,
        version,
        timeout,
        download_timeout,
        download_limit,
        upload_limit,
        max_con,
        retries,
        hw_board_ids,
    ):
        torrent_params = ctrlTypes.UpgradeTorrentParams(
            download_timeout or timeout, download_limit, upload_limit, max_con
        )

        upgrade_request = ctrlTypes.UpgradeReq(
            ctrlTypes.UpgradeReqType.PREPARE_UPGRADE,
            self._upgrade_req_id,  # request timestamp
            md5,
            magnet,
            0,  # scheduleToCommit (not required for prepare)
            0,  # downloadAttempts (not required for torrent)
            torrent_params,
            "",  # nextNodeConfigJson (not required for prepare)
            hw_board_ids,
        )

        self._send_to_ctrl(
            ctrlTypes.MessageType.UPGRADE_GROUP_REQ,  # Message type
            ctrlTypes.UpgradeGroupReq(  # Message data
                self._group_type,  # Group type
                self._names,  # Node names
                self._exclude,  # Exclude names
                upgrade_request,  # UpgradeReq
                timeout,  # timeout
                True,  # skipFailure
                True,  # skipPopFailure
                version,  # image version
                [],  # skipLinks
                limit,  # batch limit
                retries,
            ),  # retry limit
            consts.UPGRADE_APP_CTRL_ID,
        )  # Receiver app id

        e2e_ack = self._recv_e2e_ack(consts.UPGRADE_APP_CTRL_ID)
        e2e_ack.message = consts.byte_string_decode(e2e_ack.message)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class UpgradeCommitCmd(upgrade_base.UpgradeBaseCmd):
    def __init__(self, cli_opts):
        super(UpgradeCommitCmd, self).__init__(cli_opts)
        self._group_type = cli_opts.group_type
        self._names = getattr(cli_opts, "names", [])  # For NODE
        self._exclude = getattr(cli_opts, "exclude", [])  # For NETWORK

        self._connect_to_controller()

    def _link_to_nodes(self, link_name):
        for link in self._get_topology().links:
            link.name = consts.byte_string_decode(link.name)
            if link.name == link_name:
                link.a_node_name = consts.byte_string_decode(link.a_node_name)
                link.z_node_name = consts.byte_string_decode(link.z_node_name)
                return [link.a_node_name, link.z_node_name]

        return []

    def _print_commit_plan(self, commit_plan):
        click.echo(click.style("Commit batches:", fg="green"))
        for (i, nodeNames) in enumerate(commit_plan.commitBatches):
            click.echo(click.style("  Batch {}:".format(i), fg="blue"))
            for node in nodeNames:
                node = consts.byte_string_decode(node)
                click.echo("    " + node)
            click.echo("")

        click.echo("")

    def _get_commit_plan(self, limit):
        self._send_to_ctrl(
            ctrlTypes.MessageType.UPGRADE_COMMIT_PLAN_REQ,
            ctrlTypes.UpgradeCommitPlanReq(limit, self._exclude),
            consts.UPGRADE_APP_CTRL_ID,
        )

        commit_plan = ctrlTypes.UpgradeCommitPlan()
        self._recv_from_ctrl(
            ctrlTypes.MessageType.UPGRADE_COMMIT_PLAN,
            commit_plan,
            consts.UPGRADE_APP_CTRL_ID,
        )

        return commit_plan

    def _send_commit_msg(
        self,
        group_type,
        names,
        exclude,
        version,
        timeout,
        skip_failure,
        skip_pop_failure,
        delay,
        skip_links,
        limit,
        retries,
    ):
        upgrade_request = ctrlTypes.UpgradeReq(
            ctrlTypes.UpgradeReqType.COMMIT_UPGRADE,
            self._upgrade_req_id,  # request timestamp
            "",  # md5 (not required for commit)
            "",  # image url (not required for commit)
            delay,
        )  # scheduleToCommit

        self._send_to_ctrl(
            ctrlTypes.MessageType.UPGRADE_GROUP_REQ,  # Message type
            ctrlTypes.UpgradeGroupReq(  # Message data
                group_type,  # Group type
                names,  # Node names
                exclude,  # Exclude names
                upgrade_request,  # UpgradeReq
                timeout,  # timeout
                skip_failure,  # skipFailure
                skip_pop_failure,  # skipPopFailure
                version,  # image version
                skip_links,  # skipLinks
                limit,  # limit
                retries,
            ),  # retry limit
            consts.UPGRADE_APP_CTRL_ID,
        )  # Receiver app id

        return self._recv_e2e_ack(consts.UPGRADE_APP_CTRL_ID)

    def commit(
        self,
        version,
        timeout,
        skip_failure,
        skip_pop_failure,
        delay,
        skip_links,
        limit,
        retries,
    ):
        if self._group_type == ctrlTypes.UpgradeGroupType.NODES:
            return self._send_commit_msg(
                ctrlTypes.UpgradeGroupType.NODES,
                self._names,
                [],
                version,
                timeout,
                skip_failure,
                skip_pop_failure,
                delay,
                skip_links,
                limit,
                retries,
            )
        else:
            return self._send_commit_msg(
                ctrlTypes.UpgradeGroupType.NETWORK,
                [],
                self._exclude,
                version,
                timeout,
                skip_failure,
                skip_pop_failure,
                delay,
                skip_links,
                limit,
                retries,
            )

    # Wrapper for click
    def run(self, *args):
        _log.info("Sending upgrade commit request")
        e2e_ack = self.commit(*args)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    # Wrapper for click
    def dry_run(self, limit):
        commit_plan = self._get_commit_plan(limit)
        self._print_commit_plan(commit_plan)
        self._my_exit(True)


class UpgradeFullCmd(upgrade_base.UpgradeBaseCmd):
    def __init__(self, cli_opts):
        super(UpgradeFullCmd, self).__init__(cli_opts)
        self._group_type = cli_opts.group_type
        self._names = getattr(cli_opts, "names", [])  # For NODE
        self._exclude = getattr(cli_opts, "exclude", [])  # For NETWORK

        self._connect_to_controller()

    def send_full_upgrade_msg(
        self,
        version,
        timeout,
        skip_failure,
        skip_pop_failure,
        batch_size,
        skip_links,
        delay,
        retries,
        download_attempts,
        download_timeout,
        download_limit,
        upload_limit,
        max_connection,
        magnet,
        md5,
    ):
        torrent_params = ctrlTypes.UpgradeTorrentParams(
            download_timeout or timeout, download_limit, upload_limit, max_connection
        )

        upgrade_request = ctrlTypes.UpgradeReq(
            ctrlTypes.UpgradeReqType.FULL_UPGRADE,
            self._upgrade_req_id,  # request timestamp
            md5,
            magnet,
            delay,  # scheduleToCommit
            download_attempts,
            torrent_params,
        )

        self._send_to_ctrl(
            ctrlTypes.MessageType.UPGRADE_GROUP_REQ,  # Message type
            ctrlTypes.UpgradeGroupReq(  # Message data
                self._group_type,  # Group type
                self._names,  # Node names
                self._exclude,  # Exclude names
                upgrade_request,  # UpgradeReq
                timeout,  # timeout
                skip_failure,  # skipFailure
                skip_pop_failure,  # skipPopFailure
                version,  # image version
                skip_links,  # skipLinks
                batch_size,  # limit
                retries,
            ),  # retry limit
            consts.UPGRADE_APP_CTRL_ID,
        )  # Receiver app id

        return self._recv_e2e_ack(consts.UPGRADE_APP_CTRL_ID)

    # Wrapper for click
    def run(self, *args):
        _log.info("Sending full upgrade request")
        e2e_ack = self.send_full_upgrade_msg(*args)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class UpgradeResetCmd(upgrade_base.UpgradeBaseCmd):
    def __init__(self, cli_opts):
        super(UpgradeResetCmd, self).__init__(cli_opts)
        self._group_type = cli_opts.group_type
        self._names = getattr(cli_opts, "names", [])  # For NODE
        self._exclude = getattr(cli_opts, "exclude", [])  # For NETWORK

        self._connect_to_controller()

    def send_reset_msg(self):
        upgrade_request = ctrlTypes.UpgradeReq(
            ctrlTypes.UpgradeReqType.RESET_STATUS,  # Request type
            self._upgrade_req_id,  # Request timestamp
            "",  # md5 not required
            "",
        )  # imageUrl not required

        self._send_to_ctrl(
            ctrlTypes.MessageType.UPGRADE_GROUP_REQ,  # Message type
            ctrlTypes.UpgradeGroupReq(  # Message data
                self._group_type,  # Group type
                self._names,  # Node names
                self._exclude,  # Exclude names
                upgrade_request,  # UpgradeReq
                0,  # timeout
                False,  # skipFailure
                False,  # skipPopFailure
                "",  # version
                [],  # skipLinks
                0,
            ),  # limit
            consts.UPGRADE_APP_CTRL_ID,
        )  # Receiver app id

        return self._recv_e2e_ack(consts.UPGRADE_APP_CTRL_ID)

    def run(self):
        _log.info("Sending upgrade reset request")
        e2e_ack = self.send_reset_msg()
        e2e_ack.message = consts.byte_string_decode(e2e_ack.message)
        self._my_exit(e2e_ack.success, e2e_ack.message)


class UpgradeAbortCmd(upgrade_base.UpgradeBaseCmd):
    def __init__(self, cli_opts):
        super(UpgradeAbortCmd, self).__init__(cli_opts)
        self._connect_to_controller()

    def run(self, req_ids, abort_all, reset_status):
        _log.info("Sending upgrade abort request")

        abort_req = ctrlTypes.UpgradeAbortReq(abort_all, req_ids, reset_status)
        self._send_to_ctrl(
            ctrlTypes.MessageType.UPGRADE_ABORT_REQ,
            abort_req,
            consts.UPGRADE_APP_CTRL_ID,
        )

        e2e_ack = self._recv_e2e_ack(consts.UPGRADE_APP_CTRL_ID)
        e2e_ack.message = consts.byte_string_decode(e2e_ack.message)
        self._my_exit(e2e_ack.success, e2e_ack.message)
