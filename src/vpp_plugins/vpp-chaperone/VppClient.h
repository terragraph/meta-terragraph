/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <vector>
#include <unordered_map>

#include <vapi/classify.api.vapi.hpp>
#include <vapi/l2.api.vapi.hpp>
#include <vapi/policer_types.api.vapi.hpp>
#include <vapi/vapi.hpp>

namespace facebook
{
namespace terragraph
{
namespace vpp
{

// VxLAN: map<dstIP, pair<srcIP, vni>>.
using VxlanTunnelMap =
    std::unordered_map<std::string, std::pair<std::string, int>>;
// SRv6: map<vlanID, pair<encapAddr, decapAddr>>.
using Srv6TunnelMap =
    std::unordered_map<int, std::pair<std::string, std::string>>;

enum class VppNat64Flags
{
  EGRESS_INTF = 0,
  INGRESS_INTF = 1
};

const std::string kVppRouteTypeDrop = "_drop";

//
// VPP client class, which holds the VAPI connection and handles all API calls.
//
class VppClient final
{
public:
  VppClient (int maxOutstandingRequests, int responseQueueSize);
  ~VppClient ();

  // Try to connect to VAPI, returning true if successful.
  // Note that vlibmemory/memory_client.c has a hardcoded 10-second timeout.
  bool connect ();

  // Construct a map of interface names to VPP interface index numbers.
  std::unordered_map<std::string, u32> getIfaceToVppIndexMap ();

  // Get all VPP intefaces with a specified prefix.
  std::unordered_map<std::string, u32>
  ifacePrefixToVppIndex (const std::string &ifPrefix);

  // Get the VPP interface index for the given name.
  // Return ~0 if not found.
  u32 ifaceToVppIndex (const std::string &ifName);

  // Get the name for the given VPP interface index.
  // Return an empty string if not found.
  std::string vppIndexToIface (u32 index);

  // Get any IPv6 prefix on the given interface.
  // Return an empty string if not found.
  std::string getSwInterfacePrefix (const std::string &ifName);

  // Get the interface name with the given IPv6 prefix.
  // Return an empty string if not found.
  std::string getSwInterfaceByPrefix (const std::string &prefix);

  // Set flags on the given interface, namely the admin UP/DOWN state.
  // Return true upon success.
  bool setInterfaceFlags (const std::string &ifName, bool up);

  // Enable or disable an IPv6 interface.
  // Return true upon success.
  bool enableDisableIp6Interface (const std::string &ifName, bool enable);

  // Return IPv4/IPv6 address dumps on the given interface.
  std::string getInterfaceAddr (const std::string &ifName, bool isIPv6);

  // Add or remove an IPv6 prefix on the given interface.
  // Return true upon success.
  bool addDelInterfaceAddress (const std::string &ifName,
                               const std::string &prefix, bool add);

  // Add or remove an IPv6 route.
  // Return true upon success.
  bool addDelRoute (const std::string &dstPrefix,
                    const std::string &nextHopAddr,
                    const std::string &nextHopIfName, bool add);

  // Return whether the given IPv6 route exists in VPP's FIB.
  bool hasRoute (const std::string &dstPrefix, const std::string &nextHopAddr,
                 const std::string &nextHopIfName, bool nextHopRequired);

  // Return all stale routes in VPP FIB using nextHopAddr and given interface.
  std::vector<std::string>
  getFibStaleRoutes (std::vector<std::string> &curPrefixes,
                     const std::string &nextHopAddr,
                     const std::string &nextHopIfName);

  // Create a loopback interface, optionally using a given MAC address.
  // Return the new interface name, or an empty string upon failure.
  std::string createLoopbackInterface (const std::string &macAddr = "");

  // Delete a loopback interface by name.
  // Return true upon success.
  bool delLoopbackInterface (const std::string &ifName);

