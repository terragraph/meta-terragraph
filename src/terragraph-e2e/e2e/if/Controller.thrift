/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

namespace cpp2 facebook.terragraph.thrift
namespace py terragraph_thrift.Controller
namespace lua terragraph_thrift.Controller

cpp_include "<unordered_map>"
cpp_include "<unordered_set>"

include "openr/if/Types.thrift"

include "BWAllocation.thrift"
include "NodeConfig.thrift"
include "Topology.thrift"

enum MessageType {

  // ===  StatusApp  === //
  // Requests handled (by Ctrl StatusApp)
  GET_STATUS_DUMP = 101,
  REBOOT_REQUEST = 102,
  GET_CTRL_NEIGHBORS_REQ = 103,
  RESTART_MINION_REQUEST = 104,
  GET_GPS_TIME = 105,
  GET_SYSDUMP = 106,
  IS_ALIVE = 107,
  // Responses given (by Ctrl StatusApp)
  STATUS_DUMP = 121,
  GET_CTRL_NEIGHBORS_RESP = 122,
  GET_NEIGHBORS_RESP = 123,
  GPS_TIME = 124,
  GET_SYSDUMP_RESP = 125,
  // Requests handled (by Minion StatusApp)
  SET_NODE_PARAMS = 141,
  REBOOT_NODE = 142,
  GET_MINION_NEIGHBORS_REQ = 143,
  UPDATE_LINK_METRICS = 144,
  RESTART_MINION = 145,
  GET_STATUS_REPORT = 146,
  GET_MINION_SYSDUMP = 147,
  UPDATE_FW_STATS_CONFIG = 148,
  // Messages originated (by Minion StatusApp)
  STATUS_REPORT = 161,
  STATUS_REPORT_ACK = 162,
  GET_MINION_NEIGHBORS_RESP = 163,
  GPS_ENABLED = 164,
  CONTROLLER_CONNECTED = 165,
  MINION_SYSDUMP_STARTED = 166,
  MINION_SYSDUMP_COMPLETE = 167,

  // ===  IgnitionApp  === //
  // Requests handled (by Ctrl IgnitionApp)
  GET_IGNITION_STATE = 201,
  SET_IGNITION_PARAMS = 202,
  SET_LINK_STATUS_REQ = 203,
  LINK_STATUS_EVENT = 204,
  FORCE_DISSOC_REQ = 205,
  // Responses given (by Ctrl IgnitionApp)
  IGNITION_STATE = 221,
  // Requests handled (by Minion IgnitionApp)
  SET_LINK_STATUS = 241,
  GET_LINK_STATUS = 242,
  GET_LINK_STATUS_DUMP = 245,
  BF_RESP_SCAN = 243,
  BF_RESP_SCAN_DISABLE = 247,
  BF_SLOT_EXCLUSION_REQ = 244,
  START_CONTINUOUS_TOPO_SCAN = 246,
  // Messages originated (by Minion IgnitionApp)
  LINK_STATUS = 261,
  LINK_STATUS_DUMP = 262,
  LINK_AUTHORIZED = 263,

  // ===  TopologyApp  === //
  // Requests handled (by Ctrl TopologyApp)
  GET_TOPOLOGY = 301,
  GET_NETWORK_AIRTIME = 319,
  SET_NODE_STATUS = 302,
  SET_NODE_MAC = 303,
  SET_NODE_MAC_LIST = 315,
  SET_WIRED_LINK_STATUS = 342,
  ADD_NODE_WLAN_MACS = 330,
  DEL_NODE_WLAN_MACS = 331,
  CHANGE_NODE_WLAN_MAC = 332,
  SET_NODE_PARAMS_REQ = 304,
  BUMP_LINKUP_ATTEMPTS = 305,
  GET_NODE = 336,
  GET_LINK = 337,
  GET_SITE = 338,
  ADD_NODE = 306,
  ADD_LINK = 307,
  ADD_SITE = 310,
  DEL_NODE = 308,
  DEL_LINK = 309,
  DEL_SITE = 311,
  EDIT_SITE = 317,
  EDIT_NODE = 318,
  SET_NETWORK_PARAMS_REQ = 312,
  RESET_TOPOLOGY_STATE = 313,
  SET_TOPOLOGY_NAME = 314,
  BULK_ADD = 316,
  GET_ROUTES = 320,
  GET_DEFAULT_ROUTES = 334,
  SET_PREFIXES = 324,
  GET_NODE_PREFIXES = 325,
  GET_ZONE_PREFIXES = 326,
  ALLOCATE_PREFIXES = 329,
  PREFIX_ALLOC_PARAMS_UPDATED = 333,
  // Responses given (by Ctrl TopologyApp)
  TOPOLOGY = 321,
  NETWORK_AIRTIME = 322,
  GET_ROUTES_RESP = 323,
  GET_DEFAULT_ROUTES_RESP = 335,
  GET_NODE_PREFIXES_RESP = 327,
  GET_ZONE_PREFIXES_RESP = 328,
  NODE = 339,
  LINK = 340,
  SITE = 341,
  BULK_ADD_RESULT = 343,

  // ===  UpgradeApp  === //
  // Requests handled (by minion UpgradeApp)
  UPGRADE_REQ = 401,
  // Messages originated (by minion UpgradeApp)
  SET_UPGRADE_STATUS = 421,
  // Requests handled (by Ctrl UpgradeApp)
  UPGRADE_GROUP_REQ = 441,
  UPGRADE_STATE_REQ = 442,
  UPGRADE_ABORT_REQ = 443,
  UPGRADE_COMMIT_PLAN_REQ = 444,
  UPGRADE_ADD_IMAGE_REQ = 445,
  UPGRADE_DEL_IMAGE_REQ = 446,
  UPGRADE_LIST_IMAGES_REQ = 447,
  // responses given (by Ctrl UpgradeApp)
  UPGRADE_STATE_DUMP = 451,
  UPGRADE_COMMIT_PLAN = 452,
  UPGRADE_LIST_IMAGES_RESP = 453,

  // ===  ConfigApp  === //
  // Requests handled (by minion ConfigApp)
  GET_MINION_CONFIG_REQ = 721,
  GET_MINION_CONFIG_RESP = 722,
  SET_MINION_CONFIG_REQ = 723,
  GET_MINION_CONFIG_ACTIONS_REQ = 725,
  GET_MINION_CONFIG_ACTIONS_RESP = 726,
  GET_MINION_BASE_CONFIG = 727,

  // Requests handled (by ctrl ConfigApp)
  GET_CTRL_CONFIG_REQ = 731,
  GET_CTRL_CONFIG_RESP = 732,
  GET_CTRL_CONFIG_NODE_OVERRIDES_REQ = 733,
  GET_CTRL_CONFIG_NODE_OVERRIDES_RESP = 734,
  GET_CTRL_CONFIG_BASE_REQ = 735,
  GET_CTRL_CONFIG_BASE_RESP = 736,
  GET_CTRL_CONFIG_FIRMWARE_BASE_REQ = 768,
  GET_CTRL_CONFIG_FIRMWARE_BASE_RESP = 769,
  GET_CTRL_CONFIG_HARDWARE_BASE_REQ = 757,
  GET_CTRL_CONFIG_HARDWARE_BASE_RESP = 758,
  GET_CTRL_CONFIG_NETWORK_OVERRIDES_REQ = 737,
  GET_CTRL_CONFIG_NETWORK_OVERRIDES_RESP = 738,
  SET_CTRL_CONFIG_NODE_OVERRIDES_REQ = 739,
  SET_CTRL_CONFIG_NETWORK_OVERRIDES_REQ = 740,
  GET_CTRL_CONFIG_PATHS_REQ = 760,
  GET_CTRL_CONFIG_PATHS_RESP = 761,
  // 741, 742 (deprecated in RELEASE_M21)
  GET_CTRL_CONFIG_METADATA_REQ = 743,
  GET_CTRL_CONFIG_METADATA_RESP = 744,
  GET_CTRL_CONFIG_NETWORK_OVERRIDES_ACTIONS_REQ = 745,
  GET_CTRL_CONFIG_NODE_OVERRIDES_ACTIONS_REQ = 746,
  GET_CTRL_CONFIG_OVERRIDES_ACTIONS_RESP = 747,
  GET_CTRL_CONFIG_ACTIONS_RESULTS_REQ = 748,
  GET_CTRL_CONFIG_ACTIONS_RESULTS_RESP = 749,
  GET_CTRL_CONFIG_CONTROLLER_REQ = 750,
  GET_CTRL_CONFIG_CONTROLLER_RESP = 751,
  SET_CTRL_CONFIG_CONTROLLER_REQ = 752,
  GET_CTRL_CONFIG_CONTROLLER_METADATA_REQ = 753,
  GET_CTRL_CONFIG_CONTROLLER_METADATA_RESP = 754,
  GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_REQ = 755,
  GET_CTRL_CONFIG_AUTO_NODE_OVERRIDES_RESP = 756,
  TRIGGER_POLARITY_OPTIMIZATION = 759,
  TRIGGER_CONTROL_SUPERFRAME_OPTIMIZATION = 762,
  TRIGGER_GOLAY_OPTIMIZATION = 763,
  TRIGGER_CHANNEL_OPTIMIZATION = 764,
  CLEAR_AUTO_NODE_OVERRIDES_CONFIG = 765,
  MODIFY_CTRL_CONFIG_NETWORK_OVERRIDES_REQ = 766,
  MODIFY_CTRL_CONFIG_NODE_OVERRIDES_REQ = 767,
  MINION_BASE_CONFIG = 770,
  UPDATE_TUNNEL_CONFIG = 771,

  // ===  ScanApp === //
  // E2E -> Minion and Minion -> FW
  SCAN_REQ = 601,
  // FW->Minion and Minion -> E2E
  SCAN_RESP = 621,
  // CLI -> E2E
  START_SCAN = 641,
  START_SCAN_RESP = 652,
  GET_SCAN_STATUS = 642,
  RESET_SCAN_STATUS = 643,
  GET_SCAN_SCHEDULE = 644,
  SET_SCAN_SCHEDULE = 645,
  RESET_CBF_CONFIG = 646,
  GET_CBF_CONFIG = 647,
  SET_CBF_CONFIG = 648,
  RESET_RF_STATE = 649,
  GET_RF_STATE = 650,
  SET_RF_STATE = 651,
  SET_LATPC_PARAMS = 653,
  // 654 deprecated (CLEAR_LATPC_PARAMS)
  // E2E -> CLI
  SCAN_STATUS = 661,
  SCAN_SCHEDULE = 662,
  CBF_CONFIG = 663,
  RF_STATE = 664,
  // Config
  SCAN_SCHEDULE_UPDATED = 670,

  // === SchedulerApp === //
  // CLI -> E2E
  GET_SLOT_MAP_CONFIG = 701,
  SET_SLOT_MAP_CONFIG = 702,
  // E2E -> CLI
  SLOT_MAP_CONFIG = 703,

  // === DriverApp === //
  // Message exchange with driver
  // north bound
  DR_ACK = 491,
  GPS_GET_POS_RESP = 492,
  DR_DEV_ALLOC_RES = 493,
  DR_WSEC_STATUS = 494,
  DR_WSEC_LINKUP_STATUS = 497,
  DR_DEV_UPDOWN_STATUS = 498,
  // south bound
  GPS_GET_POS_REQ = 495,
  DR_DEV_ALLOC_REQ = 496,
  GPS_SET_POS_REQ = 499,

  // Message exchange with firmware
  // south bound
  NODE_INIT = 501,
  DR_SET_LINK_STATUS = 502,  // link up/link down request
  FW_SET_NODE_PARAMS = 503,  // set bandwidth allocation map
  FW_STATS_CONFIGURE_REQ = 504,
  PHY_LA_CONFIG_REQ = 505,
  GPS_ENABLE_REQ = 506,
  FW_SET_CODEBOOK = 507,
  FW_DEBUG_REQ = 508,
  PHY_AGC_CONFIG_REQ = 509,
  PHY_GOLAY_SEQUENCE_CONFIG_REQ = 510,
  FW_CONFIG_REQ = 511,
  PHY_TPC_CONFIG_REQ = 512,
  FW_BF_RESP_SCAN = 513,
  FW_SET_LOG_CONFIG = 514,
  GPS_SEND_TIME = 515,
  PHY_TPC_ADJ_TBL_CFG_REQ = 516,
  // north bound
  NODE_INIT_NOTIFY = 551,
  DR_LINK_STATUS = 552,
  FW_STATS = 553,
  FW_ACK = 591,  // fw ack for passthru message
  FW_HEALTHY = 592,
  FW_GET_CODEBOOK = 593,
  FW_CONFIG_RESP = 594,
  FW_ROUTING_INFO = 595,
  FW_ADJ_REQ = 596,
  // 597 (deprecated in RELEASE_M48)
  FW_PPS_HTSF_INFO = 598,
  FW_GET_PHY_TPC_ADJ_TBL = 599,

  // ===  OpenrClientApp  === //
  SYNC_LINK_MONITOR = 801,
  INJECT_KVSTORE_KEYS = 802,
  GET_ROUTING_ADJACENCIES = 810,
  ROUTING_ADJACENCIES = 811,
  SET_LINK_METRIC = 812,
  FW_ADJ_RESP = 813,

  // ===  TrafficApp  === //
  // Requests handled (by ctrl TrafficApp)
  START_IPERF = 901,
  START_IPERF_RESP = 902,
  STOP_IPERF = 903,
  GET_IPERF_STATUS = 904,
  IPERF_STATUS = 905,
  START_PING = 906,
  START_PING_RESP = 907,
  STOP_PING = 908,
  GET_PING_STATUS = 909,
  PING_STATUS = 910,
  // Messages originated by ctrl TrafficApp to minion
  START_IPERF_SERVER = 911,
  START_IPERF_CLIENT = 912,
  // Messages originated by minion to ctrl TrafficApp
  START_IPERF_SERVER_RESP = 921,
  IPERF_OUTPUT = 922,
  PING_OUTPUT = 923,

  // ===  TopologyBuilderApp  === //
  // Requests handled (by Ctrl TopologyBuilderApp)
  START_TOPOLOGY_SCAN = 1301,
  TOPOLOGY_SCAN_RESULT = 1302,
  START_NETWORK_TOPOLOGY_SCAN = 1303,
  STOP_NETWORK_TOPOLOGY_SCAN = 1304,
  GET_NETWORK_TOPOLOGY_SCAN_STATUS = 1305,
  START_LINK_DISCOVERY_SCAN = 1306,
  GET_LINK_DISCOVERY_SCAN_STATUS = 1307,
  // Responses given (by Ctrl TopologyApp)
  START_TOPOLOGY_SCAN_RESP = 1311,
  NETWORK_TOPOLOGY_SCAN_STATUS = 1312,
  LINK_DISCOVERY_SCAN_STATUS = 1313,

