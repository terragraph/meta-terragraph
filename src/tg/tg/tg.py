#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

#
# Set encoding to UTF-8 for all modules as it is needed for click in python3
#
import inspect
import locale
import logging
import sys

import click


# TODO: Monkey patch click until a cleaner API is available
# Copied from the click.decorators module and does not .replace("_", "-")
#
# In Click>=7.1, delete this block and instead add in @click.group():
#
#  def normalize(name):
#    return name.replace("_", "-")
#
# @click.group(context_settings={"token_normalize_func": normalize})
# def your_group():
#    <...>
#
def _make_command(f, name, attrs, cls):
    if isinstance(f, click.Command):
        raise TypeError("Attempted to convert a callback into a command twice.")
    try:
        params = f.__click_params__
        params.reverse()
        del f.__click_params__
    except AttributeError:
        params = []
    help = attrs.get("help")
    if help is None:
        help = inspect.getdoc(f)
        if isinstance(help, bytes):
            help = help.decode("utf-8")
    else:
        help = inspect.cleandoc(help)
    attrs["help"] = help
    # We don't need to waste time checking for this - We're Python 3
    # _check_for_unicode_literals()
    # Do not replace _ with - like upstream does ...
    return cls(name=name or f.__name__.lower(), callback=f, params=params, **attrs)


if click.__version__[0] == "7":
    click.decorators._make_command = _make_command


def getpreferredencoding(do_setlocale=True):
    return "utf-8"


locale.getpreferredencoding = getpreferredencoding


_log = logging.getLogger(__name__)


class CliOptions(object):
    """ Object for holding CLI state information """

    def __init__(
        self,
        controller_host,
        controller_port,
        aggregator_host,
        aggregator_port,
        agent_host,
        agent_port,
        minion_host,
        minion_port,
        verbosity,
    ):
        self.controller_host = controller_host
        self.controller_port = controller_port
        self.aggregator_host = aggregator_host
        self.aggregator_port = aggregator_port
        self.agent_host = agent_host
        self.agent_port = agent_port
        self.minion_host = minion_host
        self.minion_port = minion_port
        self.ch = logging.StreamHandler(sys.stdout)
        self.verbosity = verbosity

        root_logger = logging.getLogger()

        # defaults
        root_logger.setLevel(logging.DEBUG)
        self.ch.setLevel(logging.DEBUG)
        formatter = logging.Formatter(
            "%(asctime)s [%(levelname)s]: %(message)s", "%H:%M:%S"
        )
        self.ch.setFormatter(formatter)
        root_logger.addHandler(self.ch)
        self.set_verbosity(self.verbosity)

    def set_verbosity(self, verbosity):
        if verbosity >= 4:
            formatter = logging.Formatter(
                "%(asctime)s [%(levelname)s]: %(filename)s:%(lineno)d " "%(message)s",
                "%H:%M:%S",
            )
            self.ch.setFormatter(formatter)
        if verbosity == 1:
            self.ch.setLevel(logging.ERROR)
        elif verbosity == 2:
            self.ch.setLevel(logging.INFO)
        elif verbosity >= 3:
            self.ch.setLevel(logging.DEBUG)


