# Stats, Events, Logs
This document describes Terragraph's monitoring infrastructure for pushing
network stats, events, and logs.

## Collection
This section describes the services which collect and export data.

### Stats Agent
Terragraph nodes utilize the `ZmqMonitor` feature of [fbzmq] to collect stats
and events. `ZmqMonitor` provides a submit port (a ZMQ router socket) to which
different system components can send generated stats. It also provides a publish
port to which multiple consumers can listen and receive updates on generated
stats and events. Apps generating stats can use `ZmqMonitorClient` to easily
inject stats into `ZmqMonitor`. Stat keys have arbitrary names, generally with
period (`.`) delimiters; stats published from `driver-if` may also contain a
null character (`\0`) delimiter followed by the originating radio MAC address.
The Thrift type for stats is `fbzmq::thrift::MonitorPub` (defined in
`Monitor.thrift`), with a `pubType` of `fbzmq::thrift::PubType::COUNTER_PUB`.

Each node runs a dedicated "stats agent" process (`stats_agent`) which collects
all generated stats and events via `ZmqMonitor` and periodically publishes them
to configured external endpoints. The stats agent uses the node ID (MAC address)
as its source entity. The controller also runs a stats agent instance for its
stats and events (and uses a dummy MAC address as its source entity). On the
controller, the stats agent reads the controller configuration file instead of
the node configuration file.

The stats agent subscribes to all sockets specified in the configuration under
the `statsAgentParams.sources` key. By default, the stats agent listens to and
publishes stats/events from the following sources:

| Source       | Publish ZMQ URL         |
| ------------ | ----------------------- |
| `controller` | `tcp://localhost:28989` |
| `driver-if`  | `tcp://localhost:18990` |
| `minion`     | `tcp://localhost:18989` |
| `system`     | `tcp://localhost:18991` |

The "system" source is a local `ZmqMonitor` instance within the stats agent.
This is used by the following:
* `LocalStatsFetcher` thread to collect and push various system stats, such as
  VPP stats (when `envParams.DPDK_ENABLED` is set), Open/R stats (when
  `statsAgentParams.collectors.openrStatsEnabled` is set), and various local
  counters relating to network interfaces, processes, memory, CPU, etc. By
  default, the thread will collect stats every 30 seconds
  (`statsAgentParams.collectors.systemStatsCollectionInterval`), but can be
  modified based on the desired low or high frequency publishing intervals (see
  below).
* `InputListener` thread to push events sent directly to the stats agent (e.g.
  `tg2 event` CLI).

There are three consumers of the same data within the stats agent:
* `KafkaPublisher` - Pushes the collected data to a given [Kafka] cluster. This
  is configured under the node configuration structure
  `statsAgentParams.endpointParams.kafkaParams`.
* `NmsPublisher` - Pushes the collected data to the NMS aggregator. This service
  has been deprecated in favor of Kafka, but can be manually enabled in the node
  configuration structure `statsAgentParams.endpointParams.nmsPublisherParams`.
  Stats and events are collectively sent in a `STATS_REPORT` message, and a
  small set of whitelisted stats are sent more frequently in a
  `HIGH_FREQUENCY_STATS_REPORT` message (e.g. for real-time data during node
  installation and alignment).
* `GraphPublisher` - Pushes the collected data to Meta internal endpoints
  (Scribe, ODS, Pelican) for storage and analysis, and is disabled by default.

The data format for counter-type (`fbzmq::thrift::CounterValueType::COUNTER`)
stats is determined by the following node configuration fields, which must be
consistent across all nodes:
* `statsAgentParams.publisherParams.convertToRate` - If set, counters will be
  published as computed rates, otherwise they will be published as raw values
  with field `isCounter = true`.
* `statsAgentParams.publisherParams.publishValueWithRate` - If this and
  "convertToRate" are both set, then 2 keys will be published for each counter:
    * `<key name>` (`isCounter = true`) containing the raw value
    * `<key name>.rate` (`isCounter = false`) containing the computed rate

