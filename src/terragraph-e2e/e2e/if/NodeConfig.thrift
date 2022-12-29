/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

namespace cpp2 facebook.terragraph.thrift
namespace py terragraph_thrift.NodeConfig
namespace lua terragraph_thrift.NodeConfig

cpp_include "<unordered_map>"

include "BWAllocation.thrift"
include "FwOptParams.thrift"
include "Topology.thrift"

// -----------------------------------------------------------------------------
// After making changes to this file, please update config metadata at:
//   ../config/config_metadata.json
// -----------------------------------------------------------------------------

struct SystemParams {
  1: bool managedConfig;
  2: map<string, string> sshTrustedUserCAKeys;
  3: map<string, string> dnsServers;
  4: bool allowFactoryCA;
  5: map<string, string> ntpServers;
  6: bool distributedIgnitionEnabled;
} (no_default_comparators)

struct NodeEnvParams {
  1: optional string OPENR_ENABLED;
  2: optional string OPENR_ALLOC_PREFIX;
  3: optional string OPENR_USE_RTT_METRIC;
  4: optional string OPENR_USE_FIB_NSS;
  5: optional string OOB_NETNS;
  6: optional string OOB_INTERFACE;
  7: optional string CPE_INTERFACE; //deprecated
  8: optional string E2E_ENABLED;
  9: optional string FW_IF2IF; //deprecated
  // 10: OPENR_DOMAIN (deprecated)
  11: optional string FB_DRIVER_VERBOSE;
  12: optional string HMAC_VERBOSE;
  13: optional string KMOD_VERBOSE;
  14: optional string OPENR_STATIC_PREFIX_ALLOC;
  15: optional string OPENR_IFACE_PREFIXES;
  // 16: MARVELL_SFP_SPEED (deprecated)
  17: optional string MINION_VERBOSE;
  18: optional string STATS_AGENT_VERBOSE;
  19: optional string CPE_IFACE_IP_PREFIX; //deprecated
  20: optional string BH_AMSDU_FRAME_FORMAT;
  // 21: FB_PLATFORM_ENABLE_DPAA2 (deprecated in RELEASE_M79)
  22: optional string TIMEZONE;
  // 23: OPENR_ENABLE_OLD_DECISION_MODULE (deprecated)
  24: optional string OPENR_LINK_FLAP_MAX_BACKOFF_MS;
  25: optional string UDP_PING_SERVER_ENABLED;
  26: optional string SSH_ENABLED;
  27: optional string OPENR_VERBOSE;
  28: optional string LOGTAIL_ENABLED;
  29: optional string DPDK_ENABLED;
  30: optional string GPSD_ENABLED;
  31: optional string GPSD_DEVICE;
  32: optional string GPSD_BAUD_RATE;
  33: optional string GPSD_PPS_DEVICE;
  34: optional string GPSD_GPS_MODULE;
  35: optional string CPE_TUNNEL_ENDPOINT; //deprecated
  36: optional string OPENR_USE_FIB_VPP;
  37: optional string GPSD_NMEA_TIME_OFFSET;
  38: optional string FW_USE_NATIVE_BOARDFILE;
  39: optional string VPP_COREDUMP_ENABLED;
  40: optional string SNMP_ENABLED;
  41: optional string VPP_USE_EEPROM_MACS;
  42: optional string FW_LOGGING_ENABLED;
  43: optional string FW_LOG_VERBOSE;
  44: optional string DVPP_ENABLED;
  45: optional string GPSD_POSITION_HOLD_ENABLED;
  46: optional string VPP_FW_RECOVERY_ENABLED;
  47: optional string WEBUI_ENABLED;
  48: optional string FW_TX_RING_DEPTH;
  49: optional string LED_AGENT_ENABLED;
  50: optional string SLIP_ENABLED;
  51: optional string SLIP_SERIAL_PORT;
  52: optional string SLIP_BAUD_RATE;
  53: optional string VPP_ULA_TEST_PREFIX;
  54: optional string MINION_VMODULE;
  55: optional string FW_LOGGING_FILE_COUNT;
  56: optional string FW_LOGGING_FILESIZE_MB;
  57: optional string OPENR_ENABLE_SPARK2;
  58: optional string OPENR_ENABLE_DEPRECATED_PREFIXES;
  59: optional string SERIAL_CONSOLE_DISABLE;
}

