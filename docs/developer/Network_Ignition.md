# Network Ignition
This document describes the link bring-up procedure, or "ignition", at the E2E
layer.

## Controller Ignition Management
The controller's `IgnitionApp` is responsible for managing the ignition
procedure. Ignition involves forming a link from an "initiator" node, which is
already connected to the controller in this case, to a "responder" node.

### Node Selection
`IgnitionApp` will automatically ignite links during network startup and
whenever nodes or links subsequently fail. The app periodically runs a procedure
to select nodes to ignite (every 5 seconds), using the current topology state to
determine the ignition order; multiple links can be ignited in parallel. This
algorithm is defined in `IgnitionAppUtil`, and is described below.

1. Determine the set of "candidate" links to ignite, comprised of all offline
   links emanating from all currently-reachable nodes. Additionally, the
   initiator DN on a candidate link must be time-synchronized, i.e. in the
   `ONLINE_INITIATOR` state (refer to
   [Topology Management](Topology_Management.md) for further details). Backup
   links to CNs are not tried until a minimum time has elapsed (*5 minutes*, via
   controller flag `--linkup_backup_cn_link_interval`) since any link to a
   particular CN could have been considered a candidate.
2. An initiator node must wait for the current ignition attempt to finish, fail,
   or time out before accepting another request. On P2MP (point to multi-point)
   initiator nodes, wait until any response is received (up to *16 seconds*, or
   controller config `ignitionParams.bfTimeoutSec`) before attempting ignition
   again for any link.
3. Group candidates by link name. For links that can be ignited in both
   directions, pick the initiator node randomly and push the other candidate
   node to the end of the list (in case the first node gets filtered out in a
   subsequent step).
4. Each node can only have one link ignited at a time, so filter out any
   candidate links to the same responder node.
5. Ignition attempts across the same link are dampened (once per *10 seconds*,
   or controller config `ignitionParams.linkUpDampenInterval`); filter out any
   candidate links that had ignition attempts within the dampening interval.
   Note that this timeout may get cancelled when the controller receives any
   link status event for the link (depending on controller flag
   `--linkup_ignore_dampen_interval_after_resp`). If the same link fails to
   ignite for an extended period (*30 minutes*, via controller flag
   `--linkup_extended_dampen_failure_interval`), the dampening interval is
   increased (to once per *5 minutes*, via controller flag
   `--linkup_extended_dampen_interval`) in order to reduce possible interference
   to nearby nodes as well as loss of available link bandwidth caused by the
   association process.

The controller ignition algorithm has the following limitations:
* While Terragraph firmware supports time propagation over *two hops* to a CN,
  the controller only allows ignition from time-synchronized DNs. For example,
  the following case is unsupported:
  `DN1 (ONLINE_INITIATOR) -> DN2 (ONLINE) -> CN`
* Each initiator node can only be scheduled once per ignition cycle. On
  multi-sector DNs, ignition will *not* be performed in parallel on different
  sectors, even if this may be feasible depending on interference constraints.

### Slot Exclusion
During ignition, the initiator node sweeps its transmit beam in all directions,
which can create interference to nearby nodes. To avoid such interference
potentially disrupting surrounding links, `BF_SLOT_EXCLUSION_REQ` commands may
be sent to nodes in close proximity to an initiator, which instructs them to not
use BF slots for the duration of the IBF procedure. This achieves greater link
stability at the cost of reduced maximum throughput (due to reserved BF slots).

The IBF slot exclusion feature is disabled by default, and can be enabled via
the controller flag `--enable_linkup_slot_exclusion`.

The slot exclusion set will include a node/radio if:
1. It doesn't have any links being ignited in the current ignition cycle
2. It has polarity opposite of the initiator
3. It uses the same wireless channel as the initiator
4. At least one of the following conditions are met:
    * Node is within a short distance of an initiator node (*50 meters*, or
      controller flag `--linkup_exclusion_distance_short`)
    * Node is within a longer distance of an initiator node (*350 meters*, or
      controller flag `--linkup_exclusion_distance_long`) and has one or more
      links with receive beam pointing towards the initiator (relative angle
      less than *5 degrees*, or controller flag `--linkup_exclusion_angle_rel`)

