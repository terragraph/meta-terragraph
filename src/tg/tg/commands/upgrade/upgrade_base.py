#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import datetime
import json
import logging
import re
import subprocess
import urllib.request

from terragraph_thrift.Controller import ttypes as ctrlTypes
from tg.commands import base, consts


_log = logging.getLogger(__name__)


class UpgradeBaseCmd(base.BaseCmd):
    def __init__(self, cli_opts):
        super(UpgradeBaseCmd, self).__init__(cli_opts)
        self._upgrade_req_id = datetime.datetime.now().strftime("%Y%m%d%H%M%S")
        _log.info("Request Id: %s", self._upgrade_req_id)

    def _load_meta(self, image):
        """ Extract meta data from an upgrade image
            @param image: file like type object"""

        # Terragraph images have three blocks:
        #  1. preamble block (run script)
        #  2. meta data block
        #  3. ubi image block

        # Find the preamble block size
        try:
            # Read preamble chunk big enough to contain all the upgrade image parameters
            preamble = image.read(consts.UPGRADE_IMAGE_PARAM_MAX_POSITION)
            preamble_block_size_match = re.findall(
                b"PREAMBLE_BLOCK_SIZE=([0-9]+)", preamble
            )
            if preamble_block_size_match:
                preamble_block_size = int(preamble_block_size_match[0])
            else:
                preamble_block_size = consts.UPGRADE_IMAGE_LEGACY_PREAMBLE_BLOCK_SIZE
            if preamble_block_size < consts.UPGRADE_IMAGE_PARAM_MAX_POSITION:
                raise Exception(
                    "Small preamble block size {}".format(preamble_block_size)
                )
        except Exception as ex:
            self._my_exit(False, ex, "Preamble block size")

        # Find the meta data size
        meta_size_match = re.findall(b"METASIZE=([0-9]+)", preamble)
        if not meta_size_match:
            self._my_exit(False, "Meta data size")
        meta_size = int(meta_size_match[0])

        # Read the meta data
        try:
            # Skip to the meta data block
            image.read(preamble_block_size - consts.UPGRADE_IMAGE_PARAM_MAX_POSITION)
            content = image.read(meta_size)
        except Exception as ex:
            self._my_exit(False, ex, "Read meta data")

        if isinstance(content, bytes):
            content = content.decode("utf-8")

        try:
            image_meta = json.loads(content)
        except Exception as ex:
            self._my_exit(False, ex, "Serialize meta data into json format")

        if "version" not in image_meta:
            self._my_exit(False, "invalid meta file: version does not exit")
        if "md5" not in image_meta:
            self._my_exit(False, "invalid meta file: md5 does not exit")
        if "model" not in image_meta:
            pass  # not checked
        if "hardwareBoardIds" not in image_meta:
            pass  # not checking for backwards compatibility
        if not re.match(r"([a-fA-F\d]{32})", image_meta["md5"]):
            self._my_exit(False, "invalid meta file: invalid md5 format")

        _log.info("Version: %s", image_meta["version"])
        _log.info("MD5: %s", image_meta["md5"])
        _log.info("Model: %s", image_meta.get("model"))
        _log.info("Hardware Board IDs: %s", image_meta.get("hardwareBoardIds"))

        return image_meta

    def _get_meta_from_url(self, image_url):
        # get image from url
        try:
            image = urllib.request.urlopen(image_url)
        except Exception as ex:
            self._my_exit(False, ex, "Download image")
        return self._load_meta(image)

    def _set_ignition_params(self, enable, linkup_interval, linkup_dampen_interval):
        self._send_to_ctrl(
            ctrlTypes.MessageType.SET_IGNITION_PARAMS,
            ctrlTypes.IgnitionParams(enable, linkup_interval, linkup_dampen_interval),
            consts.IGNITION_APP_CTRL_ID,
        )

        e2e_ack = self._recv_e2e_ack(consts.IGNITION_APP_CTRL_ID)
        if not e2e_ack.success:
            self._my_exit(e2e_ack.success, e2e_ack.message)

    def _run_sys_cmd(self, cmd):
        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True
        )
        out, err = proc.communicate()
        if proc.returncode != 0:
            raise Exception(
                'Command, "%s", failed with code %d, error: %s'
                % (" ".join(cmd), proc.returncode, err)
            )
        return out

    def _get_global_ipv6(self, interface):
        """Validates the input interface and returns its IP address."""
        import netifaces

        try:
            return netifaces.ifaddresses(interface)[netifaces.AF_INET6][0]["addr"]
        except Exception as ex:
            self._my_exit(False, ex, "Interface Validation")

    def _validate_port(self, ip, port, process_name):
        """Checks if the port is in use. If it's being used by process_name,
           that process will be killed. Otherwise, this program terminates."""
        import psutil

        for conn in psutil.net_connections("inet6"):
            (conn_ip, conn_port) = conn.laddr
            if conn_ip == ip and conn_port == port:
                # Check process name
                proc = psutil.Process(conn.pid)
                if proc.name() == process_name or proc.exe() == process_name:
                    _log.info("%s found on %d. Reusing.", process_name, port)
                    proc.kill()
                    break
                else:
                    self._my_exit(
                        False,
                        "Process {}(pid: {})".format(proc.name(), conn.pid)
                        + " found on {}.".format(port)
                        + " Try using a different port, or kill the service.",
                    )
