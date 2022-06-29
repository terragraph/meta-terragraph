# Prefix Allocation
This document describes Terragraph's algorithms for allocating prefixes to
nodes. Network prefix validation and allocation is handled by either
`CentralizedPrefixAllocator` or `DeterministicPrefixAllocator`.

## Background
Terragraph networks are given an IPv6 "seed prefix" (e.g.
`face:b00c:cafe:ba00::/56`) from which subnet prefixes are allocated to all DNs
and CNs. There are three different methods for allocating node prefixes with
Open/R, described briefly below. For more details, refer to the Open/R
documentation for [openr::PrefixAllocator].
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

Both centralized and deterministic prefix allocation require initial parameters
to be provided through the `thrift::PrefixAllocParams` structure in the
controller config file:

```c
struct PrefixAllocParams {
  1: string seedPrefix;
  2: i32 allocPrefixLen;
}
```

`seedPrefix`, the prefix of the entire Terragraph network, is given in [CIDR]
notation. `allocPrefixLen` specifies the bit-length of prefixes allocated to
each node. If these parameters are not provided in the controller config, the
controller will automatically revert to distributed prefix allocation.

Terragraph currently takes the *centralized* approach.

## Centralized Prefix Allocation (CPA)
### Allocation
Given a valid `thrift::PrefixAllocParams` struct, the controller is able to
allocate prefixes for all the nodes in the network automatically. However,
individual prefixes can also be manually defined through the `prefix` field in
each `thrift::Node` entry.

The allocation steps are as follows:

* On controller startup:
    * If the topology file specifies any node prefixes, mark them as taken.
    * For all nodes without prefixes, allocate one.
* When adding a node:
    * If the prefix is unspecified, allocate one.

Allocation is performed by iterating sequentially through all possible prefixes
and assigning the first unallocated prefix. For example, if allocating `/64`
prefixes for nodes from the seed prefix `face:b00c:cafe:ba00::/56`, the
controller would assign prefixes in the following order:
```
face:b00c:cafe:ba00::/64
face:b00c:cafe:ba01::/64
...
face:b00c:cafe:baff::/64
```

`TopologyApp` computes prefixes and periodically sends a `SET_PREFIXES` command
to any reachable POP node's `OpenrClientApp`. If any prefixes changed, the
minion injects the new prefixes into Open/R using the `KvStore` key
`e2e-network-allocations`.

### Validation
Checks:
* All prefixes (seed prefix and node prefixes) must be valid IPv6 prefixes in
  CIDR notation.
* Node prefixes must be contained within the subnet of the seed prefix.
* Node prefixes must be of the specified bit-length (`allocPrefixLen`).

Constraints:
* Node prefixes must be unique within the network topology.

## Deterministic Prefix Allocation (DPA)
The CPA allocation is somewhat arbitrary. That is, no subnetting of the seed
prefix occurs and each POP node is only able to advertise the entire seed prefix
to BGP peers, rather than a more specific subnet. This makes it possible for
ingress traffic to enter the network through a suboptimal POP node.

Deterministic Prefix Allocation (DPA) assigns prefixes to nodes based on a given
topology to allow POP nodes to take advantage of route summarization and help
load balance ingress traffic. DPA attempts to segment a TG network so that each
POP site can advertise its own specific BGP routes. Ingress traffic will then
automatically flow through the optimal POP, instead of tromboning between POPs.

<p align="center">
  <img src="../media/figures/dpa_prefix_zones.jpg" width="650" />
</p>

Terminology:

* **Prefix zone** - A group of nodes to their closest POP site (using wireless
  hop count as the distance metric).
* **Prefix space** - The number of prefixes with length `allocPrefixLen`
  within a prefix. For example, `face:b00c:cafe:ba00::/56` has a prefix space of
  256 (there are 256 unique /64s in a /56). Prefix spaces are always powers of 2
  and there is a direct translation from prefix length to prefix space:
  ```
  prefix_space = 2^(allocPrefixLen - prefix_length)
  ```
* **Zone prefix** - A prefix assigned to a prefix zone. Zone prefixes are used
  to allocate node prefixes.
* **Node prefix** - A prefix with length `allocPrefixLen` that can be assigned
  to a node.

### Segmentation Into Prefix Zones
DPA begins by segmenting the network into prefix zones (one per POP site). This
is accomplished by running a BFS algorithm starting from POP sites to find the
closest POP site, in terms of hop count, to each node. Nodes closest to a
specific POP site form a prefix zone.