For DNs, the `BF_SLOT_EXCLUSION_REQ` is sent to the node subject to interference
from the initiator, whereas for CNs the command is sent to the parent DN.

### Message Exchange
At this point, the controller has determined all links to ignite along with
their initiator nodes and any nearby links that require slot exclusion to avoid
interference. `IgnitionApp` issues a series of commands to minions, which are
listed below.

1. Send `BF_RESP_SCAN` to any DNs that should now become responders.
2. Send `BF_SLOT_EXCLUSION_REQ` to any DN subject to interference that should
   avoid using BF slots during this ignition cycle.
3. Wait for the previous messages to propagate (1 second).
4. Send `SET_LINK_STATUS` to all initiator nodes, with any required firmware
   parameters (e.g. polarity, control superframe, channel, Golay index).

The `BF_RESP_SCAN` message instructs nodes to send a `FW_BF_RESP_SCAN` (ON)
message to the driver, setting the "BF responder mode" config which schedules
the BF slot in the slotmap. Before the next ignition cycle, nodes will send a
`FW_BF_RESP_SCAN` (OFF) message to the driver to disable responder mode.

Responder mode reserves the Rx BF slots, reducing maximum throughput by roughly
50%. Each radio starts in responder mode, and goes back into responder mode if
all links have gone down or whenever a DN-to-DN link has gone down. Note that on
P2MP DNs, this may cause the radio to go into responder mode regardless of
whether there are other links currently up (and causing a throughput drop on
those links). Nodes disable responder mode automatically once any link is
formed. The controller does not need to send these commands to CNs because they
can only form one link.

### Management
`IgnitionApp` normally performs network-wide ignition automatically, but accepts
the following commands for manual management:

* `SET_LINK_STATUS_REQ` - Bring any link up or down via the normal association
  and disassociation procedures, respectively.
* `FORCE_DISSOC_REQ` - Bring down a link forcefully, used when the link may not
  exist in the current topology (e.g. deleted nodes/links).
* `SET_IGNITION_PARAMS` - Set ignition parameters, including the time interval
  to run the ignition procedure, the dampening interval, and whether automatic
  ignition should be disabled network-wide or on any specific links. Note that
  these parameters are not persisted, and will be lost if the controller
  reboots.
* `GET_IGNITION_STATE` - Retrieve the current ignition parameters, along with
  debug information about the node selection algorithm.

Note that `IgnitionApp` is always responsible for instructing nodes to report
their link status to `TopologyApp`. If automatic ignition is enabled, this will
happen as a result of the `SET_LINK_STATUS` commands. If disabled, then the app
manually sends out `GET_LINK_STATUS` commands.

## Minion Ignition Procedure
The minion's `IgnitionApp` accepts commands from the controller and exchanges
ignition-related messages with the driver to associate or disassociate with a
neighboring node.

### Overview
The ignition procedure begins when the minion receives a `SET_LINK_STATUS`
command from the controller's `IgnitionApp`, and normally ends with a
`LINK_STATUS` notification to the controller's `TopologyApp` indicating the
status of the link. If the link has not come up after a timeout interval (15
seconds, or `FLAGS_linkup_resp_wait_timeout_s`), the ignition attempt is
abandoned and no `LINK_STATUS` message is sent.

Only one neighbor can be ignited at a time. The minion starts the process by
sending a `DR_SET_LINK_STATUS` command to the driver, which attempts to form a
link using the parameters received from the controller (e.g. polarity, control
superframe, Golay index). Afterwards, the driver will pass a `DR_LINK_STATUS`
message back to the minion indicating the link status.

<a id="network-ignition-link-layer-security"></a>

### Link-Layer Security
When link-layer encryption ("wsec") is enabled on the minion, `IgnitionApp` also
becomes responsible for managing two additional Linux processes for each
wireless interface: an "authenticator" (`hostapd`) on the initiator node and a
"supplicant" (`wpa_supplicant`) on the responder node. Logs for these processes
are written to `/tmp/hostapd_<iface>` and `/var/log/wpa_supplicant.log`,
respectively, with logging verbosity determined by the `wsecParams` node
configuration field. These processes cannot both be enabled on the same
interface. The current implementation uses a global `wpa_supplicant` process but
spawns a separate `hostapd` process for each interface due to limitations in
`hostapd` software.

