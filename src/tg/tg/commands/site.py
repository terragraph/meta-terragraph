#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging

import click
from terragraph_thrift.Controller import ttypes as ctrlTypes
from terragraph_thrift.Topology import ttypes as topoTypes
from tg.commands import base, consts


_log = logging.getLogger(__name__)


class SiteCli(object):
    def __init__(self):
        self.site.add_command(self._add, name="add")
        self.site.add_command(self._del, name="del")
        self.site.add_command(self._rename, name="rename")
        self.site.add_command(self._relocate, name="relocate")

    @click.group()
    def site():
        """ Add/Modify/Delete Sites """
        pass

    @click.command()
    @click.option("--name", "-n", type=str, required=True, help="site name")
    @click.option("--lon", type=float, required=True, help="site longitude")
    @click.option("--lat", type=float, required=True, help="site latitude")
    @click.option("--alt", type=float, required=True, help="site altitude")
    @click.option(
        "--acc", type=float, required=True, help="location accuracy in meters"
    )
    @click.pass_obj
    def _add(cli_opts, name, lon, lat, alt, acc):
        """ Add a site """
        SiteCmd(cli_opts, name, None, lon, lat, alt, acc)._add_site()

    @click.command()
    @click.option("--name", "-n", type=str, required=True, help="site name")
    @click.pass_obj
    def _del(cli_opts, name):
        """ Delete a site """
        SiteCmd(cli_opts, name)._del_site()

    @click.command()
    @click.option("--name", "-n", type=str, required=True, help="site name")
    @click.option("--new_name", "-r", type=str, required=True, help="new site name")
    @click.pass_obj
    def _rename(cli_opts, name, new_name):
        """ Rename a site """
        SiteCmd(cli_opts, name, new_name)._rename_site()

    @click.command()
    @click.option("--name", "-n", type=str, required=True, help="site name")
    @click.option("--lon", type=float, required=True, help="site longitude")
    @click.option("--lat", type=float, required=True, help="site latitude")
    @click.option("--alt", type=float, required=True, help="site altitude")
    @click.option(
        "--acc", type=float, required=True, help="location accuracy in meters"
    )
    @click.pass_obj
    def _relocate(cli_opts, name, lon, lat, alt, acc):
        """ Edit a site location """
        SiteCmd(cli_opts, name, None, lon, lat, alt, acc)._relocate()


class SiteCmd(base.BaseCmd):
    def __init__(
        self,
        cli_opts,
        site_name=None,
        new_site_name=None,
        site_long=None,
        site_lat=None,
        site_alt=None,
        site_acc=None,
    ):
        base.BaseCmd.__init__(self, cli_opts)
        self._site_name = site_name
        self._new_site_name = new_site_name
        self._site_loc = topoTypes.Location(site_lat, site_long, site_alt, site_acc)
        self._connect_to_controller()

    def _add_site(self):
        site_to_add = topoTypes.Site()
        site_to_add.name = self._site_name
        site_to_add.location = self._site_loc

        self._send_to_ctrl(
            ctrlTypes.MessageType.ADD_SITE,
            ctrlTypes.AddSite(site_to_add),
            consts.TOPOLOGY_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _del_site(self):
        self._send_to_ctrl(
            ctrlTypes.MessageType.DEL_SITE,
            ctrlTypes.DelSite(self._site_name),
            consts.TOPOLOGY_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _rename_site(self):
        renamed_site = topoTypes.Site()
        renamed_site.name = self._new_site_name
        self._send_to_ctrl(
            ctrlTypes.MessageType.EDIT_SITE,
            ctrlTypes.EditSite(self._site_name, renamed_site),
            consts.TOPOLOGY_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def _relocate(self):
        new_site = topoTypes.Site()
        new_site.location = self._site_loc
        self._send_to_ctrl(
            ctrlTypes.MessageType.EDIT_SITE,
            ctrlTypes.EditSite(self._site_name, new_site),
            consts.TOPOLOGY_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.TOPOLOGY_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)
