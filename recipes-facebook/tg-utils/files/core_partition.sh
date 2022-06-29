#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

### BEGIN INIT INFO
# Provides: Create partition to store cores
# Required-Start:
# Required-Stop:
# Default-Start:   S
# Default-Stop:
# Short-Description: Create partition to store cores
### END INIT INFO


mkdir /var/volatile/cores
mount -t tmpfs -o size=200M,nosuid,nodev,noexec tmpfs /var/volatile/cores
