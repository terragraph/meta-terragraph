#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import asyncio
import unittest
from ipaddress import ip_address, ip_network
from typing import List
from unittest.mock import Mock, patch

from exabgp_to_openr import fibs
from exabgp_to_openr.tests.fixtures import TEST_CONFIG
from openr.Lsdb import ttypes as lsdb_types
from openr.Network import ttypes as network_types
from openr.OpenrCtrl.ttypes import OpenrError
from openr.Platform.ttypes import PlatformError
from openr.utils import ipnetwork


fibs.LOG = Mock()


def mock_empty_list(*args, **kwargs) -> List:
    return []


async def mock_fib_routes(*args, **kwargs) -> List[network_types.UnicastRoute]:
    return mock_getRouteTableByClient()


def mock_getRouteTableByClient(*args, **kwargs) -> List[network_types.UnicastRoute]:
    prefix = ip_network("69::/69")
    next_hop = ip_address("70::1")
    return [
        network_types.UnicastRoute(
            dest=ipnetwork.ip_str_to_prefix(prefix.exploded),
            nextHops=[
                network_types.NextHopThrift(
                    address=ipnetwork.ip_str_to_addr(next_hop.exploded)
                )
            ],
        )
    ]


def mock_raisePlatformError(*args, **kwargs) -> None:
    raise PlatformError("Unittest Error")


class FibVppTests(unittest.TestCase):
    def setUp(self) -> None:
        self.loop = asyncio.get_event_loop()
        self.vf = fibs.VppFib(TEST_CONFIG)
        self.test_prefix = ip_network("69::/69")
        self.test_next_hop = ip_address("70::1")
        self.test_args = (self.test_prefix, self.test_next_hop)

    def test_add_route(self) -> None:
        with patch("exabgp_to_openr.fibs.fib_client_run", mock_empty_list):
            self.assertTrue(
                self.loop.run_until_complete(self.vf.add_route(*self.test_args))
            )

        self.assertFalse(
            self.loop.run_until_complete(
                self.vf.add_route(self.test_prefix, ip_address("fe80::1"))
            )
        )

        with patch("exabgp_to_openr.fibs.fib_client_run", mock_raisePlatformError):
            self.assertFalse(
                self.loop.run_until_complete(self.vf.add_route(*self.test_args))
            )

    def test_check_for_route(self) -> None:
        with patch("exabgp_to_openr.fibs.fib_client_run", mock_getRouteTableByClient):
            self.assertTrue(
                self.loop.run_until_complete(self.vf.check_for_route(*self.test_args))
            )

        with patch("exabgp_to_openr.fibs.fib_client_run", mock_raisePlatformError):
            self.assertFalse(
                self.loop.run_until_complete(self.vf.check_for_route(*self.test_args))
            )

        with patch("exabgp_to_openr.fibs.fib_client_run", mock_empty_list):
            self.assertFalse(
                self.loop.run_until_complete(self.vf.check_for_route(*self.test_args))
            )

    def test_del_all_routes(self) -> None:
        # Test getting routes and ensure we call fib_client_run once as
        # we skip the first call away with our mock to _get_all_fib_routes
        with patch("exabgp_to_openr.fibs.VppFib._get_all_fib_routes", mock_fib_routes):
            with patch("exabgp_to_openr.fibs.fib_client_run") as mock_client:
                self.assertTrue(
                    self.loop.run_until_complete(
                        self.vf.del_all_routes(self.test_next_hop)
                    )
                )
                self.assertEqual(1, mock_client.call_count)

        # Test getting no unicast routes returned
        with patch("exabgp_to_openr.fibs.fib_client_run", mock_empty_list):
            self.assertFalse(
                self.loop.run_until_complete(self.vf.del_all_routes(self.test_next_hop))
            )

    def test_del_route(self) -> None:
        with patch("exabgp_to_openr.fibs.fib_client_run", mock_empty_list):
            self.assertTrue(
                self.loop.run_until_complete(self.vf.del_route(*self.test_args))
            )

        with patch("exabgp_to_openr.fibs.fib_client_run", mock_raisePlatformError):
            self.assertFalse(
                self.loop.run_until_complete(self.vf.del_route(*self.test_args))
            )


def mock_getPrefixesByType(*args, **kwargs) -> List[lsdb_types.PrefixEntry]:
    prefix = ip_network("69::/69")
    return [
        lsdb_types.PrefixEntry(
            forwardingType=lsdb_types.PrefixForwardingType.IP,
            prefix=ipnetwork.ip_str_to_prefix(prefix.exploded),
            type=network_types.PrefixType.BREEZE,
        )
    ]


def mock_raiseOpenrError(*args, **kwargs) -> None:
    raise OpenrError("Unittest Error")


class OpenrPrefixMgrTests(unittest.TestCase):
    def setUp(self) -> None:
        self.loop = asyncio.get_event_loop()
        self.opm = fibs.OpenrPrefixMgr(TEST_CONFIG)
        self.test_prefix = ip_network("69::/69")

    def test_add_route(self) -> None:
        with patch("exabgp_to_openr.fibs.openr_prefixmgr_client", mock_empty_list):
            self.assertTrue(
                self.loop.run_until_complete(self.opm.add_route(self.test_prefix, None))
            )

        self.assertFalse(
            self.loop.run_until_complete(
                self.opm.add_route(self.test_prefix, ip_address("fe80::1"))
            )
        )

        with patch("exabgp_to_openr.fibs.openr_prefixmgr_client", mock_raiseOpenrError):
            self.assertFalse(
                self.loop.run_until_complete(self.opm.add_route(self.test_prefix, None))
            )

    def test_check_for_route(self) -> None:
        with patch(
            "exabgp_to_openr.fibs.openr_prefixmgr_client", mock_getPrefixesByType
        ):
            self.assertTrue(
                self.loop.run_until_complete(
                    self.opm.check_for_route(self.test_prefix, None)
                )
            )

        with patch("exabgp_to_openr.fibs.openr_prefixmgr_client", mock_empty_list):
            self.assertFalse(
                self.loop.run_until_complete(
                    self.opm.check_for_route(self.test_prefix, None)
                )
            )

    def test_del_all_routes(self) -> None:
        with patch("exabgp_to_openr.fibs.openr_prefixmgr_client", mock_empty_list):
            self.assertTrue(
                self.loop.run_until_complete(
                    self.opm.del_all_routes(network_types.PrefixType.BREEZE)
                )
            )

        with patch("exabgp_to_openr.fibs.openr_prefixmgr_client", mock_raiseOpenrError):
            self.assertFalse(
                self.loop.run_until_complete(
                    self.opm.del_all_routes(network_types.PrefixType.BREEZE)
                )
            )

    def test_del_route(self) -> None:
        with patch("exabgp_to_openr.fibs.openr_prefixmgr_client", mock_empty_list):
            self.assertTrue(
                self.loop.run_until_complete(self.opm.del_route(self.test_prefix, None))
            )

        with patch("exabgp_to_openr.fibs.openr_prefixmgr_client", mock_raiseOpenrError):
            self.assertFalse(
                self.loop.run_until_complete(self.opm.del_route(self.test_prefix, None))
            )