Terragraph nodes support two wsec authentication mechanisms:
1. WPA-PSK (`wsecEnable = 1`)
2. WPA-Enterprise / 802.1X (`wsecEnable = 2`)

Each authentication mechanism uses a different configuration template for the
authenticator and supplicant. Under 802.1X, some configurations are populated
from the node configuration, and confidential fields (e.g. passwords) are pulled
from device-specific secure storage and submitted via `hostapd_cli` and
`wpa_cli`.

The minion assumes a responder role on startup and runs the supplicant for every
interface. When a minion becomes the initiator for a link, it must remove the
link's interface from the supplicant and start the authenticator. To do so, the
minion first requests the interface name by sending a `DR_DEV_ALLOC_REQ` request
to the driver; it receives a `DR_DEV_ALLOC_RES` response. If association is
successful, then the authenticator will continue to run on this interface. If
the ignition times out or the link goes down, then the minion returns to its
initial state by killing the authenticator and re-adding the interface to the
supplicant. The responder node should also reset its supplicant interface state
as a safety measure. Between stopping one service and starting the other, the
minion must wait for the interface to reset and become available for use again;
this is a static duration which may need to be tuned based on the host hardware
via the flag `--wsec_stop_delay_ms` (default 200ms).

After association, the minion must wait for the secure port to be set to the
"authorized" state following successful key negotiation. This is signaled by the
`DR_WSEC_LINKUP_STATUS` event from the driver, after which the minion will
forward a corresponding `LINK_AUTHORIZED` notification to the controller. While
waiting for key negotiation, the minion must not ignite other links.
Additionally, no other authenticator or supplicant processes should be started
or stopped; this means that actions for all `LINK_DOWN` messages on other
interfaces should be queued for after the key negotiation (or failure/timeout).

For CNs, because only one link can be formed, the minion will remove all other
interfaces from the supplicant after a link successfully associates (i.e.
reaches the "authorized" state). On a `LINK_DOWN` event, the minion must then
restart supplicants on every interface.

After a `LINK_UP` event, the driver sends `DR_WSEC_STATUS` to the minion when
wsec is disabled (in place of `DR_WSEC_LINKUP_STATUS`). This event is ignored by
the minion. Previously, the message was part of an "open mode fallback" feature
to allow an open connection even when operating in secure mode, where the minion
would kill any supplicant or authenticator on the given interface to allow the
connection to complete. This fallback feature is no longer supported in
firmware.

#### WPA-PSK
The default WPA-PSK passphrase (i.e. 8-63 ASCII characters) can be changed via
the node configuration field
`radioParamsBase.wsecParams.wpaPskParams.wpa_passphrase`. In addition to the
default passphrase, link-specific passphrases can also be set via the field
`radioParamsBase.wsecParams.wpaPskParams.wpa_passphrase_override`, which maps
from neighbor radio MAC addresses to passphrases.

#### 802.1X
802.1X requires a RADIUS server to be present in the same network to
authenticate requests from the authenticator and supplicant. It also requires
certificates to be provisioned to the nodes and to the RADIUS server by the same
certificate authority (CA). Nodes support the EAP-TLS protocol.

<p align="center">
  <img src="../media/figures/802.1X.svg" width="720" />
</p>

The authentication process is as follows:
1. The supplicant initiates the process with the authenticator using EAPoL.
2. The authenticator creates a UDP-based TLS tunnel with the RADIUS server using
   a shared secret, and forwards EAPoL messages from the supplicant to the
   RADIUS server in this tunnel.
3. The supplicant now creates a secure session with the RADIUS server using a
   shared client username and password.
4. Once the communication is secured, the supplicant sends the pre-provisioned
   device certificate to the RADIUS server.