  // ===  BinaryStarApp  === //
  // Messages between peer controllers
  BSTAR_SYNC = 1101,
  // Messages between BinaryStarApp / Broker
  BSTAR_FSM = 1102,
  // Messages between BinaryStarApp / controller apps
  BSTAR_GET_APP_DATA = 1103,
  BSTAR_APP_DATA = 1104,
  BSTAR_GET_STATE = 1105,
  // Messages to minion
  BSTAR_SWITCH_CONTROLLER = 1110,

  // == Event Stream == //
  // Topology Events
  EVENT_ADD_NODE = 1201,
  EVENT_DEL_NODE = 1202,
  EVENT_EDIT_NODE = 1203,
  EVENT_ADD_LINK = 1204,
  EVENT_DEL_LINK = 1205,
  EVENT_ADD_SITE = 1206,
  EVENT_DEL_SITE = 1207,
  EVENT_EDIT_SITE = 1208,
  // Status Change Events
  EVENT_NODE_STATUS = 1209,
  EVENT_LINK_STATUS = 1210,

  // Miscellaneous (common)
  NONE = 1001,
  HELLO = 1002,
  E2E_ACK = 1003,
  TEST = 1004,
  DR_RESP = 1005, // driver response to all south bound messages to fw
  DR_STAT_PUSH = 1006, // push NB stats from driver (also used within driver-if)
  EVENT = 1007,
  RESTART = 1008,
  SOCKET_HEALTH_CHECK = 2000,
}

// link update action type
enum LinkActionType {
  LINK_UP = 1,
  LINK_DOWN = 2,
  LINK_ADD = 3,
  LINK_DELETE = 4,
}

// link status type used in e2e scope
enum LinkStatusType {
  LINK_UP = 1,
  LINK_DOWN = 2,
}

############# Upgrade App #############

enum UpgradeStatusType {
  NONE = 10,
  DOWNLOADING_IMAGE = 20,
  DOWNLOAD_FAILED = 30,
  FLASHING_IMAGE = 40,
  FLASH_FAILED = 50,
  FLASHED = 60,
  COMMIT_FAILED = 70,
}

/**
 * @apiDefine ImageMeta_SUCCESS
 * @apiSuccess (:ImageMeta) {String} md5 The image MD5 digest
 * @apiSuccess (:ImageMeta) {String} version The image version string
 * @apiSuccess (:ImageMeta) {String} model The targeted hardware model
 * @apiSuccess (:ImageMeta) {String[]} hardwareBoardIds
 *                          The supported hardware board IDs
 */
// terragraph image meta struct
struct ImageMeta {
  1: string md5;
  2: string version;
  3: string model;
  4: list<string> hardwareBoardIds;
}

/**
 * @apiDefine UpgradeStatus_SUCCESS
 * @apiSuccess (:UpgradeStatus) {Int(UpgradeStatusType)=10,20,30,40,50,60,70} usType
 *                              The upgrade status type
 *                              (10=NONE, 20=DOWNLOADING_IMAGE,
 *                               30=DOWNLOAD_FAILED, 40=FLASHING_IMAGE,
 *                               50=FLASH_FAILED, 60=FLASHED, 70=COMMIT_FAILED)
 * @apiSuccess (:UpgradeStatus) {Object(ImageMeta)} nextImage
 *                              The meta-info for the next image
 * @apiSuccess (:UpgradeStatus) {String} reason
 *                              The reason for the current status (if any)
 * @apiSuccess (:UpgradeStatus) {String} upgradeReqId The upgrade request ID
 * @apiSuccess (:UpgradeStatus) {Int64} whenToCommit
 *                              When to commit the upgrade (UNIX time)
 */
struct UpgradeStatus {
  1: UpgradeStatusType usType;
  2: ImageMeta nextImage;
  3: string reason;
  4: string upgradeReqId;
  5: i64 whenToCommit;
}

enum UpgradeReqType {
  PREPARE_UPGRADE = 10,
  COMMIT_UPGRADE = 20,
  RESET_STATUS = 30,
  FULL_UPGRADE = 40,
}

/**
 * @apiDefine UpgradeTorrentParams_GROUP
 * @apiParam (:UpgradeTorrentParams) {Int64} downloadTimeout=100
 *           The timeout for downloading the torrent (in seconds)
 * @apiParam (:UpgradeTorrentParams) {Int64} [downloadLimit=-1]
 *           The download bandwidth limit
 *           (in bytes per second, unlimited by default)
 * @apiParam (:UpgradeTorrentParams) {Int64} [uploadLimit=-1]
 *           The upload bandwidth limit
 *           (in bytes per second, unlimited by default)
 * @apiParam (:UpgradeTorrentParams) {Int64} [maxConnections=-1]
 *           The maximum number of connections that the torrent will open
 *           (must be at least 2, unlimited by default)
 * @apiParam (:UpgradeTorrentParams) {Int64} [peerTos]
 *           The ToS (type of service) byte set in the IP header of every packet
 *           sent to peers
 * @apiParam (:UpgradeTorrentParams) {Int64} [maxFailCount]
 *           The maximum tries to connect to a peer before stopping
 * @apiParam (:UpgradeTorrentParams) {Int64} [minReconnectTimeSec]
 *           The number of seconds to wait to reconnect to a peer
 * @apiParam (:UpgradeTorrentParams) {Int64} [peerConnectTimeoutSec]
 *           The number of seconds to wait after a connection attempt is
 *           initiated to a peer until it is considered as having timed out
 * @apiParam (:UpgradeTorrentParams) {Int64} [handshakeTimeoutSec]
 *           The number of seconds to wait for a handshake response from a peer
 */
struct UpgradeTorrentParams {
  1: i64 downloadTimeout; // required. Download timeout
  2: optional i64 downloadLimit = -1;  // Unlimited by default
  3: optional i64 uploadLimit = -1;  // Unlimited by default
  4: optional i64 maxConnections = -1; // Unlimited by default
  5: optional i64 peerTos;
  6: optional i64 maxFailCount;
  7: optional i64 minReconnectTimeSec;
  8: optional i64 peerConnectTimeoutSec;
  9: optional i64 handshakeTimeoutSec;
}

/**
 * @apiDefine UpgradeReq_GROUP
 * @apiParam (:UpgradeReq) {Int(UpgradeReqType)=10,20,30,40} urType
 *           The upgrade request type
 *           (10=PREPARE_UPGRADE, 20=COMMIT_UPGRADE, 30=RESET_STATUS,
 *            40=FULL_UPGRADE)
 * @apiParam (:UpgradeReq) {String} upgradeReqId
 *           The unique identifier for the upgrade request
 * @apiParam (:UpgradeReq) {String} md5
 *           The expected MD5 hash of the upgrade image file
 *           (only used in prepare)
 * @apiParam (:UpgradeReq) {String} hardwareBoardIds
 *           The hardware board IDs that this image supports
 *           (only used in prepare)
 * @apiParam (:UpgradeReq) {String} imageUrl
 *           The URI for the upgrade image, which must be either an HTTP/HTTPS
 *           URL or Magnet URI (only used in prepare)
 * @apiParam (:UpgradeReq) {Int64} [scheduleToCommit=0]
 *           The number of seconds before a minion reboots after being flashed
 *           (only used in commit)
 * @apiParam (:UpgradeReq) {Int64=1-10} [downloadAttempts=3]
 *           The maximum number of attempts for a minion to download the upgrade
 *           image (only used in prepare over HTTP/HTTPS)
 * @apiParam (:UpgradeReq) {Object(UpgradeTorrentParams)} [torrentParams]
 *           The torrent parameters (only used in prepare over BitTorrent)
 * @apiParam (:UpgradeReq) {String} [nextNodeConfigJson]
 *           The preloaded node config JSON for the new software image
 */
struct UpgradeReq {
  1: UpgradeReqType urType;
  2: string upgradeReqId;
  3: string md5;  // for PREPARE_UPGRADE
  4: string imageUrl;  // for PREPARE_UPGRADE
  5: optional i64 scheduleToCommit;
  6: optional i64 downloadAttempts;  // for PREPARE_UPGRADE
  7: optional UpgradeTorrentParams torrentParams;  // for PREPARE_UPGRADE
  8: optional string nextNodeConfigJson; // for COMMIT_UPGRADE
  9: list<string> hardwareBoardIds;  // for PREPARE_UPGRADE
}

enum UpgradeGroupType {
  NODES = 10,    // upgrade operation on a list of nodes
  NETWORK = 20,  // upgrade operation on the entire network
}

/**
 * @apiDefine UpgradeGroupReq
 * @apiParam {Int(UpgradeGroupType)=10,20} ugType
 *           Whether to upgrade a list of nodes or the entire network
 *           (10=NODES, 20=NETWORK)
 * @apiParam {String[]} nodes The nodes to upgrade (for node-level upgrades)
 * @apiParam {String[]} excludeNodes The nodes to exclude from the upgrade
 *           (for network-level upgrades)
 * @apiParam {Object(UpgradeReq)} urReq The upgrade request parameters
 * @apiParam {Int64} timeout
 *           The per-batch timeout for the upgrade operation (in seconds)
 * @apiParam {Boolean} skipFailure
 *           If true, the controller will move on to the next node if the
 *           current node can't be upgraded; if false, it will abort the upgrade
 *           upon seeing a single node failure
 * @apiParam {Boolean} skipPopFailure
 *           If true, the controller will not perform extra checks for POP
 *           nodes; if false, it will abort the upgrade upon seeing a POP node
 *           fail to re-establish BGP connections after upgrading
 * @apiParam {String} version Skip nodes with this version before prepare/commit
 * @apiParam {String[]} skipLinks Skip the link aliveness check for these links
 *           when updating the commit status
 * @apiParam {Int64} limit Maximum number of nodes to commit simultaneously
 *           (<0 Upgrade all nodes at once;
 *            0 Staged commit with no limit per stage;
 *            >0 Staged commit with limiting number of nodes per stage)
 * @apiParam {Int64} retryLimit=3 The maximum retry attempts for each node
 */
// upgrade request sent to controller to upgrade a group of nodes
struct UpgradeGroupReq {
  1: UpgradeGroupType ugType;
  2: list<string> nodes;
  3: list<string> excludeNodes;
  4: UpgradeReq urReq;
  5: i64 timeout;
  6: bool skipFailure;
  11: bool skipPopFailure;
  7: string version;
  8: list<string> skipLinks;
  9: i64 limit;
  10: i64 retryLimit = 3;
}

/**
 * @apiDefine UpgradeStateReq
 */
struct UpgradeStateReq {}

/**
 * @apiDefine UpgradeStateDump_SUCCESS
 * @apiSuccess {String[]} curBatch
 *             The names of the nodes currently upgrading
 * @apiSuccess {String[][]} pendingBatches
 *             The batches of nodes pending for the current request
 * @apiSuccess {Object(UpgradeGroupReq)} curReq
 *             The current upgrade group request
 * @apiSuccess {Object(UpgradeGroupReq)[]} pendingReqs
 *             The queued upgrade group requests
 */
struct UpgradeStateDump {
  1: list<string> curBatch;
  2: list<list<string>> pendingBatches;
  3: UpgradeGroupReq curReq;
  4: list<UpgradeGroupReq> pendingReqs;
}

/**
 * @apiDefine UpgradeAbortReq
 * @apiParam {Boolean} abortAll Whether to abort all upgrades or only specific
 *                              requests (reqIds)
 * @apiParam {String[]} reqIds The request IDs to abort (if abortAll is false)
 * @apiParam {Boolean} resetStatus Whether to reset the upgrade state on
 *                                 affected nodes (if the current upgrde request
 *                                 is aborted)
 */
struct UpgradeAbortReq {
  1: bool abortAll;
  2: list<string> reqIds;
  3: bool resetStatus;
}

/**
 * @apiDefine UpgradeCommitPlanReq
 * @apiParam {Int64} limit The maximum number of nodes per batch
 * @apiParam {String[]} excludeNodes The nodes to exclude from the upgrade
 */
struct UpgradeCommitPlanReq {
  1: i64 limit;
  2: list<string> excludeNodes;
}

/**
 * @apiDefine UpgradeCommitPlan_SUCCESS
 * @apiSuccess (:UpgradeCommitPlan) {Set(String)[]} commitBatches
 *                                 List of commit batches consisting of nodes
 *                                 to upgrade in each batch
 */
struct UpgradeCommitPlan {
  1: list<set<string>>
     (cpp.type = "std::vector<std::unordered_set<std::string>>") commitBatches;
} (no_default_comparators)

/**
 * @apiDefine UpgradeImage_SUCCESS
 * @apiSuccess (:UpgradeImage) {String} name The unique, descriptive image name
 * @apiSuccess (:UpgradeImage) {String} magnetUri The magnet URI for the image
 * @apiSuccess (:UpgradeImage) {String} httpUri The HTTP URI for the image
 * @apiSuccess (:UpgradeImage) {String} md5 The MD5 hash of the image
 * @apiSuccess (:UpgradeImage) {String[]} hardwareBoardIds
 *                             The supported hardware board IDs
 */
struct UpgradeImage {
  1: string name; // version string (not filename)
  2: string magnetUri;
  5: optional string httpUri;
  3: string md5; // needed for PREPARE_UPGRADE/COMMIT_UPGRADE
  4: list<string> hardwareBoardIds;
}

/**
 * @apiDefine UpgradeAddImageReq
 * @apiParam {String} imageUrl The HTTP/HTTPS URL of the image to download
 */
struct UpgradeAddImageReq {
  1: string imageUrl; // image http URL (for controller to download)
}

/**
 * @apiDefine UpgradeDelImageReq
 * @apiParam {String} name The name of the upgrade image to delete
 */
struct UpgradeDelImageReq {
  1: string name; // 'name' from UpgradeImage
}

/**
 * @apiDefine UpgradeListImagesReq
 */
struct UpgradeListImagesReq {}

/**
 * @apiDefine UpgradeListImagesResp_SUCCESS
 * @apiSuccess {Object(UpgradeImage)[]} images
 *             The list of images hosted by the controller
 */
struct UpgradeListImagesResp {
  1: list<UpgradeImage> images;
}

############# Config App #############