@click.group()
@click.option(
    "--controller_host",
    "-c",
    default="localhost",
    type=str,
    help="E2E controller Hostname or IP (default=localhost)",
)
@click.option(
    "--controller_ip", default="", type=str, help="[DEPRECATED] E2E controller IP"
)
@click.option(
    "--controller_port",
    "-p",
    default=17077,
    type=int,
    help="E2E controller port (default=17077)",
)
@click.option(
    "--aggregator_host",
    default="localhost",
    type=str,
    help="NMS aggregator Hostname or IP (default=localhost)",
)
@click.option(
    "--aggregator_ip", default="", type=str, help="[DEPRECATED] NMS aggregator IP"
)
@click.option(
    "--aggregator_port",
    default=18100,
    type=int,
    help="NMS aggregator port (default=18100)",
)
@click.option(
    "--agent_host",
    default="localhost",
    type=str,
    help="Stats agent Hostname or IP (default=localhost)",
)
@click.option("--agent_ip", default="", type=str, help="[DEPRECATED] Stats agent IP")
@click.option(
    "--agent_port", default=4231, type=int, help="Stats agent port (default=4231)"
)
@click.option(
    "--minion_host",
    default="localhost",
    type=str,
    help="E2E Minion Hostname or IP (default=localhost)",
)
@click.option(
    "--minion_port", default=17177, type=int, help="E2E minion port (default=17177)"
)
@click.option("-v", "--verbosity", count=True, default=2, help="Control CLI verbosity")
@click.pass_context
def tg(
    ctx,
    controller_host,
    controller_ip,
    controller_port,
    aggregator_host,
    aggregator_ip,
    aggregator_port,
    agent_host,
    agent_ip,
    agent_port,
    minion_host,
    minion_port,
    verbosity,
):
    """ Terragraph CLI """

    # Convert IPs to hosts for backwards compatibility
    if controller_ip:
        controller_host = "[{}]".format(controller_ip)
    if aggregator_ip:
        aggregator_host = "[{}]".format(aggregator_ip)
    if agent_ip:
        agent_host = "[{}]".format(agent_ip)

    ctx.obj = CliOptions(
        controller_host,
        controller_port,
        aggregator_host,
        aggregator_port,
        agent_host,
        agent_port,
        minion_host,
        minion_port,
        verbosity,
    )


def _add_commands(args, add_all=False):
    if "counters" in args or add_all:
        from tg.commands import counters

        tg.add_command(counters.CountersCli().counters)
    if "ssh" in args or add_all:
        from tg.commands import debug

        tg.add_command(debug.SshCli().ssh)
    if "scp" in args or add_all:
        from tg.commands import debug

        tg.add_command(debug.ScpCli().scp)
    if "fw" in args or add_all:
        from tg.commands import fw

        tg.add_command(fw.FwCli().fw)
    if "ignition" in args or add_all:
        from tg.commands import ignition

        tg.add_command(ignition.IgnitionCli().ignition)
    if "link" in args or add_all:
        from tg.commands import link

        tg.add_command(link.LinkCli().link)
    if "node" in args or add_all:
        from tg.commands import node

        tg.add_command(node.NodeCli().node)
    if "scan" in args or add_all:
        from tg.commands import scan

        tg.add_command(scan.ScanCli().scan)
    if "site" in args or add_all:
        from tg.commands import site

        tg.add_command(site.SiteCli().site)
    if "status" in args or add_all:
        from tg.commands import status

        tg.add_command(status.StatusCli().status)
    if "topology" in args or add_all:
        from tg.commands import topology

        tg.add_command(topology.TopologyCli().topology)
    if "traffic" in args or add_all:
        from tg.commands import traffic

        tg.add_command(traffic.TrafficCli().traffic)
    if "upgrade" in args or add_all:
        from tg.commands.upgrade import upgrade

        tg.add_command(upgrade.UpgradeCli().upgrade)
    if "config" in args or add_all:
        from tg.commands import config

        tg.add_command(config.ConfigCli().config)
    if "version" in args or add_all:
        from tg.commands import version

        tg.add_command(version.VersionCli().version)
    if "help" in args or add_all:
        from tg.commands import help

        tg.add_command(help.help)
    if "event" in args or add_all:
        from tg.commands import event

        tg.add_command(event.EventCli().event)
    if "zeroize" in args or add_all:
        from tg.commands import zeroize

        tg.add_command(zeroize.ZeroizeCli().zeroize)
    if "minion" in args or add_all:
        from tg.commands import minion

        tg.add_command(minion.MinionCli().minion)


# Add only (potentially) necessary commands to avoid loading unused modules
# Exception: need to load all commands for root '--help' and the 'help' command
def main():
    _add_commands(set(sys.argv[1:]), "help" in sys.argv[1:])
    if not tg.commands:
        _add_commands({}, True)
    tg()


if __name__ == "__main__":
    main()
