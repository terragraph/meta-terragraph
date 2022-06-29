#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# flake8: noqa

import sys
import unittest
from unittest import mock

from click.testing import CliRunner


# Mock out tg module imports we don't need to test
sys.modules["terragraph_thrift"] = mock.MagicMock()
sys.modules["terragraph_thrift.Controller"] = mock.MagicMock()
sys.modules["terragraph_thrift.Aggregator"] = mock.MagicMock()
sys.modules["terragraph_thrift.DriverMessage"] = mock.MagicMock()
sys.modules["terragraph_thrift.Topology"] = mock.MagicMock()
sys.modules["thrift"] = mock.Mock()
sys.modules["thrift.protocol"] = mock.Mock()
sys.modules["thrift.protocol.TCompactProtocol"] = mock.Mock()
sys.modules["thrift.protocol.TSimpleJSONProtocolFactory"] = mock.Mock()
sys.modules["thrift.protocol.TSimpleJSONProtocol"] = mock.Mock()
sys.modules["thrift.util"] = mock.Mock()
sys.modules["zmq"] = mock.Mock()


# Modules to test - Must be imported after tge mocks above
from tg import tg  # isort:skip

# Other tests imported so they run
from tg.tests.test_scan import TestScan  # isort:skip
from tg.tests.test_zeroize import TestZeroize  # isort:skip
from tg.tests.test_consts import TestConsts  # isort:skip


class TestTg(unittest.TestCase):
    """ Test click running """

    def test_help_output(self):
        runner = CliRunner()
        result = runner.invoke(tg.tg, ["--help"])
        self.assertEqual(result.exit_code, 0)


if __name__ == "__main__":
    unittest.main()
