#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import sys
import unittest
from unittest.mock import MagicMock, Mock

from click import BadParameter


sys.modules["fbzmq"] = Mock()
sys.modules["fbzmq.Monitor"] = Mock()
sys.modules["fbzmq.Monitor.ttypes"] = Mock()
sys.modules["terragraph_thrift"] = Mock()
sys.modules["terragraph_thrift.BWAllocation"] = Mock()
sys.modules["terragraph_thrift.Controller"] = MagicMock()
sys.modules["terragraph_thrift.DriverMessage"] = Mock()
sys.modules["terragraph_thrift.FwOptParams"] = Mock()
sys.modules["terragraph_thrift.NodeConfig"] = Mock()
sys.modules["terragraph_thrift.PassThru"] = Mock()
sys.modules["terragraph_thrift.Topology"] = Mock()
sys.modules["thrift"] = Mock()
sys.modules["thrift.protocol"] = Mock()
sys.modules["thrift.protocol.TSimpleJSONProtocol"] = Mock()
sys.modules["thrift.transport"] = Mock()
sys.modules["thrift.util"] = Mock()
sys.modules["zmq"] = Mock()
sys.modules["zmq.backend"] = Mock()
sys.modules["zmq.backend.cython"] = Mock()
sys.modules["zmq.backend.cython.constants"] = Mock()

from click.testing import CliRunner  # isort:skip
from r2d2 import r2d2_commands  # isort:skip
from r2d2.r2d2 import r2d2, validate_optional_mac  # isort:skip


class TestR2d2Commands(unittest.TestCase):
    def test_main_help_output(self) -> None:
        runner = CliRunner()
        result = runner.invoke(r2d2, ["--help"])
        self.assertEqual(result.exit_code, 0)

    @unittest.skip("Hacks to maintain _ in CLI sub commands don't seem to work anymore")
    def test_sub_command_help(self) -> None:
        runner = CliRunner()
        # Ensure we override the default Click >= 7.0 behavior and support _
        result = runner.invoke(r2d2, ["fw_set_log_config", "--help"])
        self.assertEqual(result.exit_code, 0)

    def test_commands_imported(self) -> None:
        dir(r2d2_commands)

    def test_mac_regex(self) -> None:
        test_valid_mac = "aa:bb:cc:dd:ee:ff"
        for mac in (test_valid_mac, test_valid_mac.replace(":", "-")):
            self.assertEqual(mac, validate_optional_mac(None, None, mac))
        self.assertEqual("", validate_optional_mac(None, None, ""))
        with self.assertRaises(BadParameter):
            validate_optional_mac(None, None, "aa/bb/cc/dd/ee/ff")


if __name__ == "__main__":
    unittest.main()
