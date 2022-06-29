#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from sys import version_info

from setuptools import setup


assert version_info >= (3, 5, 0), "hbhssh requires >= Python 3.5"
# Specific Python Test Runner (ptr) params for Unit Testing Enforcement
ptr_params = {
    "entry_point_module": "hbhssh",
    "test_suite": "hbhssh_test",
    "test_suite_timeout": 300,
    "required_coverage": {"hbhssh.py": 75},
    "run_black": True,
    "run_mypy": True,  # TODO: Add PEP484 type annotations
}


setup(
    name=ptr_params["entry_point_module"],
    version="19.6.26",
    description=(
        "Take a TG Topology and generate a SSH client config to "
        + "ssh hop by hop over IPv6 Link Local"
    ),
    py_modules=ptr_params["entry_point_module"],
    url="http://github.com/facebook/terragraph",
    author="Alex Landau",
    author_email="alandau@fb.com",
    classifiers=(
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3 :: Only",
        "Programming Language :: Python :: 3.5",
        "Programming Language :: Python :: 3.6",
        "Development Status :: 3 - Alpha",
    ),
    python_requires=">=3.5",
    install_requires=["networkx"],
    entry_points={"console_scripts": ["hbhssh = hbhssh:main"]},
    test_suite=ptr_params["test_suite"],
)
