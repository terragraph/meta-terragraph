#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import concurrent.futures
import logging
import subprocess
from copy import copy

import click
from tg.commands import base


_log = logging.getLogger(__name__)
ATONCE_DEFAULT = 20
DEFAULT_FAB_KWARGS = {"user": "root", "forward_agent": True}
DEFAULT_SSH_USER = "root"


class SshCli:
    @click.command()
    @click.option(
        "--atonce", default=ATONCE_DEFAULT, help="Number of ssh commands to run at once"
    )
    @click.argument("node_name", nargs=1)
    @click.argument("cmds", nargs=-1)
    @click.pass_obj
    def ssh(cli_opts, atonce, node_name, cmds):
        """ ssh like command for given node using inband address
            E.g. ssh terra111 -- ls -ltr /usr/sbin,
            To run command on all nodes, use:
            ssh all -- ls -ltr /usr/sbin"""
        if node_name == "all" and not cmds:
            raise click.UsageError("Please provide command to run on all nodes")
        SshCmd(cli_opts, node_name, cmds, atonce).run()


class ScpCli(object):
    def __init__(self):
        self.scp.add_command(self._put, name="put")
        self.scp.add_command(self._get, name="get")

    @click.group()
    def scp():
        """scp like command for given node using inband address"""
        pass

    @click.command()
    @click.argument("node_name", type=str)
    @click.option(
        "--atonce", default=ATONCE_DEFAULT, help="Number of ssh commands to run at once"
    )
    @click.option("--src", "-s", type=click.Path(exists=True), help="local source file")
    @click.option("--dst", "-d", help="remote destination")
    @click.pass_obj
    def _put(cli_opts, node_name, atonce, src, dst):
        """ copy local file to given node """
        ScpPutCmd(cli_opts, node_name, src, dst, atonce).run()

    @click.command()
    @click.argument("node_name", type=str)
    @click.option(
        "--atonce", default=ATONCE_DEFAULT, help="Number of ssh commands to run at once"
    )
    @click.option("--src", "-s", help="remote source file")
    @click.option("--dst", "-d", help="local destination")
    @click.pass_obj
    def _get(cli_opts, node_name, atonce, src, dst):
        """ copy file from given node """
        ScpGetCmd(cli_opts, node_name, src, dst, atonce).run()


class DebugCmd(base.BaseCmd):
    def __init__(self, cli_opts):
        base.BaseCmd.__init__(self, cli_opts)

    # TODO: (wishlist) Yield and make a generator to save the memories
    def _get_hosts(self):
        if self._node_name == "all":
            return self._get_all_loopbacks()
        else:
            return self._get_loopback(self._node_name)

    def _get_loopback(self, node_name):
        self._connect_to_controller()
        topology = self._get_topology()

        mac = None
        for node in topology.nodes:
            if node.name == node_name:
                mac = node.mac_addr
        if mac is None:
            self._my_exit(False, "Invalid node name")

        status_dump = self._get_status_dump()
        loopback = None
        for minion, status_report in status_dump.statusReports.items():
            if minion == mac:
                loopback = status_report.ipv6Address
        if loopback is None:
            self._my_exit(
                False, "No reachable inband address to node {}".format(node_name)
            )

        _log.info("Inband address of {}: {}".format(node_name, loopback))
        return loopback

    def _get_all_loopbacks(self):
        self._connect_to_controller()

        topology = self._get_topology()
        mac2name = {n.mac_addr: n.name for n in topology.nodes}

        status_dump = self._get_status_dump()
        loopbacks = []
        for minion, status_report in status_dump.statusReports.items():
            ipv6_address = status_report.ipv6Address
            if not ipv6_address:
                _log.info(
                    "%s has no reachable ipv6 address - skip.",
                    "--" if mac2name.get(minion) is None else mac2name[minion],
                )
                continue
            loopbacks.append(ipv6_address)

        return loopbacks

    def _wait_for_futures(self, futures, operation):
        success = 0
        total_hosts = len(futures)
        failed = 0
        for future in concurrent.futures.as_completed(futures):
            host = futures[future]
            result = future.result()
            if isinstance(result, subprocess.SubprocessError):
                failed += 1
                _log.error(
                    "{} failed - {} ({})".format(
                        host, result.stderr.decode("utf-8"), result.returncode
                    )
                )
            else:
                success += 1
                if result:
                    print("{}: {}".format(host, result))
                else:
                    print("{}: {} success".format(host, operation))

        pct = int(success / total_hosts) * 100
        _log.info(
            "{} / {} ({}%) hosts succeeded - {} Failed".format(
                success, total_hosts, pct, failed
            )
        )

    def _run_sys_cmd(self, cmds, stdout=None, check=None):
        try:
            sc = subprocess.run(cmds, stdout=stdout, stderr=stdout)
            if check:
                sc.check_returncode()
            if stdout:
                return sc.stdout.decode("utf-8")
        except subprocess.CalledProcessError as cpe:
            return cpe


