#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import os
import unittest
from contextlib import redirect_stdout
from tempfile import NamedTemporaryFile
from unittest.mock import MagicMock, patch

import tg.commands.scan as scan

from . import test_scan_fixtures as tsf


class TestScan(unittest.TestCase):
    def test_scan_thrift_to_dict(self):
        # Int
        res = scan.thrift_to_dict(1)
        self.assertEqual(res, 1)
        # String
        res = scan.thrift_to_dict("Hello")
        self.assertEqual(res, "Hello")
        # Set
        res = scan.thrift_to_dict({1, 2})
        self.assertEqual(res, [1, 2])
        # Dict
        res = scan.thrift_to_dict({"key": 1})
        self.assertEqual(res, {"key": 1})
        # Thrift
        res = scan.thrift_to_dict(tsf.ThriftObject())
        self.assertEqual(res, {"addr": "00:00:00:00:00:00", "beam": 0})

    def test_scan_TimeConverter(self):
        tc = scan.TimeConverter()
        # Unix - no convert
        res = tc.conv("unix", "unix", 579169800)
        self.assertEqual(res, 579169800)
        # Local - SF
        res = tc.conv("local", "sf", "1988-05-09 08:30:00")
        self.assertEqual(res, 164503136250)
        # Local - SF
        res = tc.conv("sf", "local", 164503136250)
        self.assertEqual(res, "1988-05-09 08:30:00")

    @patch("tg.commands.scan.time.time", MagicMock(return_value=579169800))
    def test_scan_TimeConverter_guess_format(self):
        tc = scan.TimeConverter()
        tc.bounds = None

        res = tc.guess_format(579169800)
        self.assertEqual(res, ("unix", 579169800.0))

        res = tc.guess_format(0)
        self.assertEqual(res, (None, None))

        res = tc.guess_format("now")
        self.assertEqual(res, (None, None))

        res = tc.guess_format("1988-05-09 08:30:00")
        self.assertEqual(res, ("unix", 579169800.0))

    @patch("tg.commands.scan.time.time", MagicMock(return_value=579169800))
    def test_scan_TimeConvCmd(self):
        tcc = scan.TimeConvCmd(MagicMock(return_value=MagicMock()))

        with NamedTemporaryFile("w", delete=False) as tf:
            with redirect_stdout(tf):
                tcc.run("unix", "unix", [579169800])
            tf.close()
            with open(tf.name) as tfr:
                result = tfr.read()
            self.assertEqual(result, "579169800\n")
            os.unlink(tf.name)

        with NamedTemporaryFile("w", delete=False) as tf:
            with redirect_stdout(tf):
                tcc.run(None, None, [579169800, 579169800])
            tf.close()
            with open(tf.name) as tfr:
                result = tfr.read()
            self.assertEqual(result, tsf.TIMECONVCMD_OUTPUT2)
            os.unlink(tf.name)

    def test_scan_SlotMapConfigCmd(self):

        smc = scan.SlotMapConfigCmd(MagicMock(return_value=MagicMock()))
        smc._connect_to_controller = MagicMock()
        smc._send_to_ctrl = MagicMock()
        smc._recv_from_ctrl = MagicMock()

        smc.run(None)

        smc._connect_to_controller.assert_called()
        smc._recv_from_ctrl.assert_called()
        smc._recv_from_ctrl.assert_called()

    @patch("sys.exit", tsf._raiseSysExit)
    def test_scan_ScanScheduleCmd(self):

        ssc = scan.ScanScheduleCmd(MagicMock(return_value=MagicMock()))
        ssc._connect_to_controller = MagicMock()
        ssc._send_to_ctrl = MagicMock()
        ssc._recv_from_ctrl = MagicMock()

        with self.assertRaises(SystemExit):
            ssc.run(None, None, None, None, None, None)


if __name__ == "__main__":
    unittest.main()
