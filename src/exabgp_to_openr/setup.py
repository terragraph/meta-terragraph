#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from setuptools import setup


# Needs Open/R libs installed to test ... So disabled by default
ptr_params = {
    "disabled": True,
    "entry_point_module": "exabgp_to_openr/eto",
    "test_suite": "exabgp_to_openr.tests.base",
    "test_suite_timeout": 120,
    "required_coverage": {
        "exabgp_to_openr/eto.py": 60,
        "exabgp_to_openr/fibs.py": 80,
        "exabgp_to_openr/healthcheck.py": 80,
    },
    "run_black": True,
    "run_mypy": True,
}


setup(
    name="exabgp_to_openr",
    version="20.6.24",
    description=("Take routes from ExaBGP and add to OpenR VPP"),
    packages=["exabgp_to_openr", "exabgp_to_openr.tests"],
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
    install_requires=["click"],
    tests_require=["ptr"],
    entry_points={"console_scripts": ["exabgp-to-openr = exabgp_to_openr.eto:main"]},
    test_suite=ptr_params["test_suite"],
)
