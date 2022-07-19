#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from setuptools import setup


# Specific Python Test Runner (ptr) params for Unit Testing Enforcement
ptr_params = {
    "entry_point_module": "r2d2.r2d2_commands",
    "test_suite": "r2d2.tests.r2d2_commands_test",
    "test_suite_timeout": 30,
    "required_coverage": {
        "r2d2/help.py": 20,
        "r2d2/r2d2.py": 60,
        "r2d2/r2d2_commands.py": 15,
        "TOTAL": 35,
    },
    "run_black": True,
    "run_flake8": False,  # Turn on once we fix 'self' issue
    "run_mypy": False,
}

setup(
    name="r2d2",
    version="2019.8.22",
    packages=["r2d2"],
    install_requires=["click", "pyzmq"],
    entry_points={"console_scripts": ["r2d2 = r2d2.r2d2:r2d2"]},
)