struct PopConfigParams {
  1: string POP_ADDR;
  2: optional string POP_IFACE;
  3: optional string POP_STATIC_ROUTING;
  4: optional string POP_BGP_ROUTING;
  5: optional string GW_ADDR;
  6: optional string VPP_ADDR;
  7: optional string NAT64_POP_ENABLED;
  8: optional string NAT64_IPV6_PREFIX;
  9: optional string NAT64_IPV4_ADDR;
  10: optional string POP_VLAN_ID;
  11: optional string POP_ADDR_PREFIX_LEN;
}

struct TimingParams {
  1: optional string PPS_TIMESTAMP_SOURCE;
  2: optional string PTP_TIMER_SOURCE;
  3: optional string PTP_DEVICE;
  4: optional string HTSF_SRC_MAC;
  5: optional string HTSF_MAX_LOOP_SIZE;
  6: optional string PTP_VSC_CTRL_SOCKET;
  7: optional string PTP_VSC_PORT;
  8: optional string ESMC_ENABLED;
  9: optional string PTP_VPP_INTERFACE;
  10: optional string PTP_VPP_OFFSET_NS;
  11: optional string PTP_VPP_NXP_PORT;
}

struct EventFilter {
  1: string regex;
  2: string level;
}

struct LogTailSource {
  1: bool enabled;
  2: string filename;
  3: map<string /* event name */, EventFilter>
     (cpp.template = "std::unordered_map") eventFilters;
} (no_default_comparators)

struct LogTailParams {
  1: map<string, LogTailSource> sources;
} (no_default_comparators)

struct StatsAgentSource {
  1: bool enabled;
  2: string zmq_url;
}

struct OdsParams {
  1: bool enabled;
  2: string remoteEndpoint;
  3: string accessToken;
  4: i32 categoryId;
  5: string entityPrefix;
  6: string keyPrefix;
}

struct PelicanParams {
  1: bool enabled;
  2: string remoteEndpoint;
  3: string accessToken;
}

struct ScribeParams {
  1: bool enabled;
  2: string remoteEndpoint;
}

struct KafkaConfig {
  1: string brokerEndpointList;
  2: i32 queueBufferingMaxMs;
  3: i32 queueBufferingMaxMessages;
  4: i32 queueBufferingMaxKbytes;
  5: i32 batchNumMessages;
  6: bool enableIdempotence;
  7: string compressionCodec;
  8: i32 compressionLevel;
  9: i32 messageTimeoutMs;
}

struct KafkaTopics {
  1: string eventsTopic;
  2: string statsTopic;
  3: string hfStatsTopic;
  4: string scanResultsTopic;
  5: string iperfResultsTopic;
  6: string pingResultsTopic;
}

struct KafkaParams {
  1: bool enabled;
  2: KafkaConfig config;
  3: KafkaTopics topics;
}

struct NmsPublisherParams {
  7: bool enabled;
  // 1: defaultStatsInterval (deprecated in RELEASE_M46)
  // 2: highFrequencyStatsInterval (deprecated in RELEASE_M46)
  // 3: statsBufferSize (deprecated in RELEASE_M46)
  // 4: eventLogsBufferSize (deprecated in RELEASE_M46)
  5: i32 zmqSndHwm = 100;
  // 6: highFrequencyStatsWhitelist (deprecated in RELEASE_M46)
}

struct StatsEndpointParams {
  1: optional OdsParams odsParams;
  2: optional PelicanParams pelicanParams;
  3: optional ScribeParams scribeParams;
  4: optional KafkaParams kafkaParams;
  5: optional NmsPublisherParams nmsPublisherParams;
}

struct StatsCollectors {
  // 1: marvellSwitchStatsEnabled (deprecated in RELEASE_M78)
  2: bool openrStatsEnabled;
  3: i32 systemStatsCollectionInterval = 30;
}

struct StatsPublisherParams {
  1: i32 defaultStatsInterval = 30;
  2: i32 highFrequencyStatsInterval = 1;
  3: map<string, string>
     (cpp.template = "std::unordered_map") statsBlacklist;
  4: map<string, string>
     (cpp.template = "std::unordered_map") highFrequencyStatsWhitelist;
  5: i32 statsBufferSize = 10000;
  6: i32 eventLogsBufferSize = 2000;
  7: bool convertToRate = true;
  8: bool publishValueWithRate = false;
}