  // Bridge domain options (see "bridge_domain_add_del").
  struct BridgeDomainConfig
  {
    u8 flood = 1;
    u8 uu_flood = 1;
    u8 forward = 1;
    u8 learn = 1;
    u8 arp_term = 0;
    u8 mac_age = 0;
  };

  // Add or remove a bridge domain.
  // Return true upon success.
  bool addDelBridgeDomain (u32 bdId, const BridgeDomainConfig &bdConfig,
                           bool add);

  // Return whether the given bridge domain exists.
  bool hasBridgeDomain (u32 bdId);

  // Set L2 bridging on a given interface.
  bool setInterfaceL2Bridge (
      u32 bdId, const std::string &ifName, u8 shg,
      vapi_enum_l2_port_type portType = L2_API_PORT_TYPE_NORMAL);

  // Find the tap interface for the given device name.
  // Return an empty string if none found.
  std::string getTapInterface (const std::string &devName);

  // Connect a tap interface.
  // Return the new interface name, or an empty string upon failure.
  std::string connectTap (const std::string &ifName,
                          const std::string &macAddr = "");

  // Delete a tap interface.
  // Return true upon success.
  bool delTap (const std::string &ifName);

  // Router advertisement options (see "sw_interface_ip6nd_ra_config").
  struct RaConfig
  {
    u8 suppress = 0;
    u8 managed = 0;
    u8 other = 0;
    u8 ll_option = 0;
    u8 send_unicast = 0;
    u8 cease = 0;
    u8 is_no = 0;
    u8 default_router = 0;
    u32 max_interval = 0;
    u32 min_interval = 0;
    u32 lifetime = 0;
    u32 initial_count = 0;
    u32 initial_interval = 0;
  };

  // Configure router advertisement options on a given interface.
  // Return true upon success.
  bool setInterfaceRaConfig (const std::string &ifName,
                             const RaConfig &raConfig);

  // Router advertisement prefix options (see "sw_interface_ip6nd_ra_prefix").
  struct RaPrefixConfig
  {
    u8 use_default = 0;
    u8 no_advertise = 0;
    u8 off_link = 0;
    u8 no_autoconfig = 0;
    u8 no_onlink = 0;
    u8 is_no = 0;
    u32 val_lifetime = 0;
    u32 pref_lifetime = 0;
  };

  // Configure prefix options on a given interface.
  // Return true upon success.
  bool setInterfaceRaPrefixConfig (const std::string &ifName,
                                   const std::string &prefix,
                                   const RaPrefixConfig &raPrefixConfig);

  // Configure DHCPv6 proxy/relay on a given CPE IP address.
  // Return true upon success.
  bool setDhcpv6ProxyConfig (const std::string &dhcpServerIp,
                             const std::string &relayIp, bool remoteIdOption,
                             bool subscriberIdOption, const u32 enterpriseId,
                             const std::string &remoteIdMac, bool add);

  // Set the SRv6 encapsulation source.
  // Return true upon success.
  bool setSrEncapsulationSource (const std::string &addr);

  // SRv6 policy options (see "sr_policy_add").
  struct SrPolicyConfig
  {
    u32 weight = ~0;
    u8 is_encap = 1;
    u8 is_spray = 0;
    u32 fib_table = ~0;
  };

  // Add a SRv6 policy.
  // Return true upon success.
  bool addSrPolicy (const std::string &bsid,
                    const std::vector<std::string> &sids,
                    const SrPolicyConfig &srPolicyConfig);

  // Delete a SRv6 policy.
  // Return true upon success.
  bool deleteSrPolicy (const std::string &bsid);

  // Get SRv6 policy dumps.
  Srv6TunnelMap getSrPolicyDumps (const std::string &encapBaseHextet,
                                  const std::string &decapBaseHextet);

  // Get the interface that is associated with a particular BSID.
  std::string getSrSteeringPolicyIfName (const std::string &bsid);

  // Steer L2 traffic through a given SR policy, or unset this configuration.
  bool addDelSrSteeringPolicy (const std::string &ifName,
                               const std::string &bsid, bool add);

