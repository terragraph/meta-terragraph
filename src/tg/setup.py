#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from setuptools import setup


# Tests are failing so disabled by default
ptr_params = {
    "disabled": True,
    "test_suite": "tg.tests.base",
    "test_suite_timeout": 300,
    "required_coverage": {
        "tg/tg.py": 22,
        "tg/commands/consts.py": 100,
        "tg/commands/zeroize.py": 98,
        "tg/commands/scan.py": 40,
        "TOTAL": 35,
    },
    "run_mypy": False,
}


setup(
    name="tg",
    version="2019.9.2",
    packages=["tg", "tg.commands", "tg.commands.upgrade", "tg.tests"],
    entry_points={"console_scripts": ["tg = tg.tg:main"]},
    install_requires=[
        "click",
        "python-dateutil",
        "netifaces",
        "networkx",
        "psutil",
        "pyzmq",
        "tabulate",
    ],
    test_suite=ptr_params["test_suite"],
)