struct StatsAgentParams {
  1: map<string, StatsAgentSource>
     (cpp.template = "std::unordered_map") sources;
  // 2: statsBlacklist (deprecated in RELEASE_M46)
  3: StatsEndpointParams endpointParams;
  // 4: nmsPublisherParams (deprecated in RELEASE_M46)
  5: StatsCollectors collectors;
  6: StatsPublisherParams publisherParams;
} (no_default_comparators)

struct OpenrLinkMetricConfig {
  1: bool enabled;
  2: map<string, i32> metricMap;
  3: double tokenGenRate;
  4: i32 tokenBurstSize;
  5: i32 successiveChangeCount;
} (no_default_comparators)

struct OpenrParams {
  1: OpenrLinkMetricConfig linkMetricConfig;
} (no_default_comparators)

struct OpenrLinkParams {
  1: optional bool softDisable;
  2: optional i32 fixedMetric;
}

struct LinkParams {
  1: OpenrLinkParams openrLinkParams;
  2: FwOptParams.FwOptParams fwParams;
  3: optional BWAllocation.AirtimeConfig airtimeConfig;
}

struct WpaPskParams {
  1: string wpa_passphrase;
  2: optional map<string, string>  // per-link: <nbr_radio_mac, wpa_passphrase>
     (cpp.template = "std::unordered_map") wpa_passphrase_override;
}

struct WsecParams {
  1: i32 hostapdVerbosity;
  2: i32 supplicantVerbosity;
  // 3: eapolParams (deprecated in RELEASE_M77_2)
  4: WpaPskParams wpaPskParams;
}

struct RadioParams {
  1: FwOptParams.FwOptParams fwParams;
  2: WsecParams wsecParams;
  3: map<string, bool> (cpp.template = "std::unordered_map") fwStatsConfig;
}

struct DhcpParams {
  1: optional string dhcpGlobalConfigAppend;
  2: string dhcpNameServer;
  3: i32 dhcpRangeMax = 65535;
  4: i32 dhcpRangeMin = 10;
  5: string dhcpInterface = "nic0";
  6: i32 dhcpPreferredLifetime = 3600;
  7: i32 dhcpValidLifetime = 7200;
  8: i32 dhcpRenewTimer = 1800;
  9: i32 dhcpRebindTimer = 2700;
  10: string dhcpPdPool;
  11: i32 dhcpPdDelegatedLen;
  12: bool keaEnabled = false;
  13: optional map<string, string> optionData;
  14: bool prefixSplitEnabled = false;
} (no_default_comparators)

struct BgpNeighbor {
  1: i64 asn;
  2: string ipv6;
  3: optional i32 maximumPrefixes = 128;
  4: optional string hostname;
  5: optional string domainname;
}

struct BgpParams {
  1: i64 localAsn;
  2: map<string, BgpNeighbor> neighbors;
  3: i32 keepalive = 10;
  4: optional string cpeNetworkPrefix;
  5: optional string delegatedNetworkPrefixes;
  6: optional string specificNetworkPrefixes;
  7: optional bool noPrefixCheck;
  8: optional string md5Password;
  9: bool cpePrefixesAutoAdvertisement;
} (no_default_comparators)

struct SyslogParams {
  1: bool enabled;
  2: string remoteHost;
  3: i32 remotePort;
  4: string protocol;
  5: string selector;
}

struct FirewallConfig {
  1: bool allowEstablished = true;
  2: bool allowICMPv6 = true;
  3: bool allowLinkLocal = true;
  4: bool allowLoopback = true;
  5: string defaultPolicy;
  6: string tcpPorts;
  7: string udpPorts;
}

struct FluentdSource {
  1: bool enabled;
  2: string filename;
}

struct FluentdEndpoint {
  1: string host;
  2: i32 port;
}

struct FluentdParams {
  1: map<string, FluentdSource> sources;
  2: map<string, FluentdEndpoint> endpoints;
  3: optional i64 memBufLimit;
} (no_default_comparators)

struct NeighborInfo {
  1: string initiatorMac;
  2: optional i64 controlSuperframe;
  3: optional Topology.NodeType nodeType;  // neighbor node type
  4: optional Topology.GolayIdx golayIdx;  // neighbor golay index
  5: optional Topology.PolarityType nodePolarity;  // neighbor polarity
}

struct RadioInfo {
  1: optional i32 distributedIgnitionColor;
}

