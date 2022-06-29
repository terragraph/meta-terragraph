# Overview
Terragraph is a technology that leverages 60 GHz spectrum to deliver fast,
reliable internet. The documents below serve as a detailed reference for many
layers of the Terragraph stack.

## Architecture

#### [Communication Protocol](Communication_Protocol.md)
Terragraph uses [ZeroMQ] (or ZMQ) for all inter- and intra-process message
passing at the application layer. Messages are serialized using the [Thrift]
protocol, then transported using ZMQ sockets over TCP/IP. In general, all
communication is asynchronous.

#### [Routing Layer](Routing_Layer.md)
Terragraph uses [Open/R] as its routing platform. The core module in Open/R is
the distributed, eventually-consistent key-value store, named `KvStore`, which
is used to disseminate information such as routing adjacencies and network
prefixes across the entire network; this is used to implement a link-state
routing protocol. Each Terragraph node runs an `openr` process.

#### [Driver Interface](Driver_Interface.md)
The interface between user space applications and the Terragraph driver and
firmware is referred to as the *driver interface* (`driver-if`). `driver-if`
normally runs alongside the E2E minion, and translates between Thrift messages
(user space) and Netlink messages (driver). Additionally, Terragraph provides
command-line interfaces to communicate with `driver-if` directly.

#### [Driver Stack](Driver_Stack.md)
The driver stack consists of the Terragraph driver and a wireless driver. Among
other things, these drivers are responsible for configuring transmit and receive
paths for the wireless devices, creating virtual network interfaces on the host,
and handling Netlink messages sent from `driver-if`, e.g. by passing requests
and responses to and from the firmware. Terragraph implements its datapath using
[DPDK], a framework for fast packet processing in user space.

#### [VPP Implementation](VPP_Implementation.md)
[VPP] is used with DPDK to implement Terragraph's datapath.

#### [Timing and Synchronization](Timing_Synchronization.md)
Terragraph networks are precisely time-synchronized using GPS or an
"over-the-air sync" mechanism.

#### [PTP & SyncE](PTP_SyncE.md)
Using specialized Puma hardware, Terragraph software supports 1588v2 Transparent
Clock (Class A/B) and SyncE protocols to meet the synchronization requirements
of cellular networks.

#### [Wi-Fi](WiFi.md)
Puma uses an ESP32 module for Wi-Fi access, and enables TCP/IP using SLIP. Wi-Fi
is only used for administrative purposes.

## Firmware Layer
Protocols and algorithms implemented in Terragraph firmware are documented
below.

#### [Beamforming and Link Adaptation](Beamforming_Link_Adaptation.md)
Beamforming (BF) is the process of using signal propagation information between
two antennas and modifying antenna characteristics to maximize the signal
quality between those antennas.

#### [MAC & PHY Specification](MAC_PHY_Specification.md)
Terragraph's link protocol is based on IEEE 802.11-2016, and uses a modified
Directed Multigigabit (DMG) physical layer (PHY) and a significantly slimmed
down Media Access Control (MAC) layer.

#### [PHY Algorithms](PHY_Algorithms.md)
Several physical layer (PHY) algorithms can be tuned via knobs in the node
configuration.

#### [Firmware Stats](Firmware_Stats.md)
Terragraph firmware exports a broad set of statistics to aid debugging.

## End-to-End (E2E) Service
The End-to-End (E2E) service sits at the application layer, and handles
important management functions such as link bring-up, software upgrades,
configuration management, and more. Terragraph networks are managed by a
centralized cloud controller (`e2e_controller`), and each Terragraph node runs a
lightweight client (`e2e_minion`) that connects to the controller. The
controller can run on any host with a route to the Terragraph network, including
a Terragraph node itself.

The primary features of the E2E service are listed below. In general, each
logical management feature resides in its own class, which is referred to as an
"app". Each app runs in a separate thread. Functionally, these threads are
simple event loops, and process messages (such as commands) one at a time. Both
the controller and minion share this design. Source code documentation can be
generated via [Doxygen] (see `src/terragraph-e2e/e2e/Doxyfile`).

#### [Topology Management](Topology_Management.md)
`TopologyApp` holds and manages the network topology, a structure containing
all details about nodes and links within the network. The topology must be
provided upon starting the controller, but the app also exposes APIs to add,
edit, and delete topology elements during runtime. The app performs thorough
validation of all requested topology changes, allocates prefixes for nodes, and
contains several algorithms to automatically assign node and link parameters
such as polarity, Golay codes, channel, and control superframes.