### Allocate Prefix Spaces
Each prefix zone is allocated a number of prefixes so that the sum of their
prefix spaces is at least as large as the number of nodes in the zone.

The recursive algorithm in pseudocode (Python) is as follows:

```Python
def allocate_prefix_spaces(zones, network_prefix_space):
    unsatisfied_zones = zones.get_unsatisfied_zones()
    prefix_space_needed = 0
    for zone in zones:
        prefix_space_needed += zone.get_unsatisfied_node_count()

    # Not enough network prefix space, so no solution.
    if network_prefix_space < prefix_space_needed:
         return False

    # Used up all of the network_prefix_space, if no more prefix space is
    # needed, then a valid solution was found, otherwise no solution.
    if network_prefix_space == 0:
        return prefix_space_needed == network_prefix_space

    prefix_space_divisor = next_power_of_2(num_unsatisfied_zones)
    while True:
        remaining = network_prefix_space
        prefix_space = remaining / prefix_space_divisor

        # For each unsatisfied zone, add a prefix_space to it.
        for zone in unsatisfied_zones:
            zone.add_prefix_space(prefix_space)
            remaining -= prefix_space

        # Recurse with the remaining prefix space, if this results in a valid
        # solution, return. Otherwise, double the divisor and repeat.
        if allocate_prefix_spaces(zones, remaining):
            return True

        prefix_space_divisor *= 2
```

The recursive algorithm is best explained with an example. Assume the following
set up (based off of TowerG data):

* 3 prefix zones
    * Prefix zone A with 100 nodes
    * Prefix zone B with  68 nodes
    * Prefix zone C with  20 nodes
* seedPrefix: `face:b00c:cafe:ba00::/56`
* allocPrefixLen: 64
* Network prefix space: 256

Since the seedPrefix is a `/56`, the seedPrefix has a prefix space of 256.
In other words, this network can support up to 256 nodes, since each requires a
`/64`.

The algorithm first tries to equally divide the network prefix space to each
prefix zone. Since prefix spaces are always powers of 2, divide the the network
prefix space by 4 (since we have 3 prefix zones). This results in 4 prefix
spaces of size 64 which are assigned to the three prefix zones and with a 64
left over.

| Prefix zone A (100) | Prefix zone B (68) | Prefix zone C (20) | Remaining |
| ------------------- | ------------------ | ------------------ | --------- |
| 64                  | 64                 | 64                 | 64        |

Prefix zone C is satisfied since it has only 20 nodes. Since prefix zones A and
C are not yet satisfied, recurse using the remaining prefix space of 64. Split
the the network prefix space (64) equally (divide by 2) and assign the prefix
spaces (32) to each zone:

| Prefix zone A (100) | Prefix zone B (68) | Prefix zone C (20) | Remaining |
| ------------------- | ------------------ | ------------------ | --------- |
| 64                  | 64                 | 64                 | 64        |
| 32                  | 32                 |                    | 0         |

Now, prefix zone B is satisfied, but prefix zone A is not and there is no more
remaining prefix space. The algorithm reaches a dead end, so it backtracks to
the previous state:

| Prefix zone A (100) | Prefix zone B (68) | Prefix zone C (20) | Remaining |
| ------------------- | ------------------ | ------------------ | --------- |
| 64                  | 64                 | 64                 | 64        |

This time, instead of dividing by 2, the divisor is multiplied by 2 and we
divide the remaining (64) by 4. Then, assign a prefix space (16) to each of the
two unsatisfied zones:

| Prefix zone A (100) | Prefix zone B (68) | Prefix zone C (20) | Remaining |
| ------------------- | ------------------ | ------------------ | --------- |
| 64                  | 64                 | 64                 | 64        |
| 16                  | 16                 |                    | 32        |

Now, prefix zone A is the only unsatisfied zone and it can use the rest of the
remaining prefix space.

| Prefix zone A (100) | Prefix zone B (68) | Prefix zone C (20) | Remaining |
| ------------------- | ------------------ | ------------------ | --------- |
| 64                  | 64                 | 64                 | 64        |
| 16                  | 16                 |                    | 32        |
| 32                  |                    |                    | 0         |

All prefix zones are satisfied and the entire network prefix space has
successfully been divvied up.

### Prefix spaces to prefix lengths
Next, the prefix spaces need to be converted to prefix lengths.

