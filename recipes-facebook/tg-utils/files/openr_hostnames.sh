#!/bin/sh

### BEGIN INIT INFO
# Provides: Link /etc/hosts to /var/run/hosts
# Required-Start:
# Required-Stop:
# Default-Start:   2 3 4 5
# Default-Stop:
# Short-Description: Link /etc/hosts to /var/run/hosts
### END INIT INFO

# read only rootfs.  Copy /etc/hosts.bak to /var/run/hosts
# A symbolic link exists from /etc/sv to /var/run/sv
/bin/cp -a /etc/hosts.bak /var/run/hosts
