#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

TEST_CONFIG = {
    "conf_version": "0.0.1",
    "advertise": {
        "interval": 5.0,
        "prefixes": {
            "69::/32": [{"class": "OpenRChecker", "kwargs": {}}],
            "70::/32": [{"class": "OpenRChecker", "kwargs": {}}],
        },
        "withdraw_on_exit": True,
    },
    "learn": {
        "allow_default": True,
        "allow_non_default": False,
        "allow_ll_nexthop": False,
        "fibs": ["openr-prefixmgr", "vpp"],
        "filter_prefixes": [],
        "prefix_limit": 100,
    },
}
