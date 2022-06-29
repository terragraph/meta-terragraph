# Troubleshooting
This document includes troubleshooting steps and addresses frequently asked
questions related to Terragraph deployments.

<a id="troubleshooting-basic-network"></a>

## Basic Network Debugging
This section describes the workflow for debugging general network issues.

### Checking controller status
Verify that the `e2e_controller` service is active and running without any
critical errors.
* For Docker Swarm installations, use the `docker service ls` and
  `docker service logs` commands to show the service state and logs,
  respectively.
* For legacy systemd installations, use the following commands:
```bash
# Print the systemd service status
$ systemctl status e2e_controller

# Stream the controller logs
$ journalctl -u e2e_controller -f
```

If something appears wrong, verify the controller configuration (see [Deployment
and Installation](Deployment.md)).

### Checking network status
Run the following commands on the controller to find the current status of all
nodes and links in the network:

```bash
# Print topology information, including node/link status
$ tg2 topology ls

# Print node IPv6 addresses and software versions
$ tg2 controller status
```

In a healthy network, the status of all DNs should be `ONLINE_INITITATOR`, and
all CNs should be `ONLINE`. There should be a status report for every node in
the network.

### All nodes are offline
If all nodes are offline, including PoP nodes, check the routing from the PoP
nodes to the controller.
* Check that the PoP node has been configured correctly (see
  [Deployment and Installation](Deployment.md)).
* Verify that firewall rules are not blocking communication. Disabling
  `firewalld` is recommended to ensure no restrictions are in place. Otherwise,
  see [Deployment and Installation](Deployment.md) for a list of ports used by
  the cloud services.

```bash
$ systemctl stop firewalld
$ systemctl disable firewalld

# If NMS is installed on the same host, restart Docker as well to re-add its iptables rules
$ systemctl restart docker
```

### PoP nodes are online, and remaining nodes/links are offline
Check the status of a PoP node with the `tg2 topology ls` command:
* `ONLINE` - The PoP node is unable to ignite the network due to GPS issues.
    * Check the GPS lock on each PoP node using the command below. There should
      be a 3D fix and ideally 16 GPS satellites locked to each Terragraph
      device.
```bash
$ tg2 stats driver-if | grep -e "tgd.gpsStat.fixType" -e "tgd.gpsStat.fixNumSat"
```
* `ONLINE_INITIATOR` - There is likely a routing issue from all other nodes.
    * Check the route on the gateway or controller towards the Terragraph
      network for the Terragraph prefix.
    * Check that BGP is working. Use the command below to print the default
      routes on the PoP node; there should be a default route towards nic2.
```bash
$ ip -6 route
default via fe80::66d1:54ff:feeb:a863 dev nic2 proto zebra metric 1024 pref medium
```

### A specific node is offline
If the controller sees a node as `OFFLINE`, but it can be reached in-band, check
the following items:
* Verify that the node ID (MAC address) in the topology file matches the one on
  the node. This can be printed on the node using the following command:
```bash
$ get_hw_info NODE_ID
```
* Verify that the controller URL on the node is correct:
```bash
$ cat /tmp/mynetworkinfo | grep e2eCtrlUrl
  "e2eCtrlUrl": "tcp://[2001::1]:7007",
```
* Verify that the minion is running and attempting to report to the controller:
```bash
$ tail -f /var/log/e2e_minion/current
I0419 13:12:50.506603  3202 StatusApp.cpp:744] Reporting status to controller
```
* Try to ping the controller from node. If the controller is pingable, check
  if TCP port 7007 is open (as described in an earlier section).
* Verify that the default route (`::/0`) in the routing table points to the
  PoP node. Refer to the "Routing" section for additional troubleshooting steps.
```bash
$ breeze fib list
...
> ::/0
via fe80::250:43ff:fe59:a9c5@nic2
```

### A specific link is offline
If a link is down and is not being ignited, check the following items:
* Verify that "auto-ignition" on the controller is not disabled for the link or
  the entire network.