struct TopologyInfo {
  1: string nodeName;
  2: string topologyName;
  3: Topology.Site site;
  4: optional Topology.NodeType nodeType;
  5: optional map<string /* responderMac */, NeighborInfo>
     (cpp.template = "std::unordered_map") neighborInfo;
  6: optional map<string /* radioMac */, RadioInfo>
     (cpp.template = "std::unordered_map") radioInfo;
  7: optional i32 distributedIgnitionNumColors;
  8: optional string zoneNodes;
} (no_default_comparators)

struct SnmpV2C {
  1: string community;
  2: optional string source;
}

struct SnmpV3 {
  1: string username;
  2: string authPassphrase;
  3: string authType;
  4: optional string privPassphrase;
  5: optional string privProtocol;
}

struct SnmpConfig {
  1: string contact;
  2: string location;
  3: map<string, SnmpV2C>
     (cpp.template = "std::unordered_map") snmpV2C;
  4: map<string, SnmpV3>
     (cpp.template = "std::unordered_map") snmpV3;
}

struct TunnelParams {
  1: optional i32 vlanId;
  2: optional string primaryTunnelName;
}

struct TunnelConfig {
  // all parameters are optional to allow for node + auto config layering to
  // pass validation on their own
  1: optional bool enabled;
  2: optional string localInterface;
  3: optional string localIp;
  4: optional string dstNodeName;
  5: optional string dstIp;
  // allowed types: GRE_L2, VXLAN, SRV6
  6: optional string tunnelType;
  // extra parameters needed for some tunnel types
  7: optional TunnelParams tunnelParams;
}

struct PolicerDefinition {
  // Committed Information Rate (kbps)
  1: i32 cir;
  // Excess Information Rate (kbps)
  2: i32 eir;
}

struct DhcpRelayConfig {
  1: bool enabled;
  2: string dhcpServerIp;
  3: optional bool dhcpOptionRemoteId;
  4: optional i32 enterpriseId;
  5: optional string remoteIdMac;
  6: optional bool dhcpOptionSubscriberId;
}

struct CpeConfig {
  1: optional string prefix;
  2: optional map<string, PolicerDefinition>
     (cpp.template = "std::unordered_map") policers;
  3: optional bool wiredSecurityEnable;
  4: optional DhcpRelayConfig dhcpRelay;
}

struct DscpEntry {
  // Traffic Class index. Valued 0 - 3
  1: i16 tc;
  // Queue index. Valued 0
  2: i16 queue;
  // "R" - red, "Y" - yellow, "G" - green.
  3: string color;
}

struct QosConfig {
  // Key: DSCP value (0 - 63)
  1: map<string, DscpEntry>
    (cpp.template = "std::unordered_map") dscpEntries;
}

struct EAPoLParams {
  1: string radius_server_ip;
  2: i32 radius_server_port = 1812;
  3: string radius_user_identity = "tg";
  4: string ca_cert_path = "/data/secure/keys/ca.pem";
  5: string client_cert_path = "/data/secure/keys/client.pem";
  6: string private_key_path = "/data/secure/keys/client.key";
  7: map<string, string> secrets;
}

struct NodeConfig {
  1: NodeEnvParams envParams;
  // 2: fwParams (deprecated in RELEASE_M21)
  3: LogTailParams logTailParams;
  4: StatsAgentParams statsAgentParams;
  5: SystemParams sysParams;
  6: optional map<string, string> kvstoreParams;
  7: PopConfigParams popParams;
  // 8: linkConfig (deprecated in RELEASE_M21)
  9: optional OpenrParams openrParams;
  10: LinkParams linkParamsBase;
  11: map<string, LinkParams> linkParamsOverride;
  12: RadioParams radioParamsBase;
  13: map<string, RadioParams> radioParamsOverride;
  14: DhcpParams dhcpParams;
  15: BgpParams bgpParams;
  16: SyslogParams syslogParams;
  17: FirewallConfig firewallConfig;
  18: FluentdParams fluentdParams;
  19: TopologyInfo topologyInfo;
  20: SnmpConfig snmpConfig;
  21: map<string, TunnelConfig> tunnelConfig;
  22: TimingParams timingParams;
  // 23: cpeParams (deprecated in RELEASE_M78)
  24: optional QosConfig qosConfig;
  25: EAPoLParams eapolParams;
  26: map<string, CpeConfig> cpeConfig;
} (no_default_comparators)