// Action after setting config
enum CfgAction {
  NO_ACTION = 0,
  REBOOT = 10,
  RESTART_MINION = 20,
  RESTART_STATS_AGENT = 21,
  RESTART_LOGTAIL = 22,
  RESTART_ROUTING = 23,
  RESTART_SQUIRE = 24,
  REDO_POP_CONFIG = 25,
  RELOAD_RSYSLOG_CONFIG = 26,
  RESTART_KEA = 27,
  UPDATE_FIREWALL = 28,
  SYMLINK_TIMEZONE = 29,
  SYNC_LINK_MONITOR = 30,
  INJECT_KVSTORE_KEYS = 31,
  UPDATE_LINK_METRICS = 32,
  UPDATE_GLOG_LEVEL = 40,
  UPDATE_GFLAG = 41,
  UPDATE_SCAN_CONFIG = 50,
  UPDATE_PREFIX_ALLOC_PARAMS = 60,
  RELOAD_SSHD_CA_KEYS = 70,
  RELOAD_DNS_SERVERS = 71,
  RELOAD_NTP_CONFIG = 72,
  RELOAD_FIRMWARE = 73,
  SET_FW_PARAMS = 80,
  RESTART_UDP_PING_SERVER = 81,
  RELOAD_SSHD = 82,
  SET_AIRTIME_PARAMS = 83,
  SET_FW_PARAMS_SYNC_OR_RELOAD_FIRMWARE = 84,
  SET_FW_STATS_CONFIG = 85,
  RESTART_FLUENTD_AGENT = 90,
  UPDATE_CHANNEL_ASSIGNMENT = 91,
  RESTART_SNMP = 100,
  RESTART_WEBUI = 110,
  RESTART_SLIP = 120,
  RELOAD_TUNNEL_CONFIG = 130,
  RELOAD_VPP_CONFIG_AND_MONITOR = 140,
  UPDATE_ZONE = 141,
  RELOAD_TOPOLOGY_NAMES = 142,
}

// Config parameter data types
enum CfgParamType {
  INTEGER = 10,
  STRING = 20,
  BOOLEAN = 30,
  OBJECT = 40,
  MAP = 50,
  FLOAT = 60,
}

struct GetMinionConfigResp {
  1: string config;
}

struct GetMinionBaseConfig {
  1: bool getHwConfigs;
  2: bool getMetadata;
  3: bool getBaseConfigs;
}

struct MinionBaseConfig {
  1: optional map<string /* version */, string /* config json */> hwConfigs;
  2: optional string metadata;
  3: optional map<string /*version */, string /* config json */> baseConfigs;
}

struct SetMinionConfigReq {
  1: string config; // node config json string
  2: i64 bwgdIdx; // BWGD index at which to apply firmware changes (if needed)
}

struct GetMinionConfigActionsReq {
  1: string config; // node config json string
  2: string id;
}

struct GetMinionConfigActionsResp {
  // node actions that would be performed
  1: set<CfgAction> (cpp.template = "std::unordered_set") actions;
  2: string id;
} (no_default_comparators)

/**
 * @apiDefine GetCtrlConfigReq
 * @apiParam {String} node The node name
 * @apiParam {String} [swVersion]
 *           The software version to use as the base config.
 *           If this is omitted, the controller will use the last version that
 *           the node reported; if no version is known to the controller, an
 *           error will be returned.
 * @apiParam {String} [hwBoardId]
 *           The hardware board ID to use for the hardware base config.
 *           See above for actions taken when this is omitted.
 * @apiParam {String} [fwVersion]
 *           The firmware version to use as the firmware base config.
 *           See above for actions taken when this is omitted;
 *           however, omission of this field will not result in an error.
 */
struct GetCtrlConfigReq {
  1: string node;
  2: optional string swVersion;  // Determines the config base to use
  3: optional string hwBoardId;  // Determines the hardware config base to use
  4: optional string fwVersion;  // Determines the firmware config base to use
}

/**
 * @apiDefine GetCtrlConfigResp_SUCCESS
 * @apiSuccess {String} config The full node config (JSON)
 */
struct GetCtrlConfigResp {
  1: string config;
}

/**
 * @apiDefine GetCtrlConfigAutoNodeOverridesReq
 * @apiParam {String[]} nodes The list of nodes, or all nodes if empty
 */
struct GetCtrlConfigAutoNodeOverridesReq {
  1: list<string> nodes; // get for all nodes if empty
}

/**
 * @apiDefine GetCtrlConfigAutoNodeOverridesResp_SUCCESS
 * @apiSuccess {String} overrides The automated node config overrides (JSON)
 */
struct GetCtrlConfigAutoNodeOverridesResp {
  1: string overrides;
}

/**
 * @apiDefine GetCtrlConfigNodeOverridesReq
 * @apiParam {String[]} nodes The list of nodes, or all nodes if empty
 */
struct GetCtrlConfigNodeOverridesReq {
  1: list<string> nodes; // get for all nodes if empty
}

/**
 * @apiDefine GetCtrlConfigNodeOverridesResp_SUCCESS
 * @apiSuccess {String} overrides The node config overrides (JSON)
 */
struct GetCtrlConfigNodeOverridesResp {
  1: string overrides;
}

/**
 * @apiDefine SetCtrlConfigNodeOverridesReq
 * @apiParam {String} overrides The node config overrides (JSON), mapping node
 *           names to their config overrides
 */
struct SetCtrlConfigNodeOverridesReq {
  1: string overrides;
}

/**
 * @apiDefine ModifyCtrlConfigNodeOverridesReq
 * @apiParam {String} overrides The node config overrides (JSON), mapping node
 *           names to their config overrides
 */
struct ModifyCtrlConfigNodeOverridesReq {
  1: string overrides;
}

/**
 * @apiDefine GetCtrlConfigNodeOverridesActionsReq
 * @apiParam {String} overrides The node config overrides (JSON), mapping node
 *           names to their config overrides
 */
struct GetCtrlConfigNodeOverridesActionsReq {
  1: string overrides;
}

/**
 * @apiDefine GetCtrlConfigBaseReq
 * @apiParam {String[]} swVersions
 *           The software versions, or all versions if empty
 */
struct GetCtrlConfigBaseReq {
  1: list<string> swVersions; // get all base configs if empty
}

/**
 * @apiDefine GetCtrlConfigBaseResp_SUCCESS
 * @apiSuccess {String} config The base configs (JSON), mapping software version
 *             names to their base configs
 */
struct GetCtrlConfigBaseResp {
  1: string config;
}

/**
 * @apiDefine GetCtrlConfigFirmwareBaseReq
 * @apiParam {String[]} fwVersions The firmware versions, or all if empty
 */
struct GetCtrlConfigFirmwareBaseReq {
  1: list<string> fwVersions;
}

/**
 * @apiDefine GetCtrlConfigFirmwareBaseResp_SUCCESS
 * @apiSuccess {String} config
 *             The firmware base configs (JSON), mapping firmware version names
 *             to their base configs
 */
struct GetCtrlConfigFirmwareBaseResp {
  1: string config;
}

/**
 * @apiDefine GetCtrlConfigHardwareBaseReq
 * @apiParam {String[]} hwBoardIds The hardware board IDs, or all if empty
 * @apiParam {String[]} swVersions The software versions, or all if empty
 */
struct GetCtrlConfigHardwareBaseReq {
  1: list<string> hwBoardIds;
  2: list<string> swVersions;
}

/**
 * @apiDefine GetCtrlConfigHardwareBaseResp_SUCCESS
 * @apiSuccess {String} config
 *             The hardware base configs (JSON), mapping hardware board IDs to
 *             software version names to their base configs
 */
struct GetCtrlConfigHardwareBaseResp {
  1: string config;
}

/**
 * @apiDefine GetCtrlConfigNetworkOverridesReq
 */
struct GetCtrlConfigNetworkOverridesReq {}

/**
 * @apiDefine GetCtrlConfigNetworkOverridesResp_SUCCESS
 * @apiSuccess {String} overrides The network config overrides (JSON)
 */
struct GetCtrlConfigNetworkOverridesResp {
  1: string overrides; // Json of network overrides
}

/**
 * @apiDefine SetCtrlConfigNetworkOverridesReq
 * @apiParam {String} overrides The network config overrides (JSON)
 */
struct SetCtrlConfigNetworkOverridesReq {
  1: string overrides;
}

/**
 * @apiDefine ModifyCtrlConfigNetworkOverridesReq
 * @apiParam {String} overrides The network config overrides (JSON)
 */
struct ModifyCtrlConfigNetworkOverridesReq {
  1: string overrides;
}

/**
 * @apiDefine GetCtrlConfigNetworkOverridesActionsReq
 * @apiParam {String} overrides The network config overrides (JSON)
 */
struct GetCtrlConfigNetworkOverridesActionsReq {
  1: string overrides;
}

/**
 * @apiDefine GetCtrlConfigOverridesActionsResp_SUCCESS
 * @apiSuccess {String} overrides A copy of the input config overrides (JSON)
 * @apiSuccess {String} id The unique identifier that will be attached to all
 *             associated node reports
 * @apiSuccess {Int64} availableUntil The time (UNIX time) at which the
 *             controller will discard results for this ID
 */
struct GetCtrlConfigOverridesActionsResp {
  1: string overrides; // Json of overrides (either node or network)
  2: string id;
  3: i64 availableUntil; // UNIX time
}

/**
 * @apiDefine GetCtrlConfigActionsResultsReq
 * @apiParam {String} id The ID to query results for
 */
struct GetCtrlConfigActionsResultsReq {
  1: string id;
}

/**
 * @apiDefine NodeConfigActionsResult_SUCCESS
 * @apiSuccess (:NodeConfigActionsResult) {Set(Int(CfgAction))} actions
 *             The actions this node will take
 *             (0=NO_ACTION, 10=REBOOT, 20=RESTART_MINION,
 *              21=RESTART_STATS_AGENT, 22=RESTART_LOGTAIL,
 *              23=RESTART_ROUTING, 24=RESTART_SQUIRE, 25=REDO_POP_CONFIG,
 *              27=RESTART_KEA, 28=UPDATE_FIREWALL, 30=SYNC_LINK_MONITOR,
 *              31=INJECT_KVSTORE_KEYS, 32=UPDATE_LINK_METRICS)
 */
struct NodeConfigActionsResult {
  1: set<CfgAction> (cpp.template = "std::unordered_set") actions;
} (no_default_comparators)

/**
 * @apiDefine GetCtrlConfigActionsResultsResp_SUCCESS
 * @apiSuccess {Map(String:Object(NodeConfigActionsResult))} results
 *             Per-node config actions reports
 */
struct GetCtrlConfigActionsResultsResp {
  1: map<string /* node name */, NodeConfigActionsResult>
     (cpp.template = "std::unordered_map") results;
} (no_default_comparators)

/**
 * @apiDefine GetCtrlConfigMetadata
 */
struct GetCtrlConfigMetadata {}

/**
 * @apiDefine GetCtrlConfigMetadataResp_SUCCESS
 * @apiSuccess {String} metadata The config parameter metadata (JSON)
 */
struct GetCtrlConfigMetadataResp {
  1: string metadata;
}

/**
 * @apiDefine GetCtrlConfigPathsReq
 * @apiParam {String[]} nodeNames List of node names to get config for.
 *           Get for all nodes when empty.
 * @apiParam {String[]} configPaths List of node config paths to return
 *           Each config path is a text traversal of the JSON tree with dot
 *           delimiters and '*' wildcards
 *           (e.g. radioParamsOverride.*.fwParams.wsecEnable)
 */
struct GetCtrlConfigPathsReq {
  1: list<string> nodeNames;
  2: list<string> configPaths;
}

/**
 * @apiDefine GetCtrlConfigPathsResp_SUCCESS
 * @apiSuccess {Map(String:String)} config Maps node name to expanded config
 *             JSON string
 */
struct GetCtrlConfigPathsResp {
  1: map<string /* nodeName */, string>
      (cpp.template = "std::unordered_map") config;
} (no_default_comparators)

struct MinionConfigChanged {}

/**
 * @apiDefine TriggerPolarityOptimization
 * @apiParam {String} clearUserPolarityConfig Clear user-configured polarities
 */
struct TriggerPolarityOptimization {
  1: bool clearUserPolarityConfig;
}

/**
 * @apiDefine TriggerChannelOptimization
 * @apiParam {String} clearUserChannelConfig Clear user-configured channels
 */
struct TriggerChannelOptimization {
  1: bool clearUserChannelConfig;
}

/**
 * @apiDefine TriggerControlSuperframeOptimization
 * @apiParam {String} clearUserConfig Clear user-configured control superframe
 */
struct TriggerControlSuperframeOptimization {
  1: bool clearUserConfig;
}

/**
 * @apiDefine TriggerGolayOptimization
 * @apiParam {String} clearUserConfig Clear user-configured golays
 */
struct TriggerGolayOptimization {
  1: bool clearUserConfig;
}

/**
 * @apiDefine GetCtrlControllerConfigReq
 */
struct GetCtrlControllerConfigReq {}

/**
 * @apiDefine GetCtrlControllerConfigResp_SUCCESS
 * @apiSuccess {String} config The controller config (JSON)
 */
struct GetCtrlControllerConfigResp {
  1: string config; // Json of controller config
}

/**
 * @apiDefine SetCtrlControllerConfigReq
 * @apiParam {String} config The controller config (JSON)
 */
struct SetCtrlControllerConfigReq {
  1: string config; // Json of controller config
}

/**
 * @apiDefine GetCtrlControllerConfigMetadata
 */
struct GetCtrlControllerConfigMetadata {}

/**
 * @apiDefine GetCtrlControllerConfigMetadataResp_SUCCESS
 * @apiSuccess {String} metadata The controller config parameter metadata (JSON)
 */
struct GetCtrlControllerConfigMetadataResp {
  1: string metadata;
}

// fw log levels
enum LogLevel {
  DEBUG = 0,
  INFO = 1,
  ERROR = 2,
  FATAL = 3,
}

// enum for associating logs with fw modules
// each fw module has an independently settable log level
enum LogModule {
  DEFAULT = 0, // default module - for all unclassified code
  AGC = 1, // automatic gain control
  BF = 2, // initial beam forming
  BW = 3, // bandwidth handler
  FRAMER = 4, // framer
  FSM = 5, // state machine
  GPS = 6, // GPS
  IOCTL = 7, // driver-firmware ioctl interface
  LA = 8, // link adaptation
  MGMT_FRM = 9, // management frame handler
  SLOT_PROG = 10, // slot programmer
  TPC = 11, // transmit power control
  LA_TPC = 12, // joint TPC and LA
  RUNTIME_CFG = 13, // runtime fw cfg
  CHN = 14, // channel
  SCAN = 15, // scan
  ANT_CB = 16, //Antenna Code Book
  AWV_GEN = 17, // AWV Generation for IBF,Scans
  TSF = 18, // TSF
}

/**
 * @apiDefine SetFwLogConfig
 * @apiParam {String[]} nodes The list of nodes, or all nodes if empty
 * @apiParam {Map(Int(LogModule):Int(LogLevel))} [configs]
 *           The per-module log configuration (will override "level" if given)
 * @apiParam {Int(LogLevel)=0,1,2,3} [level]
 *           The log level to use for all modules
 *           (0=DEBUG, 1=INFO, 2=ERROR, 3=FATAL)
 */
struct SetFwLogConfig {
  1: list<string> nodes;
  2: optional map<LogModule, LogLevel>
     (cpp.template = "std::unordered_map") configs;
  3: optional LogLevel level;
} (no_default_comparators)

