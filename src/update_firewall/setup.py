#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from setuptools import setup


# Specific Python Test Runner (ptr) params for Unit Testing Enforcement
ptr_params = {
    "entry_point_module": "update_firewall",
    "test_suite": "update_firewall_tests",
    "test_suite_timeout": 150,
    "required_coverage": {"update_firewall.py": 70},
    "run_black": True,
    "run_mypy": True,
}


setup(
    name="update_firewall",
    version="20.2.10",
    description=("Generate IPTables rules"),
    py_modules=["update_firewall", "update_firewall_tests"],
    url="http://github.com/facebook/terragraph",
    author="Cooper Lees",
    author_email="cooper@fb.com",
    classifiers=(
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3 :: Only",
        "Programming Language :: Python :: 3.7",
        "Development Status :: 3 - Alpha",
    ),
    python_requires=">=3.7",
    install_requires=["python-iptables"],
    entry_points={"console_scripts": ["update_firewall = update_firewall:main"]},
    test_suite=ptr_params["test_suite"],
)