Events that were not successfully transmitted to a Kafka broker (by
`KafkaPublisher`) or the NMS aggregator (by `NmsPublisher`) are cached to
`/tmp/events.json` and `/tmp/kafka_events.json`, respectively, upon stats agent
shutdown, and are loaded again during initialization. A watchdog script,
`persist_event_cache.sh`, moves these cache files to `/data/` upon a graceful
Linux shutdown.

### Fluent Bit
Terragraph nodes can push log files to [Fluentd] servers via a [Fluent Bit]
client process (`fluent-bit`) when server endpoints are provided under the
`fluentdParams.endpoints` node configuration structure. Where possible, logs are
tagged with `mac_addr` holding the node ID, and `node_name` and `topology_name`
as given in the controller's topology.

The set of log files is specified in the node configuration under the
`fluentdParams.sources` structure, containing the following defaults (depending
on hardware type):

| Source       | File                          |
| ------------ | ----------------------------- |
| kern         | `/var/log/kern.log`           |
| e2e_minion   | `/var/log/e2e_minion/current` |
| openr        | `/var/log/openr/current`      |
| fib_vpp      | `/var/log/fib_vpp/current`    |
| vpp          | `/var/log/vpp/current`        |
| vpp_vnet     | `/var/log/vpp/vnet.log`       |

### Logtail
Each Terragraph node can run a "logtail" process (`logtail`) which tails a set
of log files and publishes them to the central aggregator. This feature has been
deprecated in favor of Fluent Bit, but can be enabled via node configuration
field `envParams.LOGTAIL_ENABLED`. Unlike the stats agent, logtail is not used
on the controller, and sends its MAC address to the aggregator (via
`LogPublisher`) in every message (`SYSLOG_REPORT`). The set of log files is
specified under the node configuration structure `logTailParams.sources`, and
contains the same defaults as Fluent Bit.

### SNMP
When enabled via the node configuration field `envParams.SNMP_ENABLED`,
Terragraph nodes will run a [Net-SNMP] (Simple Network Management Protocol)
daemon process (`snmpd`) alongside a Terragraph SNMP agent process
(`snmp_agent`) which can be polled for statistics. The SNMP agent registers
`TERRAGRAPH-RADIO-MIB` to handle all requests for the MIB (Management
Information Base). The MIB for Terragraph radio stats is located in
`src/terragraph-stats/src/mib`.

## Aggregation
This section describes the services which aggregate data.

### Kafka
Data pushed to [Kafka] via `stats_agent` (on nodes and the controller) can be
accessed in *topics* defined under the
`statsAgentParams.endpointParams.kafkaParams.topics` node configuration
structure. The data in each topic is a string representing a JSON-serialized
Thrift structure. The default topic names and their data formats are given in
the table below.

| Topic Name    | Data Format                      |
| ------------- | -------------------------------- |
| events        | `thrift::Event`                  |
| hf_stats      | `thrift::AggrStat`               |
| stats         | `thrift::AggrStat`               |
| scan_results  | `thrift::ScanResultEventWrapper` |
| iperf_results | `thrift::IperfOutput`            |
| ping_results  | `thrift::PingOutput`             |

### Fluentd
Logs pushed to [Fluentd] servers via `fluent-bit` (on nodes) are tagged with the
value `log.node.<name>` (where `<name>` is the corresponding key from the node
configuration structure `fluentdParams.sources`) and may contain the following
fields:

| Field         | Description               |
| ------------- | ------------------------- |
| mac_addr      | Node ID (MAC address)     |
| node_name     | Node name (from topology) |
| topology_name | Topology name             |

Also note that certain application crash traces are sent as multiline logs.

### NMS Aggregator
When enabled, the central aggregator (`nms_aggregator`) receives stats, events,
and logs from `stats_agent` and `logtail` processes as described above. It
periodically pushes this data upstream via HTTP POST requests to endpoints
defined in the aggregator configuration file (under `dataEndpoints`):

| Category             | Endpoint                             |
| -------------------- | ------------------------------------ |
| Stats                | `host` + `statsWriterSuffix`         |
| High-frequency stats | `host` + `highFreqStatsWriterSuffix` |
| Events               | `host` + `events_writer`             |
| Logs                 | `host` + `logs_writer`               |

This data is pushed every 30 seconds (`data_publish_interval_s`) by default;
high-frequency stats are pushed every 1 second
(`high_frequency_data_publish_interval_s`).

