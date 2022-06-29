# systemd scripts for x86 controller box
This directory contains the `systemd` scripts for running services related to
the E2E controller and NMS aggregator.

## Installation
Installation involves creating a `tg_services` configuration file and enabling
the `*.service` scripts in `systemctl`. The file paths will differ based on the
host OS.

### tg_services
The `tg_services` file defines important service configuration details, such as
the rootfs location and command-line arguments for each service. This file
should be created in `/etc/sysconfig/tg_services` for CentOS, and in
`/etc/default/tg_services` for Debian/Ubuntu.

Sample file contents:
```
E2E_ROOTFS="/root/rootfs"
NMS_ROOTFS="/root/rootfs"
AGENT_ROOTFS="/root/rootfs"
API_ROOTFS="/root/rootfs"
DATA_DIR="/root/data"
E2E_TOPOLOGY_FILE="/e2e_topology.conf"
E2E_CONFIG_FILE="/cfg/controller_config.json"
NMS_CONFIG_FILE="/cfg/aggregator_config.json"
API_ARGS="-http_port 8080"
OPENTRACKER_ARGS="-i fc00:cafe:babe:ffff::1"
```

Variable descriptions:
* `E2E_ROOTFS`, `NMS_ROOTFS`, `AGENT_ROOTFS`, `API_ROOTFS`: The location of the
  rootfs on disk where e2e_controller, nms_aggregator, stats_agent, and
  api_service will be executed from, respectively.
* `DATA_DIR`: The data directory that will be bind mounted into the rootfs as
  /data (for e2e_controller and nms_aggregator).
* `E2E_TOPOLOGY_FILE`, `E2E_CONFIG_FILE`, `NMS_CONFIG_FILE`: The relative file
  paths of the E2E topology and E2E/NMS config files inside of DATA_DIR.
* `API_ARGS`, `OPENTRACKER_ARGS`: Additional command-line arguments to be
  passed to api_service and opentracker, respectively.
* `AGGR_HOST`: The Hostname or IP address of the NMS aggregator, used by
  stats_agent. Leave unset if both services are running on the same machine.
* `AGENT_MAC`: The MAC address with which stats_agent will publish stats and
  events. Defaults to *0:0:0:0:0:0*.

### systemd
To enable the services, the `*.service` files must first be copied into the
`systemd` directory, which is `/usr/lib/systemd/system/` for CentOS and
`/lib/systemd/system/` for Debian/Ubuntu. Afterwards, running `systemctl enable`
will install the services.

Example commands for CentOS:
```
$ cd /path/to/rootfs
$ cp etc/tg_systemd_config/*.service /usr/lib/systemd/system/
$ for f in etc/tg_systemd_config/*.service; do systemctl enable $(basename $f); done
```

## Running
### Managing services
Use `systemctl` commands `start`, `stop`, `restart`, etc. to manage services.
```
$ systemctl start e2e_controller.service
$ systemctl start nms_aggregator.service
$ systemctl start stats_agent.service
$ systemctl start opentracker.service
$ systemctl start api_service.service
```

### Viewing logs
Use `journalctl` to view the output from a service.
```
$ journalctl -u e2e_controller -f
```