In addition, `TopologyApp` records dynamic topology properties, such as node and
link liveness and nodes' routing adjacencies. Liveness is determined from the
presence or absence of periodic status reports from nodes, handled by
`StatusApp` on both the controller and minion.

#### [Network Ignition](Network_Ignition.md)
On the controller, `IgnitionApp` is responsible for bringing up (or "igniting")
links in the network. Ignition involves forming a link from an "initiator" node,
which is already connected to the controller, to a "responder" node. Under the
default "auto-ignition" configuration, the app will automatically ignite links
during network startup and whenever nodes or links subsequently fail. It applies
an algorithm on the current topology state to determine ignition order; multiple
links can be ignited in parallel.

The minion's `IgnitionApp` exchanges ignition-related messages with the driver.
There are two distinct flows based on whether link-layer encryption ("wsec") is
turned on or off. If wsec is enabled, the app is additionally responsible for
managing two additional Linux processes for each wireless interface: an
"authenticator" (`hostapd`) on the initiator node and a "supplicant"
(`wpa_supplicant`) on the responder node.

#### [Software Upgrade](Software_Upgrade.md)
The controller manages in-band software upgrades through `UpgradeApp`. Upgrades
consist of two phases: "prepare" and "commit". In the "prepare" phase, the
controller distributes the new software image to nodes, over either BitTorrent
or HTTP; upon completion, the nodes will flash the new image onto a secondary
disk partition. The "commit" command simply instructs nodes to reboot to the
newly-written partition.

The main complication for in-band upgrades is that node reboots will bring down
all links to and from a node, which can affect reachability to the rest of the
network. The controller's `UpgradeApp` includes a scheduling algorithm that
parallelizes commits (in "batches" of nodes) while minimizing network isolation,
along with a retry mechanism to handle failures during any upgrade step. The
minion's `UpgradeApp` is responsible for obtaining, validating, and flashing the
new software images, and reporting the node's current upgrade status to the
controller.

#### [Configuration Management](Configuration_Management.md)
Terragraph utilizes a centralized node configuration manager and a layered
configuration model. Initially, nodes start with a version-dependent
"base configuration", which holds all default config values based on the node's
software version; this is static and bundled with the software image. The
"network-wide overrides" layer is applied above the base configuration, and
contains any config values that should be overridden uniformly across the
network. The topmost layer, the "node-specific overrides", applies to individual
nodes. All node configuration files are stored as JSON-serialized Thrift
structures. A separate metadata file describes all available configs, their
validation (e.g. type and range checking), and any node actions required
following a value change (e.g. reboot).

The controller's `ConfigApp` exposes GET/SET operations on all override layers,
and enforces validation on SET methods. The minion's `ConfigApp` processes new
config from the controller and takes any associated actions. To keep config in
sync, nodes send a hash of their local config to the controller in their
periodic status reports, and the controller will overwrite a node's config upon
receiving a mismatch (unless the config is marked as "unmanaged").

#### [Scans](Scans.md)
`ScanApp` is responsible for initiating scans on nodes and collecting the
measurement results. There are several scan types. For instance,
"Periodic Beamforming" (PBF) scans identify independent RF paths between pairs
of nodes; these scans are uni-directional, and run between nodes with L1/L2
connectivity. "Interference Measurement" (IM) scans measure interference between
links, and involve a single transmitter and multiple receivers.

Scans are scheduled by the controller to run periodically and in parallel, using
a graph coloring algorithm in `ScanScheduler` and a slot scheduling mechanism in
`SchedulerApp`. The minion simply passes controller commands to the driver, and
returns results from the driver to the controller.

#### [Network Measurements](Network_Measurements.md)
The controller can initiate and manage network performance measurements through
`TrafficApp`. The app supports running ping and iPerf sessions, and dispatches
commands to each participating minion's `TrafficApp` to start or stop these
processes. The controller is responsible for maintaining the state of each
session and handling the outputs.

#### [Prefix Allocation](Prefix_Allocation.md)
Terragraph nodes can be allocated IPv6 prefixes in three different ways.
Distributed prefix allocation is when the nodes allocate prefixes amongst
themselves using a distributed allocation scheme and is handled entirely by
[Open/R]. Centralized prefix allocation (CPA) is a scheme where the controller
allocates prefixes to all the nodes. This scheme linearly scans through the
prefix range and assigns unallocated prefixes to nodes. CPA serves mostly as a
stepping stone for more advanced allocation schemes such as Deterministic prefix
allocation (DPA). DPA involves segmenting the network into prefix zones, which
are assigned subnet prefixes of the network seed prefix. Nodes will be allocated
prefixes from their zone's prefixes, allowing the POPs to advertise these
subnets to their BGP peers and load-balance ingress traffic.

