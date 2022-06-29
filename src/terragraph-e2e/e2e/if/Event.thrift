/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

namespace py terragraph_thrift.Event
namespace cpp2 facebook.terragraph.thrift
namespace lua terragraph_thrift.Event

enum EventCategory {
  IGNITION = 100,
  TOPOLOGY = 200,
  UPGRADE = 300,
  SCAN = 400,
  CONFIG = 500,
  TRAFFIC = 600,
  STATUS = 700,
  DRIVER = 800,
  SCHEDULER = 900,
  OPENR = 1000,
  WATCHDOG = 1100,
  SYSTEM = 1200,
  FIRMWARE = 1300,
  ZMQ = 1400,
  LOGTAIL = 1500,
  HIGH_AVAILABILITY = 1600,
}

enum EventId {
  // === IGNITION === //
  SET_LINK_STATUS = 101,
  MINION_SET_LINK_STATUS = 102,
  DRIVER_LINK_STATUS = 103,
  FORCE_DISSOC = 104,

  // === TOPOLOGY === //
  TOPOLOGY_NAME_MODIFIED = 201,
  TOPOLOGY_NODE_ADDED = 202,
  TOPOLOGY_NODE_MODIFIED = 203,
  TOPOLOGY_NODE_REMOVED = 204,
  TOPOLOGY_LINK_ADDED = 205,
  TOPOLOGY_LINK_MODIFIED = 206,
  TOPOLOGY_LINK_REMOVED = 207,
  TOPOLOGY_SITE_ADDED = 208,
  TOPOLOGY_SITE_MODIFIED = 209,
  TOPOLOGY_SITE_REMOVED = 210,

  // === UPGRADE === //
  UPGRADE_PREPARE = 301,
  UPGRADE_COMMIT = 302,
  UPGRADE_INFO = 303,
  UPGRADE_IMAGE_INFO = 304,

  // === SCAN === //
  SCAN_REQ = 401,
  SCAN_RESP = 402,
  SCAN_COMPLETE = 403,

  // === CONFIG === //
  CONFIG_MODIFIED = 501,
  SET_CONFIG = 502,
  MINION_SET_CONFIG = 503,
  CONFIG_POLARITY_INFO = 504,
  CONFIG_GOLAY_INFO = 505,
  CONFIG_CONTROL_SUPERFRAME_INFO = 506,
  CONFIG_CHANNEL_INFO = 507,
  MINION_BASE_CONFIG_RECEIVED = 508,

  // === TRAFFIC === //
  IPERF_INFO = 601,
  PING_INFO = 602,

  // === STATUS === //
  NODE_STATUS = 701,
  LINK_STATUS = 702,
  GPS_SYNC = 703,
  NODE_INFO = 704,
  UNKNOWN_NODE = 705,
  REBOOT_NODE_REQ = 706,
  RESTART_MINION_REQ = 707,
  WIRED_LINK_STATUS = 708,
  MARVELL_SWITCH_STATUS = 709,
  NODE_PARAMS = 710,
  BGP_STATUS = 711,

  // === DRIVER === //
  DRIVER_NODE_INIT = 801,
  DRIVER_DEVICE_STATUS = 802,

  // === OPENR === //
  OPENR_KVSTORE_MODIFIED = 1001,
  OPENR_LINK_MONITOR_MODIFIED = 1002,

  // == WATCHDOG == //
  WDOG_REPAIR_FW_RESTART = 1101,
  WDOG_REPAIR_NO_FW_RESTART = 1102,
  WDOG_REBOOT = 1103,

  // === LOGTAIL === //
  LOG_BASED_EVENT = 1501,

  // === HIGH_AVAILABILITY === //
  HIGH_AVAILABILITY_STATE_CHANGE = 1601,
  PEER_VERSION_MISMATCH = 1602,
}

enum EventLevel {
  INFO = 10,
  WARNING = 20,
  ERROR = 30,
  FATAL = 40,
}

struct Event {
  1: string source;  // The source program (ZMQ identity, process name, etc.)
  2: i64 timestamp;  // The event creation time (UNIX time in seconds)
  3: string reason;  // The event description, in plain English
  4: string details;  // Supplemental information, as a JSON string (optional)
  5: EventCategory category;
  // 6 deprecated
  7: EventLevel level;
  8: optional string entity;  // The entity this event is associated with
  9: optional string nodeId;  // The associated node ID (MAC)
  10: EventId eventId;  // The event ID, for directly associated events
  11: optional string topologyName;  // The topology name
  12: optional string nodeName;  // The associated node name (if applicable)
}
