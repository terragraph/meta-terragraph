#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

### BEGIN INIT INFO
# Provides: Data Startup
# Required-Start: $local_if $network
# Required-Stop:
# Default-Start:   2 3 4 5
# Default-Stop:
# Short-Description: Execute startup scripts under /data/startup/ directory upon startup
### END INIT INFO

start() {
    echo "Checking for scripts under /data/startup/"
    if [ -d "/data/startup" ]; then
        for SCRIPT in /data/startup/*
        do
            if [ -f "$SCRIPT" ]; then
                if [ -x "$SCRIPT" ]; then
                    echo "Executing $SCRIPT"
                    $SCRIPT
                else
                    echo "Not an excutable file"
                fi
            fi
        done
    fi
}

case "$1" in
    start)
        start
        ;;
    stop)
	;;
    *)
        echo "Usage: $0 {start}"
        exit 1
esac

exit 0
