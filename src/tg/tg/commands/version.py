#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging

import click
from terragraph_thrift.Controller import ttypes as ctrlTypes
from tg.commands import base


_log = logging.getLogger(__name__)


class VersionCli(object):
    def __init__(self):
        self.version.add_command(self._controller, name="controller")
        self.version.add_command(self._aggregator, name="aggregator")

    @click.group()
    def version():
        """ Show version of controller or aggregator
            (located in /etc/tgversion)
        """
        pass

    @click.command()
    @click.pass_obj
    def _controller(cli_opts):
        """ Show controller version """
        VersionCmd(cli_opts)._controller()

    @click.command()
    @click.pass_obj
    def _aggregator(cli_opts):
        """ Show aggregator version """
        VersionCmd(cli_opts)._aggregator()


class VersionCmd(base.BaseCmd):
    def __init__(self, cli_opts):
        base.BaseCmd.__init__(self, cli_opts)

    def _controller(self):
        self._connect_to_controller()
        _log.info("Controller version: %s", self._get_status_dump().version)
        _log.info(
            "High Availability state: %s",
            ctrlTypes.BinaryStarFsmState._VALUES_TO_NAMES.get(
                self._get_bstar_state().state, "UNKNOWN"
            ),
        )

    def _aggregator(self):
        self._connect_to_aggregator()
        _log.info("Aggregator version: %s", self._get_aggr_status_dump().version)