/**
 * @apiDefine ClearAutoNodeOverridesConfig
 * @apiParam {String[]} nodes The list of nodes, or all nodes if empty
 * @apiParam {String[]} configPaths List of node config paths to return
 *           Each config path is a text traversal of the JSON tree with dot
 *           delimiters and '*' wildcards
 *           (e.g. radioParamsOverride.*.fwParams.wsecEnable)
 */
struct ClearAutoNodeOverridesConfig {
  1: list<string> nodeNames;
  2: list<string> configPaths;
}

#############  StatusApp ##############

/**
 * @apiDefine RebootReq
 * @apiParam {String[]} nodes The list of nodes
 * @apiParam {Boolean} force Force reboot
 * @apiParam {Int32} secondsToReboot The number of seconds until reboot
 */
struct RebootReq {
  1: list<string> nodes;
  2: bool force;
  3: i32 secondsToReboot;
}

struct RebootNode {
  1: bool force;
  2: optional i32 secondsToReboot = 5;
}

enum NodeParamsType {
  INIT = 0,
  GPS = 1,
  NETWORK = 2,
  MANUAL = 3,
}

// Node parameters configured on each node.
struct NodeParams {
  // 1: (deprecated in RELEASE_M27)
  2: optional Topology.PolarityType polarity;
  3: optional Topology.GolayIdx golayIdx;
  4: optional Topology.Location location;
  5: optional BWAllocation.NodeAirtime airtimeAllocMap;
  6: optional bool enableGps;
  7: optional byte channel;
  8: NodeParamsType type;
  9: optional string radioMac;
}

/**
 * @apiDefine BgpRouteInfo_SUCCESS
 * @apiSuccess (:BgpRouteInfo) {String} network The network's IPv6 address
 * @apiSuccess (:BgpRouteInfo) {String} nextHop
 *                             The next hop network's IPv6 address
 */
struct BgpRouteInfo {
  1: string network;
  2: string nextHop;
}

/**
 * @apiDefine BgpInfo_SUCCESS
 * @apiSuccess (:BgpInfo) {String} ipv6Address The BGP neighbor's IPv6 address
 * @apiSuccess (:BgpInfo) {Boolean} online
 *                        Whether the neighbor is online or not
 * @apiSuccess (:BgpInfo) {Int32} asn
 *                        Autonomous System Number
 * @apiSuccess (:BgpInfo) {String} upDownTime
 *                        Connection up/down time in hh:mm:ss format
 * @apiSuccess (:BgpInfo) {String} stateOrPfxRcd
 *                        If connected, the number of prefixes received,
 *                        otherwise the current state of the neighbor
 * @apiSuccess (:BgpInfo) {Object(BgpRouteInfo)[]} advertisedRoutes
 *                        List of advertised routes
 * @apiSuccess (:BgpInfo) {Object(BgpRouteInfo)[]} receivedRoutes
                          List of received routes
 */
struct BgpInfo {
  1: string ipv6Address;
  2: bool online;
  3: i32 asn;
  4: string upDownTime;
  5: string stateOrPfxRcd;
  6: list<BgpRouteInfo> advertisedRoutes;
  7: list<BgpRouteInfo> receivedRoutes;
}

/**
 * @apiDefine RadioStatus_SUCCESS
 * @apiSuccess (:RadioStatus) {Boolean} initialized
 *                                      Whether the radio is initialized
 * @apiSuccess (:RadioStatus) {Boolean} gpsSync Whether the GPS is in sync
 * @apiSuccess (:RadioStatus) {Boolean} nodeParamsSet Whether any NodeParams
 *                                      message has been sent and acknowledged
 */
struct RadioStatus {
  1: bool initialized = false;
  2: bool gpsSync = false;
  3: bool nodeParamsSet = false;
}

/**
 * @apiDefine StatusReport_SUCCESS
 * @apiSuccess (:StatusReport) {Int64} timeStamp
 *                             The time at which this response was received
 * @apiSuccess (:StatusReport) {String} ipv6Address
 *                             The globally-reachable IPv6 address of the minion
 * @apiSuccess (:StatusReport) {String} version
 *                             The current minion version (from "/etc/tgversion")
 * @apiSuccess (:StatusReport) {String} ubootVersion
 *                             The uboot version string (obtained at startup)
 * @apiSuccess (:StatusReport) {Int(NodeStatusType)=1,2,3} status
 *                             The ignition state of the minion
 *                             (1=OFFLINE, 2=ONLINE, 3=ONLINE_INITIATOR)
 * @apiSuccess (:StatusReport) {Object(UpgradeStatus)} upgradeStatus
 *                             The upgrade status
 * @apiSuccess (:StatusReport) {String} hardwareModel
 *                             The hardware model
 * @apiSuccess (:StatusReport) {Map(String:Object(BgpInfo))} bgpStatus
 *                             If this is a POP node, this will contain a map of
 *                             BGP neighbor IPs to summary and route information
 * @apiSuccess (:StatusReport) {String} hardwareBoardId
 *                             The hardware board identity
 * @apiSuccess (:StatusReport) {Int64} nodeReachability
 *                             Percentage of ack success for status reports sent
 *                             from node
 * @apiSuccess (:StatusReport) {Map(String:Boolean} neighborConnectionStatus
 *                             Map of remote mac addresses for this node's wired
 *                             connections to status of those conenctions
 * @apiSuccess (:StatusReport) {Map(String:String)} networkInterfaceMacs
 *                             Map of network interfaces on node to
 *                             MAC addresses of the interfaces
 * @apiSuccess (:StatusReport) {Int64} lastAckGpsTimestamp
 *                             The GPS timestamp when the node received the
 *                             last status report ACK (microseconds since
 *                             GPS epoch).
 * @apiSuccess (:StatusReport) {Int64} sentGpsTimestamp
 *                             The GPS timestamp when the node is sending
 *                             this status report (microseconds since GPS
 *                             epoch).
 * @apiSuccess (:StatusReport) {Map(String:Object(RadioStatus))} radioStatus
 *                             Map of radio MAC addresses to status information
 * @apiSuccess (:StatusReport) {String} firmwareVersion
 *                             The wireless firmware version
 */
 // NOTE: Some fields will be omitted after the controller initially learns
 // them, to save bandwidth. This list includes:
 //   version, ubootVersion, hardwareModel, hardwareBoardId, firmwareVersion,
 //   networkInterfaceMacs
struct StatusReport {
  1: i64 timeStamp;  // timestamp at which this response was received
  2: string ipv6Address;  // global-reachable IPv6 address for minion
  3: string version; // current minion version obtained from "/etc/tgversion"
  6: string ubootVersion; // uboot version string obtained during startup
  4: Topology.NodeStatusType status; // ignition state of minion
  5: UpgradeStatus upgradeStatus;
  7: string configMd5;
  // 8: nodeIsPrimary (deprecated in RELEASE_M74)
  9: string hardwareModel;
  // map from bgp neighbor addresses to bgp information for pop nodes
  10: optional map<string /* neighborIp */, BgpInfo>
      (cpp.template = "std::unordered_map") bgpStatus;
  11: string hardwareBoardId;
  12: optional Topology.NodeType nodeType;
  13: optional double nodeReachability;
  14: optional map<string /* macAddress */, bool>
      (cpp.template = "std::unordered_map") neighborConnectionStatus;
  15: optional map<string /* networkInterfaceName */, string /* macAddress */>
      (cpp.template = "std::unordered_map") networkInterfaceMacs;
  // GPS timestamp (microseconds) when the node received the last status
  // report ACK
  16: i64 lastAckGpsTimestamp (cpp.type = "std::uint64_t");
  // GPS timestamp (microseconds) when the node sent this status report
  17: i64 sentGpsTimestamp (cpp.type = "std::uint64_t");
  18: map<string /* radioMac */, RadioStatus>
      (cpp.template = "std::unordered_map") radioStatus;
  19: string firmwareVersion;
} (no_default_comparators)

struct StatusReportAck {
  1: bool requestFullStatusReport;
}

// Used locally only
struct GetStatusReport {}

/**
 * @apiDefine GetStatusDump
 */
struct GetStatusDump {}

/**
 * @apiDefine StatusDump_SUCCESS
 * @apiSuccess {Int64} timeStamp
 *             The time at which this response was generated
 * @apiSuccess {Map(String:Object(StatusReport))} statusReports
 *             The per-node status reports
 * @apiSuccess {String} [version]
 *             The controller version sourced from "/etc/tgversion"
 */
struct StatusDump {
  1: i64 timeStamp;  // timestamp at which this response was generated
  2: map<string /* node id */, StatusReport>
     (cpp.template = "std::unordered_map") statusReports;
  3: optional string version;
} (no_default_comparators)

struct GetCtrlNeighborsReq {
  // The list of network devices to query for IPv6 neighbors
  1: list<string> devices;
}

struct GetCtrlNeighborsResp {
  // The list of network devices to query for IPv6 neighbors
  1: list<string> devices;
  // The unique identifier that will be attached to all associated node reports
  2: string reqId;
}

// Sent from ctrl to API/CLI after GetCtrlNeighborsResp
struct GetNeighborsResp {
  // Map from devices to a list of ipv6 neighbors on that device
  1: map<string /* device */, list<MinionNeighbor>>
     (cpp.template = "std::unordered_map") deviceNeighborsMap;
  // The unique identifier that will be attached to all associated node reports
  2: string reqId;
  // Minion mac addr that sends this response
  4: string minion;
} (no_default_comparators)

// States from ip-neighbour(8) man page
enum MinionNeighborState {
  PERMANENT = 0,
  NOARP = 1,
  REACHABLE = 2,
  STALE = 3,
  NONE = 4,
  INCOMPLETE = 5,
  DELAY = 6,
  PROBE = 7,
  FAILED = 8,
  UNKNOWN = 9, // Default in case parsing the neighbors goes wrong
}

// Represents a single ipv6 neighbor
struct MinionNeighbor {
  1: string ipv6Address;
  2: string macAddr;
  3: MinionNeighborState state;
}

struct GetMinionNeighborsReq {
  // The list of network devices to query for IPv6 neighbors
  1: list<string> devices;
  // The unique identifier that will be attached to all associated node reports
  2: string reqId;
  // App/zmq socket that sent the original GetCtrlNeighborsReq
  3: string senderApp;
}

// Sent back from minion to ctrl
struct GetMinionNeighborsResp {
  // Map from devices to a list of ipv6 neighbors on that device
  1: map<string /* device */, list<MinionNeighbor>>
     (cpp.template = "std::unordered_map") deviceNeighborsMap;
  // The unique identifier that will be attached to all associated node reports
  2: string reqId;
  // App/zmq socket that sent the original GetCtrlNeighborsReq
  3: string senderApp;
} (no_default_comparators)

struct UpdateLinkMetrics {}

/**
 * @apiDefine RestartMinionRequest
 * @apiParam {String[]} nodes The list of nodes to restart minion on
 * @apiParam {Int32} secondsToRestart The number of seconds until minion restart
 */
 struct RestartMinionRequest {
  1: list<string> nodes;
  3: i32 secondsToRestart;
}

struct RestartMinion {
  1: i32 secondsToRestart;
}

struct GetGpsTime {}

struct GpsTime {
  // UNIX time on the controller (nanoseconds)
  1: i64 unixTimeNanoseconds;
  // GPS time on the controller (microseconds)
  2: i64 gpsTimeMicroseconds;
}

struct GpsEnabled {}

struct ControllerConnected {}

/**
 * @apiDefine GetSysdump
 * @apiParam {String} node The node to fetch a sysdump from
 */
struct GetSysdump {
  // the node to fetch a sysdump from
  1: string node;
}

/**
 * @apiDefine GetSysdump_SUCCESS
 * @apiSuccess {bool} success Successfully started sysdump
 * @apiSuccess {string} filename The filename of the sysdump tarball
 * @apiSuccess {string} error Error message
 */
struct GetSysdumpResp {
  // successfully started sysdump
  1: bool success;
  // the filename of the sysdump tarball
  2: string filename;
  // error message
  3: string error;
}

struct GetMinionSysdump {
  // the URL to upload the sysdump to
  1: string url;
  // the original requester
  2: string sender;
}

struct MinionSysdumpStarted {
  // the original requester
  1: string sender;
  // error message
  2: string error;
  // sysdump tarball filename
  3: string filename;
}

struct MinionSysdumpComplete {
  // the original requester
  1: string sender;
  // error message
  2: string error;
}

#############  IgnitionApp ##############

/**
 * @apiDefine GetIgnitionState
 */
struct GetIgnitionState {}

/**
 * @apiDefine IgnitionParams
 * @apiParam {Boolean} [enable] The state of network-wide ignition
 * @apiParam {Int64} [linkUpInterval] The frequency of ignition
 * @apiParam {Int64} [linkUpDampenInterval]
 *           The interval of ignition on the same link
 * @apiParam {Map(String:Boolean)} [linkAutoIgnite]
 *           The per-link auto ignition (linkName:enable)
 * @apiParam {Int64} [bfTimeoutSec]
 *           The ignition timeout on each minion (also accounting for network
 *           delays), in seconds
 */
/**
 * @apiDefine IgnitionParams_SUCCESS
 * @apiSuccess (:IgnitionParams) {Boolean} [enable]
 *                               The state of network-wide ignition
 * @apiSuccess (:IgnitionParams) {Int64} [linkUpInterval]
 *                               The frequency of ignition
 * @apiSuccess (:IgnitionParams) {Int64} [linkUpDampenInterval]
 *                               The interval of ignition on the same link
 * @apiSuccess (:IgnitionParams) {Map(String:Boolean)} [linkAutoIgnite]
 *                               The per-link auto ignition (linkName:enable)
 * @apiSuccess (:IgnitionParams) {Int64} [bfTimeoutSec]
 *                               The ignition timeout on each minion (also
 *                               accounting for network delays), in seconds
 */
// Parameters controlling the ignition in the controller
struct IgnitionParams {
  1: optional bool enable;
  2: optional i64 linkUpInterval;
  3: optional i64 linkUpDampenInterval;
  4: optional map<string /* link name */, bool>
     (cpp.template = "std::unordered_map") linkAutoIgnite;
  5: optional i64 bfTimeoutSec;
} (no_default_comparators)

/**
 * @apiDefine SetLinkStatusReq
 * @apiParam {Int(LinkActionType)=1,2} action
 *           The link update action (1=LINK_UP, 2=LINK_DOWN)
 * @apiParam {String} initiatorNodeName The initiator node
 * @apiParam {String} responderNodeName The responder node
 */
// Set Link Status Request sent from cli to controller ignition app
// instructs controller to send a SetLinkStatus msg to initiator node
struct SetLinkStatusReq {
  1: LinkActionType action; // link update action (up/down)
  2: string initiatorNodeName;
  3: string responderNodeName;
}

/**
 * @apiDefine ForceDissocReq
 * @apiParam {String} initiatorMac The initiator MAC address
 * @apiParam {String} responderMac The responder MAC address (does not need to
 *                                 be in the topology)
 */
