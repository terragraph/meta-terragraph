/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

namespace php CXL_Terragraph
namespace py terragraph_thrift.Topology
namespace cpp2 facebook.terragraph.thrift
namespace lua terragraph_thrift.Topology

enum NodeType {
    CN = 1,
    DN = 2,
}

enum PolarityType {
    ODD = 1,
    EVEN = 2,
    HYBRID_ODD = 3,
    HYBRID_EVEN = 4,
}

enum LinkType {
    WIRELESS = 1,
    ETHERNET = 2,
}

/**
 * @apiDefine GolayIdx_GROUP
 * @apiParam (:GolayIdx) {Int64} txGolayIdx The tx golay index
 * @apiParam (:GolayIdx) {Int64} rxGolayIdx The rx golay index
 */
struct GolayIdx {
    1: i64 txGolayIdx;
    2: i64 rxGolayIdx;
}

/**
 * @apiDefine Location_GROUP
 * @apiParam (:Location) {Double} latitude The latitude
 * @apiParam (:Location) {Double} longitude The longitude
 * @apiParam (:Location) {Double} altitude
 *                       The altitude (in meters above WGS84 ellipsoid)
 * @apiParam (:Location) {Double} accuracy=40000000
 *                       The location accuracy (in meters)
 */
struct Location {
    2: double latitude = 0;
    3: double longitude = 0;
    4: double altitude = 0;
    // default value of accuracy set to 40Mm (order of size of earth)
    5: double accuracy = 40000000;
}

/**
 * @apiDefine Site_GROUP
 * @apiParam (:Site) {String} name The site name
 * @apiParam (:Site) {Object(Location)} location The site location
 */
struct Site {
    1: string name;
    2: Location location;
}

// Typical DN's Lifecycle:
//     OFFLINE -> ONLINE (message exchange with controller)
//             -> ONLINE_INITIATOR (GPS enabled, can act as an initiator)
// Typical CN's Lifecycle:
//     OFFLINE -> ONLINE (message exchange with controller)
enum NodeStatusType {
    OFFLINE = 1,
    ONLINE = 2,
    ONLINE_INITIATOR = 3,  // node is online and can act as an initiator
}

/**
 * @apiDefine Node_GROUP
 * @apiParam (:Node) {String} name The node name
 * @apiParam (:Node) {Int(NodeType)=1,2} node_type The type of node (1=CN, 2=DN)
 * @apiParam (:Node) {String} mac_addr
 *                   The node's MAC address (can be left blank)
 * @apiParam (:Node) {String[]} wlan_mac_addrs
 *                   MAC addresses of wlan interfaces (can be empty)
 * @apiParam (:Node) {Boolean} pop_node Whether the node is connected to a POP
 * @apiParam (:Node) {Int(NodeStatusType)=1,2,3} status The node status
 *                   (1=OFFLINE, 2=ONLINE, 3=ONLINE_INITIATOR)
 * @apiParam (:Node) {String} site_name The site name
 * @apiParam (:Node) {Double} ant_azimuth The antenna azimuth
 * @apiParam (:Node) {Double} ant_elevation The antenna elevation
 * @apiParam (:Node) {Boolean} [has_cpe]
 *                   Whether the node is attached to a customer
 * @apiParam (:Node) {String} [prefix]
 *                   Network prefix allocated to this node if available
 */
struct Node {
    1: string name;
    2: NodeType node_type;
    // 3: deprecated in M74 (is_primary)
    4: string mac_addr;
    5: bool pop_node;
    // 7: deprecated in M37 (polarity)
    // 8: deprecated in M37 (golay_idx)
    9: NodeStatusType status;  // modified by controller
    // 10: deprecated (secondary_mac_addrs)
    11: list<string> wlan_mac_addrs;
    100: string site_name;
    101: double ant_azimuth;  // not used in e2e
    102: double ant_elevation;  // not used in e2e
    103: optional bool has_cpe;   // node has attached CPE
    // allocated network prefix for this node e.g. "face:b00c:0:12::/64"
    104: optional string prefix;
}

