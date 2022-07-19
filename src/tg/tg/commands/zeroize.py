#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging
from pathlib import Path
from shutil import copy2, rmtree
from subprocess import run

import click
from tg.commands import base


_log = logging.getLogger(__name__)


class ZeroizeCli:
    @click.command()
    @click.option("-r", "--reboot", is_flag=True, help="Reboot once done")
    @click.option("-y", "--yes", is_flag=True, help="Do not prompt to confirm *DANGER*")
    @click.pass_obj
    def zeroize(cli_opts, **kwargs) -> None:
        """Wipe node config to restore factory settings"""
        ZeroizeCmd(cli_opts).zeroize(**kwargs)


class ZeroizeCmd(base.BaseCmd):
    BASE_CONF = "/etc/e2e_config/base_versions/"
    CONFIG_BASE = "/data/cfg"
    DEFAULT_NODE_CONFIG = "/data/cfg/node_config.json"

    def __init__(self, cli_opts):
        base.BaseCmd.__init__(self, cli_opts)

    def _find_release(self, version_file: str = "/etc/tgversion") -> str:
        with open(version_file, "r") as vfp:
            version_parts = vfp.read().strip().split()
            release_parts = version_parts[3].split("-")
            return release_parts[0]

    def zeroize(
        self, reboot: bool, yes: bool, version_file: str = "/etc/tgversion"
    ) -> None:
        if not yes:
            click.prompt("Are you sure you want to remove {}".format(self.CONFIG_BASE))

        _log.info("Recusively removing {}".format(self.CONFIG_BASE))
        rmtree(self.CONFIG_BASE)
        _log.info("Finished removing {}".format(self.CONFIG_BASE))

        config_base_path = Path(self.CONFIG_BASE)
        config_base_path.mkdir(mode=0o755)

        release = self._find_release(version_file)
        default_json_name = "{}.json".format(release)
        default_json_path = Path(self.BASE_CONF) / default_json_name
        _log.info(
            "Restoring default config from {} to {}".format(
                default_json_path.as_posix(), self.DEFAULT_NODE_CONFIG
            )
        )
        try:
            # TODO: When >= 3.6 on node - Path() object is fine
            copy2(str(default_json_path), self.DEFAULT_NODE_CONFIG)
        except OSError as ose:
            _log.exception(
                "Failed to copy in default config {}: {}".format(default_json_path, ose)
            )

        if reboot:
            if not yes:
                click.prompt("Reboot?")
            run(["/sbin/reboot"])