struct ForceDissocReq {
  1: string initiatorMac;
  2: string responderMac;
}

/**
 * @apiDefine IgnitionCandidate_SUCCESS
 * @apiSuccess (:IgnitionCandidate) {String} initiatorNodeName
 *                                  The name of the initiator node
 * @apiSuccess (:IgnitionCandidate) {String} linkName The link name
 */
struct IgnitionCandidate {
  1: string initiatorNodeName;
  2: string linkName;
}

/**
 * @apiDefine IgnitionState_SUCCESS
 * @apiSuccess {Object(IgnitionCandidate)[]} igCandidates
 *             The ignition candidates
 * @apiSuccess {Object(IgnitionCandidate)[]} lastIgCandidates
 *             The last ignition candidates
 * @apiSuccess {Object(IgnitionParams)} igParams The ignition parameters
 */
struct IgnitionState {
  // 1: (deprecated)
  2: list<IgnitionCandidate> igCandidates;
  3: list<IgnitionCandidate> lastIgCandidates;
  4: IgnitionParams igParams;
}

// Set Link Status message sent from controller to minion on node
// instructs initiator node to perfrom link association / dissociation
// controller expects to receive Link Status message after this request
struct SetLinkStatus {
  1: LinkStatusType linkStatusType; // whether it's link up or link down
  2: string responderMac; // responder mac address
  // link up specific parameters
  3: optional Topology.NodeType responderNodeType; // responder node type
  4: optional Topology.GolayIdx golayIdx; // responder golay code
  5: optional i64 controlSuperframe;  // control superframe for the link
  6: optional Topology.PolarityType responderNodePolarity;  // responder Node
                                                            // Polarity
  7: string initiatorMac; // initiator mac address
}

// GetLinkStatus messge sent from controller to minion on node
struct GetLinkStatus {
  1: string responderMac; // responder mac address
}

// Link Status message sent from minion (initiator/responder) to
// controller (TopologyApp) indicates link status change:
// LINK_UP / LINK_DOWN (LINK_PAUSE)
struct LinkStatus {
  1: string responderMac; // mac address of the other end of link
  2: LinkStatusType linkStatusType; // whether link is up or down
  3: optional string radioMac;
  4: optional string ifname;
  5: bool isEvent; // true upon change (e.g. FW event), false upon query
}

// GetLinkStatusDump messge sent to minion on node
struct GetLinkStatusDump {}

struct LinkStatusDump {
  1: map<string /* responderMac */, LinkStatus>
     (cpp.template = "std::unordered_map") linkStatusDump;
}

// Link Status event forwarded from controller TopologyApp to IgnitionApp
struct LinkStatusEvent {
  // 1: string linkName  (deprecated in RELEASE_M81)
  2: LinkStatusType linkStatusType;
  3: string nodeName;  // node that reported this event
  4: Topology.Link link;
}

struct BfRespScan {
  1: string radioMac;
  2: i32 secondsToDisable;
}

struct LinkAuthorized {
  1: string responderMac; // mac address of the other end of link
  2: string radioMac;
  3: string ifname;
  4: i64 wsecEnable;
}

#############  TopologyApp ##############

/**
 * @apiDefine GetTopology
 */
struct GetTopology {}

/**
 * @apiDefine GetNetworkAirtime
 */
struct GetNetworkAirtime {}

struct SetNodeStatus {
  1: string nodeMac;
  2: Topology.NodeStatusType nodeStatus;
}

struct SetNodeParamsReq {
  1: string nodeMac;
  2: optional BWAllocation.NodeAirtime nodeAirtime;
  // 3: (deprecated in RELEASE_M27)
  // 4: nodeIsPrimary (deprecated in RELEASE_M74);
}

struct SetNetworkParamsReq {
  1: optional BWAllocation.NetworkAirtime networkAirtime;
  // 2: (deprecated in RELEASE_M27)
  // 3: channel (deprecated in RELEASE_M40)
}

/**
 * @apiDefine SetNodeMac
 * @apiParam {String} nodeName The node name
 * @apiParam {String} nodeMac The MAC address to set
 * @apiParam {Boolean} force Force set
 */
/**
 * @apiDefine SetNodeMac_GROUP
 * @apiParam (:SetNodeMac) {String} nodeName The node name
 * @apiParam (:SetNodeMac) {String} nodeMac The MAC address to set
 * @apiParam (:SetNodeMac) {Boolean} force Force set
 */
struct SetNodeMac {
  1: string nodeName;
  2: string nodeMac;
  3: bool force;
}

/**
 * @apiDefine SetNodeMacList
 * @apiParam {Object(SetNodeMac)[]} setNodeMacList
 *           The list of node MAC addresses to set
 */
struct SetNodeMacList {
  1: list<SetNodeMac> setNodeMacList;
}

struct SetWiredLinkStatus {
  1: string nodeMac;
  2: map<string /* secondaryMacAddress */, bool /* connectionStatus */>
     (cpp.template = "std::unordered_map") linkStatus;
} (no_default_comparators)

/**
 * @apiDefine AddNodeWlanMacs
 * @apiParam {String} nodeName The node name
 * @apiParam {String[]} wlanMacs Wlan MAC addresses to add
 */
struct AddNodeWlanMacs {
  1: string nodeName;
  2: list<string> wlanMacs;
}

/**
 * @apiDefine DelNodeWlanMacs
 * @apiParam {String} nodeName The node name
 * @apiParam {String[]} wlanMacs Wlan MAC addresses to delete
 * @apiParam {Boolean} force Force delete
 */
struct DelNodeWlanMacs {
  1: string nodeName;
  2: list<string> wlanMacs;
  3: bool force;
}

/**
 * @apiDefine ChangeNodeWlanMac
 * @apiParam {String} nodeName The node name
 * @apiParam {String} oldWlanMac Wlan MAC address to change
 * @apiParam {String} newWlanMac New wlan MAC address
 * @apiParam {Boolean} force Force set
 */
struct ChangeNodeWlanMac {
  1: string nodeName;
  2: string oldWlanMac;
  3: string newWlanMac;
  4: bool force;
}

/**
 * @apiDefine SetTopologyName
 * @apiParam {String} name The new topology name
 */
struct SetTopologyName {
  1: string name;
}

struct BumpLinkUpAttempts {
  1: string linkName;
}

/**
 * @apiDefine GetNode
 * @apiParam {String} name The node name
 */
struct GetNode {
  1: string name;
}

/**
 * @apiDefine AddNode
 * @apiParam {Object(Node)} node The node
 */
struct AddNode {
  1: Topology.Node node;
}

/**
 * @apiDefine DelNode
 * @apiParam {String} nodeName The node name
 * @apiParam {Boolean} force Force node deletion
 */
struct DelNode {
  1: string nodeName;
  2: bool force;
}

/**
 * @apiDefine EditNode
 * @apiParam {String} nodeName The node name
 * @apiParam {Object(Node)} newNode The new node parameters
 */
struct EditNode {
  1: string nodeName;
  2: Topology.Node newNode;
}

/**
 * @apiDefine GetLink
 * @apiParam {String} name The link name
 */
struct GetLink {
  1: string name;
}

/**
 * @apiDefine AddLink
 * @apiParam {Object(Link)} link The link
 */
struct AddLink {
  1: Topology.Link link;
}

/**
 * @apiDefine DelLink
 * @apiParam {String} aNodeName The A-node name
 * @apiParam {String} zNodeName The Z-node name
 * @apiParam {Boolean} force Force link deletion
 */
struct DelLink {
  1: string aNodeName;
  2: string zNodeName;
  3: bool force;
}

/**
 * @apiDefine GetSite
 * @apiParam {String} name The site name
 */
struct GetSite {
  1: string name;
}

/**
 * @apiDefine AddSite
 * @apiParam {Object(Site)} site The site
 */
struct AddSite {
  1: Topology.Site site;
}

/**
 * @apiDefine DelSite
 * @apiParam {String} siteName The site name
 */
struct DelSite {
  1: string siteName;
}

/**
 * @apiDefine EditSite
 * @apiParam {String} siteName The site name
 * @apiParam {Object(Site)} newSite The new site parameters
 */
struct EditSite {
  1: string siteName;
  2: Topology.Site newSite;
}

struct ResetTopologyState {
  1: bool resetLinkupAttempts;
}

/**
 * @apiDefine BulkAdd
 * @apiParam {Object(Site)[]} sites The sites to add
 * @apiParam {Object(Node)[]} nodes The nodes to add
 * @apiParam {Object(Link)[]} links The links to add
 */
struct BulkAdd {
  1: list<Topology.Site> sites;
  2: list<Topology.Node> nodes;
  3: list<Topology.Link> links;
}

/**
 * @apiDefine BulkAddResult_SUCCESS
 * @apiSuccess {Boolean} success The response status
 * @apiSuccess {String} message The response message
 * @apiSuccess {String[]} addedSites List of successfully added site names
 * @apiSuccess {Map(String:String)} failedSites
 *             Map of any failed site names to error messages
 * @apiSuccess {String[]} addedNodes List of successfully added node names
 * @apiSuccess {Map(String:String)} failedNodes
 *             Map of any failed node names to error messages
 * @apiSuccess {String[]} addedLinks List of successfully added link names
 * @apiSuccess {Map(String:String)} failedLinks
 *             Map of any failed link names to error messages
 */
// "success" and "message" fields here for backwards compat with E2EAck
struct BulkAddResult {
  1: bool success;
  2: string message;
  3: list<string> addedSites;
  4: map<string /* name */, string /* error */>
     (cpp.template = "std::unordered_map") failedSites;
  5: list<string> addedNodes;
  6: map<string /* name */, string /* error */>
     (cpp.template = "std::unordered_map") failedNodes;
  7: list<string> addedLinks;
  8: map<string /* name */, string /* error */>
     (cpp.template = "std::unordered_map") failedLinks;
}

/**
 * @apiDefine GetRoutes
 * @apiParam {String} srcNode The source node name
 * @apiParam {String} dstNode The destination node name
 */
struct GetRoutes {
  1: string srcNode;
  2: string dstNode;
}

/**
 * @apiDefine GetRoutesResp_SUCCESS
 * @apiSuccess {String[][]} routes
 *              The list of routes between the source and destination nodes
 */
struct GetRoutesResp {
  1: list<list<string /* node name */>> routes;
}

/**
 * @apiDefine GetDefaultRoutes
 * @apiParam {String[]} nodes The list of source node names
 */
struct GetDefaultRoutes {
  1: list<string /* node name */> nodes;
}

/**
 * @apiDefine GetDefaultRoutesResp_SUCCESS
 * @apiSuccess {Map(String:String[][])} defaultRoutes
 *              Map of node names to list of default routes
 */
struct GetDefaultRoutesResp {
  1: map<string /* node name */, list<list<string>>>
     (cpp.template = "std::unordered_map") defaultRoutes;
} (no_default_comparators)

/**
 * @apiDefine GetNodePrefixes
 */
struct GetNodePrefixes {}

/**
 * @apiDefine GetZonePrefixes
 */
struct GetZonePrefixes {}

/**
 * @apiDefine GetZonePrefixesResp_SUCCESS
 * @apiSuccess (:GetZonePrefixesResp) {Map(String:Set(String))} zonePrefixes
 *             Map of site names to zone prefixes
 */
struct GetZonePrefixesResp {
  1: map<
        string /* site name */,
        set<string> (cpp.template = "std::unordered_set")>
     (cpp.template = "std::unordered_map") zonePrefixes;
} (no_default_comparators)

/**
 * @apiDefine GetNodePrefixesResp_SUCCESS
 * @apiSuccess (:GetNodePrefixesResp) {Map(String:String)} nodePrefixes
 *             Map of node names to their assigned prefixes
 */
struct GetNodePrefixesResp {
  1: map<string /* node name */, string /* prefix */>
     (cpp.template = "std::unordered_map") nodePrefixes;
} (no_default_comparators)

/**
 * @apiDefine AllocatePrefixes
 */
struct AllocatePrefixes {}

struct PrefixAllocParamsUpdated {}

// openr local adjacency response back to fw
struct FwAdjResp {
  1: map<string, string> (cpp.template = "std::unordered_map") adjs;
} (no_default_comparators)

############# Scan App #############

/**
 * @apiDefine MicroRoute_SUCCESS
 * @apiSuccess (:MicroRoute) {Int16} tx
 *             The transmit beamforming index of the micro-route
 * @apiSuccess (:MicroRoute) {Int16} rx
 *             The receive beamforming index of the micro-route
 */
// transmit and receive beamforming indices of a micro route
struct MicroRoute {
  1: i16 tx;
  2: i16 rx;
}

/**
 * @apiDefine RouteInfo_SUCCESS
 * @apiSuccess (:RouteInfo) {Object(MicroRoute)} route
 *             The beamforming indices of the micro-route
 * @apiSuccess (:RouteInfo) {Double} rssi
 *             The received signal strength indicator (RSSI), in dBm
 * @apiSuccess (:RouteInfo) {Double} snrEst
 *             The signal-to-noise ratio (SNR) measured during the short
 *             training field, in dB
 * @apiSuccess (:RouteInfo) {Double} postSnr
 *             The signal-to-noise ratio (SNR) measured after the equalizer,
 *             in dB
 * @apiSuccess (:RouteInfo) {Int32} rxStart
 *             The relative arrival time of the packet, in microseconds
 * @apiSuccess (:RouteInfo) {Byte} packetIdx
 *             The repeat count of this packet (0-based)
 * @apiSuccess (:RouteInfo) {Int16} sweepIdx
 *             The sweep index of the packet (0-based)
 */
// individual micro-route measurement/report
struct RouteInfo {
  1: MicroRoute route; // beamforming indices of micro route
  2: double rssi;      // received signal strength, in dBm
  3: double snrEst;    // measured during the short training field, in dB
  4: double postSnr;   // measured after the equalizer, in dB
  5: i32 rxStart;      // relative arrival time of the packet, in us
  6: byte packetIdx;   // Repeat count of this packet, 0-based
  7: i16 sweepIdx;     // Sweep index for the packet, 0-based
}

enum ScanType {
  PBF = 1,      // Periodic beamforming
  IM = 2,       // Interference measurement
  RTCAL = 3,    // Runtime calibration
  CBF_TX = 4,   // Coordinated beamforming (aka interference nulling), tx side
  CBF_RX = 5,   // Same, rx side
  TOPO = 6,     // Topology_scan
  TEST_UPD_AWV = 7, // Test Update AWV
}

enum ScanMode {
  COARSE = 1,
  FINE = 2,
  SELECTIVE = 3,
  RELATIVE = 4, // Relative to the last Azimuth beam selected by FW
}

