#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

import update_firewall
from update_firewall_tests_fixtures import NODE_CONFIG_JSON_STR, NODE_CONFIG_THRIFT


class TestUpdateFirewall(unittest.TestCase):
    def test_generate_rules(self) -> None:
        input_chain = update_firewall.generate_rules(
            update_firewall.INPUT_CHAIN, NODE_CONFIG_THRIFT.firewallConfig
        )
        self.assertTrue(input_chain.chain.is_builtin())

        # We expect 6 rules based on our config
        self.assertEqual(6, len(input_chain.rules))

        # TODO! Test "allow_established"
        # (broken as of python3-iptables v1.0.0 + iptables v1.8.3)

    def test_get_firewall_config(self) -> None:
        with TemporaryDirectory() as td:
            td_path = Path(td)
            node_config_path = td_path / "unittest_node_config.json"

            # Test we return None if config does not exist
            with patch("update_firewall.print") as mock_print:
                self.assertIsNone(update_firewall.get_firewall_config(node_config_path))
                self.assertEqual(2, mock_print.call_count)

            # Make a valid config
            with node_config_path.open("w") as ncfp:
                ncfp.write(NODE_CONFIG_JSON_STR)

            self.assertEqual(
                NODE_CONFIG_THRIFT.firewallConfig,
                update_firewall.get_firewall_config(node_config_path),
            )


if __name__ == "__main__":
    unittest.main()
