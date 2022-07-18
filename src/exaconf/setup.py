#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from setuptools import setup


# Need Open/R libs installed - Disabled by default
ptr_params = {
    "disabled": True,
    "entry_point_module": "exaconf/__init__",
    "test_suite": "exaconf.tests.base",
    "test_suite_timeout": 120,
    "required_coverage": {
        "exaconf/__init__.py": 82,
        "exaconf/bgp.py": 100,
        "exaconf/kvstore.py": 95,
    },
    "run_black": True,
    "run_mypy": True,
}


setup(
    name="exaconf",
    version="20.6.24",
    description=("Take node config and generate BGP configuration"),
    packages=["exaconf", "exaconf.tests"],
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
    tests_require=["ptr"],
    entry_points={"console_scripts": ["exaconf = exaconf:main"]},
    test_suite=ptr_params["test_suite"],
)