  // Return the local SID that is associated with a given interface.
  std::string getSrLocalSid (const std::string &ifName);

  // Add or remove a local SID with behavior "end.dx2" (endpoint with
  // decapsulation and L2 cross-connect) for the given interface.
  // Return true upon success.
  bool addDelLocalSid (const std::string &localSid, const std::string &ifName,
                       bool add);

  // Set MAC address for VPP software interface. Returns true
  // on success and false on failure.
  bool setSwInterfaceMacAddress (const std::string &ifName,
                                 const std::string &macAddr);
  // Get MAC address for VPP software interface as std::string
  // format.
  std::string getSwInterfaceMacAddress (const std::string &ifName);

  // Set link-local address on VPP interface.
  bool setInterfaceLinkLocalAddress (const std::string &ifName,
                                     const std::string &addr);

  // Add/Delete VxLAN L2 tunnels if CPE tunnel config is present.
  std::string addDelVxLanTunnel (const std::string &tunnelSrcIP,
                                 const std::string &tunnelDstIP, int vni,
                                 bool add);

  // Get VxLAN tunnel dumps.
  VxlanTunnelMap getVxlanTunnelDumps ();

  // Create sub-interface(VLAN) on a given interface.
  std::string createSubInterface (const std::string &ifName, int subId);

  // Delete the given sub-interface.
  bool deleteSubInterface (const std::string &ifName);

  // Set/Rewrite VLAN tag on a VPP interface.
  bool setInterfaceL2VLANTag (const std::string &ifName, int tag);

  // Cross-connect two VPP interfaces.
  bool setInterfaceL2xConnect (const std::string &srcIf,
                               const std::string &dstIf);

  // Setup NAT64 ingress and egress interfaces.
  bool setNat64Interface (const std::string &natIf, VppNat64Flags flag,
                          bool add);

  // Program NAT64 prefix.
  bool addDelNat64TranslationPrefix (const std::string &nat64Prefix, bool add);

  // Return IPv4 address in current NAT64 pool.
  std::string getNat64PoolRange ();

  // Program IPv4 pool range for NAT64 allocation purpose.
  bool addDelNat64PoolRange (const std::string &startAddr,
                             const std::string &endAddr, bool add);

  // Change TC-DSCP table entry.
  bool hqosTctbl (const u32 sw_if_index, const u32 dscp, const u32 tc,
                  const u32 queue, const u32 color);

  // QoS policer options (see "vnet/policer/xlate.h" in VPP source).
  typedef struct PolicerConfig
  {
    // The below fields are expected to be in node config.

    u8 name[64]; // Policer name (Max length 64)
    u32 cir = 0; // Committed information rate (kbps)
    u32 eir = 0; // Excess information rate (kbps)

    // cb and eb are calculated from cir and eir, respectively
    u64 cb = (u64)~0; // Committed burst (bytes)
    u64 eb = (u64)~0; // Excess burst (bytes)

    // The below fields are not configurable via node config.
    u8 conformDscp = 0; // Packets under CIR marked with this DSCP
    u8 exceedDscp = 0;  // Packets over CIR under PIR marked with this DSCP
    u8 violateDscp = 0; // Packets over PIR marked with this DSCP
    u8 isAdd;           // 0 == Delete, 1 == Add
    vapi_enum_sse2_qos_rate_type rateType =
        SSE2_QOS_RATE_API_KBPS; // unit for rates (kbps)
    vapi_enum_sse2_qos_round_type roundType =
        SSE2_QOS_ROUND_API_TO_UP; // Stats rounding
    vapi_enum_sse2_qos_policer_type type =
        SSE2_QOS_POLICER_TYPE_API_2R3C_RFC_4115; // Policer algorithm used.
    u8 colorAware = 0; // 0 == color-blind, 1 == color-aware

    vapi_enum_sse2_qos_action_type conformActionType =
        SSE2_QOS_ACTION_API_MARK_AND_TRANSMIT; // Action for packets under CIR
    vapi_enum_sse2_qos_action_type exceedActionType =
        SSE2_QOS_ACTION_API_MARK_AND_TRANSMIT; // Action for packets over CIR
                                               // under PIR
    vapi_enum_sse2_qos_action_type violateActionType =
        SSE2_QOS_ACTION_API_DROP; // Action for packets over PIR
  } PolicerConfig_t;

