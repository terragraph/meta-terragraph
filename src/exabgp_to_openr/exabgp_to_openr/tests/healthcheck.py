#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import asyncio
import unittest
from ipaddress import ip_network
from typing import List
from unittest.mock import patch

from exabgp_to_openr import healthcheck
from openr.Fib import ttypes as fib_types
from openr.Network import ttypes as network_types
from openr.utils import ipnetwork


healthcheck.LOG = unittest.mock.Mock()


def mock_empty_list(*args, **kwargs) -> List:
    return []


def mock_prefix_list(*args, **kwargs) -> fib_types.RouteDatabase:
    prefix = ip_network("69::/69")
    return fib_types.RouteDatabase(
        thisNodeName="Unittest",
        mplsRoutes=[],
        unicastRoutes=[
            network_types.UnicastRoute(
                dest=ipnetwork.ip_str_to_prefix(prefix.exploded), nextHops=[]
            )
        ],
    )


class OpenrRCheckerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.loop = asyncio.get_event_loop()
        self.orc = healthcheck.OpenRChecker("69::/64")

    def test_check_local_interface(self) -> None:
        self.assertFalse(self.orc.check_local_interface())
        with patch("exabgp_to_openr.healthcheck.ifaddresses") as mock_ifaddr:
            mock_ifaddr.return_value = {
                10: [{"addr": "69::69", "netmask": "FF:FF:FF/69"}]
            }
            self.assertTrue(self.orc.check_local_interface())

    def test_check(self) -> None:
        with patch("exabgp_to_openr.healthcheck.get_openr_fib_db", mock_empty_list):
            self.assertFalse(self.loop.run_until_complete(self.orc.check()))

        with patch("exabgp_to_openr.healthcheck.get_openr_fib_db", mock_prefix_list):
            self.assertTrue(self.loop.run_until_complete(self.orc.check()))
