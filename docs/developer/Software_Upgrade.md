# Software Upgrade
This document describes the in-band software upgrade procedure for Terragraph
nodes.

## Upgrade Stages
The controller's `UpgradeApp` manages in-band software upgrades. An upgrade is
divided into two stages:
1. **Prepare**: A new software image is distributed to nodes in the network.
   Each minion downloads the image, verifies its integrity, and flashes it to
   its secondary disk partition.
2. **Commit**: Nodes are instructed to switch to the new software image. Each
   minion swaps its primary and secondary disk partitions and performs a full
   reboot. This stage is disruptive, and may affect other elements in the
   network.

## Parallelization
Within each upgrade stage, commands are executed across multiple nodes in
parallel. The specifics are given in the sections below.

### "Prepare" Stage
Software images are distributed over BitTorrent by default. The controller seeds
each image through its own BitTorrent tracker, and sends the magnet URIs to each
minion to download. Alternatively, images can be distributed over HTTP or HTTPS
by sending the web URL instead; the images must be hosted via an external HTTP
server (ex. nginx in the default Docker Swarm deployment).

The distribution of software images is fully parallelizable, with the only
concern being high bandwidth utilization. This can be limited by setting speed
limits in each BitTorrent client, or by limiting the number of nodes downloading
the image at any point in time ("batching").

When using BitTorrent, both the controller and minion will publish the following
stats during the "prepare" stage:

| Key                               | Description |
| --------------------------------- | ----------- |
| `upgrade.bt.progressPpm`          | The torrent download progress in parts per million, i.e. `[0,1000000]` |
| `upgrade.bt.seeds`                | The number of connected peers that are seeding the torrent |
| `upgrade.bt.peers`                | The number of connected peers |
| `upgrade.bt.connections`          | The number of peer connections, including half-open connections |
| `upgrade.bt.downloadBps`          | The total download rate for all peers (in bps) |
| `upgrade.bt.uploadBps`            | The total upload rate for all peers (in bps) |
| `upgrade.bt.downloadPayloadBytes` | The numer of payload bytes received during this session (ignoring protocol overhead) |
| `upgrade.bt.uploadPayloadBytes`   | The numer of payload bytes sent during this session (ignoring protocol overhead) |

### "Commit" Stage
The commit of new images is more difficult to parallelize, as nodes must reboot
to apply the new image. A reboot brings down all of a node's wireless and
Ethernet links, potentially disconnecting parts of the network ("network
isolation"). A bad sequence of reboots could repeatedly isolate the same
portions of the network.

The commit parallelization algorithm is designed as follows:
1. Avoid network isolation. If upgrading a node must cause network
   isolation (due to poor network design), then upgrade the isolated portion of
   the network at the same time.
2. Upgrade entire sites at once. This simplifies the graph logic, and aligns
   with future DN hardware (i.e. single nodes with multiple radios).
3. Commit sites that are one hop away from another reachable site (when
   possible). This reduces the amount of time required to re-ignite sites.

The parallelization algorithm is a two-step procedure, and contained within
`GraphHelper`. The steps are described below.

#### Step 1: Identify Articulation Points
In a connected graph, a vertex is called an *[articulation point]* (AP) if
removing it would result in a disconnected graph. In the commit algorithm, APs
are the sites that will isolate portions of the network if taken down or removed
from the topology.

In the example below, site F is identified as an AP because taking it down would
isolate sites H and G.

<p align="center">
  <img src="../media/figures/upgrade_ap.svg" width="750" />
</p>

After identifying all APs, the list is filtered further to remove any "sub-APs"
(i.e. APs isolated by other APs). Each AP forms an "AP group", consisting of
itself and all sites it isolates. All sites in an AP group will be updated
simultaneously in the same batch.

In the previous example, sites F, H, and G form an AP group and are treated as a
single virtual site.

#### Step 2: Select Leaf Sites
Once all AP groups are identified, each site or AP group is now accessible via
multiple paths. The graph is then converted into a spanning tree using a custom
spanning tree decomposition algorithm, which is a modified DFS with a bias
toward previously-upgraded sites. This will influence previously-upgraded sites
to form the core of the tree, rather than the edge.

Leaf sites in the resulting spanning tree can be upgraded simultaneously (in a
"batch") without causing any disruption to other sites in the spanning tree. The
spanning tree algorithm is repeated until each site is picked once, resulting in
several "batches".

Using the same example from above, the following illustration shows this step of
the algorithm, which requires three iterations to perform a full network
upgrade.

<p align="center">
  <img src="../media/figures/upgrade_tree.svg" width="750" />
</p>

The controller sends commit requests to all nodes in an upgrade batch. Once the
operation is complete, the two-step algorithm is run again to identify the next
batch. The algorithm always uses the current network state when computing the
next batch, accounting for any topology changes that might have occurred between
steps.

## Mixed Hardware Upgrades
Different node hardware may require different software images; the list of
supported hardware board IDs is written in each image's metadata section
(`thrift::ImageMeta::hardwareBoardIds`). The following procedure is used to
upgrade a network with mixed node hardware:
1. Issue a "prepare" request for each software image.
2. Issue a single "commit" request for all nodes.

## Golden Images
The controller can be configured to automatically upgrade nodes to a "golden
image" through the controller configuration field `upgradeParams.goldenImage`.
When enabled, the controller periodically looks for nodes running software
versions older than the golden image (by comparing major/minor numbers only)
and schedules "prepare" and "commit" procedures back-to-back for these nodes.

## Message Interface
All controller messages related to upgrades are described below.

### Upgrade Procedure
The following commands address the upgrade procedure itself:

| User Operation          | Command                   |
| ----------------------- | ------------------------- |
| Send Upgrade Request    | `UPGRADE_GROUP_REQ`       |
| Abort Upgrade           | `UPGRADE_ABORT_REQ`       |
| Get Upgrade State       | `UPGRADE_STATE_REQ`       |
| Get Upgrade Commit Plan | `UPGRADE_COMMIT_PLAN_REQ` |

All types of upgrades are initiated via the `UpgradeGroupReq` structure, which
contains the nodes to upgrade and a nested `UpgradeReq` structure that defines
the actual upgrade procedure. The type of upgrade is determined by the
`thrift::UpgradeReqType` enum:
* `PREPARE_UPGRADE` - The "prepare" stage (described above).
* `COMMIT_UPGRADE` - The "commit" stage (described above).
* `FULL_UPGRADE` - Execute the "prepare" and "commit" stages back-to-back.
* `RESET_STATUS` - Reset each node's local upgrade state, and cancel any ongoing
  image downloads or scheduled commits.

All upgrade requests are forwarded from the controller to minions via the
`UPGRADE_REQ` message.

### Software Image Management
The following commands manage the software images hosted and seeded by the
controller:

| User Operation       | Command                   |
| -------------------- | ------------------------- |
| Add Upgrade Image    | `UPGRADE_ADD_IMAGE_REQ`   |
| Delete Upgrade Image | `UPGRADE_DEL_IMAGE_REQ`   |
| List Upgrade Images  | `UPGRADE_LIST_IMAGES_REQ` |

The `thrift::UpgradeImage` structure contains the URIs for downloading the
software images:
* `magnetUri` - The magnet URI (i.e. for BitTorrent)
* `httpUri` - The HTTP URI (only set if HTTP serving is enabled)

## Resources
* [articulation point] - Articulation point algorithms

[articulation point]: https://www.geeksforgeeks.org/articulation-points-or-cut-vertices-in-a-graph/
