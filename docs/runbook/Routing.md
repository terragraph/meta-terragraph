# Routing and Traffic Engineering
This document describes Terragraph's routing and traffic engineering features.

<a id="routing-routing"></a>

## Routing
Routing in Terragraph involves two main pieces of software: Open/R and a
platform agent (`fib_nss`, `fib_vpp`, or `platform_linux`). Open/R performs all
routing and runs a distributed key-value store to implement a link-state routing
protocol. The platform agent is the entity that configures the forwarding table
received from Open/R in Linux or hardware using a platform-specific API.

### Open/R
Open/R computes routes by building a graph using the adjacency and prefix
information in the distributed key-value store. It runs a weighted
shortest-paths algorithm to all other nodes, and uses Equal-Cost Multi-Path
routing (ECMP) when multiple "best paths" exist to distribute traffic along each
path. Open/R then uses the best paths to determine the next hop for each
destination, and provides this information to the platform agent to program into
the hardware; this determines how data gets forwarded between interfaces on the
node.

Open/R is upgraded between the M78, M79, and M80 Terragraph releases. A number
of backwards compatibility changes are included in Terragraph's version of
Open/R. In particular, the `Spark`, `KvStore`, and `PrefixManager` modules are
made backwards compatible. If pre-M80 backwards compatibility is not needed,
compatibility features can be disabled in node configuration by enabling
`envParams.OPENR_ENABLE_SPARK2` and disabling
`envParams.OPENR_ENABLE_DEPRECATED_PREFIXES`.

### Platform Agent
Open/R uses the link-local IPv6 address and interface name as the next hop for
forwarding purposes. It is the job of the platform agent to convert this
link-local IPv6 address to a MAC address for forwarding.

#### Linux
Open/R provides a Linux-based platform agent called `platform_linux`. When Linux
is used as the forwarding agent, all the routes are programmed into the Linux
routing table. Linux performs neighbor discovery and maps the next hop IPv6
addresses to MAC addresses.

#### VPP
Terragraph's Puma hardware uses the Data Plane Development Kit ([DPDK]) along
with the Vector Packet Processing ([VPP]) framework for packet forwarding.
Terragraph provides a software agent, `fib_vpp`, which programs routes learned
via Open/R into VPP's Forwarding Information Base (FIB).

<a id="routing-te"></a>

