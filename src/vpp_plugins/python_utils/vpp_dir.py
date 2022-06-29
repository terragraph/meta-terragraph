#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import vpp_utils
from vpp_papi import VPP


# construct a list of all the json api files
jsonfiles = []
vpp_utils.load_api_json(jsonfiles)
if not jsonfiles:
    print("Error: no json api files found")
    exit(-1)

# use all those files to create vpp.
# Note that there will be no vpp method available before vpp.connect()
vpp = VPP(jsonfiles)
r = vpp.connect("papi-example")

# show vpp version
rv = vpp.api.show_version()
version = rv.version
print(f"VPP version = {version}\n")

print("Available methods:")
for call in dir(vpp.api):
    print(f"\t{call}")

# disconnect from vpp
if vpp.disconnect():
    print("VPP did not disconnect")
    exit(1)