Data formats for each category are shown below.

#### Stats
```json
{
  "topology": {
    "name": <topology name : string>,
  },
  "interval": <stats push interval (in seconds) : int>,
  "agents": [
    {
      "mac": <source MAC address : string>,
      "name": <source node name (or empty) : string>,
      "site": <source node site (or empty) : string>,
      "stats": [
        {
          "ts": <unix timestamp (microseconds) : long>,
          "key": <key name : string>,
          "value": <raw value or rate (for counters) : double>,
        },
        ... <more stats>
      ]
    },
    ... <more nodes>
  ]
}
```

#### Events
```json
{
  "topology": {
    "name": <topology name : string>,
  },
  "agents": [
    {
      "mac": <source MAC address : string>,
      "name": <source node name (or empty) : string>,
      "site": <source node site (or empty) : string>,
      "events": [
        {
          "timestamp": <unix timestamp (seconds) : long>,
          "source": <event source : string>,
          "reason": <event description : string>,
          "details": <JSON-formatted event details : string>,
          "category": <event category : enum(thrift::EventCategory)>,
          "eventId": <event ID : enum(thrift::EventId)>,
          "level": <event level : enum(thrift::EventLevel)>,
          "entity": <event entity : string>,
          "nodeId": <source node ID (MAC address) : string>,
          "topologyName": <topology name : string>,
          "nodeName": <node name : string>
        },
        ... <more events>
      ]
    },
    ... <more nodes>
  ]
}
```

Sample "events" entry:

```json
{
  "timestamp": 1549494973,
  "source": "minion-app-IGNITION_APP",
  "reason": "Sending assoc request for neighbor 00:00:00:10:0b:4a",
  "details": "{\"linkStatusType\":1,\"responderMac\":\"00:00:00:10:0b:4a\",\"responderNodeType\":2,\"responderNodePolarity\":2,\"initiatorMac\":\"00:00:00:10:0b:44\"}",
  "category": 100,
  "eventId": 102,
  "level": 10,
  "entity": "00:00:00:10:0b:44",
  "nodeId": "00:00:00:10:0b:44",
  "topologyName": "Lab F8 B",
  "nodeName": "terra111.f5.tb.a404-if"
}
```

The list of event categories, IDs, and levels is defined in `Event.thrift`.

Events can be used to generate alarms, using event data in the following way:
* `eventId` - shared for all directly associated events (e.g. link up/down)
* `level` - implicitly defines raising or clearing an alarm (e.g. `ERROR` to
  raise and `INFO` to clear)
* `entity` - an arbitrary string used to differentiate events with the same ID
  (e.g. node ID, link name)
* `nodeId` - the node ID (MAC address) that the event/alarm should be associated
  with
* `nodeName` - the node name associated with the node ID

#### Logs
```
{
  "topology": {
    "name": <topology name : string>,
  },
  "agents": [
    {
      "mac": <source MAC address : string>,
      "name": <source node name (or empty) : string>,
      "site": <source node site (or empty) : string>,
      "logs": [
        {
          "ts": <unix timestamp (microseconds) : long>,
          "file": <file name : string>,
          "log": <log line : string>,
        },
        ... <more logs>
      ]
    },
    ... <more nodes>
  ]
}
```

## Common Stats
Some common stat formats are shown in the table below.

| Type             | Prefix           | Format                                 | Example                            |
| ---------------- | ---------------- | -------------------------------------- | ---------------------------------- |
| TG Firmware      | `tgf`            | `tgf.<remote MAC address>.<key name>`  | `tgf.00:00:00:10:0b:40.staPkt.mcs` |
| TG Driver        | `tgd`            | `tgd.<key name>`                       | `tgd.gpsStat.fixNumSat`            |
| Network Counters | `link`           | `link.<remote MAC address>.<key name>` | `link.00:00:00:10:0b:40.tx_bytes`  |
| VPP Counters     | `vpp`            | -                                      | `vpp.00:00:00:10:0b:40.if.tx.pkts` |
| Process Counters | `<process name>` | `<process name>.<key name>`            | `e2e_minion.mem.util`              |
| System Counters  | none             | `<key name>`                           | `uptime`                           |