5. The RADIUS server verifies the certificate and sends an "Accept" or "Reject"
   indication (in the UDP datagram header).
6. Upon seeing "Accept", the authenticator proceeds with the creation of an
   encrypted link between the nodes.

Parameters associated with 802.1X reside in the node configuration structure
`eapolParams`.

### Message Exchange
The order of the ignition-related messages during association is shown below.
Steps marked with (\*) are only applicable in secure mode.

#### Initiator
1. `SET_LINK_STATUS` (`LINK_UP`) -
   Request from the controller's `IgnitionApp` to begin igniting a link.
2. (\*) `DR_DEV_ALLOC_REQ` -
   Request to the driver for the link's interface name.
3. (\*) `DR_DEV_ALLOC_RES` -
   Response from the driver with the link's interface name, at which point the
   minion removes the interface from the supplicant and starts the
   authenticator.
4. `DR_SET_LINK_STATUS` -
   Request to the driver to begin igniting a link with the given parameters.
5. `DR_LINK_STATUS` (`LINK_UP`) -
   Event from the driver that the association completed and the link is up.
6. (\*) `DR_WSEC_LINKUP_STATUS` -
   Event from the driver that the secure port is authorized. If operating in
   open mode, `DR_WSEC_STATUS` will be received instead.

#### Responder
1. `DR_LINK_STATUS` (`LINK_UP`) -
   Event from the driver that the association completed and the link is up.
2. (\*) `DR_WSEC_LINKUP_STATUS` -
   Event from the driver that the secure port is authorized. If operating in
   open mode, `DR_WSEC_STATUS` will be received instead.

#### Other Messages
Some other common ignition-related messages are described below.
* `DR_LINK_STATUS` (`LINK_DOWN`) -
  Event from the driver that a link went down, including the cause (see
  `thrift::LinkDownCause`).
* `DR_LINK_STATUS` (`LINK_PAUSE`) -
  Event from the driver that either end of a link is impaired (i.e. LSM state
  `LINK_UP_DATADOWN`). In this transient state, the link is unusable for data
  but is still able to pass link-level heartbeats. The "fast link impairment"
  algorithm will send another `LINK_DOWN` event, this time with cause
  `HB_KA_LOSS_DETECTED`, if this condition persists, or transition to
  `LINK_UP` if it resolves. The minion treats impaired links in the
  `LINK_PAUSE` state as alive, but also brings down links that have been
  impaired for too long (`FLAGS_link_pause_dissoc_delay_s`).
* `SET_LINK_STATUS` (`LINK_DOWN`) -
  Request from the controller's `IgnitionApp` to dissociate a link
  (user-triggered).
* `DR_DEV_UPDOWN_STATUS` -
  Event from the driver (forwarded from `StatusApp`) that a backhaul baseband
  device went offline (so all associated links are now also down) or has come
  back online ("up" notification may be emitted twice for unknown reasons).

## Distributed Ignition
"Distributed ignition" is a best-effort algorithm for node-initiated link
ignition, where the minion attempts to bring up links to pre-configured
neighbors until connected to the controller.

### Node Configuration
The minion's `IgnitionApp` attempts distributed ignition using a structure of
one or more (in the case of point-to-multi-point) wireless neighbors defined in
the node configuration (`topologyInfo.neighborInfo`). This structure is kept
up-to-date by the controller, usually immediately but sometimes delayed by up to
5 minutes (`FLAGS_topology_info_sync_interval`).

To improve the likelihood of success and reduce interference, the controller
assigns a coloring to each radio, and buckets ignition attempts into different
time slots based on colors. The total number of colors is defined by the
even-number constant `ConfigHelper::kNumColors`. Radios at the same site should
avoid igniting simultaneously, and radios on opposite ends of a link should also
avoid igniting simultaneously. These constraints match the polarity assignment
constraints, so color assignments are based on radio polarity. The color
assignment algorithm is defined in `ConfigHelper`, and is described below.

1. Divide `kNumColors` by 2 to determine the number of colors available for each
   polarity (with software-hybrid polarity treated as non-hybrid). Radios with
   different polarities will always receive different colors, but radios with
   the same polarity may receive the same color for small `kNumColors`.
