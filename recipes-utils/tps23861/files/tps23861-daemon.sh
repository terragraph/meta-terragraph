#! /bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

### BEGIN INIT INFO
# Provides:        tps23861
# Required-Start:
# Required-Stop:
# Default-Start:   2 3 4 5
# Default-Stop:
# Short-Description: Start tps23861-daemon
### END INIT INFO

PATH=/sbin:/bin:/usr/bin:/usr/sbin

DAEMON=/usr/sbin/tps23861-daemon
DAEMON_ARGS="-D -c /data/etc/tps23861.conf -c /etc/tps23861.conf"

test -x $DAEMON || exit 0

# Source function library.
. /etc/init.d/functions

startdaemon(){
	echo -n "Starting POE Manager Daemon: "
	start-stop-daemon --start --quiet --oknodo --startas $DAEMON -- $DAEMON_ARGS
	echo "done"
}
stopdaemon(){
	echo -n "Stopping POE Manager Daemon: "
	start-stop-daemon --stop --quiet --oknodo -x $DAEMON
	echo "done"
}

case "$1" in
  start)
	startdaemon
	;;
  stop)
  	stopdaemon
	;;
  restart)
	stopdaemon
	startdaemon
	;;
  reload)
	stopdaemon
	startdaemon
	;;
  status)
	status $DAEMON
	exit $?
	;;
  *)
	echo "Usage: tps23861-daemon { start | stop | status | restart | reload }" >&2
	exit 1
	;;
esac

exit 0
