#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import unittest
from argparse import Namespace
from copy import deepcopy
from json import dump
from pathlib import Path
from re import sub
from tempfile import TemporaryDirectory
from unittest.mock import Mock, patch

import exaconf
from exaconf import bgp, kvstore

from .fixtures import (
    EXPECTED_AIOEXABGP_CONF,
    EXPECTED_EXABGP_CONF,
    EXPECTED_EXABGP_MD5_CONF,
    EXPECTED_EXABGP_NEIGHBOR_CONF,
    EXPECTED_EXABGP_NEIGHBOR_HOSTNAME_CONF,
    EXPECTED_LOADED_CONF,
    EXPECTED_MAC_ADDRESS,
    FAKE_E2E_SITE_PREFIX,
    FAKE_NODE_CONFIG,
    FAKE_NODE_INFO,
    FakeOpenRClient,
    FakeOpenRClientEmpty,
)


def return_zero(*args, **kwargs) -> int:
    return 0


# Comment this to unsilent logging
exaconf.LOG = Mock()


class BaseTests(unittest.TestCase):
    maxDiff = None

    # This test relies on running in image for the template file
    def test_generate_aioexabgp_config(self) -> None:
        with TemporaryDirectory() as td:
            td_path = Path(td)
            with patch("exaconf.get_e2e_network_prefix") as mock_kvstore:
                mock_kvstore.return_value = FAKE_E2E_SITE_PREFIX
                self.assertEqual(
                    0,
                    exaconf.generate_aioexabgp_config(
                        FAKE_NODE_CONFIG["bgpParams"],
                        FAKE_NODE_CONFIG["envParams"],
                        FAKE_NODE_CONFIG["popParams"],
                        Path("/etc/exabgp/aioexabgp.template"),
                        td_path,
                    ),
                )
            expected_conf = td_path / "aioexabgp.conf"
            self.assertTrue(expected_conf.exists())
            self.assertEqual(exaconf._load_json(expected_conf), EXPECTED_AIOEXABGP_CONF)

    # This test relies on running in image for the template file
    def test_generate_aioexabgp_config_no_e2e_network_prefix(self) -> None:
        expected_aioexabgp_conf = deepcopy(EXPECTED_AIOEXABGP_CONF)
        del expected_aioexabgp_conf["advertise"]["prefixes"]["2400:69::/48"]

        with TemporaryDirectory() as td:
            td_path = Path(td)
            with patch("exaconf.get_e2e_network_prefix") as mock_kvstore:
                mock_kvstore.return_value = None
                self.assertEqual(
                    0,
                    exaconf.generate_aioexabgp_config(
                        FAKE_NODE_CONFIG["bgpParams"],
                        FAKE_NODE_CONFIG["envParams"],
                        FAKE_NODE_CONFIG["popParams"],
                        Path("/etc/exabgp/aioexabgp.template"),
                        td_path,
                    ),
                )
            expected_conf = td_path / "aioexabgp.conf"
            self.assertTrue(expected_conf.exists())
            self.assertEqual(exaconf._load_json(expected_conf), expected_aioexabgp_conf)

    # This test relies on running in image for the template file
    def test_generate_aioexabgp_config_no_specific(self) -> None:
        bgp_params = deepcopy(FAKE_NODE_CONFIG["bgpParams"])
        del bgp_params["specificNetworkPrefixes"]

        expected_aioexabgp_conf = deepcopy(EXPECTED_AIOEXABGP_CONF)
        del expected_aioexabgp_conf["advertise"]["prefixes"]["1469:1280::/57"]
        del expected_aioexabgp_conf["advertise"]["prefixes"]["1469::/54"]

        with TemporaryDirectory() as td:
            td_path = Path(td)
            with patch("exaconf.get_e2e_network_prefix") as mock_kvstore:
                mock_kvstore.return_value = FAKE_E2E_SITE_PREFIX
                self.assertEqual(
                    0,
                    exaconf.generate_aioexabgp_config(
                        FAKE_NODE_CONFIG["bgpParams"],
                        FAKE_NODE_CONFIG["envParams"],
                        FAKE_NODE_CONFIG["popParams"],
                        Path("/etc/exabgp/aioexabgp.template"),
                        td_path,
                    ),
                )
            expected_conf = td_path / "aioexabgp.conf"
            self.assertTrue(expected_conf.exists())
            self.assertEqual(exaconf._load_json(expected_conf), EXPECTED_AIOEXABGP_CONF)

    # Will test these functions in dedicated separate tests
    @patch("exaconf.generate_aioexabgp_config", return_zero)
    @patch("exaconf.generate_exabgp_config", return_zero)
    def test_generate_configs(self) -> None:
        with TemporaryDirectory() as td:
            td_path = Path(td)
            output_path = td_path / "output"
            aioexabgp_conf_path = td_path / "aioexabgp.conf"
            exabgp_conf_path = td_path / "exabgp.conf"
            fake_args = Namespace(
                aioexabgp_template=str(aioexabgp_conf_path),
                exabgp_template=str(exabgp_conf_path),
                output_dir=str(output_path),
            )

            with patch("exaconf.load_node_config") as mock_conf_good:
                mock_conf_good.return_value = EXPECTED_LOADED_CONF
                self.assertEqual(0, exaconf.generate_configs(fake_args, td_path))
                mock_conf_good.return_value = (None, None, None)
                self.assertEqual(10, exaconf.generate_configs(fake_args, td_path))

    # This test relies on running in image for the template file
    @patch("exaconf.get_mac_address")
    def test_generate_exabgp_config(self, mock_mac: Mock) -> None:
        mock_mac.return_value = EXPECTED_MAC_ADDRESS

        # Add a md5 password to test we generate correctly
        md5_fake_config = deepcopy(FAKE_NODE_CONFIG)
        md5_fake_config["bgpParams"]["md5Password"] = "MD5PASS"

        for expected_config, fake_node_config in (
            (EXPECTED_EXABGP_CONF, FAKE_NODE_CONFIG),
            (EXPECTED_EXABGP_MD5_CONF, md5_fake_config),
        ):
            with TemporaryDirectory() as td:
                td_path = Path(td)
                self.assertEqual(
                    0,
                    exaconf.generate_exabgp_config(
                        fake_node_config["bgpParams"],
                        fake_node_config["popParams"],
                        Path("/etc/exabgp/exabgp.conf.template"),
                        td_path,
                    ),
                )
                expected_conf = td_path / "exabgp.conf"
                self.assertTrue(expected_conf.exists())
                with expected_conf.open("r") as ecfp:
                    generated_exabgp_conf = ecfp.read()

                # Hack to ensure we get a consistent path for aioexabgp.conf location
                # /tmp/tmpwoyoxylx/aioexabgp.conf
                normalized_exabgp_conf = sub(
                    r"-c.*aioexabgp.conf;",
                    "-c /etc/aioexabgp.conf;",
                    generated_exabgp_conf,
                )
                self.assertEqual(normalized_exabgp_conf, expected_config)

    def test_generate_exabgp_config_neighbors(self) -> None:
        host_domain_neigh_conf = deepcopy(FAKE_NODE_CONFIG)
        for idx, neighbor in host_domain_neigh_conf["bgpParams"]["neighbors"].items():
            neighbor["hostname"] = f"router{idx}"
            neighbor["domainname"] = "network.com"

        for expected_config, fake_node_config in (
            (EXPECTED_EXABGP_NEIGHBOR_CONF, FAKE_NODE_CONFIG),
            (EXPECTED_EXABGP_NEIGHBOR_HOSTNAME_CONF, host_domain_neigh_conf),
        ):
            self.assertEqual(
                exaconf.generate_exabgp_config_neighbors(
                    fake_node_config["bgpParams"]["neighbors"]
                ),
                expected_config,
            )

    def test_get_mac_address(self) -> None:
        with TemporaryDirectory() as td:
            td_path = Path(td)
            node_info_path = td_path / "node_info"
            with node_info_path.open("w") as nifp:
                nifp.write(FAKE_NODE_INFO)

            self.assertEqual(
                EXPECTED_MAC_ADDRESS, exaconf.get_mac_address(node_info_path)
            )

    def test_load_node_config(self) -> None:
        with TemporaryDirectory() as td:
            td_path = Path(td)
            node_config_path = td_path / "node_config.json"
            with node_config_path.open("w") as ncfp:
                dump(FAKE_NODE_CONFIG, ncfp)

            self.assertEqual(
                exaconf.load_node_config(node_config_path), EXPECTED_LOADED_CONF
            )
            node_config_path.unlink()
            self.assertEqual(
                exaconf.load_node_config(node_config_path), (None, None, None)
            )

            with patch("exaconf.load") as load_mock:
                load_mock.return_value = {}
                self.assertEqual(
                    exaconf.load_node_config(node_config_path), (None, None, None)
                )

    def test_main(self) -> None:
        # Expect 1 as e2e_config_path should not exist
        self.assertEqual(1, exaconf.main())