2. For each site in the topology, assign colors as follows:
    1. Compute the approximate angle of every link originating from each radio
       at the site.
    2. Sort the radios based on link angle.
    3. Assign colors to each radio based on its polarity and an offset based on
       the iteration index (i.e. alternating colors in the order of link angle).
       This aims to reduce same-site interference by picking different colors
       for adjacent radios. For example, on a site with 4 evenly-spaced
       odd-polarity radios and `kNumColors = 4`, radios 90 degrees apart will
       have different colors and radios 180 degrees apart will have the same
       color.

### Distributed Ignition Procedure
The minion's `IgnitionApp` manages the distributed ignition procedure via a
5-second loop (`FLAGS_distributed_ignition_cooldown_duration_ms`). The feature
is disabled if neighbor information is missing, or if switched off in the node
configuration (`sysParams.distributedIgnitionEnabled`).

#### Triggers
1. `GPS_ENABLED` - Event from the driver (forwarded by the minion's `StatusApp`)
   that the GPS module is initialized. Ignition requires GPS-based time
   synchronization, so distributed ignition is only enabled after receiving this
   message.
2. `CONTROLLER_CONNECTED` - Event from the minion's `StatusApp` that
   the minion is now connected to the controller, which disables the distributed
   ignition feature until the minion process restarts.

Note that while distributed ignition is active, the minion will automatically
enable responder mode on a radio after receiving any `LINK_UP` event, ensuring
that links can still be initiated to it if it serves as a P2MP DN in the
topology. After connecting to the controller, the minion will switch responder
mode off on all radios.

#### Time Synchronization
Ignition attempts are bucketed into time slots based on each node's GPS clock,
ensuring that all ignition attempts are properly time-synchronized to avoid
collisions. The ignition algorithm contains configurable parameters to modify
the length of ignition slots and the acceptable time bounds where ignition can
still occur before or after a bucket boundary.

For example, consider a cooldown duration of 5 seconds, a maximum offset of 1
second (i.e. highest permitted timing error in either direction), and
`kNumColors = 4`. The 4 buckets are distributed into a 20-second time range as
follows:

* Bucket #1 starts at *t* seconds (+/- 1 second)
* Bucket #2 starts at *t+5* seconds (+/- 1 second)
* Bucket #3 starts at *t+10* seconds (+/- 1 second)
* Bucket #4 starts at *t+15* seconds (+/- 1 second)

This pattern repeats every 20-second interval on all nodes, based on the current
GPS time modulo 20 seconds.

#### Backoff
To avoid repeated ignition failures for any given link, attempts are subject to
exponential backoff as follows:

* On the initial ignition failure, set the cooldown time to the total bucket
  time range (as described above).
* On each subsequent ignition attempt for the same link, double the cooldown
  duration.
* Randomly delay ignition attempts by an additional cycle (25% of the time).
  This adds some jitter to prevent repeated synchronized collisions.

Backoff is stored per-link. To achieve quicker ignition, 3 link ignition
attempts are allowed before beginning the backoff procedure
(`FLAGS_distributed_ignition_attempts_before_backoff`). After 18 ignition
attempts, link ignition stops (`FLAGS_distributed_ignition_max_attempts`).

#### Ignition Algorithm
The minion's distributed ignition algorithm is described below.

1. Determine the time until the next ignition bucket, and schedule a timer to
   fire at that time.
2. When the timer fires, if the current time does not fall within the maximum
   allowed offset for a synchronized attempt slot (i.e. too far from a bucket
   boundary), skip this interval.
3. Calculate the bucket index based on the current time and find a viable
   candidate link. Ignore any links already alive, any links for which the
   backoff timer has not expired, as well as any links with a color not matching
   the current bucket.
4. Increment the link iteration index. Links from a site are considered in a
   round robin fashion. For example, P2MP radios will attempt to ignite each
   link once before revisiting previously attempted links.

## Resources
* [hostapd and wpa_supplicant] - IEEE 802.11* wireless security processes

[hostapd and wpa_supplicant]: https://w1.fi/