#### [Topology Discovery](Topology_Discovery.md)
Terragraph provides the capability to automatically discover the network
topology through the use of a broadcast beamforming protocol. The high-level
discovery algorithm is orchestrated by the controller's `TopologyBuilderApp`,
which extends the topology by incrementally adding and establishing links to
responding nodes.

## Application Layer Modules
Several additional application-layer modules are described below.

#### [Stats, Events, Logs](Stats_Events_Logs.md)
Terragraph includes a separate pipeline for pushing node statistics, events, and
logs via several processes running on each node. These are typically aggregated
using Kafka and Fluentd servers.

#### [Terragraph CLI](Terragraph_CLI.md)
Terragraph provides command-line interfaces, `tg2` (Lua-based) and `tg`
(Python-based), to interact with various Terragraph software components.

#### [API Service](API_Service.md)
Terragraph also provides a REST API service, `api_service`, written in C++ using
[Proxygen]. API Service translates HTTP/JSON requests into ZMQ/Thrift calls to
the controller or aggregator, then returns the responses to the client
JSON-encoded. Documentation is generated from Javadoc-style annotations in the
source code using [apiDoc], and is statically hosted on the Proxygen server.

#### [Local Web Interface](Local_Web_Interface.md)
Terragraph provides a proof-of-concept web portal that is accessible over Wi-Fi,
with the primary purpose of enabling easy initial setup. This consists of two
components: a web server (`webui`) and UI (`tg95`).

#### [LED Agent](LED_Agent.md)
On Puma hardware, `led-agent` controls three LED lights to display basic health
information.

## System Management
The following features are included with Terragraph to assist with system
management.

#### [Service Scripts](Service_Scripts.md)
Terragraph includes scripts to manage its processes using [systemd] on x86 hosts
(e.g. controller) and [runit] on nodes. The service scripts perform all required
environment setup and cleanup for each process. The service managers handle
other tasks such as logging and automatic process restarts.

#### [Watchdog](Watchdog.md)
Terragraph nodes contain watchdog processes which detect, repair, and log
various faults. Kernel crashes (panics) are also logged.

#### [High Availability](High_Availability.md)
The controller supports a high-availability configuration using a primary-backup
protocol. In this setup, two controllers (or "peers") are run on separate
machines, and are designated as either "primary" or "backup". If the primary
catastrophically fails (e.g. power outage, network failure, hardware failure,
etc.), the backup will assume control of the Terragraph network.

`BinaryStarApp` manages the underlying finite state machine (FSM), heartbeating
with the peer via a dedicated socket, and data synchronization with the peer
and local apps. Some additional logic resides in the controller's and minion's
`Broker` for failover scenarios.

#### [Security](Security.md)
Terragraph wireless links and wired CPE interfaces can be secured using the IEEE
802.1X standard. Nodes can also apply `ip6tables` firewall rules via node
configuration.

## Version Control
Information about code versioning is provided below.

#### [Release Conventions](Release_Conventions.md)
Terragraph manages its code repository using Git, and releases software with
major and minor versions.

#### [Firmware Versioning](Firmware_Versioning.md)
Terragraph's wireless firmware is versioned separately from Terragraph software.

## Resources
* [ZeroMQ] - Distributed messaging library
* [Thrift] - Meta's interface definition language
* [Open/R] - Meta's routing platform
* [DPDK] - Data Plane Development Kit
* [VPP] - Vector Packet Processing
* [Doxygen] - Documentation generator
* [systemd] - Linux init system with service management
* [runit] - UNIX init scheme with service supervision
* [Proxygen] - Meta's C++ HTTP libraries
* [apiDoc] - Documentation generator for REST APIs

[ZeroMQ]: http://zeromq.org/
[Thrift]: https://thrift.apache.org/
[Open/R]: https://github.com/facebook/openr
[DPDK]: https://www.dpdk.org/
[VPP]: https://wiki.fd.io/view/VPP
[Doxygen]: https://www.doxygen.nl/
[systemd]: https://www.freedesktop.org/wiki/Software/systemd/
[runit]: http://smarden.org/runit/
[Proxygen]: https://github.com/facebook/proxygen
[apiDoc]: http://apidocjs.com/