class BGPTests(unittest.TestCase):
    maxDiff = None

    def test_get_router_id(self) -> None:
        MAC_ADDR = "FF:FF:FF:FF:FF:FF"
        ASN = 2 ** 16 - 1

        self.assertEqual("79.255.255.255", bgp.gen_router_id(ASN, MAC_ADDR))

        # Ensure we only care about "Class C" / 24 bits of the returned ID
        MAC_ADDR = "FF:FF:FF:FF:FF:00"
        self.assertNotEqual("79.255.255.255", bgp.gen_router_id(ASN, MAC_ADDR))

        MAC_ADDR = "nope"
        with patch("exaconf.bgp.LOG.exception") as mock_log_exception:
            self.assertFalse(bgp.gen_router_id(ASN, MAC_ADDR))
            mock_log_exception.assert_called()


class KVStoreTests(unittest.TestCase):
    maxDiff = None

    def test_get_e2e_network_prefix(self) -> None:
        with patch("exaconf.kvstore.get_openr_ctrl_client", FakeOpenRClient):
            self.assertEqual(FAKE_E2E_SITE_PREFIX, kvstore.get_e2e_network_prefix())

        with patch("exaconf.kvstore.get_openr_ctrl_client", FakeOpenRClientEmpty):
            self.assertIsNone(kvstore.get_e2e_network_prefix())


if __name__ == "__main__":
    unittest.main()
