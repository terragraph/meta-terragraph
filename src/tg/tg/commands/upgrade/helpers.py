#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging
import time
import urllib.parse

import tabulate
from terragraph_thrift.Controller import ttypes as ctrlTypes
from terragraph_thrift.Topology import ttypes as topoTypes
from tg.commands import consts


_log = logging.getLogger(__name__)

MATCH = 0
NOT_MATCH = -1
IN_PROGRESS = 1


class PrepareHelper:
    def __init__(self, cmd, image, timeout, download_attempts):
        self._cmd = cmd
        if urllib.parse(image).scheme != "":
            self._image_url = image
            self._image_meta = cmd._get_meta_from_url(image)
        else:
            cmd._my_exit(False, "Invalid image argument")
        self._timeout = timeout
        self._download_attempts = download_attempts

    def do_prepare(self, node_name, node_mac):
        if self._is_pending_commit(node_name, node_mac):
            _log.error("%s : Has a pending commit. Skipping ...", node_name)
            return False
        if self._is_prepared(node_name, node_mac):
            return True
        _log.info("%s : [%s] Sending prepare request ...", node_name, node_mac)
        self._send_prepare_req(node_mac)
        _log.info("%s : Checking prepare status ...", node_name)
        return self._check_prepare_status(node_name, node_mac)

    def _is_pending_commit(self, node_name, node_mac):
        status_report = self._cmd._get_status_dump().statusReports[node_mac]
        upgrade_status = status_report.upgradeStatus
        return (upgrade_status.usType == ctrlTypes.UpgradeStatusType.FLASHED) and (
            upgrade_status.whenToCommit != 0
        )

    def _is_prepared(self, node_name, node_mac):
        status_report = self._cmd._get_status_dump().statusReports[node_mac]
        upgrade_status = status_report.upgradeStatus

        if status_report.version.strip() == self._image_meta["version"].strip():
            _log.info("%s : Is running new image. Skipping ...", node_name)
            return True

        if (
            upgrade_status.usType == ctrlTypes.UpgradeStatusType.FLASHED
            and upgrade_status.nextImage.md5 == self._image_meta["md5"]
        ):
            _log.info("%s : Is already prepared. Skipping ...", node_name)
            return True

        return False

    def _send_prepare_req(self, node_mac):
        self._cmd._send_to_ctrl(
            ctrlTypes.MessageType.UPGRADE_REQ,
            ctrlTypes.UpgradeReq(
                ctrlTypes.UpgradeReqType.PREPARE_UPGRADE,
                self._cmd._upgrade_req_id,
                self._image_meta["md5"],
                self._image_url,
                None,  # delay timer, not used in prepare
                self._download_attempts,
            ),
            consts.UPGRADE_APP_MINION_ID,
            node_mac,
        )

    def _check_prepare_status(self, node_name, node_mac):
        start_time = time.time()
        elapsed_time = 0  # s

        while elapsed_time < self._timeout:
            # within timeout, query controller for minion status every 5s
            time.sleep(5)

            status_report = self._cmd._get_status_dump().statusReports[node_mac]
            upgrade_status = status_report.upgradeStatus
            time_str = time.strftime(
                "%D %H:%M:%S", time.localtime(status_report.timeStamp)
            )

            _log.info(
                "%s : %s (reqId: %s) - updated at %s",
                node_name,
                ctrlTypes.UpgradeStatusType._VALUES_TO_NAMES.get(
                    upgrade_status.usType, "UNKNOWN"
                ),
                upgrade_status.upgradeReqId,
                time_str,
            )

            # prepare/flash succeeded
            if (
                upgrade_status.usType == ctrlTypes.UpgradeStatusType.FLASHED
                and upgrade_status.nextImage.md5 == self._image_meta["md5"]
            ):
                return True

            # prepare/flash failed
            if (
                upgrade_status.usType
                in [
                    ctrlTypes.UpgradeStatusType.DOWNLOAD_FAILED,
                    ctrlTypes.UpgradeStatusType.FLASH_FAILED,
                ]
                and upgrade_status.upgradeReqId == self._cmd._upgrade_req_id
            ):
                _log.info("%s : %s", node_name, upgrade_status.reason)
                return False

            elapsed_time = time.time() - start_time

        _log.info(
            "%s : %s",
            node_name,
            "Moving to next node. The preparation " "should continue in the background",
        )
        return True


