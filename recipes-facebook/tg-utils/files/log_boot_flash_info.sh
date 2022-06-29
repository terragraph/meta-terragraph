#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

logger -s "Boot Flash Info /$(/usr/sbin/testcode | tr '\n' '/' | tr -s ' ')"

# Simplify retrieving the secondary image version from C/C++ applications.
# Note: in the testcode state, the secondary image will be "unknown".
readonly file="/tmp/secondaryImageVersion"
testcode v | grep RELEASE > "${file}" 2>/dev/null
if [ $? -ne 0 ]; then
	echo "unknown" > "{file}"
fi
