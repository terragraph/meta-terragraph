# Driver Interface
This document describes the interface between user space applications and the
Terragraph driver and firmware.

## Architecture
The E2E minion is responsible for communicating with the Terragraph driver and
firmware via its `DriverApp` thread. `DriverApp` uses a ZMQ pair socket
(`ZMQ_PAIR`) to send and receive messages with the *driver interface*
(`driver-if`). `driver-if` then communicates with the Terragraph driver over a
Netlink socket.

<p align="center">
  <img src="../media/figures/driver_if.svg" width="320" />
</p>

### Implementation
Normally, `driver-if` runs as another thread on the E2E minion process (spawned
by the `Minion` main class). `driver-if` can run also in *daemon mode* without
`DriverApp` or other E2E minion modules by passing the `--driver_if_only` flag.

A dedicated `ZmqMonitor` instance is also spawned in a separate thread for
pushing driver and firmware stats (refer to
[Stats, Events, Logs](Stats_Events_Logs.md) for further details).

`driver-if` is a subclass of `BaseDriverIf`, and holds a corresponding Netlink
socket subclass of `BaseNetlinkSocket`. The subclasses are based on the
machine's architecture, listed in the table below. Note that the x86 classes
are intended for emulation only and are largely incomplete.

| Architecture | `BaseDriverIf` | `BaseNetlinkSocket` |
| ------------ | -------------- | ------------------- |
| ARM          | `ArmDriverIf`  | `ArmNetlinkSocket`  |
| x86          | `X86DriverIf`  | `X86NetlinkSocket`  |

### Message Handling
Upon receiving a message from the *Netlink socket*, `driver-if` does the
following:
1. Convert the Netlink message to a `DriverNlMessage` instance using the
   `ArmNetlinkSocket::recvFunc_()` callback.
2. Convert the `DriverNlMessage` to the corresponding `thrift::Message`
   structure using `DriverIfUtil::driverNl2IfMessage()`.
3. Filter out and handle specific messages, including:
    * `DR_RESP` - log driver acknowledgements
    * `FW_STATS`, `DR_STAT_PUSH` - publish stats from the firmware or driver to
       `ZmqMonitor`
    * `FW_HEALTHY` - write firmware health status for the watchdog
    * `FW_PPS_HTSF_INFO` - HTSF timing information
4. Forward remaining messages through the pair socket using
   `BaseDriverIf::sendToDriverApp()`.

Upon receiving a message from the *pair socket*, `driver-if` does the following:
1. Handle the `thrift::Message` by invoking the corresponding function within
   `BaseDriverIf::processMessage()`.
2. Convert the `thrift::Message` structure to a `DriverNlMessage` instance, then
   send it to the driver as a Netlink message using
   `ArmNetlinkSocket::sendMessage()`.

`driver-if` publishes the following stats related to Netlink I/O:
* `tgd.nl.recv_ok` - number of Netlink messages received successfully
* `tgd.nl.recv_err` - number of Netlink messages received unsuccessfully

### Features
Some other notable features of `driver-if` are described below.

##### Firmware Codebook Management
The firmware downloads *codebooks* (tables of antenna weights) from `driver-if`
via `FW_GET_CODEBOOK` requests (`thrift::CodebookFetchReq`), which specify a
channel and range of beams. In order to respond with the correct codebook,
`driver-if` maintains state learned from the `NODE_INIT` and `NODE_INIT_NOTIFY`
message sequence for each radio, and captures relevant information (i.e. vendor,
initial beamforming procedure type, codebook variant) using the
`ArmDriverIf::RadioProperties` struct. `driver-if` will load codebooks on demand
and cache them for the lifecycle of the process.

##### GPS Management
`driver-if` contains an optional feature (enabled via the node configuration
flag `envParams.GPSD_ENABLED`) to connect to a local [gpsd] socket (on port
2947). When enabled, `BaseDriverIf` creates a separate thread to read events
from gpsd using the `GpsdClient` class, which does the following:
* Forwards all received timestamps to each firmware instance via `GPS_SEND_TIME`
  once per second (only if `timingParams.PPS_TIMESTAMP_SOURCE` is set to "GPS"
  in the node configuration).
* Pushes GPS stats via `DR_STAT_PUSH`.
* Forwards received location data to each firmware instance via
  `GPS_SET_POS_REQ` once only.

For more details, see [Timing and Synchronization](Timing_Synchronization.md).

##### PPS Timestamp Forwarding
More generally, `driver-if` can forward 1PPS timestamps to Terragraph firmware
from the following sources (`timingParams.PPS_TIMESTAMP_SOURCE` in the node
configuration) to enable GPS/PPS sync mode:
* GPS, via `GpsdClient` (as described above).
* PTP hardware clock, via `PTPClockHelper`. This utilizes a separate thread to
  read EXTTS events from a PTP device (`timingParams.PTP_DEVICE`) using the
  `ioctl` command `PTP_EXTTS_REQUEST`.
* Software HTSF messages from Terragraph firmware, via `BaseDriverIf`. On
  multi-radio nodes with a common PPS signal to all baseband sectors, the
  software HTSF PPS timestamps received from one RF sync sector are sent to the
  other sectors to enable PPS sync mode. When multiple sectors are publishing
  HTSF timestamps, `driver-if` picks the "source" sector based on the first
  message received, and will not switch to a different source sector until
  `timingParams.HTSF_MAX_LOOP_SIZE * 13` seconds have elapsed in order to avoid
  timing loops in the network; this delay is based on the number of missed
  seconds (PPS timestamps) before firmware will transition from PPS to RF sync
  (or bring down a link). Alternatively, the source sector can be hardcoded via
  the node configuration field `timingParams.HTSF_SRC_MAC`.