```bash
$ tg ignition state
14:19:23 [INFO]: IgnitionState(
    ...
    igParams=IgnitionParams(
        enable=True,
        ...
        linkAutoIgnite={}))
```
* If there are continuously increasing *ignition attempts* (as seen in
  `tg2 topology ls`), check the failure reason or look for any error messages in
  the minion or firmware logs. Refer to the
  [Firmware](Troubleshooting.md#troubleshooting-fw) section for additional
  troubleshooting steps.
```bash
# See LinkDownCause in DriverMessage.thrift
$ tg2 stats driver-if | grep "linkDown.cause"
```

### Terragraph unit is unresponsive or not powered

* Verify that the unit has power.
    * Unscrew the power cable gland.
    * Verify that the power connector for the primary sector is securely
      inserted.
    * Use a digital multimeter to verify that the Terragraph power terminal
      block has the correct DC voltage and polarity.
* Power cycle the unit. Remove and reseat the power connector, or disconnect
  power to the power supply, then reconnect it.
* If the problem persists, replace the unit.

### Fiber is connected to Terragraph, but remote switch/router indicates no link integrity

* Verify that the device at the other end of the fiber is powered on and
  configured correctly.
* Verify that the fiber settings at both sides of the connection match, and
  are set as follows:
    * Fiber type: Single or Multi-mode
    * SFPs: Single or Multi-mode
    * Speed: 1Gb or 10Gb
* Verify that the fiber cable polarity is plugged in correctly at both ends.
    * Near side SFP - RX/TX connects to far side SFP - TX/RX.
* Using a fiber cable tester, verify that the fiber running to the Terragraph
  unit has received light power.
* Plug the fiber cable into a known good SFP module.
* Plug the known good SFP module into the Terragraph unit.
* If the problem persists, replace the unit.

### Ethernet is connected to Terragraph, but Ethernet device indicates no link integrity

* Verify that the Ethernet device is powered on and configured correctly.
* If the Ethernet device is powered by PoE, verify that it is connected to the
  unit.
* Test the Ethernet cable from the Terragraph unit on a known good laptop.
* Remove the Ethernet cable from the Terragraph unit and use a cable tester to
  verify the cable integrity.
* If the problem persists, replace the unit.

<a id="troubleshooting-basic-node"></a>

## Basic Node Debugging
This section lists steps for triaging node-level issues, mainly useful during
development or initial hardware bring-up.

### First Steps
The commands below provide a starting point for debugging and are not meant to
be comprehensive.

* **Check the node configuration.** Look for any modifications in the node
  configuration file and understand why they are there.
```bash
$ diff_node_config
$ cat /data/cfg/node_config.json
```
* **Check software/firmware versions.** Are there matching or compatible
  versions running on all nodes?
```bash
$ tg2 version          # show versions for several components
$ cat /etc/tgversion   # show Terragraph build version
$ get_fw_version       # show firmware version number
```
* **Verify hardware information.** In particular, check that MAC addresses are
  as expected (radio MAC addresses may depend on the node configuration field
  `envParams.VPP_USE_EEPROM_MACS`).
```bash
$ cat /tmp/node_info
```
* **Look for core dumps.** If anything crashed, understand why.
```bash
$ ls -al /var/volatile/cores/
$ ls /data/kernel_crashes/vmcore.*
```
* **Use CLIs.** Use CLI commands to check the state of important services.
```bash
# -- e2e_minion --
$ tg2 minion status
$ tg2 minion links
$ tg2 stats driver-if
$ tg2 stats system --dump
$ tg2 tech-support
# -- openr --
$ breeze lm links
$ breeze kvstore adj
$ breeze fib list
$ breeze tech-support
# -- vpp --
$ vppctl show int
$ vppctl show int addr
$ vppctl show ip6 fib
# -- exabgp --
$ exabgpcli show neighbor summary
$ exabgpcli show adj-rib in
$ exabgpcli show adj-rib out
```
* **Check service logs.** Make sure important services are running properly. See
  [Logs](Troubleshooting.md#troubleshooting-logs) for descriptions of log files.
```bash
$ tail -F /var/log/e2e_minion/current
$ tail -F /var/log/vpp/current
$ tail -F /var/log/vpp/vnet.log
...
```
* **Check kernel logs.** Terragraph driver logs can be seen in syslog files or
  `dmesg`.
```bash
$ dmesg -w
$ tail -F /var/log/kern.log
```
* **Check firmware logs.** If possible, enable firmware logs using the node
  configuration field `envParams.FW_LOGGING_ENABLED`. See
  [Firmware](Troubleshooting.md#troubleshooting-fw) for troubleshooting steps
  and instructions on configuring the log verbosity.
```bash
$ tail -f /var/log/wil6210/wil6210_*_fw_*.txt
```

### Manual Link Bring-Up
To establish a single Terragraph link manually using only the E2E minion, follow
the short list of steps below.
1. Generate a new node configuration file using default values.
```bash
$ mv /data/cfg/node_config.json /data/cfg/node_config.json.bak
$ config_get_base /data/cfg/node_config.json
```
2. Only if a GPS fix is unavailable (or there is no GPS module present), disable
   the GPS sync check in firmware on the initiator node.
```bash
$ config_set -i radioParamsBase.fwParams.forceGpsDisable 1
```
3. Reboot the node.
```bash
$ reboot
```
4. Wait for the E2E minion to initialize and then show the radio MAC addresses.
```bash
$ tg2 minion status
```
5. Enable GPS sync.
```bash
$ tg2 minion gps_enable
```
6. Set the wireless channel (e.g. channel 2).
```bash
$ tg2 minion set_params -c 2
```
7. Associate the link from the initiator node using radio MAC addresses above.
```bash
$ tg2 minion assoc -i <initiator_mac> -m <responder_mac> -n dn
```

At this point, verify that all expected components are working:
* `tg2 minion links` shows the link established at the driver/firmware layer.
* `breeze lm links` shows the link established at the routing layer.
* `ping6 ff02::1%terraX` (find "X" from output above) shows "DUP" responses
  confirming link-local connectivity.
* `vppctl show int` shows incrementing "vpp-terraX" interface counters.

<a id="troubleshooting-routing"></a>

## Routing
### Triage
Run the command below to print debug information for an engineer to examine.
```bash
$ breeze tech-support
```

### Background
Every link-state routing protocol incorporates the following steps and
components:
* **Link discovery** - Learn the underlying system's link information,
  including name, status, and addresses.
* **Neighbor discovery** - Discover neighbors on the VLAN of each link.
* **Link-state database** - Record the neighbor and prefix information for each
  node.
* **Route computation** - Use the global link-state database to compute a route to
  each destination.
* **Route programming** - Program routes onto the underlying system.

Routing can fail if a problem occurs in any of these stages. It is always
recommended to troubleshoot issues serially.

### Basic Checks
Verify that Open/R is running *and not crashing*. To do so, run the following
command several times and check that the process ID has not changed:

```bash
$ ps -A | grep openr
```

If applicable, do the same for the `fib_vpp` process.

### Link Discovery
Use the following command to list all links known to Open/R's `LinkMonitor`
module:

```bash
$ breeze lm links

== Node Overload: NO  ==

Interface  Status  Overloaded  Metric Override  ifIndex  Addresses
---------  ------  ----------  ---------------  -------  ------------------------
nic1       Up                                         3  fe80::250:43ff:fe46:dbbb
nic2       Up                                         4  fe80::250:c2ff:fec9:9e9a
```

Any interface on the system must be seen by Open/R before neighbor discovery can
be performed. Look for the following issues:
* Make sure all expected links are listed with the correct status ("Up" or "Down")
  and address.
* If a link status is "Down", check if a lower layer is down (L2 or driver issue).
* If a link is not listed, then the link is not configured on the system (driver
  issue).
* A node or interface should *not* be "overloaded" unless undergoing a drain
  operation. If the "overload" flag is incorrectly set, run the appropriate
  command to unset it:
```bash
$ breeze lm unset-node-overload
$ breeze lm unset-link-overload <interface>
```
* If pinging any of the link-local addresses is unsuccessful, then either a
  problem exists in the underlying driver/lower layer or the other end of the
  link is not ignited correctly.

### Neighbor Discovery
Use the following command to list the adjacencies that have propagated to
Open/R's `KvStore`:

```bash
$ breeze kvstore adj

> node-01.02.03.04.05.06's adjacencies, version: 3, Node  Label: 1064, Overloaded?: False
Neighbor                Local Interface    Remote  Interface      Metric    Weight    Adj Label  NextHop-v4     NextHop-v6                   Uptime
node-00.00.00.10.0e.45  terra0              terra0                     1         1            0   0.0.0.0       fe80::200:ff:fe10:e45        8d1h
node-00.00.00.10.0e.47  nic2                nic2                       1         1            0   0.0.0.0       fe80::250:c2ff:fec9:9c5d     8d1h
```

If an expected neighbor is missing over a working link, then go through the
following steps:
* Verify that link-local multicast packets are received from the neighboring
  node.
```bash
$ tcpdump -i terra0 udp port 6666
tcpdump: verbose output suppressed, use -v or -vv for full protocol decode
listening on terra0, link-type EN10MB (Ethernet), capture size 262144 bytes
15:17:25.350335 IP6 fe80::200:ff:fe10:e45.6666 > ip6-allnodes.6666: UDP, length 189
15:17:26.189291 IP6 fe80::3a3a:21ff:feb0:29d.6666 > ip6-allnodes.6666: UDP, length  189
```
* If the link-local multicast packets are not received, check if the `MULTICAST`
  configuration is set on the interface.
```bash
$ ip link show terra0
12: terra0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP mode DEFAULT group default qlen 1000
    link/ether 00:00:00:10:0b:47 brd ff:ff:ff:ff:ff:ff
```
* If packets are going through but no adjacencies are formed, verify that both
  ends of the link are using the same domain. Open/R will not form adjacencies
  between nodes with different domains. The domain is set in the Open/R
  configuration file ("domain" field in `/var/run/openr_config.json`).

### Key-Value Store (`KvStore`)
`KvStore` provides a data bus for distributed computing, as it enables all nodes
to have the same view of the network.

#### Peers
Every neighbor listed in the adjacencies database should be added as a `KvStore`
peer. Use the following command to list all peers:
```bash
$ breeze kvstore peers

== node-01.02.03.04.05.06's peers  ==

> node-00.00.00.10.0e.45
cmd via tcp://[fe80::200:ff:fe10:e45%terra0]:60002

> node-00.00.00.10.0e.47
cmd via tcp://[fe80::250:c2ff:fec9:9c5d%nic2]:60002
```

If the same neighbor appears over multiple links, it is added only once into
`KvStore`. This could happen when, for instance, two PoP sites have a wireless
link between them along with a wired link (required for PoP nodes). Open/R will
typically sync on the wired link, and the wireless link will never be used for
any traffic. A Terragraph watchdog monitors all wireless links to check if any
data is flowing, and will restart the E2E minion if no data is passed for a few
minutes. **This should be prevented in Terragraph by not allowing wireless links
between PoP sites.**

#### Consistency
In steady state and under stable network conditions, all nodes should report the
same keys and hashes for each. This can be seen using the following command:
```bash
$ breeze kvstore keys
```

### Link-State Database (`Lsdb`)
Every node in the network contributes two major pieces of information to
`KvStore` with the following key format:
* `adj:<node-name>` - The node's adjacency information.
* `prefix:<node-name>` - The network prefix owned/proxied by the node.

Use the following command to dump all keys:
```bash
$ breeze kvstore keys

== Available keys in KvStore  ==

Key                            OriginatorId              Version  Hash
-----------------------------  ----------------------  ---------  ----------
adj:node-00.00.00.10.0e.42     node-00.00.00.10.0e.42          4  3845919268
allocprefix:188                node-00.00.00.10.0e.42          1  3527252845
prefix:node-00.00.00.10.0e.42  node-00.00.00.10.0e.42          9  520548462
...
```

Verify that the `prefix` and `adj` keys are present for all nodes. Otherwise,
follow these steps to troubleshoot:
* If prefixes are missing, check that the Open/R prefix allocator is enabled
  (i.e. either `OPENR_ALLOC_PREFIX` or `OPENR_STATIC_PREFIX_ALLOC` is set in the
  configuration file). The following command lists the prefixes advertised by
  all nodes:
```bash
$ breeze kvstore prefixes --nodes all
```

### Route Computation
Open/R's `Decision` module consumes the global link-state database from
`KvStore` and, on each node, computes the best path to all other prefixes and
generates routing information. Along with the best paths, Loop-Free Alternates
(LFAs) are also programmed.

Use the following command to request the routing table from `Decision` module:
```bash
$ breeze decision routes

== Routes for node-00.00.00.10.0b.47  ==

> 2001:a:b:c::/64
via fe80::200:ff:fe10:b4c@terra0 metric 2

...

> ::/0
via fe80::250:43ff:fe59:a9c5@nic2 metric 4
via fe80::250:43ff:fef2:130a@nic2 metric 5
```

When troubleshooting routes to a particular prefix, verify that a route has been
computed and that the metric values are correct for the best next-hops (lower is
more preferable).

### Route Programming
Routes computed by the `Decision` module are then programmed by the `Fib` agent
(ex. `fib_vpp`) via Thrift APIs.

Use the following command to print all routes on the `Fib` agent:
```bash
$ breeze fib list

== node-00.00.00.10.0b.47's FIB routes by client 786  ==

> 2001:a:b:c::/64
via fe80::200:ff:fe10:b4c@terra0

...

> ::/0
via fe80::250:43ff:fe59:a9c5@nic2
```

When troubleshooting routes to a particular prefix, verify that the intended
next-hop is programmed. Otherwise, follow these steps to troubleshoot:
* If a route is missing but was reported by the `Decision` module, then it is
  a sync error that may be resolved by restarting the `openr` service.
* If applicable, check that routes are programmed correctly into the VPP FIB
  by running the command below.
```bash
$ vppctl show ip6 fib
```

<a id="troubleshooting-logs"></a>

## Logs
This section describes important logs generated on Terragraph nodes.

### Log Descriptions
The table below provides a non-exhaustive list of log files.

Note that the logs in `/var` are archived to flash on each reboot and also
periodically, so a longer continuous log history is often available.

| Log                                   | Description |
| ------------------------------------- | ----------- |
| `/data/log/reboot_history`            | Time and reason for each reboot. These are rotated (older file is suffixed ".2"). |
| `/data/format_data.log`               | Contains the timestamp when the /data partition required re-formatting and re-initialization. The reformat attempts to preserve some configuration, but not logs. |
| `/var/log/e2e_minion/current`         | E2E minion logs. |
| `/var/log/e2e_minion/process_history` | E2E minion startup history, created the first time that the minion restarts. |
| `/var/log/e2e_controller/current`     | E2E controller logs (when running the controller on a TG node). |
| `/var/log/stats_agent/current`        | Stats agent logs. |
| `/var/log/fluent-bit/current`         | Fluent Bit (log daemon) logs. |
| `/var/log/openr/current`              | Open/R (routing daemon) logs. |
| `/var/log/fib_vpp/current`            | Open/R VPP FIB agent logs. |
| `/var/log/exabgp/current`             | ExaBGP (BGP daemon) logs. |
| `/var/log/gpsd/current`               | gpsd (GPS daemon) logs. |
| `/var/log/vpp/current`                | VPP startup logs. |
| `/var/log/vpp/vnet.log`               | VPP and DPDK logs, including `wil6210` and `dpaa2` Poll Mode Driver (PMD) logs. |
| `/var/log/vpp-debug.log`              | VPP CLI (`vppctl`) command history. |
| `/var/log/vpp_chaperone/current`      | VPP configuration service logs. |
| `/var/log/wil6210/`                   | Directory containing firmware and microcode logs when using the node configuration field `envParams.FW_LOGGING_ENABLED`. |
| `/var/log/kern.log`                   | Kernel and TG driver logs. |
| `/var/log/dmesg`                      | Recent items in `kern.log`, also accessible via the `dmesg` command. |
| Linux crash (panic) logs              | Panic logs and kernel context are saved in a dedicated flash partition. On Puma, the kernel message log is saved to `/data/kernel_crashes/vmcore.<date>`. Note that the node will reboot within 3 minutes of a kernel crash. |

If the software watchdog is enabled, the following logs will also be generated:

| Log                                   | Description |
| ------------------------------------- | ----------- |
| `/var/log/wdog_repair_history`        | Log of every watchdog-initiated repair since the last reboot. These are rotated (older file is suffixed ".2"). |
| `/var/log/openr/openr_debug.log`      | Periodic traceroute logs collected by the watchdog when no PoP node was reachable. The log also shows the times when PoP nodes were unknown. This log is rotated, and the older version has a suffix. |

### Automatically-Generated Logs
Certain log files are collected continuously on the temporary filesystem in the
`/var/log/` directory. These include logs for Terragraph software as well as
system state. The files in this directory are archived and rotated to the flash
in `/data/log/logs.x.tar.gz` (with x=1 being the latest), both periodically and
before reboots. Note that some less frequently updated logs only exist on the
flash within `/data/log/` or `/data/`.

### Logs Generated Manually
An extensive system dump archive can be created manually in
`/tmp/sysdump-<date>.tgz` using the `sys_dump` command.

<a id="troubleshooting-fw"></a>

## Firmware
This section contains frequently asked questions relating to firmware issues.

### What are the signs of firmware crashes?
* The node has no RF links.
* A radio displays unknown status:
```bash
$ tg2 minion status

Radio MAC           Status           GPS Sync
------------------  ---------------  ---------
00:00:00:10:0b:47   N/A (crashed?)   false
```
* There are firmware core dumps present.
```bash
$ ls -al /var/volatile/cores/wil6210_fw_core_*
```
* The E2E minion logs indicate that a Wigig device is down:
```bash
$ tail -f /var/log/e2e_minion/current
I0305 00:06:12.570667  3342 StatusApp.cpp:1196] <00:00:00:10:0b:47> Device status: DOWN
```

### What are the possible causes of firmware crashes?
Firmware-related error messages are printed out by the wil6210 Poll Mode Driver
(PMD) in VPP logs which are found in `/var/log/vpp/vnet.log`. Causes include the
following:
* The initialization of the firmware failed. The VPP logs show messages such as:
```
Firmware not ready after x ms
```
* A firmware assert occurred. Assert codes can be used to identify specific
  firmware errors. The VPP logs show messages such as:
```
Firmware error detected, assert codes FW <hex fw assert code>, UCODE <hex ucode assert code>
```
* The node is not configured as an RF node. This fundamental setting is
  controlled by a variable read from EEPROM, `tg_if2if`, which must be set to 0
  for RF nodes.
```bash
# Check if configured as an IF node
$ get_hw_info TG_IF2IF
1
# Switch between RF/IF node
$ db_eeprom=$(fdtget /sys/firmware/fdt /chosen eeprom)
$ fdtput -p "$db_eeprom" -t i /board tg-if2if <0|1>
```

### What are signs that a node has rebooted unexpectedly?
* Unexpected reboots are logged in the file `/data/log/reboot_history`. If the
  "dirty" tag appears *by itself* on any line, it indicates an abnormal reboot
  which was not initiated by the "reboot" command or the watchdog.
```bash
$ cat /data/log/reboot_history
up 1523315537 Mon, 09 Apr 2018 16:12:17 -0700 dirty
```
* Linux crashes will create logs in a dedicated flash partition. On Puma, the
  kernel message log is saved to `/data/kernel_crashes/vmcore.<date>`.

### Why can't a wireless link be established?
* Verify that at least one end of the link is in the `ONLINE_INITIATOR` state.
  Typically, it should take less than a minute for a sector to transition from
  `ONLINE` to `ONLINE_INITIATOR`. If the node remains only `ONLINE`, then it
  has not acquired GPS timing and cannot initiate a wireless link. This can be
  verified via the firmware stat `tgf.<mac_addr>.tsf.syncModeGps`, which will
  be 0 (instead of 1).
    * There are potential issues if the following firmware stats are increasing:
        * `tgf.<mac_addr>.gps.numMissedSec`, `tgf.<mac_addr>.gps.numPpsErr`
    * Check the following stats to see why the GPS chip may not be able to
      estimate time (and position):
        * `tgd.gpsStat.fixNumSat` - Must be at least 1, or at least 4 if the
          site location is missing from the topology.
    * Check the list of all satellites visible to the GPS chip:
        * `tgd.gpsStat.<id>.used` - Whether this satellite is in use.
        * `tgd.gpsStat.<id>.snr` - Usable satellites would typically have SNR
          greater than 25.
* Verify that beamforming (BF) messages (Tx/Rx) are being successfully
  exchanged using the commands below. On the initiator node, the
  `tgf.MAC.mgmtTx.bfTrainingReq` and `tgf.MAC.mgmtRx.bfTrainingRsp` stats
  should be non-zero. On the responder node, the `tgf.MAC.mgmtRx.bfTrainingReq`
  and `tgf.MAC.mgmtTx.bfTrainingRsp` stats should be non-zero. If this is not
  the case, then check for misalignment between the nodes.
```bash
$ tg2 stats driver-if | grep -e "bfTrainingReq" -e "bfTrainingRsp"
```
* The node configuration for over-the-air (OTA) encryption should match on
  both ends of the link. Verify that the `wsecEnable` config field is the same
  on both nodes, as well as 802.1X parameters if applicable.
* Verify that the interface stats for "RX packets" and "TX packets" are
  increasing, which indicates that the link is active.
```bash
$ ifconfig terra0
terra0 Link encap:Ethernet HWaddr 02:08:02:00:01:00
inet6 addr: fe80::8:2ff:fe00:100/64 Scope:Link
 UP BROADCAST RUNNING MULTICAST MTU:7800 Metric:1
RX packets:7260 errors:0 dropped:0 overruns:0 frame:0
 TX packets:7264 errors:0 dropped:0 overruns:0 carrier:0
collisions:0 txqueuelen:1000
 RX bytes:1429692 (1.3 MiB) TX bytes:1428796 (1.3 MiB)
```
* Initiate a ping on both nodes, either using the other end's IPv6 address as
  the destination or doing a multicast ping on the link's interface. If this
  ping succeeds, then the link is active.
```bash
# Ping the destination IPv6 address
$ ping6 fe80::8:2ff:fe00:103%terra0
# Ping the multicast address
$ ping6 ff02::1%terra0
```

### Why is throughput lower than expected?
* Check the firmware stats for PER (`tgf.<mac_addr>.staPkt.perE6`) and MCS
  (`tgf.<mac_addr>.staPkt.mcs`). The PER should be close to 0%. The MCS should
  be greater than 9 for high throughput.
```bash
$ tg2 stats driver-if | grep -e "staPkt.mcs" -e "staPkt.perE6"
```
* Check the CPU usage on the Linux host and the firmware. The host CPU usage is
  tracked via the system stat `cpu.util`. The firmware CPU usage can be viewed
  via the stat `tgf.<mac_addr>.miscSys.cpuLoadAvg`, which captures the *idle*
  CPU as a percentage; if this stat drops to 3 or less, then the firmware is
  overloaded.
```bash
$ tg2 stats driver-if | grep "cpuLoadAvg"
```
* Make sure that firmware info logs are disabled. Running the command below
  will enable only error logging:
```bash
$ tg2 minion fw_set_log_config -l error
```
* Check that firmware is not in BF responder mode, which reserves the Rx BF
  slots and reduces maximum throughput by roughly 50%. If a sector is in
  responder mode, the following firmware stats will indicate the Rx BF slot
  count incrementing while the Tx BF slot count remains constant.
```bash
$ tg2 stats driver-if | grep -e "slot.numOfRxBfSlotsPgmrd" -e "slot.numOfTxBfSlotsPgmrd"
```

### Change log levels of firmware modules
* Show all supported firmware modules and logging levels:
```bash
$ tg2 minion fw_set_log_config --help
```
* Change the logging level:
```bash
# set logging level to 'info' for all fw modules
$ tg2 minion fw_set_log_config -l info
# set logging level to 'debug' for a specific node and specific fw modules
$ tg2 minion fw_set_log_config -m la_tpc -m framer -m tpc -l debug
```

<a id="troubleshooting-watchdog"></a>

## Watchdog
The Terragraph watchdog is a collection of monitors that observes different
aspects of a node's health. These monitors perform various repair actions,
including reboots when necessary.

### Watchdog Logs
All watchdog actions are timestamped and logged to the following locations:
* `/var/log/wdog_repair_history` - Log of every watchdog-initiated repair
  since the last reboot.
* `/data/log/reboot_history` - Reason for each reboot, including reboots that
  were not initiated by the watchdog.

Both of the above logs are rotated, and the older rotations are renamed with a
numeric suffix. Copies of `wdog_repair_history` are archived to flash in
`/data/log/` at least once before rebooting.

### Watchdog Fault Tables
* The tables below list all detected faults.
* The `/var/log/` files mentioned in the fault tables are also archived to flash
in `/data/log/` on reboot.

| Fault Name | Description | Repair Action | Keyword in repair/reboot history | Fault-specific logs and comments |
| ---------- | ----------- | ------------- | -------------------------------- | -------------------------------- |
| POP | No PoP node was reachable for 1 hour | Reboot | pop_unreachable | <ul><li>Traceroute logs are saved in `/var/log/openr` every few minutes when PoP is unreachable.</li> <li>The log also indicates the times when the PoP nodes were unknown.</li> <li>Use the `get_pop_ip` command to find the PoP nodes currently known to the unit.</li></ul> |
| UPG | On first boot of an upgrade image, E2E minion was unable to connect to the controller for several minutes | Revert to previous TG image and reboot | testcode-timed-out | n/a |
| LINK | No RF link formed for some minutes (default 15) on a baseband card that was healthy at least once since the last e2e_minion startup. | Reload FW on all baseband cards and restart E2E minion | prog-[macAddress]-nolink | <ul><li>The reported mac address is the first baseband card on which the fault was detected.</li><li>The actual timeout is a determined by the `radioParamsBase.fwParams.noLinkTimeout` config parameter.</li><li>`/var/log/fwdumps/RF/` (rotated log archives)</li></ul> |
| FW | Firmware has crashed, or the datapath from FW to Linux is dead on a baseband card that was healthy at least once since the last e2e_minion startup. | Reload FW on all baseband cards and restart E2E minion | prog-[macAddress]-timeout | <ul><li>The reported mac address is the first baseband card on which the fault was detected.</li> <li>`/var/log/fwdumps/RF/` (rotated log archives)</li></ul> |
| BBINIT | None of the baseband cards was initialized with a mac address for 1 minute after the last e2e_minion restart, or internal error. | Reload FW on all baseband cards and restart E2E minion | prog-init | `/var/log/fwdumps/RF/` (rotated log archives) |
| PEER | Some RF links are up, but none reached its peer for 1 minute | <ul><li>Reload FW on all baseband cards and restart E2E minion</li></ul> | link_monit.sh | `/var/log/fwdumps/RF/` (rotated log archives) |
| E2E | Failed to restart E2E minion | Reboot | e2e_minion_restart_failed_prog, e2e_minion_restart_failed_link | <ul><li>Restarting e2e minion also reloads the FW, reloads drivers, and initializes the FW.</li> <li>The suffix (`_prog` and `_link`) identifies the specific health monitor that failed to restart e2e minion.</li></ul> |
| GPS | GPS is in a bad or unlocked state continuously for 30 minutes (the timeout persists across reboots). | <ul><li>Log the fault without repairing it</li></ul> | gps | n/a |
| CFG1 | A unit running with an new/unverified config did not connect to the controller for about 7 minutes. | Reboot and fall back to the previous, working configuration | config-fallback-timed-out | n/a |
| CFG2 | A unit with a config change since startup, rebooted before connecting to the controller, and also before the deadline to do so expired. | Start up with the unverified new config  | config-unverified-new-config | Some config changes only take effect after a re-start, so we allow one re-start with an unverified new config. |
| CFG3 | A unit that booted up with an unverified new config, rebooted before connecting to the controller, and also before the deadline to do so expired.| Reboot and fall back to the previous, working configuration | config-fallback | n/a |
| DATA | /data flash partition is full | Reboot and clean up /data | fsys-data-full | <ul><li>The reboot history may be truncated.</li> <li>When `/data` is completely full, the "fsys-data-full" reboot reason may not show up in the reboot history.</li><li>Only the `/data/log` directory is cleaned up.</li></ul>  |
| TMP | /tmp is full | Reboot and also clean up /data if necessary | fsys-tmp-full | n/a |
| VPP | The vpp cli (`vppctl`) has deadlocked | Restart vpp | vpp_cli | n/a |

### Disabling the Watchdog
* Some watchdogs can be temporarily disabled during troubleshooting to avoid
  undesirable repair actions and reboots.
* The maximum watchdog disable period is one day.
* The watchdogs that monitor the filesystem, revert upgrades, and revert config
  cannot be disabled.

The watchdog CLI provides the following commands for disabling watchdogs:

```bash
# Disable all non-critical watchdogs for 45 minutes (max 1440 minutes, or 1 day)
/etc/init.d/watchdog.sh dis 45

# Re-enable all watchdogs
/etc/init.d/watchdog.sh en
```

<a id="troubleshooting-upgrades"></a>

## Manual Upgrades
Software upgrades on Terragraph nodes should normally be performed using the NMS
UI or TG CLI (see [Maintenance and Configuration](Maintenance.md)). If this is
not possible, then manual upgrades can be performed **but should only be used as
a last resort**. The Terragraph upgrade image, `tg-update-qoriq.bin`,
is a self-extracting binary file.

### Manual Upgrade Steps
1. Sanity check the release/version of the TG upgrade binary. This can be done
   in a Bash shell before copying the image to the node, or afterwards on the
   node (following step 3).
```bash
$ ./tg-update-qoriq.bin -m
{
"version":"Facebook Terragraph Release RELEASE_M70-0-g1ad294cc0 michaelcallahan@devvm4933 2021-03-05T17:21:02",
"md5":"d0149fe85367780989ea642fe0bd480b",
"model":"NXP TG Board",
"hardwareBoardIds":["NXP_LS1048A_PUMA"]
}
```
2. Temporarily disable watchdogs to ensure that the node will not reboot during
   the upgrade.
```bash
# Disable watchdogs for 20 minutes
$ /etc/init.d/watchdog.sh dis 20
```
3. Copy the upgrade image to the `/tmp/` directory on the node, replacing the
   IP address in the command below with the correct IP. Check that the
   executable permission was preserved (SCP should do this automatically).
```bash
$ scp tg-update-qoriq.bin root@[2001::1]:/tmp/
```
4. Write the new image to the flash, then boot it up.
```bash
$ /tmp/tg-update-qoriq.bin -wrt
```
5. Log back into the node after it has rebooted, and sanity check the new image
   version.
```bash
$ cat /etc/tgversion
Facebook Terragraph Release RELEASE_M70-0-g1ad294cc0 michaelcallahan@devvm4933 2021-03-05T17:21:02
```
6. Finalize the upgrade. Step 4 boots the node into a "test" state in which a
   reboot for any reason causes an automatic fallback to the old image;
   finalization cancels this "test" state.
```bash
# This command also prints info about the flash partitions,
# and it is safe to use when there is no new image to finalize.
$ testcode c
```

**Note:** If the new image is not finalized within 6 minutes, *and* the E2E
minion is enabled, *and* it is unable to connect to the E2E controller, then
the watchdog will trigger a reboot and revert to the previous image. The
"test" state and the reversion are safety features that prevent bricking
the node in all stages of the upgrade process. The "test" upgrade state can be
skipped if absolutely necessary by issuing a different command in Step 4:

```bash
$ /tmp/tg-update-qoriq.bin -ur
```

<a id="troubleshooting-reverts"></a>

## Manual Reverts
It is sometimes convenient to manually revert to the inactive/secondary boot
image. This can only be done when the unit is not in the middle of an upgrade,
and not in the "test" state described in the "Manual Upgrades" section above.

1. Sanity check the TG version of the secondary boot image. Make sure that it
is what you expect.
```bash
$ testcode v
TG version in secondary boot partition (/dev/mmcblk0p2)
Facebook Terragraph Release RELEASE_M70-0-g1ad294cc0 michaelcallahan@devvm4933 2021-03-05T17:21:02
```

2. Swap the roles of the primary and secondary boot partitions.
```bash
$ testcode x
Swapping the roles of the primary (/dev/mmcblk0p1) and
the secondary (/dev/mmcblk0p2) boot partitions.
Done
Reboot now to activate the new primary image!
```

3. Reboot to activate the newly designated primary image.
```bash
$ reboot
```

**Note:** You can undo the revert request between Steps 2 and 3 as follows:
```bash
$ testcode c
```