class CommitHelper(object):
    def __init__(self, cmd, version, timeout, skip_failure, delay):
        self._cmd = cmd
        self._version = version
        self._timeout = timeout
        self._skip_failure = skip_failure
        self._delay = delay

    def do_commit_wrapper(self, node_name, node_mac):
        ret = self.do_commit(node_name, node_mac)
        if not ret and not self._skip_failure:
            self._cmd._my_exit(False)

    def do_commit(self, node_name, node_mac):

        status_report = self._cmd._get_status_dump().statusReports[node_mac]

        if self._version:
            if status_report.version.strip() == self._version.strip():
                _log.info("%s : Has new image, skipping", node_name)
                return True

            # print next image table for minion
            next_image = status_report.upgradeStatus.nextImage
            headers = ["NodeName", "NextImageVersion", "NextImageMd5"]
            table = [[node_name, next_image.version, next_image.md5]]
            print(tabulate.tabulate(table, headers))
            print("")
        else:
            _log.warn("No version provided. Skipping version check")

        if not self._is_commit_ready(node_name, node_mac, status_report):
            return False
        _log.info(
            "%s : [%s] Sending commit/reboot req with %d seconds delay ...",
            node_name,
            node_mac,
            self._delay,
        )
        self._send_commit_req(node_mac)
        if self._delay == 0:
            _log.info("%s : Checking commit/reboot status ...", node_name)
            return self._check_commit_status(node_name, node_mac)
        return True

    def _is_commit_ready(self, node_name, node_mac, status_report):
        upgrade_status = status_report.upgradeStatus

        if upgrade_status.usType != ctrlTypes.UpgradeStatusType.FLASHED:
            _log.info(
                "%s : Is not ready for reboot.  Current Status [%s]",
                node_name,
                ctrlTypes.UpgradeStatusType._VALUES_TO_NAMES.get(
                    upgrade_status.usType, "UNKNOWN"
                ),
            )
            return False

        # verify version match
        if (
            self._version
            and upgrade_status.nextImage.version.strip() != self._version.strip()
        ):
            _log.info("%s : %s", node_name, " is flashed with different image")
            _log.info(
                "%s : got:%s -- expected:%s",
                node_name,
                upgrade_status.nextImage.version,
                self._version,
            )
            return False

        return True

    def _send_commit_req(self, node_mac):
        self._cmd._send_to_ctrl(
            ctrlTypes.MessageType.UPGRADE_REQ,
            ctrlTypes.UpgradeReq(
                ctrlTypes.UpgradeReqType.COMMIT_UPGRADE,
                self._cmd._upgrade_req_id,
                "",  # image meta md5, not used in commit
                "",  # image url, not used in commit
                self._delay,
            ),
            consts.UPGRADE_APP_MINION_ID,
            node_mac,
        )

    def _check_commit_status(self, node_name, node_mac):
        start_time = time.time()
        elapsed_time = 0  # s
        image_version_match = False

        # check minion image after upgrade
        while elapsed_time < self._timeout:
            # within timeout, query controller for minion status every 5s
            time.sleep(5)

            if image_version_match:
                # check for wireless link health:
                # - return True if at least one wireless link is up
                # - recheck if not
                if self._check_wireless_link_aliveness(node_name, node_mac):
                    return True
            else:
                # check for image version:
                # - return False if NOT_MATCH
                # - recheck if IN_PROGRESS
                # - mark image_version_match = True if MATCH
                ret = self._check_image_version(node_name, node_mac)
                if ret == MATCH:
                    image_version_match = True
                elif ret == NOT_MATCH:
                    return False

            elapsed_time = time.time() - start_time

        _log.info("%s : Timed out trying to probe the node", node_name)
        return False

    def _check_image_version(self, node_name, node_mac):
        status_report = self._cmd._get_status_dump().statusReports[node_mac]
        upgrade_status = status_report.upgradeStatus
        time_str = time.strftime("%D %H:%M:%S", time.localtime(status_report.timeStamp))
        # commit succeeded
        if upgrade_status.usType == ctrlTypes.UpgradeStatusType.NONE:
            if self._version:
                # perform version check
                if status_report.version.strip() == self._version.strip():
                    _log.info("%s : %s", node_name, "Is back with expected version")
                    return MATCH
            else:
                # No version check required
                _log.info("%s : %s", node_name, "Is back online")
                return MATCH

        # commit failed
        if upgrade_status.usType == ctrlTypes.UpgradeStatusType.COMMIT_FAILED:
            _log.info("%s : %s", node_name, upgrade_status.reason)
            return NOT_MATCH

        # commit still in process
        if upgrade_status.usType == ctrlTypes.UpgradeStatusType.FLASHED:
            # The last status we received from the node would be FLASHED.
            # But this would be confusing when printed for users.
            # Rather print a more layman message.
            _log.info(
                "%s : Waiting to hear back ... (last alive at %s)", node_name, time_str
            )
        else:
            _log.info(
                "%s : %s (last alive at %s)",
                node_name,
                ctrlTypes.UpgradeStatusType._VALUES_TO_NAMES.get(
                    upgrade_status.usType, "UNKNOWN"
                ),
                time_str,
            )
        return IN_PROGRESS

    def _check_wireless_link_aliveness(self, node_name, node_mac):
        topology = self._cmd._get_topology()
        wireless_links = [
            link
            for link in topology.links
            if node_name in [link.a_node_name, link.z_node_name]
            and link.link_type == topoTypes.LinkType.WIRELESS
        ]

        if not wireless_links:
            return True

        for link in wireless_links:
            if link.is_alive:
                _log.info("%s : Pass wireless link health check", node_name)
                return True

        _log.info(
            "%s : %s",
            node_name,
            "Waiting for one successful ignition on wireless link...",
        )
        return False


class ResetStatusHelper(object):
    def __init__(self, cmd):
        self._cmd = cmd

    def reset_status(self, node_name, node_mac):
        _log.info("%s : [%s] Sending reset status request ...", node_name, node_mac)
        self._cmd._send_to_ctrl(
            ctrlTypes.MessageType.UPGRADE_REQ,
            ctrlTypes.UpgradeReq(
                ctrlTypes.UpgradeReqType.RESET_STATUS,
                self._cmd._upgrade_req_id,
                "",  # md5, not used in reset_status
                "",
            ),  # image url, not used in reset_status
            consts.UPGRADE_APP_MINION_ID,
            node_mac,
        )
