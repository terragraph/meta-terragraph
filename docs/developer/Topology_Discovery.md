# Topology Discovery
This document describes Terragraph's topology discovery feature.

## Overview
`TopologyBuilderApp` orchestrates the high-level topology discovery algorithm,
which extends the topology by incrementally adding and establishing links to
responding nodes. The main piece of the algorithm is the broadcast beamforming
protocol, or "topology scan" (refer to [Scans](Scans.md) for further details).

There are currently three actions available in `TopologyBuilderApp`:
1. Running a single-node topology scan, and receiving results synchronously.
2. Coordinating a network-wide topology discovery.
3. Coordinating a link discovery within a search radius.

These actions are outlined in the sections below.

## Single-Node Topology Scan
The goal of the single-node topology scan command is to provide a synchronous
wrapper over `ScanApp`'s API, along with more detailed response data.
`TopologyBuilderApp` goes through the following steps upon receiving a
`START_TOPOLOGY_SCAN` command:
1. Send a `START_SCAN` request to `ScanApp`. If successful, `ScanApp` will reply
   with a `START_SCAN_RESP` message containing a unique scan identifier (or
   "token") that will be used to tag the scan results.
2. `ScanApp` forwards all topology scan results to `TopologyBuilderApp` in the
   `TOPOLOGY_SCAN_RESULT` message. Process the raw scan results and return a
   `START_TOPOLOGY_SCAN_RESP` response to the original sender.

Scan result processing is handled within
`TopologyBuilder::processTopologyScanResult()`. For each responder, this finds
the *strongest beam* with the *smallest beam angle* using the
initiator-to-responder LQM (link quality metric) matrix. This also identifies
the nearest site in the topology via GPS distance.

## Network-Wide Topology Discovery
The following user operations are available for managing network-wide topology
discovery scans:

| User Operation | Command                            |
| -------------- | ---------------------------------- |
| Start Scans    | `START_NETWORK_TOPOLOGY_SCAN`      |
| Stop Scans     | `STOP_NETWORK_TOPOLOGY_SCAN`       |
| Get Status     | `GET_NETWORK_TOPOLOGY_SCAN_STATUS` |

The *inputs* to the procedure are as follows:
* **Sites** - All site locations should be added to the topology beforehand
  (responding nodes are matched to the nearest site).
* **Links between sites** - Site-level link information (`thrift::SiteLink`)
  provided in the request will determine which links to form.

The *outputs* of the procedure are as follows:
* **Nodes** - All responder nodes and adjacent nodes/radios on the same site
  will be added to the topology.
* **Links between nodes** - Node-level link information will be added to the
  topology.

The current implementation is heavily dependent on GPS accuracy to match
responder nodes to sites in the topology, and does not run scans in parallel.

### Discovery Algorithm
The topology discovery algorithm resides in `TopologyBuilder`, which contains
three main methods for handling a network-wide topology scan:
* `initNetworkTopologyScan()` - Queues all sites specified in the scan request.
* `networkTopologyScanLoop()` - Advances the algorithm.
* `handleScanResult()` - Stores a topology scan result.

The scan loop is invoked once to initiate the scan, and then asynchronously upon
receiving new scan results or hitting a timeout. Sites are removed from the scan
queue after processing all scan results for the site. The loop terminates when
the site queue is empty.

Scan progress is recorded throughout the procedure and returned via the
`thrift::NetworkTopologyScanStatus` structure. This data persists in memory
until another scan is started.

#### Scan Parameters
Scans are initiated via a `StartNetworkTopologyScan` request. The only required
parameter is the list of all *site links*, which `TopologyBuilder` uses to build
its *site queue* (of `SiteQueueEntry` entries).

The other tunable parameters are described below:
* **MAC addresses** - If provided, only these specific MAC addresses may be
  added to the topology, and other radios ignored. This is primarily used for
  testing purposes.
* **CN sites** - All nodes added on the given sites will be labeled as CNs
  (otherwise DNs).
* **Y-street sites** - DN radios on the given sites will be allowed to form two
  DN-to-DN links (otherwise not allowed).
* **Beam angle penalty** - A coefficient penalizing high beam angles at the
  transmitter or receiver when deciding the best link to form, because
  properly-aligned P2P links should normally be close to boresight. This penalty
  is not applied to P2MP sites, since the beam angle should *average* to zero
  degrees across all links of a P2MP radio. The default penalty of *0.1* would
  penalize a combined beam angle (i.e. sum of the absolute values of transmit
  and receive angles) of 45 degrees by an SNR of 4.5dB.
* **Distance threshold** - The maximum distance, in meters, to allow between a
  responder's reported GPS position and the nearest site in order to compensate
  for GPS inaccuracies. Nodes located further away are ignored. The default
  distance threshold is *50 meters*.
* **SNR threshold** - The minimum signal-to-noise ratio, in decibels, to allow
  on new links. The default SNR threshold of *6.1dB* is the minimum needed to
  support MCS2 at a packet error rate of 1e-3.
* **Scans per node** - A scan request can be issued to each radio multiple times
  to increase the likelihood of picking up all responders.