// SubType for Runtime Calibration and CBF
enum ScanSubType {
  NO_CAL = 0, // No calibration, init state
  TOP_RX_CAL = 1, // Top Panel, responder Rx cal with fixed intiator Tx beam
  TOP_TX_CAL = 2, // Top Panel, intiator Tx cal with fixed responder Rx beam
  BOT_RX_CAL = 3, // Bot Panel, responder Rx cal with fixed intiator Tx beam
  BOT_TX_CAL = 4, // Bot Panel, intiator Tx cal with fixed responder Rx beam
  VBS_RX_CAL = 5, // Top + Bot, responder Rx cal with fixed intiator Tx beam
  VBS_TX_CAL = 6, // Top + Bot, intiator Tx cal with fixed responder Rx beam
  RX_CBF_AGGRESSOR = 7, // RX Coordinated BF Nulling, Aggressor link
  RX_CBF_VICTIM = 8,    // RX Coordinated BF Nulling, Victim link
  TX_CBF_AGGRESSOR = 9, // TX Coordinated BF Nulling, Aggressor link
  TX_CBF_VICTIM = 10,   // TX Coordinated BF Nulling, Victim link
}

enum ScanFwStatus {
  COMPLETE = 0,
  INVALID_TYPE = 1,
  INVALID_START_TSF = 2,
  INVALID_STA = 3,
  AWV_IN_PROG = 4,
  STA_NOT_ASSOC = 5,
  REQ_BUFFER_FULL = 6,
  LINK_SHUT_DOWN = 7,
  UNSPECIFIED_ERROR = 8,
  UNEXPECTED_ERROR = 9,
  EXPIRED_TSF = 10,
  INCOMPL_RTCAL_BEAMS_FOR_VBS = 11,
}

/**
 * @apiDefine BeamIndices_GROUP
 * @apiParam (:BeamIndices) {Int32} low The low beam index
 * @apiParam (:BeamIndices) {Int32} high The high beam index
 */
struct BeamIndices {
  1: i32 low;
  2: i32 high;
}

struct BeamInfo {
  1: string addr; // Peer MAC address for beam
  2: i16 beam;    // Beam index used in corresponding RouteInfo
}

struct BfSlotExclusionReq {
  1: string radioMac; // radio mac address
  2: i64 startBwgdIdx; // start time of scan in BWGD index
}

/**
 * @apiDefine StartContinuousTopoScan
 * @apiParam {String} radioMac The radio MAC address
 * @apiParam {Int32} durationSec
 *           The scan duration (in seconds), or zero to stop any running scans
 */
struct StartContinuousTopoScan {
  1: string radioMac;
  2: i32 durationSec;
}

struct ScanReq {
  17: optional string radioMac;
  1: i32 token; // token to match request to response
  13: optional ScanType scanType;
  2: ScanMode scanMode; // scan mode
  3: i64 startBwgdIdx; // start time of scan in BWGD index
  4: bool bfScanInvertPolarity; // Invert Polarity when using with same
                                // Polarity peer
  5: optional string txNodeMac; // tx node id (only present for receivers)
  6: optional string rxNodeMac; // broadcast or specific node (for tx only)
  7: optional list<MicroRoute> routes; // for partial scan, absent for full scan
  8: optional BeamIndices beams; // Beam indices range
  9: optional bool apply; // 1 - Apply new beams, 0 - ignore new beams
  // These are for selective scan
  10: optional ScanSubType subType;
  11: optional byte bwgdLen;

  // This is to control tx power
  12: optional i16 txPwrIndex; // tx power index (0 - 31)

  // For CBF scans
  14: optional i16 nullAngle;
  15: optional i16 cbfBeamIdx;
  16: optional bool isAggressor;  // victim if false
}

/**
 * @apiDefine TopoResponderInfo_SUCCESS
 * @apiSuccess (:TopoResponderInfo) {String} addr
 *             The MAC address of the responder
 * @apiSuccess (:TopoResponderInfo) {Object(Location)} [pos]
 *             The GPS position of the responder
 * @apiSuccess (:TopoResponderInfo) {Map(Int16:Map(Int16:Int16))} [itorLqmMat]
 *             The initiator-to-responder micro-route link quality metric
 *             (LQM) matrix
 * @apiSuccess (:TopoResponderInfo) {Map(Int16:Map(Int16:Int16))} [rtoiLqmMat]
 *             The responder-to-initiator micro-route link quality metric
 *             (LQM) matrix
 * @apiSuccess (:TopoResponderInfo) {Set(String)} [adjs]
 *             The set of local adjacencies at the responder
 */
// The responder information in topology scans
struct TopoResponderInfo {
  1: string addr;
  2: optional Topology.Location pos;
  3: map<i16, map<i16, i16> (cpp.template = "std::unordered_map")>
     (cpp.template = "std::unordered_map") itorLqmMat;
  4: map<i16, map<i16, i16> (cpp.template = "std::unordered_map")>
     (cpp.template = "std::unordered_map") rtoiLqmMat;
  5: set<string> (cpp.template = "std::unordered_set") adjs;
} (no_default_comparators)

/**
 * @apiDefine ScanResp_SUCCESS
 * @apiSuccess (:ScanResp) {String} radioMac
 *             The MAC address of the radio that performed the scan
 * @apiSuccess (:ScanResp) {Int32} token
 *             The token used to match the request to the response
 * @apiSuccess (:ScanResp) {Int64} curSuperframeNum
 *             The superframe number, as a timestamp of the measurement
 * @apiSuccess (:ScanResp) {Object(RouteInfo)[]} routeInfoList
 *             The list of routes
 * @apiSuccess (:ScanResp) {Int16} [txPwrIndex]
 *             The transmit power index used for the scan (0-31)
 * @apiSuccess (:ScanResp) {Int(ScanFwStatus)} status
 *             The scan status (0: complete, >=1: failure)
 * @apiSuccess (:ScanResp) {Int16} [numSweeps]
 *             The number of times that beams were scanned
 * @apiSuccess (:ScanResp) {Int64} [startSuperframeNum]
 *             The start of the BF slot allocation
 * @apiSuccess (:ScanResp) {Int64} [endSuperframeNum]
 *             The end of the BF slot allocation
 * @apiSuccess (:ScanResp) {Int16} [azimuthBeam]
 *             The beam selected from the golden codebook during initial BF or
 *             PBF
 * @apiSuccess (:ScanResp) {Int16} [oldBeam]
 *             The old beam (used by RTCAL, VBS, and CBF)
 * @apiSuccess (:ScanResp) {Int16} [newBeam]
 *             The resultant beam
 * @apiSuccess (:ScanResp) {Int16} [sweepStartBeam]
 *             The sweep start beam
 * @apiSuccess (:ScanResp) {Int16} [sweepEndBeam]
 *             The sweep end beam
 * @apiSuccess (:ScanResp) {Map(Int16:Object(TopoResponderInfo))} [topoResps]
 *             The map of responders to topology scan results
 *             (responderIndex:info)
 * @apiSuccess (:ScanResp) {Object(BeamInfo)[]} beamInfoList
 *             The list of beam info
 */
struct ScanResp {
   17: optional string radioMac;
   1: i32 token;
   2: i64 curSuperframeNum;
   3: list<RouteInfo> routeInfoList;
   4: optional i16 txPwrIndex;
   5: ScanFwStatus status;
   7: optional i16 numSweeps;
   8: optional i64 startSuperframeNum;
   9: optional i64 endSuperframeNum;
   10: optional i16 azimuthBeam;
   11: optional i16 oldBeam;
   12: optional i16 newBeam;
   13: optional i16 sweepStartBeam;
   14: optional i16 sweepEndBeam;
   15: optional map<i16 /* Responder index */, TopoResponderInfo>
       (cpp.template = "std::unordered_map") topoResps;
   16: optional list<BeamInfo> beamInfoList;
} (no_default_comparators)

/**
 * @apiDefine StartScan
 * @apiParam {Int(ScanType)} scanType
 *           The scan type (1=PBF, 2=IM, 3=RTCAL, 4=CBF_TX, 5=CBF_RX, 6=TOPO,
 *                          7=TEST_UPD_AWV)
 * @apiParam {Int(ScanMode)} scanMode
 *           The scan mode (1=COARSE, 2=FINE, 3=SELECTIVE, 4=RELATIVE)
 * @apiParam {Int64} startTime
 *           The scan start time (UNIX time), or 0 for TOPO scans to run
 *           immediately
 * @apiParam {String} [txNode]
 *           The transmitter node.
 *           If present, run the scan on transmitter-to-receiver links.
 *           Otherwise, run an IM scan on the whole network.
 * @apiParam {String[]} [rxNodes]
 *           The receiver nodes.
 *           This should be present if and only if txNode is given.
 *           For PBF/RTCAL, this list should contain a single node.
 * @apiParam {Object(BeamIndices)[]} [beams]
 *           The beam indices for every node (the transmitter and all
 *           receivers, in that order). If unset, use the default indices.
 * @apiParam {Boolean} [apply]
 *           Whether to apply the new beams after the procedure (true) or
 *           ignore them (false)
 * @apiParam {Int(ScanSubType)} [subType]
 *           The scan subtype (used in CBF/RTCAL)
 * @apiParam {Byte} [bwgdLen]
 *           The calibration length in BWGDs (2-64)
 * @apiParam {Int16} [txPwrIndex]
 *           The transmit power index (0-31, 255=current average power).
 * @apiParam {String} [mainTxNode]
 *           The main transmitter node
 *           (victim for CBF_RX, aggressor for CBF_TX)
 * @apiParam {String} [mainRxNode]
 *           The main receiver node
 *           (victim for CBF_RX, aggressor for CBF_TX)
 * @apiParam {String[]} [auxTxNodes]
 *           The auxiliary transmitter nodes (for CBF) -
 *           should be of the same length as auxRxNodes
 * @apiParam {String[]} [auxRxNodes]
 *           The auxiliary receiver nodes (for CBF) -
 *           should be of the same length as auxTxNodes
 * @apiParam {Int16[]} [auxTxPwrIndex]
 *           The transmit power indicies of auxTxNodes (for CBF)
 * @apiParam {Int16} [nullAngle]
 *           The nulling angle (for CBF)
 * @apiParam {Int16} [cbfBeamIdx]
 *           The beam index (for CBF)
 * @apiParam {Boolean} [setConfig]
 *           If true, set CBF config for a link instead of actually executing
 *           a one-time scan
 */
struct StartScan {
  1: ScanType scanType;
  2: ScanMode scanMode;
  3: i64 startTime;
  4: optional string txNode;
  5: optional list<string> rxNodes;
  6: optional list<BeamIndices> beams;
  7: optional bool apply;
  8: optional ScanSubType subType;
  9: optional byte bwgdLen;
  10: optional i16 txPwrIndex;
  // For CBF TX/RX
  11: optional string mainTxNode; // vtx for CBF_RX, atx for CBF_TX
  12: optional string mainRxNode; // vrx for CBF_RX, arx for CBF_TX
  // The lists should be of equal lengths
  13: optional list<string> auxTxNodes; // atx for CBF_RX, vtx for CBF_TX
  14: optional list<string> auxRxNodes; // arx for CBF_RX, vrx for CBF_TX
  15: optional list<i16> auxTxPwrIndex;
  16: optional i16 nullAngle;
  17: optional i16 cbfBeamIdx;
  18: optional bool setConfig; // 0 - One-time scan, 1 - update config
  19: optional i64 applyBwgdIdx;
}

/**
 * @apiDefine StartScanResp_SUCCESS
 * @apiSuccess {Boolean} success The response status
 * @apiSuccess {String} message The response message
 * @apiSuccess {Int32} [token] The token (used for retrieving scan data)
 * @apiSuccess {Int32} [lastToken] The token at the end of the token range,
 *                                 if multiple scans were scheduled
 */
struct StartScanResp {
  1: bool success;
  2: string message;
  3: optional i32 token;
  4: optional i32 lastToken;
}

struct CbfConfig {
  1: map<string, StartScan> (cpp.template = "std::unordered_map") config;
} (no_default_comparators)

struct RfImData {
  // (TX beam, RX beam) -> scaled pathloss (RX SNR for TX power 0)
  // Beams are bitpacked as: RX beam in key[31:16], TX beam in key[15:0]
  1: map<i32, double> (cpp.template = "std::unordered_map") routes;
  2: i32 scanId;
  3: i16 scanPower;
  4: i16 bestTxBeam;
  5: i16 bestRxBeam;
} (no_default_comparators)

struct RfRelImData {
  // RX link -> scaled pathloss (RX SNR for TX power 0)
  1: map<string, double> (cpp.template = "std::unordered_map") routes;
  2: i32 scanId;
  3: i16 scanPower;
} (no_default_comparators)

struct RfLinkState {
  1: i16 txBeam;
  2: i16 rxBeam;
  3: i16 txPower;
}

struct RfState {
  1: map<string, RfImData> (cpp.template = "std::unordered_map") im;
  2: map<string, RfLinkState> (cpp.template = "std::unordered_map") link;
  3: bool dirty;
  4: map<string, RfRelImData> (cpp.template = "std::unordered_map") relIm;
} (no_default_comparators)

/**
 * @apiDefine GetScanStatus
 * @apiParam {Boolean} isConcise If true, only metadata will be returned
 *           (without RSSI and SNR measurements)
 * @apiParam {Int32} [tokenFrom] The start of the token range; if omitted,
 *           returns the full scan results. if tokenTo
 *           is not specified, returns only token == tokenFrom
 * @apiParam {Int32} [tokenTo] The end of the token range, inclusive (must also
 *           specify tokenFrom); if omitted, takes the value of tokenFrom
 * @apiParam {Int32} [respIdFrom] The start of the respId range (must also
 *           specify respIdTo) (if specified, ignore tokenFrom); if respIdTo
 *           is not specified, returns only respId == respIdFrom
 * @apiParam {Int32} [respIdTo] The end of the respId range inclusive (must also
 *           specify respIdFrom); if oldest respId > respIdTo; will return
 *           the oldest scan result corresponding to the oldest respId
 *           (if specified, ignore tokenTo)
 */
struct GetScanStatus {
  1: bool isConcise;
  2: optional i32 tokenFrom;
  3: optional i32 tokenTo;
  4: optional i32 respIdFrom;
  5: optional i32 respIdTo;
}

/**
 * @apiDefine ResetScanStatus
 */
struct ResetScanStatus {
  1: optional i64 junk;
}