## Traffic Engineering
Basic traffic engineering controls are defined in the node configuration, and
are described in the following sections. See
[Maintenance and Configuration](Maintenance.md#maintenance-config) for
instructions on editing the node configuration.

### MCS-Based Routing
Terragraph can be configured to prefer high data-rate wireless links over low
data-rate links. In Open/R, each link (wired or wireless) has an associated
routing cost, or "metric", which is 1 by default. For low data-rate wireless
links, this cost is increased to make the link less preferable in routing
computations when considering any alternate routes. The default mapping of
data-rate (MCS) to link cost (metric) is shown in the table below:

| MCS  | Metric |
| ---- | ------ |
| 2-3  | 15     |
| 4-8  | 3      |
| 9-12 | 1      |

Note that Terragraph hardware does not support data on an MCS lower than 2. The
number of MCS buckets and the mapping of each MCS to metric is configurable on
each node through the `openrParams.linkMetricConfig` config field.

### Soft Draining Links
Individual links can be "soft drained" using the
`linkParamsOverride.<macAddr>.openrLinkParams.softDisable` config field. A
soft-drained link simply has a very high link metric (100000), and will be
avoided unless no other paths are possible.

### Fixed Link Metrics
Similar to soft draining, links can have fixed metrics assigned to them. This
setting is defined in the node configuration at
`linkParamsOverride.<macAddr>.openrLinkParams.fixedMetric`.

<a id="routing-prefix-alloc"></a>

## Prefix Allocation
This section describes prefix allocation procedures for Terragraph nodes and
their CPE interfaces.

<a id="routing-node-prefix-alloc"></a>

### Node Prefix Allocation
Terragraph has three different schemes that define how prefixes are allocated
for each Terragraph node:
* *Distributed* - Distributed prefix allocation is completely handled within
  Open/R. Under this scheme, nodes pick random prefixes from the allowed range
  and advertise them to the network. The algorithm attempts to resolve any
  collisions, but is not always successful in practice; this can result in
  duplicated prefixes or unreachable nodes.
* *Centralized (default)* - Centralized prefix allocation is handled by the E2E
  controller. The controller performs all prefix allocations, which prevents
  collisions and enables more sophisticated allocation algorithms.
* *Deterministic* - Deterministic prefix allocation is also handled by the E2E
  controller. The controller assigns prefixes to nodes based on the network
  topology to allow POP nodes to take advantage of route summarization and help
  load balance ingress traffic.

Terragraph uses *centralized prefix allocation* by default.

Both centralized and deterministic prefix allocation require initial parameters
to be provided through the `thrift::PrefixAllocParams` structure in the
controller configuration file:

```c
struct PrefixAllocParams {
  1: string seedPrefix;
  2: i32 allocPrefixLen;
}
```

An example controller configuration with centralized prefix allocation enabled
is shown below:

```json
{
  "flags": {
    "enable_centralized_prefix_alloc": "true",
    "enable_deterministic_prefix_alloc": "false"
  },
  "prefixAllocParams": {
    "seedPrefix": "face:b00c:babe::/48",
    "allocPrefixLen": 64
  }
}
```

Without the proper configuration, the controller will automatically revert to
distributed prefix allocation to allow the network to come up.

<a id="routing-cpe-prefix-alloc"></a>

### CPE Prefix Allocation
Terragraph nodes will allocate an IPv6 prefix for each CPE interface if a
specific CPE interface prefix (`cpeConfig.TenGigabitEthernetX.prefix`) is not
configured. Due to SLAAC being enabled on CPE interfaces automatically, the
IPv6 prefix assigned to each CPE interface must have a fixed length of `/64`.
The CPE interface prefix is derived based on the node prefix in both kernel
mode and VPP mode, with several implementation differences highlighted below:
* **Kernel mode:** Terragraph nodes only support one CPE interface in kernel
  mode. The `lo` interface will be used to set up this CPE interface, so the
  CPE interface prefix will be the same as the node prefix.
* **VPP mode:** Each CPE interface must be configured with a unique prefix due
  to VPP routing requirements. If the node prefix length is at least `/63`, VPP
  will use the node prefix as the seed prefix and generate a `/64` subnet for
  each CPE interface sequentially, or until the address space is exhausted. For
  example, if a Terragraph node has the node prefix `2001:db8::/62` and 3 CPE
  interfaces configured, a possible CPE interface prefix allocation is as
  follows:
  ```
  TenGigabitEthernet0.prefix="2001:db8:0:1::/64"
  TenGigabitEthernet1.prefix="2001:db8:0:2::/64"
  TenGigabitEthernet2.prefix="2001:db8:0:3::/64"
  ```

<a id="routing-traffic-policing-and-classification"></a>

## Traffic Policing and Classification
Packets arriving on CPE interfaces may be policed according to their assured
forwarding (AF) class by appling a two-rate/three-color policing function.
Inbound traffic is expected to be marked with DSCP corresponding to one of the
supported traffic classes with low drop precedence (green). All other traffic,
including traffic with unknown DSCP values and traffic of any other drop
precedence (non-green), is considered equivalent to AF11 (traffic class 3,
green). Policed traffic will be marked with DSCP values corresponding to AF
per-hop-behavior as defined in RFC 2597. The per-interface traffic policing
policy may be defined in node configuration like so:

```json
{
  "cpeConfig": {
    "TenGigabitEthernet0": {
      "policers": {
        "0": {"cir": 1000, "eir": 2000},
        "2": {"cir": 1000, "eir": 2000},
        "3": {"cir": 1000, "eir": 2000},
        "1": {"cir": 1000, "eir": 2000}
      }
    }
  }
}
```

`cir` and `eir` are both in units of kilobytes per second.

Here, `TenGigabitEthernet0` is a CPE interface. The
`cpeParam.TenGigabitEthernet0.policers` object is a key-value map from AF
traffic class to committed information rate (CIR) and excess information rate
(EIR). The policing behavior follows RFC 4115, where the peak information rate
(PIR) is the sum of CIR and EIR, and the committed burst size (CBS) and excess
burst size (EBS) values are set as the CIR and EIR values over 1 second,
respectively. Traffic arriving at `TenGigabitEthernet0` under the CIR has the
lowest drop priority. Traffic arriving over the CIR but under the PIR has higher
drop priority. Traffic arriving over the PIR is dropped. The EIR can also be
configured to be 0, in which case a one-rate/two-color policing function will be
used, and traffic under the CIR will be marked with the lowest drop priority
while all other traffic is dropped.

<a id="routing-hqos-configuration"></a>

## HQoS Configuration
Packets that reach the WiGig net interfaces are scheduled according to the DSCP
field present in the IP header. The 6-bit DSCP field may take 64 possible
values. Each DSCP value a packet may hold has a corresponding traffic class
(TC), color, and queue. The TC and color determine the packet's scheduling
priority.

HQoS configuration is only supported on platforms using the VPP DPDK plugin.

The HQoS module stores scheduling policies in the TC translation table which is
indexed by DSCP value. Each entry is a 3-tuple of class/queue/color. The
following is the default table:
```
[ 0 ..   7]: 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y
[ 8 ..  15]: 3/0/Y 3/0/Y 3/0/G 3/0/Y 3/0/Y 3/0/Y 3/0/R 3/0/Y
[16 ..  23]: 3/0/Y 3/0/Y 2/0/G 3/0/Y 2/0/Y 3/0/Y 2/0/R 3/0/Y
[24 ..  31]: 3/0/Y 3/0/Y 1/0/G 3/0/Y 1/0/Y 3/0/Y 1/0/R 3/0/Y
[32 ..  39]: 3/0/Y 3/0/Y 0/0/G 3/0/Y 0/0/Y 3/0/Y 0/0/R 3/0/Y
[40 ..  47]: 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y
[48 ..  55]: 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y
[56 ..  63]: 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y 3/0/Y
```

The default TC table may be overwritten by adding DSCP-policy mappings to
the node configuration with the following schema:

```json
{
  "qosConfig": {
    "dscpEntries": {
      "0": {"tc":3, "queue":0, "color":"Y"},
      "1": {"tc":2, "queue":0, "color":"Y"},
      "2": {"tc":2, "queue":0, "color":"Y"},
      "3": {"tc":1, "queue":0, "color":"G"}
    }
  }
}
```

Each entry in the TC table mapping is keyed on the DSCP value. The value of
each entry has three attributes, `tc`, `queue`, and `color`. `tc` may be valued
[0, 3], `queue` may only have value 0, and `color` may be `R`, `G`, or `Y`. Any
number of DSCP entries can be specified under `mapping`, and DSCP entries not
present in the node configuration take the default value.

[DPDK]: https://www.dpdk.org/
[VPP]: https://wiki.fd.io/view/VPP