Specific keys are shown in the following table. Refer to
[Firmware Stats](Firmware_Stats.md) for a full list of stats exported by
Terragraph firmware.

| Description                               | Format                                           |
| ----------------------------------------- | ------------------------------------------------ |
| RSSI (received signal strength indicator) | `tgf.<remote MAC address>.phystatus.srssi`       |
| SNR (signal-to-noise ratio)               | `tgf.<remote MAC address>.phystatus.ssnrEst`     |
| MCS index                                 | `tgf.<remote MAC address>.staPkt.mcs`            |
| PER (packet error rate)                   | `tgf.<remote MAC address>.staPkt.perE6`          |
| Mgmt link up count                        | `tgf.<remote MAC address>.staPkt.mgmtLinkUp`     |
| Mgmt link available count                 | `tgf.<remote MAC address>.staPkt.linkAvailable`  |
| Received packets                          | `tgf.<remote MAC address>.staPkt.rxOk`           |
| Successful MPDUs                          | `tgf.<remote MAC address>.staPkt.txOk`           |
| Failed MPDUs                              | `tgf.<remote MAC address>.staPkt.txFail`         |
| Transmit power                            | `tgf.<remote MAC address>.staPkt.txPowerIndex`   |
| Transmit beam index                       | `tgf.<remote MAC address>.phyperiodic.txbeamidx` |
| Receive beam index                        | `tgf.<remote MAC address>.phyperiodic.rxbeamidx` |
| Transmit bytes                            | `link.<remote MAC address>.tx_bytes`             |
| Receive bytes                             | `link.<remote MAC address>.rx_bytes`             |
| Transmit packets                          | `link.<remote MAC address>.tx_packets`           |
| Receive packets                           | `link.<remote MAC address>.rx_packets`           |
| Transmit errors                           | `link.<remote MAC address>.tx_errors`            |
| Receive errors                            | `link.<remote MAC address>.rx_errors`            |
| Transmit dropped                          | `link.<remote MAC address>.tx_dropped`           |
| Receive dropped                           | `link.<remote MAC address>.rx_dropped`           |
| Receive frame                             | `link.<remote MAC address>.rx_frame`             |
| Receive overruns                          | `link.<remote MAC address>.rx_overruns`          |
| Transmit overruns                         | `link.<remote MAC address>.tx_overruns`          |
| Transmit collisions                       | `link.<remote MAC address>.tx_collisions`        |
| Interface speed (mbps)                    | `link.<remote MAC address>.speed`                |
| VPP transmit bytes                        | `vpp.<remote MAC address>.if.tx.bytes`           |
| VPP receive bytes                         | `vpp.<remote MAC address>.if.rx.bytes`           |
| VPP transmit packets                      | `vpp.<remote MAC address>.if.tx.pkts`            |
| VPP receive packets                       | `vpp.<remote MAC address>.if.rx.pkts`            |
| VPP transmit error packets                | `vpp.<remote MAC address>.if.tx-error.pkts`      |
| VPP receive error packets                 | `vpp.<remote MAC address>.if.rx-error.pkts`      |
| VPP dropped packets                       | `vpp.<remote MAC address>.if.drops.pkts`         |
| VPP QoS bytes                             | `vpp.<remote MAC address>.qos.tc<N>.bytes`       |
| VPP QoS bytes dropped                     | `vpp.<remote MAC address>.qos.tc<N>.bytes_dropped` |
| VPP QoS packets                           | `vpp.<remote MAC address>.qos.tc<N>.packets`     |
| VPP QoS packets dropped                   | `vpp.<remote MAC address>.qos.tc<N>.packets_dropped` |

## Resources
* [fbzmq] - Meta's ZeroMQ wrappers
* [Kafka] - Distributed streaming platform
* [Fluentd] - Log collector
* [Fluent Bit] - Log processor and forwarder
* [Net-SNMP] - Simple Network Management Protocol (SNMP) software suite

[fbzmq]: https://github.com/facebook/fbzmq
[Kafka]: https://kafka.apache.org/
[Fluentd]: https://www.fluentd.org/
[Fluent Bit]: https://fluentbit.io/
[Net-SNMP]: http://www.net-snmp.org/