/**
 * @apiDefine ScanData_SUCCESS
 * @apiSuccess (:ScanData) {Map(String:Object(ScanResp))} responses
 *             The scan responses (node:response)
 * @apiSuccess (:ScanData) {String} txNode The transmitter node
 * @apiSuccess (:ScanData) {Int64} startBwgdIdx
 *             The starting bandwidth grant duration (BWGD) index
 * @apiSuccess (:ScanData) {Int(ScanType)} type
 *             The scan type (1=PBF, 2=IM, 3=RTCAL, 4=CBF_TX, 5=CBF_RX, 6=TOPO,
 *                            7=TEST_UPD_AWV)
 * @apiSuccess (:ScanData) {Int(ScanSubType)} [subType]
 *             The scan subtype (used in CBF/RTCAL)
 * @apiSuccess (:ScanData) {Int(ScanMode)} mode
 *             The scan mode (1=COARSE, 2=FINE, 3=SELECTIVE, 4=RELATIVE)
 * @apiSuccess (:ScanData) {Boolean} [apply]
 *             Whether to apply the new beams after the procedure (true) or
 *             ignore them (false)
 * @apiSuccess (:ScanData) {Int16} [nResponsesWaiting]
 *             The number of node responses still outstanding
 * @apiSuccess (:ScanData) {Int32} respId
 *             The response ID - increments when all responses are received
 * @apiSuccess (:ScanData) {String} [mainTxNode]
 *             The main transmitter node
 *             (victim for CBF_RX, aggressor for CBF_TX)
 * @apiSuccess (:ScanData) {String} [mainRxNode]
 *             The main receiver node
 *             (victim for CBF_RX, aggressor for CBF_TX)
 * @apiSuccess (:ScanData) {String[]} [auxTxNodes]
 *             The auxiliary transmitter nodes (for CBF) -
 *             should be of the same length as auxRxNodes
 * @apiSuccess (:ScanData) {String[]} [auxRxNodes]
 *             The auxiliary receiver nodes (for CBF) -
 *             should be of the same length as auxTxNodes
 * @apiSuccess (:ScanData) {Int16} [nullAngle]
 *             The nulling angle (for CBF)
 * @apiSuccess (:ScanData) {Int16} [cbfBeamIdx]
 *             The beam index (for CBF)
 * @apiSuccess (:ScanData) {Boolean} [convertMacToName]
 *             Internal to the controller, do not set. Used for backward
 *             compatibility when using node names rather than MACs to denote
 *             nodes to scan. When set, controller will convert results from
 *             using MACs back to node names.
 */
// Data collected from a single scan.
// Filled in incrementally, as responses arrive.
struct ScanData {
  1: map<string /* nodename */, ScanResp>
     (cpp.template = "std::unordered_map") responses;
  2: string txNode;
  3: i64 startBwgdIdx;
  4: ScanType type;
  5: optional ScanSubType subType;
  6: ScanMode mode;
  7: optional bool apply;
  8: optional i16 nResponsesWaiting;
  9: i32 respId;
  10: optional string mainTxNode;
  11: optional string mainRxNode;
  // The lists should be of equal lengths
  12: optional list<string> auxTxNodes;
  13: optional list<string> auxRxNodes;
  14: optional i16 nullAngle;
  15: optional i16 cbfBeamIdx;
  16: optional i64 applyBwgdIdx;
  17: optional list<string> rxNodes;
  18: optional i32 groupId;
  // convertMacToName is internal to the controller, do not set
  19: optional bool convertMacToName;
} (no_default_comparators)

/**
 * @apiDefine ScanStatus_SUCCESS
 * @apiSuccess {Map(Int32:Object(ScanData))} scans The scan data (token:data)
 */
struct ScanStatus {
  1: map<i32 /* token */, ScanData> scans;
} (no_default_comparators)

// Completed scan result (sent to TopologyBuilderApp)
struct ScanResult {
  1: i32 token;
  2: ScanData data;
}

// Wrapper around scan result for Kafka consumers
struct ScanResultEventWrapper {
  1: ScanResult result;
  2: optional string topologyName;
}

struct GetCbfConfig {}

struct SetCbfConfig {}

struct ResetCbfConfig {}

struct GetRfState {}

struct SetRfState {
  1: optional RfState rfState;
  2: optional ScanStatus scanStatus;
}

struct ResetRfState {}

struct SetLaTpcParams {}

struct ClearLaTpcParams {}

/**
 * @apiDefine ScanSchedule
 * @apiParam {Int32} [imScanTimeoutSec]
 *           The interval between periodic IM scans (in seconds)
 * @apiParam {Int32} [combinedScanTimeoutSec]
 *           The interval between periodic combined scans (in seconds)
 * @apiParam {Boolean} pbfEnable Whether PBF is enabled for combined scans
 * @apiParam {Boolean} rtcalEnable Whether RTCAL is enabled for combined scans
 * @apiParam {Boolean} cbfEnable Whether CBF is enabled for combined scans
 * @apiParam {Boolean} imEnable Whether fast IM is enabled for combined scans
 */
struct ScanSchedule {
  1: optional i32 imScanTimeoutSec;
  2: optional i32 combinedScanTimeoutSec;
  // 3 (deprecated)
  4: bool pbfEnable;
  5: bool rtcalEnable;
  6: bool cbfEnable;
  7: bool imEnable;
}

/**
 * @apiDefine GetScanSchedule
 */
struct GetScanSchedule {}

/**
 * @apiDefine GetScanScheduleResp_SUCCESS
 * @apiSuccess {Object(ScanSchedule)} scanSchedule The scan schedule
 * @apiSuccess {Int64} nextBwgdIdx
 *             The BWGD at which combined scans that are already scheduled
 *             will finish
 */
struct GetScanScheduleResp {
  1: ScanSchedule scanSchedule;
  2: i64 nextBwgdIdx;
}

struct ScanScheduleUpdated {}

struct GetSlotMapConfig {}

struct Slot {
  1: i32 start;
  2: i32 len;
}

enum SlotPurpose {
  SP_IM = 0,
  SP_PBF = 1,
  SP_RTAC = 2,
  SP_VBF = 3,
  SP_NULLING = 4,
  SP_IGNITION = 5,
  SP_HYBRID_PBF = 6,
  SP_NULLING_APPLY = 7,
}

struct SlotMapConfig {
  1: i32 slotLen; // in BWGDs
  2: i32 periodLen; // in slots
  // List of permissible slots per purpose/app
  3: map<SlotPurpose, list<Slot>> (cpp.template = "std::unordered_map") mapping;
} (no_default_comparators)

############# OpenrClient App #############

struct SyncLinkMonitor {}

struct InjectKvStoreKeys {}

/**
 * @apiDefine GetRoutingAdjacencies
 */
struct GetRoutingAdjacencies {}

/**
 * @apiDefine RoutingAdjacencies_SUCCESS
 * @apiSuccess {Map(String:Object(AdjacencyDatabase))} adjacencyMap
 *             The adjacency map (nodeId:adjacencyDb)
 * @apiSuccess {Map(String:Object(PrefixDatabase))} prefixMap
 *             The prefix map (nodeId:prefixDb)
 * @apiSuccess {String} network
 *             The E2E network prefix
 */
struct RoutingAdjacencies {
  1: map<string /* node id */, Types.AdjacencyDatabase>
     (cpp.template = "std::unordered_map") adjacencyMap;
  2: map<string /* node id */, Types.PrefixDatabase>
     (cpp.template = "std::unordered_map") prefixMap;
  3: string network;
} (no_default_comparators)

struct SetLinkMetric {
  1: map<string /* mac */, i32 /* metric */> linkMetricMap;
} (no_default_comparators)

############# TrafficApp ##############

// Protocol numbers:
// https://www.iana.org/assignments/protocol-numbers/protocol-numbers.xhtml
enum IperfTransportProtocol {
  TCP = 6,
  UDP = 17,
}

enum IperfFormat {
  KILOBITS = 1,
  MEGABITS = 2,
  GIGABITS = 3,
  KILOBYTES = 4,
  MEGABYTES = 5,
  GIGABYTES = 6,
}

/**
 * @apiDefine IperfOptions_GROUP
 * @apiParam (:IperfOptions) {Int64} [bitrate]
 *           The target traffic bitrate (bps)
 * @apiParam (:IperfOptions) {Int32} [timeSec]
 *           The measurement duration (in seconds)
 * @apiParam (:IperfOptions) {Int(IperfTransportProtocol)=6,17} [protocol]
 *           The transport protocol (6=TCP, 17=UDP)
 * @apiParam (:IperfOptions) {Int32} [intervalSec]
 *           The interval between periodic bandwidth reports (in seconds)
 * @apiParam (:IperfOptions) {Int64} [windowSize] The window size (in bytes)
 * @apiParam (:IperfOptions) {Int32} [mss]
 *           The TCP maximum segment size (MTU - 40 bytes)
 * @apiParam (:IperfOptions) {Boolean} [noDelay] Disable Nagle's Algorithm
 * @apiParam (:IperfOptions) {Int32} [omitSec]
 *           Omit the first n seconds of the measurement
 * @apiParam (:IperfOptions) {Boolean} [verbose] Show more detailed output
 * @apiParam (:IperfOptions) {Boolean} [json] Output in JSON format
 * @apiParam (:IperfOptions) {Int64} [bufferLength] The buffer length (in bytes)
 * @apiParam (:IperfOptions) {Int(IperfFormat)=1,2,3,4,5,6} [format]
 *           The format to report (1=KILOBITS, 2=MEGABITS, 3=GIGABITS,
 *           4=KILOBYTES, 5=MEGABYTES, 6=GIGABYTES)
 * @apiParam (:IperfOptions) {Int32} [parallelStreams]
 *           The number of parallel client streams to run
 * @apiParam (:IperfOptions) {Boolean} [useVppDataPath]
 *           Bind iperf directly to VPP TCP stack instead of the kernel.  This
 *           option requires using IPv6 addresses inside the configured
 *           VPP_ULA_TEST_PREFIX
 * @apiParam (:IperfOptions) {Int32} [clientDelayMs]
 *           Server-client delay (in milliseconds). This option requires JSON
 *           output mode to be enabled.
 */
struct IperfOptions {
  1: optional i64 bitrate;
  2: optional i32 timeSec;
  3: optional IperfTransportProtocol protocol;
  4: optional i32 intervalSec;
  5: optional i64 windowSize;
  6: optional i32 mss;
  7: optional bool noDelay;
  8: optional i32 omitSec;
  9: optional bool verbose;
  10: optional bool json;
  11: optional i32 bufferLength;
  12: optional IperfFormat format;
  13: optional i32 parallelStreams;
  14: optional bool useVppDataPath;
  15: optional i32 clientDelayMs;
}

/**
 * @apiDefine StartIperf
 * @apiParam {String} srcNodeId The source node MAC address
             (Optional, supply empty string to start server only)
 * @apiParam {String} dstNodeId The destination node MAC address
 * @apiParam {String} [dstNodeIpv6] The destination node IPv6 address
 * @apiParam {Object(IperfOptions)} [options] The iperf options
 * @apiParam {Boolean} [useLinkLocal]
 *           Whether to use the link-local IP address and interface
 */
/**
 * @apiDefine StartIperf_SUCCESS
 * @apiSuccess (:StartIperf) {String} srcNodeId The source node MAC address
 * @apiSuccess (:StartIperf) {String} dstNodeId The destination node MAC address
 * @apiSuccess (:StartIperf) {String} [dstNodeIpv6] The destination node IPv6 address
 * @apiSuccess (:StartIperf) {Object(IperfOptions)} [options] The iperf options
 * @apiSuccess (:StartIperf) {Boolean} [useLinkLocal]
 *             Whether to use the link-local IP address and interface
 */
struct StartIperf {
  1: string srcNodeId;
  2: string dstNodeId;
  3: optional string dstNodeIpv6;
  4: optional IperfOptions options;
  5: optional bool useLinkLocal;
}

/**
* @apiDefine StartIperfResp_SUCCESS
* @apiSuccess {String} id The unique ID for this iperf session
*/
struct StartIperfResp {
  1: string id;
}

/**
 * @apiDefine StartMinionIperf_SUCCESS
 * @apiSuccess (:StartMinionIperf) {Object(StartIperf)} iperfConfig
 *                                 The iperf config
 * @apiSuccess (:StartMinionIperf) {Int32} serverPort The server port
 * @apiSuccess (:StartMinionIperf) {String} id The iperf session ID
 * @apiSuccess (:StartMinionIperf) {String} senderApp
 *                                 The ZMQ identity of the original sender
 *                                 (empty in response to sender)
 * @apiSuccess (:StartMinionIperf) {String} [iface] The interface to use
 */
struct StartMinionIperf {
  1: StartIperf iperfConfig;
  2: i32 serverPort = 0;
  3: string id;
  4: string senderApp;
  5: optional string iface;
}

/**
 * @apiDefine StopIperf
 * @apiParam {String} id The iperf session ID
 */
struct StopIperf {
  1: string id;
}

/**
 * @apiDefine GetIperfStatus
 */
struct GetIperfStatus {}

/**
 * @apiDefine IperfStatus_SUCCESS
 * @apiSuccess {Map(String:Object(StartMinionIperf))} sessions
 *             The iperf sessions in progress, keyed by session ID
 */
struct IperfStatus {
  1: map<string /* id */, StartMinionIperf>
     (cpp.template = "std::unordered_map") sessions;
} (no_default_comparators)

struct IperfOutput {
  1: string output;
  2: StartMinionIperf startIperf;
  3: bool isServer;
}

/**
 * @apiDefine PingOptions_GROUP
 * @apiParam (:PingOptions) {Boolean} [adaptive] Adaptive ping
 * @apiParam (:PingOptions) {Int32} [count]
 *           Stop after sending count ECHO_REQUEST packets
 * @apiParam (:PingOptions) {Boolean} [timestamp]
 *           Print timestamp before each line
 * @apiParam (:PingOptions) {Boolean} [flood] Flood ping
 * @apiParam (:PingOptions) {Int32} [interval]
 *           Wait interval seconds between sending each packet
 * @apiParam (:PingOptions) {Int32} [preload]
 *           Sends this many packets not waiting for a reply
 * @apiParam (:PingOptions) {Boolean} [numeric] Numeric output only
 * @apiParam (:PingOptions) {Boolean} [outstanding]
 *           Report outstanding ICMP ECHO reply before sending next packet
 * @apiParam (:PingOptions) {Boolean} [quiet] Quiet output
 * @apiParam (:PingOptions) {Int32} [packetSize]
 *           Specifies the number of data bytes to be sent
 * @apiParam (:PingOptions) {Int32} [sndbuf] Set socket sndbuf
 * @apiParam (:PingOptions) {Int32} [ttl] Set the IP time-to-live
 * @apiParam (:PingOptions) {Boolean} [verbose] Verbose output
 * @apiParam (:PingOptions) {Int32} [deadline]
 *           Seconds before exit regardless of how many packets sent or received
 * @apiParam (:PingOptions) {Int32} [timeout]
 *           Time to wait for a response, in seconds
 */
struct PingOptions {
  1: optional bool adaptive;
  2: optional i32 count;
  3: optional bool timestamp;
  4: optional bool flood;
  5: optional i32 interval;
  6: optional i32 preload;
  7: optional bool numeric;
  8: optional bool outstanding;
  9: optional bool quiet;
  10: optional i32 packetSize;
  11: optional i32 sndbuf;
  12: optional i32 ttl;
  13: optional bool verbose;
  14: optional i32 deadline;
  15: optional i32 timeout;
}

