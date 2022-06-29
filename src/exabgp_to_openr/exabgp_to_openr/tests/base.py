#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import asyncio
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

from click.testing import CliRunner
from exabgp_to_openr import fibs
from exabgp_to_openr.eto import (
    _load_json_config,
    async_main,
    get_fib,
    get_health_checker,
    main,
)
from exabgp_to_openr.healthcheck import OpenRChecker
from exabgp_to_openr.tests.fibs import FibVppTests, OpenrPrefixMgrTests  # noqa: F401
from exabgp_to_openr.tests.fixtures import TEST_CONFIG
from exabgp_to_openr.tests.healthcheck import OpenrRCheckerTests  # noqa: F401


class BaseTests(unittest.TestCase):
    def test_help_output(self) -> None:
        runner = CliRunner()
        result = runner.invoke(main, ["--help"])
        self.assertEqual(result.exit_code, 0)

    def test_load_json_config(self) -> None:
        with TemporaryDirectory() as td:
            td_path = Path(td)
            bad_json = td_path / "bad.json"
            good_json = td_path / "good.json"

            for json_file, content in (
                (bad_json, "error"),
                (good_json, '{"test":123}\n'),
            ):
                with json_file.open("w") as jfp:
                    jfp.write(content)

            json_returned = _load_json_config(str(good_json))
            self.assertTrue("test" in json_returned)
            self.assertFalse(_load_json_config(str(bad_json)))

    def test_get_fib(self):
        self.assertEqual(
            type(get_fib("openr-PrEfIxMgr", TEST_CONFIG)),
            type(fibs.OpenrPrefixMgr(TEST_CONFIG)),
        )
        self.assertEqual(
            type(get_fib("vpp", TEST_CONFIG)), type(fibs.VppFib(TEST_CONFIG))
        )

    def test_get_health_checker(self):
        kwargs = {"prefix": "69::/69"}
        self.assertEqual(
            type(get_health_checker("opEnRcheCker", kwargs)),
            type(OpenRChecker(kwargs["prefix"])),
        )

    def test_async_main(self) -> None:
        loop = asyncio.get_event_loop()
        with patch("exabgp_to_openr.eto._load_json_config") as mock_config:
            mock_config.return_value = {}
            self.assertEqual(
                69, loop.run_until_complete(async_main("/tmp/conf", False, False))
            )


if __name__ == "__main__":
    unittest.main()
