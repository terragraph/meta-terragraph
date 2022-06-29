# Network Measurements
This document describes the network performance tools integrated into the E2E
layer.

## Architecture
The controller's `TrafficApp` supports running and managing ping and iPerf
sessions, and dispatches commands to each participating minion's `TrafficApp` to
start or stop these processes. The controller is responsible for maintaining the
state of each session in progress. When a session terminates, the participating
minion(s) will send the console output of the process to the controller, which
then forwards the output to the original sender; at this point, the controller
discards the session state.

If possible, the controller can optionally use a link-local IPv6 address instead
of a global address for the destination (e.g. to support higher data rates).
These link-local addresses are automatically populated by the controller using
adjacency information from Open/R.

## Commands
All supported commands are described in the sections below.

### Ping
The controller supports the `ping6` utility and most of its standard options
(defined in `thrift::PingOptions`).

| User Operation  | Command           |
| --------------- | ----------------- |
| Start Ping      | `START_PING`      |
| Stop Ping       | `STOP_PING`       |
| Get Ping Status | `GET_PING_STATUS` |

### iPerf
The controller supports the `iperf3` utility and most of its standard options
(defined in `thrift::IperfOptions`).

| User Operation   | Command            |
| ---------------- | ------------------ |
| Start iPerf      | `START_IPERF`      |
| Stop iPerf       | `STOP_IPERF`       |
| Get iPerf Status | `GET_IPERF_STATUS` |

iPerf requires running both a server and client (unlike ping, which has a client
only). The steps to initiate a session, in response to a `START_IPERF`
operation, are shown below.

1. **Controller** - The controller issues a `START_IPERF_SERVER` command to the
   minion serving as the iPerf destination.
2. **Minion (server)** - The minion chooses an unused port in the range
   [70001, 70050], then forks an iPerf server process and waits for it to
   initialize. The `iperf3` command is called with `--forceflush` to prevent output
   buffering. When the minion has read the first byte of output, it sends a
   `START_IPERF_SERVER_RESP` notification to the controller containing the iPerf
   server port it used.
3. **Controller** - The controller issues a `START_IPERF_CLIENT` command to the
   minion serving as the iPerf source. At this point, the session gets recorded.
4. **Minion (client)** - The minion forks an iPerf client process.
5. **Minions (both)** - When the iPerf process exits, each minion sends the
   output to the controller via the `IPERF_OUTPUT` message.
6. **Controller** - The controller forwards each `IPERF_OUTPUT` message to the
   original sender. The iPerf session is deleted upon receiving either message.