`driver-if` publishes the following stats to help monitor the state of timestamp
forwarding:
* `tgd.gpsStat.MAC.numTsSent` - number of GPS timestamps forwarded to `MAC`
  (only valid if `timingParams.PPS_TIMESTAMP_SOURCE` is set to "GPS")
* `tgd.ptp.MAC.numTsSent` - number of PTP hardware clock timestamps forwarded to
  `MAC`
* `tgd.htsf.MAC.numTsSent` - number of software HTSF timestamps forwarded to
  `MAC`

##### PTP Hardware Clock Sync
`driver-if` can be configured to synchronize a node's PTP (Precision Time
Protocol) hardware clock based on 1PPS messages from a given timing reference
(`timingParams.PTP_TIMER_SOURCE` in the node configuration) by correcting for
phase offset and drift. The PTP clock is synchronized to GPS time (not UTC).
Possible timing sources include a GPS device and software/hardware HTSF messages
from Terragraph firmware.

Currently, the following clock types are supported:
* NXP DPAA2 (Data Path Acceleration Architecture Gen2) PTP clock, via direct
  register access.
* VSC 10G "Malibu" PHY LTC (local time counter) clock, via a custom datagram
  socket protocol patched over the user-space "MESA" API. This can be enabled
  by setting the node configuration field `envParams.PTP_VSC_CTRL_SOCKET` to any
  valid path, which will be used for communication between `e2e_minion` and
  `malibu_char`.

### Performance Requirements
`driver-if` must be able to process messages in a timely manner, and major
issues may emerge here if the host CPU is overloaded. Some common examples are
described below.

* **PPS timestamp forwarding.** On PPS sync nodes, `driver-if` must reliably
  send timestamps to Terragraph firmware once per second, and 10 missed seconds
  may cause a link to go down.
    * *Signs:* `LINK_DOWN` events with reason `SYSTEM_GPS_SYNC_IN_PROG`;
      incrementing counter `tgf.MAC.gps.numMissedSec` (despite stable GPS fix,
      if applicable)
    * *Workarounds:* Increase scheduling priority of gpsd (if applicable) or
      `driver-if`, e.g. using the `nice` command

* **Netlink communication.** `driver-if` must receive northbound messages from
  Terragraph firmware faster than they are produced to avoid dropping any
  Netlink messages.
    * *Signs:* Netlink errors reported by `driver-if` and Terragraph driver (ex.
      `ENOBUFS`), esp. as number of wireless links increases; incrementing
      counter `tgd.nl.recv_err`
    * *Workarounds:* Reduce firmware stats frequency
      (`radioParamsBase.fwParams.statsLogInterval`); omit some firmware stats
      categories (`radioParamsBase.fwStatsConfig.<category> = false`)

## Message Formats
`driver-if` handles the translation between Thrift messages (user space) and
Netlink messages (driver).

### Netlink
Netlink types are enumerated in `tgd_nlsdn_commands`, defined inside
`nl-driver-if/fb_tgd_nlsdn_common.h`. These are manually mapped to
`DriverNlMessageType` enum values inside `driver-if/DriverNlMessage.h`.

### Thrift
All Thrift messages to and from the driver use the same `thrift::DriverMessage`
base structure defined in `DriverMessage.thrift`:
```c
struct DriverMessage {
  1: binary value;
  2: string radioMac;
}
```

`value` contains the compact-serialized binary value of the actual Thrift
structure, and `radioMac` is the origin or destination baseband MAC address
required for multi-baseband boards.

Note that this structure must be wrapped inside of a `thrift::Message`
structure, like with all other messages (refer to
[Communication Protocol](Communication_Protocol.md) for further details). Thus,
the actual message type is double-serialized, and the `mType` field in the outer
structure describes the message type of the actual message (i.e. innermost
structure).

Messages directly between `driver-if` and firmware use the *pass-through*
(`PassThru`) framework, with messages defined in `PassThru.thrift`. The base
message type is `thrift::PassThruMsg`, with all possible messages embedded as
nested structures and a separate message type field, `msgType`, enumerated in
`thrift::PtMsgTypes`. When sending pass-through messages over Netlink, the
`DriverNlMessageType` type for southbound messages (i.e. towards firmware) is
`PASSTHRU_SB` and the type for northbound messages (i.e. towards `driver-if`) is
`PASSTHRU_NB`.

## Command-Line Interfaces
When run in daemon mode, interaction with `driver-if` is typically done via CLI
applications, described below.

### "r2d2" CLI
`r2d2` is a Python-based command-line utility solely for communicating with
`driver-if` and its `ZmqMonitor` instance. This is mainly used for debugging and
automated testing purposes. For further implementation details, refer to
descriptions of the `tg` CLI, which has a similar architecture (see
["tg" CLI](Terragraph_CLI.md#terragraph-cli-tg-cli)).

### "tg2" CLI
The majority of `r2d2` functionality is also provided as part of the Lua-based
`tg2` CLI. Commands and syntax are nearly identical, although there may be minor
differences between argument/option names, program output, description text,
etc. A table mapping `r2d2` commands to `tg2` commands is given below:

| `r2d2` command  | `tg2` command         |
| --------------- | --------------------- |
| `r2d2 *`        | `tg2 fw *`            |
| `r2d2 fw_stats` | `tg2 stats driver-if` |
| `r2d2 fw_set_golay`<br />`r2d2 sync_bf_slot_exclusion_req`<br />`r2d2 sync_scan` | *unsupported* |

## Resources
* [gpsd] - GPS service daemon

[gpsd]: https://gpsd.gitlab.io/gpsd/