#### Scan Loop
The scan loop determines the next action to perform based on the current site
queue state. It returns an `Action` structure encapsulating the results for
`TopologyBuilderApp` to then execute.

Starting at the site at the head of the queue, the loop proceeds as follows:
* Find any DN left to scan on the site. The DN must be alive; to account for
  configuration-related reboots, the controller must have received a status
  report at least 3 seconds after a configuration change was pushed to the node.
    * If any valid DN was found, send it a topology scan request and wait for a
      response.
        * Upon receiving scan results, store them and invoke the loop again.
        * Upon a timeout, invoke the loop again.
    * If no valid DN was found, check the following cases:
        * If no scan results have been received so far, push the site to the
          bottom of the queue and continue (a node may come online later).
        * If any DN left to scan on the site was recently online (within 90
          seconds), wait because it may come back online soon (e.g. rebooting).
        * Otherwise, process the scan results for the site (see below). If all
          site links have formed, remove the site from the queue; otherwise,
          re-add the site to the bottom of the queue.
* Upon reaching the end of the queue, terminate if the queue is empty, or wait
  5 seconds if any sites remain.

#### Processing Results
After collecting all scan results for a site, responders are filtered out
through any of the following conditions:
* The responder didn't report a GPS location (possibly a transient issue).
* No site link should form between the current site and the responder's site.
* The responder's MAC address did not match the input filter, if any.
* The responder does not meet distance or SNR thresholds.
* The responder is already part of the topology *and* already has the maximum
  number of links defined.
* The responder is another radio on the node that initiated the scan.
* The responder is already part of a different site (due to past or present GPS
  errors).

The remaining responders are grouped by their nearest site; site links are added
in a greedy manner, where *link quality* is defined as follows:
```
link quality := SNR - (beam angle penalty * combined beam angle)
```

The chosen responders, their wired adjacencies (if applicable), and the
associated wireless links are added to the topology via a `BULK_ADD` request to
`TopologyApp`.

## Link Discovery
Link discovery scans aim to find the best possible link(s) from an existing node
to a newly-installed node, issuing topology scans from each DN within a given
radius. This is implemented using most of the same logic as topology discovery
scans, but as only a single step (i.e. only the initial set of sites is
enqueued).

The following user operations are available for managing link discovery scans:

| User Operation | Command                            |
| -------------- | ---------------------------------- |
| Start Scans    | `START_LINK_DISCOVERY_SCAN`        |
| Stop Scans     | `STOP_NETWORK_TOPOLOGY_SCAN`       |
| Get Status     | `GET_LINK_DISCOVERY_SCAN_STATUS`   |

The output is a list of all possible links found; no topology changes are made
automatically.

## Continuous Topology Scan
Continuous topology scans can be used for physically aligning a responder node.
The purpose of the topology scan is to trigger a beamforming procedure on the
responder at a sweep rate of roughly 2 cycles per second (depending on firmware
parameters).

The "continuous" scan is implemented by running regular scans back-to-back,
scheduling them about 4 seconds in advance
(`FLAGS_continuous_topo_scan_start_time_offset_s`) and about 1.8s apart (as
computed with `ibfNumberOfBeams=31`, or overridden via
`FLAGS_continuous_topo_scan_bwgd_delta`). In this example, there would be a
cycle consisting of ~1.5s of sweeping followed by a ~300ms gap.

The following user operations are available for managing continuous topology
scans:

| User Operation   | Command                            |
| ---------------- | ---------------------------------- |
| Start/Stop Scans | `START_CONTINUOUS_TOPO_SCAN`       |

An ongoing scan can be interrupted by sending another request with a duration of
zero.

### Radio Alignment Using Continuous Topology Scans
A continuous topology scan will continuously sweep through all (Tx, Rx) beam
combinations between the topology scan initiator (Tx beam sweep) and topology
scan responder(s) (Rx beam sweep). The beamforming stats (`TGF_STATS_BF`) at the
responder(s) can then be inspected to give quick alignment feedback, providing
higher layers with SNR/RSSI measurements for every packet detected. Note that
only a few of the (Tx, Rx) beam combinations detected at the responder(s) are
sent back to the initiator, so in general the information available at the
initiator is not enough to use for a robust alignment procedure.

Beamforming stats at each responder node contain the full heatmap of (Tx, Rx)
beam combinations, which can be used to determine the current angle of arrival
at the responder. As a simple example, if the (Tx, Rx) beam combination with
highest SNR at the responder corresponds to an Rx angle of +10 degrees relative
boresight, an installer could be instructed to rotate the responder node by +10
degrees so that the responder's boresight is aligned to the line-of-sight path
to the initiator node. The beamforming stats can optionally be post-processed to
improve robustness, for example by averaging across packets in the time and/or
spatial domain, or by adding logic to distinguish reflections from the
line-of-sight path.

Note that the host processor and software stack must be able to consume the high
volume of `TGF_STATS_BF` stats generated by firmware to avoid loss. This can be
on the order of thousands of samples per second. Refer to
[Firmware Stats](Firmware_Stats.md) for a full list of available beamforming
stats.