/**
 * @apiDefine Link_GROUP
 * @apiParam (:Link) {String} name The link name
 * @apiParam (:Link) {String} a_node_name The A-node name
 * @apiParam (:Link) {String} a_node_mac  The A-node MAC address
 *                   (if empty, uses the node's MAC address)
 * @apiParam (:Link) {String} z_node_name The Z-node name
 * @apiParam (:Link) {String} z_node_mac  The Z-node MAC address
 *                   (if empty, uses the node's MAC address)
 * @apiParam (:Link) {Int(LinkType)=1,2} link_type
 *                   The link type (1=WIRELESS, 2=ETHERNET)
 * @apiParam (:Link) {Boolean} is_alive The alive state (initialize to false)
 * @apiParam (:Link) {Int64} linkup_attempts
 *                   The link-up attempts (initialize to 0)
 * @apiParam (:Link) {Boolean} [is_backup_cn_link]
 *                   Whether this is a backup DN-to-CN link
 */
struct Link {
    1: string name;
    2: string a_node_name;
    3: string z_node_name;
    4: LinkType link_type;
    5: bool is_alive;  // modified by controller
    6: i64 linkup_attempts;  // modified by controller
    // 7: deprecated in M37 (golay_idx)
    // 8: deprecated in M37 (control_superframe)
    9: string a_node_mac;
    10: string z_node_mac;
    11: optional bool is_backup_cn_link;
}

/**
 * @apiDefine Zone_SUCCESS
 * @apiSuccess (:Zone) {Set(String)} node_names Names of nodes in this zone
 * @apiSuccess (:Zone) {Set(String)} zone_prefixes Specific network prefixes
 *                     allocated to this zone
 */
struct Zone {
  1: set<string> (cpp.template = "std::unordered_set") node_names;
  2: set<string> (cpp.template = "std::unordered_set") zone_prefixes;
} (no_default_comparators)

/**
 * @apiDefine PrefixAllocParams_DEPRECATED_SUCCESS
 * @apiSuccess (:PrefixAllocParams_DEPRECATED) {String} seed_prefix Network
 *                                             prefix from which to allocate
 *                                             per-node prefixes
 *                                             (e.g. face:b00c:cafe:ba00::/56)
 * @apiSuccess (:PrefixAllocParams_DEPRECATED) {Int16} alloc_prefix_len Length
 *                                             of per-node allocated prefixes
 *                                             (i.e. 64 in
 *                                             face:b00c:cafe:ba01::/64)
 * @apiSuccess (:PrefixAllocParams_DEPRECATED) {Map(String:Object(Zone))} zones
 *                                             Map of zone names to their zone
 */
struct PrefixAllocParams_DEPRECATED {
    1: string seed_prefix;
    2: i16 alloc_prefix_len;
    3: optional map<string /* site name */, Zone>
       (cpp.template = "std::unordered_map") zones;
} (no_default_comparators)

/**
 * @apiDefine DeterministicPrefixAllocParams_SUCCESS
 * @apiSuccess (:DeterministicPrefixAllocParams) {Map(String:Object(Zone))} zones
 *                                               Map of zone names to their zone
 */
struct DeterministicPrefixAllocParams {
  1: optional map<string /* site name */, Zone>
     (cpp.template = "std::unordered_map") zones;
} (no_default_comparators)

/**
 * @apiDefine Config_SUCCESS
 * @apiSuccess (:Config) {Byte=1-3} channel=2 The network channel
 * @apiSuccess (:Config) {Object(PrefixAllocParams_DEPRECATED)} [prefix_alloc_params]
 *                       Parameters used for per-node prefix allocation in the
 *                       network
 * @apiSuccess (:Config) {Object(DeterministicPrefixAllocParams)} [deterministic_prefix_alloc_params]
 *                       Parameters used for per-node deterministic prefix
 *                       allocation in the network
 */
struct Config {
    // 1: deprecated in M40 (channel)
    2: optional PrefixAllocParams_DEPRECATED prefix_alloc_params;
    3: optional DeterministicPrefixAllocParams deterministic_prefix_alloc_params;
}

/**
 * @apiDefine Topology_SUCCESS
 * @apiSuccess {String} name The topology name
 * @apiSuccess {Object(Node)[]} nodes The list of nodes
 * @apiSuccess {Object(Link)[]} links The list of links
 * @apiSuccess {Object(Site)[]} sites The list of sites
 * @apiSuccess {Object(Config)} config The topology config
 */
struct Topology {
    1: string name;
    2: list<Node> nodes;
    3: list<Link> links;
    4: list<Site> sites;
    5: Config config;
}
