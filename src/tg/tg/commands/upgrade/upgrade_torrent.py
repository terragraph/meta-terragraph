#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging

import click
from terragraph_thrift.Controller import ttypes as ctrlTypes
from tg.commands import consts
from tg.commands.upgrade import upgrade_base


_log = logging.getLogger(__name__)


class UpgradeTorrentCli(object):
    def __init__(self):
        self.torrent.add_command(self._add_image, name="add_image")
        self.torrent.add_command(self._del_image, name="del_image")
        self.torrent.add_command(self._list_images, name="list_images")

    @click.group()
    @click.pass_obj
    def torrent(cli_opts):
        """Utilities for torrent broadcast"""
        pass

    @click.command()
    @click.option("--url", "-u", type=str, required=True, help="URL of the image")
    @click.pass_obj
    def _add_image(cli_opts, url):
        """Download an image and start seeding it"""
        UpgradeTorrentCmd(cli_opts).add_image(url)

    @click.command()
    @click.option("--name", "-n", type=str, required=True, help="Name of the image")
    @click.pass_obj
    def _del_image(cli_opts, name):
        """Remove an image"""
        UpgradeTorrentCmd(cli_opts).del_image(name)

    @click.command()
    @click.pass_obj
    def _list_images(cli_opts):
        """List all known images"""
        UpgradeTorrentCmd(cli_opts).list_images()


class UpgradeTorrentCmd(upgrade_base.UpgradeBaseCmd):
    def __init__(self, cli_opts):
        super(UpgradeTorrentCmd, self).__init__(cli_opts)

    def add_image(self, url):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.UPGRADE_ADD_IMAGE_REQ,
            ctrlTypes.UpgradeAddImageReq(url),
            consts.UPGRADE_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.UPGRADE_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def del_image(self, name):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.UPGRADE_DEL_IMAGE_REQ,
            ctrlTypes.UpgradeDelImageReq(name),
            consts.UPGRADE_APP_CTRL_ID,
        )
        e2e_ack = self._recv_e2e_ack(consts.UPGRADE_APP_CTRL_ID)
        self._my_exit(e2e_ack.success, e2e_ack.message)

    def list_images(self):
        self._connect_to_controller()
        self._send_to_ctrl(
            ctrlTypes.MessageType.UPGRADE_LIST_IMAGES_REQ,
            ctrlTypes.UpgradeListImagesReq(),
            consts.UPGRADE_APP_CTRL_ID,
        )
        images = ctrlTypes.UpgradeListImagesResp()
        self._recv_from_ctrl(
            ctrlTypes.MessageType.UPGRADE_LIST_IMAGES_RESP,
            images,
            consts.UPGRADE_APP_CTRL_ID,
        )
        self._my_exit(True, images)