class SshCmd(DebugCmd):
    def __init__(self, cli_opts, node_name, cmds, atonce):
        DebugCmd.__init__(self, cli_opts)
        self._node_name = node_name
        self._cmds = cmds
        self._atonce = atonce

    def run(self):
        base_cmds = [
            "/usr/bin/ssh",
            "-o",
            "StrictHostKeyChecking=No",  # So we can work without a /dev/tty
            "-A",
        ]

        with concurrent.futures.ThreadPoolExecutor(
            max_workers=self._atonce
        ) as executor:
            future_ssh = {}
            for host in self._get_hosts():
                host_cmds = copy(base_cmds)
                host_cmds.append("{}@{}".format(DEFAULT_SSH_USER, host))
                if self._cmds:
                    host_cmds.append("'{}'".format(" ".join(self._cmds)))
                future_ssh[
                    executor.submit(
                        self._run_sys_cmd, host_cmds, subprocess.PIPE, check=True
                    )
                ] = host

            self._wait_for_futures(future_ssh, "ssh")


class ScpPutCmd(DebugCmd):
    def __init__(self, cli_opts, node_name, src, dst, atonce):
        DebugCmd.__init__(self, cli_opts)
        self._node_name = node_name
        self._src = src
        self._dst = dst
        self._atonce = atonce

    def run(self):
        base_cmds = ["/usr/bin/scp", self._src]

        with concurrent.futures.ThreadPoolExecutor(
            max_workers=self._atonce
        ) as executor:
            future_scp = {}
            for host in self._get_hosts():
                host_cmds = copy(base_cmds)
                host_cmds.append("{}@[{}]:{}".format(DEFAULT_SSH_USER, host, self._dst))
                future_scp[
                    executor.submit(
                        self._run_sys_cmd, host_cmds, subprocess.PIPE, check=True
                    )
                ] = host

            self._wait_for_futures(future_scp, "scp put")


class ScpGetCmd(DebugCmd):
    def __init__(self, cli_opts, node_name, src, dst, atonce):
        DebugCmd.__init__(self, cli_opts)
        self._node_name = node_name
        self._src = src
        self._dst = dst
        self._atonce = atonce

    def run(self):
        base_cmds = ["/usr/bin/scp"]

        with concurrent.futures.ThreadPoolExecutor(
            max_workers=self._atonce
        ) as executor:
            future_scp = {}
            for host in self._get_hosts():
                host_cmds = copy(base_cmds)
                host_cmds.append("{}@[{}]:{}".format(DEFAULT_SSH_USER, host, self._src))
                # Save file to a f"{self._dst}.{host}" - Replace IPv6 : to _
                host_cmds.append("{}.{}".format(self._dst, host.replace(":", "_")))
                future_scp[
                    executor.submit(
                        self._run_sys_cmd, host_cmds, subprocess.PIPE, check=True
                    )
                ] = host

            self._wait_for_futures(future_scp, "scp get")
