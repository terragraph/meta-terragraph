#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import tempfile
import unittest
from unittest.mock import Mock, patch

import tg.commands.zeroize


PRE_VERSION_STR = "Facebook Terragraph Release RELEASE_M27_PRE-58-g59fed5d1a (cooper@devbig674 Mon Sep 17 22:45:43 UTC 2018)"  # noqa: B950
VERSION_STR = "Facebook Terragraph Release RELEASE_M26_1-0-g1fc3228c1 (michaelcallahan@devvm2076 Tue Sep  4 21:42:45 UTC 2018)"  # noqa: B950
VERSION_STRS = ((PRE_VERSION_STR, "RELEASE_M27_PRE"), (VERSION_STR, "RELEASE_M26_1"))


class TestZeroize(unittest.TestCase):
    @patch("tg.commands.zeroize._log")
    @patch("tg.commands.zeroize.Path.mkdir")
    @patch("tg.commands.zeroize.click.prompt")
    @patch("tg.commands.zeroize.rmtree")
    @patch("tg.commands.zeroize.run")
    def test_zeroize(
        self, mocked_run, mocked_rmtree, mocked_prompt, mocked_mkdir, mocked_log
    ):
        with tempfile.NamedTemporaryFile("w") as tfp:
            tfp.write(VERSION_STR)
            tfp.seek(0)

            tgz = tg.commands.zeroize.ZeroizeCmd(Mock())
            tgz.zeroize(True, False, version_file=tfp.name)
            self.assertTrue(mocked_rmtree.called)
            self.assertTrue(mocked_run.called)

    def test_find_versions(self):
        tgz = tg.commands.zeroize.ZeroizeCmd(Mock())
        for version_str, expected_ver in VERSION_STRS:
            with tempfile.NamedTemporaryFile("w") as tfp:
                tfp.write(version_str)
                tfp.seek(0)
                self.assertEqual(tgz._find_release(tfp.name), expected_ver)


if __name__ == "__main__":
    unittest.main()