The prefix space to prefix length conversion is trivial:

```
prefix_length = allocPrefixLen - log_2 (prefix_space)
```

The `log_2` will work nicely since prefix spaces are restricted to powers of 2.
The prefix spaces from the previous example turn into:

| Prefix zone A (100) | Prefix zone B (68) | Prefix zone C (20) |
| ------------------- | ------------------ | ------------------ |
| /58                 | /58                | /58                |
| /60                 | /60                |                    |
| /59                 |                    | &nbsp;             |

### Zone prefixes from prefix lengths
Converting a set of prefix lengths into actual prefixes involves traversing a
binary prefix tree from shortest to longest prefix.

The prefix tree looks like the following:

<p align="center">
  <img src="../media/figures/dpa_prefix_tree.svg" width="650" />
</p>

Create a mapping from prefix length to prefix zone:

```
prefix_length_to_zone = {
    58: [A, B, C],
    59: [A],
    60: [A, B]
}
```

For each prefix length (from shortest to longest) and for each prefix zone that
requires a prefix of said length, allocate a prefix from the prefix tree.

The tree allows tracking of previously allocated prefixes. Traverse the tree
from the root, taking the left branch at each node. If an allocated prefix is
encountered, backtrack and take the right branch. Note that this only works when
prefix lengths are considered from shortest to longest.

The prefix tree concept can be replaced by a recursive function and a set
containing previously allocated prefixes. See
`DeterministicPrefixAllocator::allocateZonePrefixesHelper()`.

### Node Prefixes
After zone prefixes are allocated, node prefixes are allocated by iterating
linearly through all the zone prefixes for that node's zone. This is the same as
the currently used centralized prefix algorithm.

### Miscellaneous
During the prefix space allocation phase, multiple prefix spaces of the same
size may be allocated to the same prefix zone. These prefix spaces can be merged
into one larger space and is an implementation detail in
`PrefixZone::addToPrefixSpace()`. This also makes the entire algorithm run
significantly faster.

Currently, there is no convenient way to redistribute zone prefixes to allow for
additional nodes to be added. To mitigate this, there is
`PrefixZone::allocationSizeOverride_` which overrides the initial zone size used
for prefix space allocation. This override is configured with the controller
flag `dpa_zone_prefix_space_buffer`. It defaults to 7, so 7 additional nodes
can be added to a prefix zone before it runs out of prefix space. Additionally,
there is an API endpoint `/api/allocatePrefixes` (or the ZMQ message type
`ALLOCATE_PREFIXES`) that will redo the entire algorithm.

### Validation
Checks:
* All prefixes (seed prefix, zone prefixes and node prefixes) must be valid IPv6
  prefixes in CIDR notation.
* Node prefixes must be contained within a subnet of one of their zone's
  prefixes.
* Node prefixes must be of the specified bit-length (`allocPrefixLen`).
* Zone prefixes must be in the subnet of the seed prefix.

Constraints:
* Node prefixes must be unique within the network topology.
* Zone prefixes have enough prefix space to allocate node prefixes for all of
  the prefix zone's nodes.

## Changing Network Prefix
When using either centralized or deterministic prefix allocation, changing the
network prefix becomes a multi-step process since the controller can lose
reachability to the network when there is a mismatch between the allocated node
prefixes and the network prefix advertised in BGP.

This can be solved by advertising both the old and new network prefixes in BGP
during the transition. This can be done manually using the
`bgpParams.specificNetworkPrefixes` node config field on any POP node.

Additionally, the controller tries to automate some of these steps. The
following are the recommended steps to change the network prefix when either
centralized or deterministic prefix allocation is enabled:

1. Change the `seedPrefix` in `controller_config.json` to the new prefix.
2. Wait until the static node prefixes are updated in KvStore by running
   `breeze kvstore alloc-list` on a node in the network. Alternatively, the new
   prefix will be reflected in the nodes' IP addresses in NMS.
3. Change the `e2e-network-prefix` to the new prefix in `kvstoreParams` on the
   POP nodes.

## References
* [openr::PrefixAllocator] - Open/R's prefix allocation scheme
* [CIDR] - Classless Inter-domain Routing (RFC 4632)

[openr::PrefixAllocator]: https://github.com/facebook/openr/blob/tg/openr/docs/PrefixAllocator.md
[CIDR]: https://tools.ietf.org/html/rfc4632