  // Add or delete QoS Policer.
  bool addDelPolicer (const PolicerConfig_t &policerConfig, u32 &policerIndex);

  // Get QoS policer by name.
  bool getPolicer (const u8 name[64], PolicerConfig_t &policerConfig);

  // Classifier session options (see "vnet/classify/README" in VPP Source).
  typedef struct ClassifierSessionConfig
  {
    u8 *match;                  // Array of length mask_len
    u32 tableIndex = 0;         // Represented as table name in node config
    u32 hitNextIndex = (u32)~0; // Represented as policer name in node config
    u32 matchLen = 16;          // Match length in bytes
    u8 isAdd = 1;               // 0 == Delete, 1 == Add
    u32 opaqueIndex = (u32)~0;  // Unused
    i32 advance = 0;            // Unused
    vapi_enum_classify_action action =
        CLASSIFY_API_ACTION_NONE; // Don't use metadata
    u32 metadata = 0;             // Unused
  } ClassifierSessionConfig_t;

  // Add or delete classifier session.
  bool addDelClassifierSession (
      const ClassifierSessionConfig_t &classifierSessionConfig);

  // Classifier table options (see "vnet/classify/README" in VPP Source).
  // When matching on IPv6 DSCP, skip_n_vectors should be 0, match_n_vectors
  // should be 1, mask_len should be 16, and mask should be all zeroes except
  // for the second to last byte which is 0x0E.
  typedef struct ClassifierTableConfig
  {
    u8 *mask;                     // Array of length mask_len
    u32 tableIndex = 0;           // VPP assigned id
    u32 skipNVectors = 0;         // Skip first n*16 bytes of the packets
    u32 matchNVectors = 1;        // Number of vectors to match on
    u32 maskLen = 16;             // Mask length in bytes
    u8 isAdd;                     // 0 == Delete, 1 == Add
    u8 delChain = 0;              // If is_add == 0, 1 deletes chain of tables
    u32 nbuckets = 4;             // Number of buckets for classification
    u32 memorySize = 1 << 25;     // Heap memory for classification
    u32 nextTableIndex = (u32)~0; // Table to search if match found
    u32 missNextIndex = (u32)~0;  // Table to search if match misses
    u32 currentDataFlag = 0;      // If 1, classify on packet payload
    i32 currentDataOffset = 0;    // offset for packet payload classification
    u32 activeSessions = 0;       // Number of active policer sessions
  } ClassifierTableConfig_t;

  // Get registered classifier table IDs
  bool getClassifierTableIds (std::vector<u32> &tableIds);

  // Add or delete classifier table.
  bool addDelClassifierTable (ClassifierTableConfig &tableConfig);

  // Get classifier table by software interface index.
  bool getClassifierByIndex (const u32 swIfIndex, u32 &tableId);

  // Map classifier table to network interface.
  bool setClassifierTableNetif (const u8 isAdd, const u32 swIfIndex,
                                const u32 ip6TableIndex);

private:
  // Execute a C++ VAPI Request and wait for the response.
  // Return true if no error was encountered.
  template <class T> bool executeAndWait (T &req, const std::string &apiName);

  // The VAPI connection
  vapi::Connection connection_;

  // Whether a VAPI connection has ever succeeded
  bool connected_{false};

  // Max number of outstanding requests queued by the VAPI client
  const int maxOutstandingRequests_;

  // Size of the VAPI client's response queue
  const int responseQueueSize_;
};

} // namespace vpp
} // namespace terragraph
} // namespace facebook