/**
 * @apiDefine StartPing
 * @apiParam {String} srcNodeId The source node MAC address
 * @apiParam {String} [dstNodeId] The destination node MAC address
 * @apiParam {String} [dstNodeIpv6] The destination node IPv6 address
 * @apiParam {Object(PingOptions)} [options] The ping options
 * @apiParam {Boolean} [useLinkLocal]
 *           Whether to use the link-local IP address and interface
 */
/**
 * @apiDefine StartPing_SUCCESS
 * @apiSuccess (:StartPing) {String} srcNodeId The source node MAC address
 * @apiSuccess (:StartPing) {String} [dstNodeId] The destination node MAC address
 * @apiSuccess (:StartPing) {String} [dstNodeIpv6] The destination node IPv6 address
 * @apiSuccess (:StartPing) {Object(PingOptions)} [options] The ping options
 * @apiSuccess (:StartPing) [useLinkLocal]
 *             Whether to use the link-local IP address and interface
 */
struct StartPing {
  1: string srcNodeId;
  2: optional string dstNodeId;
  3: optional string dstNodeIpv6;
  4: optional PingOptions options;
  5: optional bool useLinkLocal;
}

/**
* @apiDefine StartPingResp_SUCCESS
* @apiSuccess {String} id The unique ID for this ping session
*/
struct StartPingResp {
  1: string id;
}

/**
 * @apiDefine StartMinionPing_SUCCESS
 * @apiSuccess (:StartMinionPing) {Object(StartPing)} pingConfig The ping config
 * @apiSuccess (:StartMinionPing) {Int32} serverPort The server port
 * @apiSuccess (:StartMinionPing) {String} id The ping session ID
 * @apiSuccess (:StartMinionPing) {String} senderApp
 *                                The ZMQ identity of the original sender
 *                                (empty in response to sender)
 * @apiSuccess (:StartMinionPing) {String} [iface] The interface to use
 */
struct StartMinionPing {
  1: StartPing pingConfig;
  2: string id;
  3: string senderApp;
  4: optional string iface;
}

/**
 * @apiDefine StopPing
 * @apiParam {String} id The ping session ID
 */
struct StopPing {
  1: string id;
}

/**
 * @apiDefine GetPingStatus
 */
struct GetPingStatus {}

/**
 * @apiDefine PingStatus_SUCCESS
 * @apiSuccess {Map(String:Object(StartMinionPing))} sessions
 *             The ping sessions in progress, keyed by session ID
 */
struct PingStatus {
  1: map<string /* id */, StartMinionPing>
     (cpp.template = "std::unordered_map") sessions;
} (no_default_comparators)

struct PingOutput {
  1: string output;
  2: StartMinionPing startPing;
}

############# TopologyBuilderApp #############

/**
 * @apiDefine StartTopologyScan
 * @apiParam {String} txNode
 *           The transmitter radio MAC address or node name (will be deprecated)
 * @apiParam {Int16} [txPwrIndex]
 *           The transmit power index (0-31, 255=current average power)
 */
struct StartTopologyScan {
  1: string txNode;
  2: optional i16 txPwrIndex;
}

/**
 * @apiDefine StartTopologyScanResp_SUCCESS
 * @apiSuccess {Object(TopologyScanInfo)[]} responders
 *             The list of responders to the topology scan
 * @apiSuccess {String} txNode The transmitter node name
 * @apiSuccess {Int16} [txPwrIndex]
 *             The transmit power index used for the scan (0-31)
 */
struct StartTopologyScanResp {
  1: list<TopologyScanInfo> responders;
  2: string txNode;
  3: optional i16 txPwrIndex;
}

/**
 * @apiDefine TopologyScanInfo_SUCCESS
 * @apiSuccess (:TopologyScanInfo) {Object(TopoResponderInfo)} responderInfo
 *             The raw responder information
 * @apiSuccess (:TopologyScanInfo) {Double} bestSnr
 *             The best signal-to-noise ratio (SNR) reported by the
 *             transmitter node, in dB
 * @apiSuccess (:TopologyScanInfo) {Double} bestTxAngle
 *             The transmitter beam angle at which the best SNR was found,
 *             in degrees
 * @apiSuccess (:TopologyScanInfo) {Double} bestRxAngle
 *             The receiver beam angle at which the best SNR was found,
 *             in degrees
 * @apiSuccess (:TopologyScanInfo) {String} nearestSite
 *             The name of the geographically nearest site in the topology
 * @apiSuccess (:TopologyScanInfo) {Double} nearestSiteDistance
 *             The approximate distance to the nearest site in the
 *             topology, in meters
 */
struct TopologyScanInfo {
  1: TopoResponderInfo responderInfo;
  2: double bestSnr;
  3: double bestTxAngle;
  4: double bestRxAngle;
  5: string nearestSite;
  6: double nearestSiteDistance;
}

/**
 * @apiDefine StartLinkDiscoveryScan
 * @apiParam {String} targetMac The MAC address of the node to find
 * @apiParam {Double} scanRadius The max distance from the target node in meters
 */
 struct StartLinkDiscoveryScan {
   1: string targetMac;
   2: double scanRadius;
 }

/**
 * @apiDefine GetLinkDiscoveryScanStatus
 */
struct GetLinkDiscoveryScanStatus {}

/**
 * @apiDefine LinkDiscoveryScanStatus_SUCCESS
 * @apiSuccess {Boolean} active Whether a link discovery scan is running
 * @apiSuccess {Int64} lastUpdateTime
 *             The UNIX timestamp when the last update occurred
 *             (e.g. started a scan, received results, finished all scans)
 * @apiSuccess {Map(String:Object(TopologyScanInfo)[])} responders
 *             The scan info of nodes that found the target, keyed by node name
 */
struct LinkDiscoveryScanStatus {
  1: bool active;
  2: i64 lastUpdateTime;
  3: map<string, list<TopologyScanInfo>>
     (cpp.template = "std::unordered_map") responders;
} (no_default_comparators)

/**
 * @apiDefine StartNetworkTopologyScan
 * @apiParam {Object(SiteLink)[]} siteLinks
 *           All links between sites that should be formed
 * @apiParam {Set(String)} macAddrs
 *           The node MAC addresses to accept (or any MAC if empty)
 * @apiParam {Set(String)} cnSites
 *           The sites comprised of CN nodes (if any)
 * @apiParam {Set(String)} yStreetSites
 *           The sites to allow creating Y-street topologies on (if any)
 * @apiParam {Double} beamAnglePenalty=0.1
 *           The penalty for high tx/rx beam angles when selecting the "best"
 *           quality link to form, except on P2MP sites:
 *           link quality := SNR - (penalty * combined beam angle)
 * @apiParam {Double} distanceThreshold=50
 *           The maximum distance, in meters, to allow between a responder's
 *           reported position and the nearest site
 * @apiParam {Double} snrThreshold=6.1
 *           The minimum signal-to-noise ratio (SNR), in dB, to allow on new
 *           links (default of 6.1dB is needed to support MCS2 at a PER of 1e-3)
 * @apiParam {Int32} scansPerNode=1
 *           The number of scans that each node will initiate (regardless of
 *           success)
 * @apiParam {Bool} mergeAdjMacs=true
 *           If set, treat all discovered nodes as potentially multi-radio
 *           nodes, and add both "addr" and "adjs" into "wlan_mac_addrs".
 *           Otherwise, treat all "adjs" as separate nodes.
 * @apiParam {Bool} storeResults=false
 *           If set, return all scan results in "NetworkTopologyScanStatus".
 * @apiParam {Bool} dryRun=false
 *           If set, do not add any elements to the topology (typically used in
 *           conjunction with "storeResults").
 */
struct StartNetworkTopologyScan {
  1: list<SiteLink> siteLinks;
  2: set<string> (cpp.template = "std::unordered_set") macAddrs;
  3: set<string> (cpp.template = "std::unordered_set") cnSites;
  4: set<string> (cpp.template = "std::unordered_set") yStreetSites;
  5: double beamAnglePenalty = 0.1;
  6: double distanceThreshold = 50;
  7: double snrThreshold = 6.1;
  8: i32 scansPerNode = 1;
  9: bool mergeAdjMacs = true;
  10: bool storeResults = false;
  11: bool dryRun = false;
}

/**
 * @apiDefine SiteLink_GROUP
 * @apiParam (:SiteLink) {String} aSite The first site (order does not matter)
 * @apiParam (:SiteLink) {String} zSite The second site (order does not matter)
 */
struct SiteLink {
  1: string aSite;
  2: string zSite;
}

/**
 * @apiDefine StopNetworkTopologyScan
 */
struct StopNetworkTopologyScan {}

/**
 * @apiDefine GetNetworkTopologyScanStatus
 */
struct GetNetworkTopologyScanStatus {}

/**
 * @apiDefine NetworkTopologyScanStatus_SUCCESS
 * @apiSuccess {Boolean} active Whether a network-wide topology scan is running
 * @apiSuccess {Int64} lastUpdateTime
 *             The UNIX timestamp when the last update occurred
 *             (e.g. started a scan, received results, finished all scans)
 * @apiSuccess {String} currentScanRadio The current scan initiator MAC
 * @apiSuccess {String[]} queuedSites The list of sites left to scan
 * @apiSuccess {String[]} emptySites The list of sites not yet discovered
 * @apiSuccess {String[]} visitedSites The list of sites already scanned
 * @apiSuccess {Object(Node)[]} newNodes The list of newly-added nodes
 * @apiSuccess {Object(Link)[]} newLinks The list of newly-added links
 * @apiSuccess {Map(String:Map(String:Object(TopologyScanInfo)))} responses
 *             All scan responses, given as a two-level map first keyed on
 *             "txNode" (initiator) and then on "addr" (responder). Only the
 *             latest response is recorded for each txNode-addr pair. This is
 *             only present if "storeResults" was set in the original request.
 */
struct NetworkTopologyScanStatus {
  1: bool active;
  2: i64 lastUpdateTime;
  3: string currentScanRadio;
  4: list<string> queuedSites;
  5: list<string> emptySites;
  6: list<string> visitedSites;
  7: list<Topology.Node> newNodes;
  8: list<Topology.Link> newLinks;
  9: map<string /* txNode */,
         map<string /* addr */, TopologyScanInfo>
         (cpp.template = "std::unordered_map")>
     (cpp.template = "std::unordered_map") responses;
}

############# BinaryStarApp #############

// All states in the Binary Star FSM
enum BinaryStarFsmState {
  STATE_PRIMARY = 1,          // Primary, waiting for peer to connect
  STATE_BACKUP = 2,           // Backup, waiting for peer to connect
  STATE_ACTIVE = 3,           // Active - accepting connections
  STATE_PASSIVE = 4,          // Passive - not accepting connections
}

// FSM events/inputs (1-4 correspond with the FSM states)
enum BinaryStarFsmEvent {
  PEER_PRIMARY = 1,           // Peer is pending primary
  PEER_BACKUP = 2,            // Peer is pending backup
  PEER_ACTIVE = 3,            // Peer is active
  PEER_PASSIVE = 4,           // Peer is passive
  CLIENT_REQUEST = 5,         // Client makes request
}

/**
 * @apiDefine BinaryStar_SUCCESS
 * @apiSuccess {Int(BinaryStarFsmState)=0,1,2,3,4} state
 *             The current state in the finite state machine
 *             (1=STATE_PRIMARY, 2=STATE_BACKUP,
 *              3=STATE_ACTIVE, 4=STATE_PASSIVE).
 *             If High Availability mode is not enabled, this will be 0.
 * @apiSuccess {Int64} peerExpiry The millisecond timestamp for a peer timeout
 */
// The Binary Star FSM
struct BinaryStar {
  1: BinaryStarFsmState state;
  2: i64 peerExpiry;
}

struct BinaryStarGetAppData {}

// Persistent application data to sync between controllers
struct BinaryStarAppData {
  1: optional Topology.Topology topology;
  2: optional string configNetworkOverrides;
  3: optional string configNodeOverrides;
  4: optional string configAutoNodeOverrides;
  5: optional string configController;
}

// Heartbeat struct
struct BinaryStarSync {
  1: BinaryStarFsmState state;
  2: i32 seqNum;
  3: BinaryStarAppData data;
  4: string version;
}

struct BinaryStarSwitchController {}

/**
 * @apiDefine BinaryStarGetState
 */
struct BinaryStarGetState {}

############# Common #############

struct Message {
  1: MessageType mType;
  2: binary value;
  3: optional bool compressed;
  4: optional CompressionFormat compressionFormat;
}

enum CompressionFormat {
  SNAPPY = 1,
}

// hello message send/reply by both sides for confirmation of established
// communication channel
struct Hello {}

/**
 * @apiDefine E2EAck_SUCCESS
 * @apiSuccess {Boolean} success The response status
 * @apiSuccess {String} message The response message
 */
// Ack to asynchronous requests
struct E2EAck {
  1: bool success;
  2: string message;
}

// network information needed by different processes
struct NetworkInfo {
  1: string e2eCtrlUrl;
  2: string e2eCtrlUrlBackup;  // in primary-backup controller setup
  3: list<string> aggrUrl;
  4: string network;
}

// Empty message
struct Empty {}

############# Controller Config #############

struct CentralLaTpcConfig {
  1: bool maxMcsAutoEnable = false;
  2: i32 maxMcsLow = 9;
  3: i32 maxMcsHigh = 12;
  4: double maxMcsInrLimit = 3.0;
  5: double maxMcsInrAlpha = 0.0;
  6: i32 maxMcsTxPowerMin = 0;
  7: i32 maxMcsTxPowerMax = 21;
}

struct ScanParams {
  1: ScanSchedule scanSchedule;
  2: string cbfConfigJson;
  3: CentralLaTpcConfig centralLaTpcConfig;
}

struct PrefixAllocParams {
  1: string seedPrefix;
  2: i32 allocPrefixLen;
}

struct TopologyParams {
  1: string enabledChannels;
}

struct GoldenImage {
  1: bool enabled = false;
  2: map<string /* hardwareBoardId */, string /* version */>
     (cpp.template = "std::unordered_map") versions;
  3: i32 batchLimit = 1;
  4: i32 timeoutSec = 180;
  5: string timeRestriction;
  6: map<string /* nodeName */, string /* version */>
     (cpp.template = "std::unordered_map") nodeBlacklist;
}

struct UpgradeParams {
  1: optional GoldenImage goldenImage;
}

struct UpdateTunnelConfig {
  1: Topology.Node node;
  2: string ipv6Address;
}

struct ControllerConfig {
  1: map<string, string> (cpp.template = "std::unordered_map") flags;
  2: ScanParams scanParams;
  3: PrefixAllocParams prefixAllocParams;
  4: IgnitionParams ignitionParams;
  5: NodeConfig.StatsAgentParams statsAgentParams;
  6: TopologyParams topologyParams;
  7: UpgradeParams upgradeParams;
} (no_default_comparators)
