#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


class ThriftObject:
    thrift_spec = None
    thrift_struct_annotations = None

    def __init__(self):
        self.__dict__ = {"addr": "00:00:00:00:00:00", "beam": 0}


def _raiseSysExit(*arg, **kwargs) -> None:
    raise SystemExit()


TIMECONVCMD_OUTPUT2 = """local: 1988-05-09 08:30:00
unix: 579169800
gps: 263205018
bwgd: 10281446015
sf: 164503136250

local: 1988-05-09 08:30:00
unix: 579169800
gps: 263205018
bwgd: 10281446015
sf: 164503136250
"""
