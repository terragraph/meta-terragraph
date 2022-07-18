#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging

import click
from terragraph_thrift.Controller import ttypes as ctrlTypes
from tg.commands.upgrade import (
    upgrade_commands,
    upgrade_state,
    upgrade_status,
    upgrade_torrent,
)


_log = logging.getLogger(__name__)


class UpgradeCli(object):
    def __init__(self):
        self.upgrade.add_command(self._node, name="node")
        self.upgrade.add_command(self._network, name="network")
        self.upgrade.add_command(self._status, name="status")

        # Upgrade utilities
        self.upgrade.add_command(self._launch_server, name="launch_server")
        self.upgrade.add_command(
            upgrade_torrent.UpgradeTorrentCli().torrent, name="torrent"
        )

        # Upgrade process commands
        self._node.add_command(self._prepare, name="prepare")
        self._node.add_command(self._commit, name="commit")
        self._node.add_command(self._reset_status, name="reset_status")
        self._node.add_command(self._full_upgrade, name="full_upgrade")
        self._node.add_command(self._prepare_torrent, name="prepare_torrent")
        self._network.add_command(self._prepare, name="prepare")
        self._network.add_command(self._commit, name="commit")
        self._network.add_command(self._reset_status, name="reset_status")
        self._network.add_command(self._full_upgrade, name="full_upgrade")
        self._network.add_command(self._prepare_torrent, name="prepare_torrent")

        # Upgrade state commands
        self.upgrade.add_command(upgrade_state.UpgradeStateCli().state, name="state")
        self.upgrade.add_command(self._abort, name="abort")

    @click.group()
    @click.pass_obj
    def upgrade(cli_opts):
        """Upgrade nodes/network"""
        pass

    @click.command()
    @click.option(
        "--image",
        "-i",
        required=True,
        type=click.Path(exists=True, readable=True, resolve_path=True),
        help="The local path to the new image (e.g. /tmp/minion_images/tg.bin)",
    )
    @click.option(
        "--server_port",
        "-s",
        default=8080,
        type=click.IntRange(min=1, max=65535),
        help="The server port on which the httpd service is"
        + "to be launched (default=8080)",
    )
    @click.option(
        "--global_v6_interface",
        "-g",
        default="lo",
        type=str,
        help="Interface name of the reachable global v6 address (default=lo)",
    )
    @click.pass_obj
    def _launch_server(cli_opts, image, server_port, global_v6_interface):
        """Launch a server for image distribution"""
        upgrade_commands.UpgradeLaunchCmd(cli_opts).run(
            image, server_port, global_v6_interface
        )

    @click.command()
    @click.option(
        "--req_ids",
        "-r",
        default="",
        type=str,
        help="The request ids to clear from the UpgradeApp queue (separated by ,)",
    )
    @click.option(
        "--all", "-a", is_flag=True, help="Abort all requests in the UpgradeApp queue"
    )
    @click.option(
        "--reset_status", is_flag=True, help="Reset upgrade state on affected nodes"
    )
    @click.pass_obj
    def _abort(cli_opts, req_ids, all, reset_status):
        """Abort queued or in-progress requests in UpgradeApp"""
        req_ids = req_ids.replace(",", " ").split()
        upgrade_commands.UpgradeAbortCmd(cli_opts).run(req_ids, all, reset_status)

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
        """Upgrade applied to node(s)"""
        cli_opts.group_type = ctrlTypes.UpgradeGroupType.NODES
        cli_opts.names = names.replace(",", " ").split()

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
        """Upgrade applied to entire network"""
        cli_opts.group_type = ctrlTypes.UpgradeGroupType.NETWORK
        cli_opts.exclude = exclude.replace(",", " ").split()

    @click.command()
    @click.pass_obj
    def _status(cli_opts):
        """Dump UpgradeStatus of all nodes"""
        upgrade_status.UpgradeStatusCmd(cli_opts).run()

    @click.command()
    @click.option(
        "--image",
        "-i",
        type=str,
        help="http url of new image (e.g. http://localhost:80/images/terragraph.bin)",
        required=True,
    )
    @click.option(
        "--timeout",
        "-t",
        default=180,
        type=int,
        help="Overall timeout for the operation in seconds (default=180)",
    )
    @click.option(
        "--download_attempts",
        "-a",
        default=3,
        type=click.IntRange(1, 10),
        help="Total attempts allowed "
        + "for image downloading (default=3, range=1-10)",
    )
    @click.option(
        "--batch_size",
        default=0,
        type=int,
        help="Number of "
        + "nodes per prepare batch. (default=0 Prepares all nodes "
        + "in a single batch)",
    )
    @click.option(
        "--retries",
        default=3,
        type=int,
        help="Number of retries if the operation fails",
    )
    @click.pass_obj
    def _prepare(cli_opts, image, timeout, download_attempts, batch_size, retries):
        """Prepare upgrade"""
        upgrade_commands.UpgradePrepareCmd(cli_opts).run(
            image, timeout, download_attempts, batch_size, retries
        )

    @click.command()
    @click.option(
        "--magnet",
        "-m",
        type=str,
        required=True,
        help="magnet link for the new image "
        + "(e.g. magnet:?xt=urn:btih:1234&dn=tg-update-qoriq.bin&",
    )
    @click.option("--md5", type=str, required=True, help="MD5 of new image")
    @click.option(
        "--batch_size",
        default=0,
        type=int,
        help="Number of "
        + "nodes per prepare batch. (default=0 Prepares all nodes "
        + "in a single batch)",
    )
    @click.option("--version", "-v", type=str, default="", help="Version of new image")
    @click.option(
        "--timeout",
        "-t",
        default=180,
        type=int,
        help="Timeout of the entire prepare operation",
    )
    @click.option(
        "--download_timeout",
        type=int,
        help="Per-node timeout for the bittorrent download "
        + "and seeding. Defaults to the overall timeout",
    )
    @click.option(
        "--download_limit",
        default=-1,
        type=int,
        help="Maximum download speed for the clients. Defaults to "
        + "-1 for unlimited.",
    )
    @click.option(
        "--upload_limit",
        default=-1,
        type=int,
        help="Maximum upload speed for the clients. Defaults to -1 for unlimited",
    )
    @click.option(
        "--max_connection",
        default=-1,
        type=int,
        help="Maximum number of peer connections per client. "
        + "Defaults to -1 for unlimited.",
    )
    @click.option(
        "--retries",
        default=3,
        type=int,
        help="Number of retries if the operation fails",
    )
    @click.option(
        "--boards",
        default=None,
        type=str,
        help="The hardware board IDs that this image supports (separated by ,)",
    )
    @click.pass_obj
    def _prepare_torrent(
        cli_opts,
        magnet,
        md5,
        batch_size,
        version,
        timeout,
        download_timeout,
        download_limit,
        upload_limit,
        max_connection,
        retries,
        boards,
    ):
        """Prepare torrent upgrade"""
        hw_board_ids = None
        if boards:
            hw_board_ids = [x.strip() for x in boards.split(",") if x.strip()]

        upgrade_commands.UpgradePrepareTorrentCmd(cli_opts).run(
            magnet,
            md5,
            batch_size,
            version,
            timeout,
            download_timeout,
            download_limit,
            upload_limit,
            max_connection,
            retries,
            hw_board_ids,
        )

    @click.command()
    @click.option(
        "--version",
        "-v",
        default="",
        type=str,
        help="The version of the new image. If this field is "
        + "provided, tg-cli will check if the node already has "
        + "the input version (skipping the commit if it does), "
        + "and also makes sure that the node has been flashed "
        + "with the correct version before committing. \n"
        + "If this field is not provided, tg-cli will commit "
        + "the node irrespective of its current version or its "
        + "flashed image version.",
    )
    @click.option(
        "--timeout",
        "-t",
        default=600,
        type=int,
        help="Timeout for the operation in seconds (default=600)",
    )
    @click.option(
        "--skip_failure",
        is_flag=True,
        help="Skip upgrade failure node and continue upgrade",
    )
    @click.option(
        "--skip_pop_failure",
        is_flag=True,
        help="Skip upgrade failure POP node and continue upgrade",
    )
    @click.option(
        "--delay",
        "-d",
        default=0,
        type=int,
        help="Schedule a commit for upgrade with specified \
                  delay in seconds (default: commit immediately)",
    )
    @click.option(
        "--skip_links",
        default="",
        type=str,
        help="Skip wirelessLinkAlive check for each link name (separated by ,)",
    )
    @click.option(
        "--batch_size",
        default=0,
        type=int,
        help="Number of nodes per commit batch. "
        + "(default=0 allows the algorithm to pick the largest "
        + "batch size possible) "
        + "(batch_size=-1 commits all nodes in a single batch)",
    )
    @click.option(
        "--dry_run",
        is_flag=True,
        help="Compute and display the order in which the nodes will be rebooted.",
    )
    @click.option(
        "--retries",
        default=3,
        type=int,
        help="Number of retries if the operation fails",
    )
    @click.pass_obj
    def _commit(
        cli_opts,
        version,
        timeout,
        skip_failure,
        skip_pop_failure,
        delay,
        skip_links,
        batch_size,
        dry_run,
        retries,
    ):
        """Commit upgrade"""
        skip_links = skip_links.replace(",", " ").split()

        if dry_run:
            upgrade_commands.UpgradeCommitCmd(cli_opts).dry_run(batch_size)
        else:
            upgrade_commands.UpgradeCommitCmd(cli_opts).run(
                version,
                timeout,
                skip_failure,
                skip_pop_failure,
                delay,
                skip_links,
                batch_size,
                retries,
            )

    @click.command()
    @click.option(
        "--version",
        "-v",
        default="",
        type=str,
        help="The version of the new image. If this field is "
        + "provided, tg-cli will check if the node already has "
        + "the input version (skipping the commit if it does), "
        + "and also makes sure that the node has been flashed "
        + "with the correct version before committing. \n"
        + "If this field is not provided, tg-cli will commit "
        + "the node irrespective of its current version or its "
        + "flashed image version.",
    )
    @click.option(
        "--timeout",
        "-t",
        default=240,
        type=int,
        help="Timeout for the operation in seconds (default=240)",
    )
    @click.option(
        "--skip_failure",
        is_flag=True,
        help="Skip upgrade failure node and continue upgrade",
    )
    @click.option(
        "--skip_pop_failure",
        is_flag=True,
        help="Skip upgrade failure POP node and continue upgrade",
    )
    @click.option(
        "--delay",
        "-d",
        default=0,
        type=int,
        help="Schedule a commit for upgrade with specified \
                  delay in seconds (default: commit immediately)",
    )
    @click.option(
        "--skip_links",
        default="",
        type=str,
        help="Skip wirelessLinkAlive check for each link name (separated by ,)",
    )
    @click.option(
        "--batch_size",
        default=0,
        type=int,
        help="Number of nodes per commit batch. "
        + "(default=0 allows the algorithm to pick the largest "
        + "batch size possible) "
        + "(batch_size=-1 commits all nodes in a single batch)",
    )
    @click.option(
        "--retries",
        default=3,
        type=int,
        help="Maximum retry attempts for each operation (prepare and commit)",
    )
    # torrent options
    @click.option(
        "--download_attempts",
        "-a",
        default=3,
        type=click.IntRange(1, 10),
        help="Total attempts allowed "
        + "for image downloading (default=3, range=1-10)",
    )
    @click.option(
        "--download_timeout",
        type=int,
        help="Per-node timeout for the bittorrent download "
        + "and seeding. Defaults to the overall timeout",
    )
    @click.option(
        "--download_limit",
        default=-1,
        type=int,
        help="Maximum download speed for the clients. Defaults to "
        + "-1 for unlimited.",
    )
    @click.option(
        "--upload_limit",
        default=-1,
        type=int,
        help="Maximum upload speed for the clients. Defaults to -1 for unlimited",
    )
    @click.option(
        "--max_connection",
        default=-1,
        type=int,
        help="Maximum number of peer connections per client. "
        + "Defaults to -1 for unlimited.",
    )
    @click.option(
        "--magnet",
        "-m",
        type=str,
        required=True,
        help="magnet link for the new image "
        + "(e.g. magnet:?xt=urn:btih:1234&dn=tg-update-qoriq.bin&",
    )
    @click.option("--md5", type=str, required=True, help="MD5 of new image")
    @click.pass_obj
    def _full_upgrade(
        cli_opts,
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
        """Full upgrade"""
        skip_links = skip_links.replace(",", " ").split()
        upgrade_commands.UpgradeFullCmd(cli_opts).run(
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
        )

    @click.command()
    @click.pass_obj
    def _reset_status(cli_opts):
        """Reset upgrade status"""
        upgrade_commands.UpgradeResetCmd(cli_opts).run()
