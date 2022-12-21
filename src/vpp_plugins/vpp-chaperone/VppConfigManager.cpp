/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "VppConfigManager.h"

#include <fstream>

#include <boost/filesystem.hpp>
#include <folly/json.h>
#include <folly/FileUtil.h>
#include <folly/String.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "NetUtils.h"

namespace
{
// HQoS Constants
const int kVppTctblSize{64};
const int kVppNumTcClasses{4};
const int kVppNumQueues{1};
const std::string kHqosIfPrefix{"Wigig"};
// CPE Constants
// TODO: these should be configurable
const std::map<int, int> kAFxyConformDscp{{0, 34}, {1, 26}, {2, 18}, {3, 10}};
const std::map<int, int> kAFxyExcessDscp{{0, 36}, {1, 28}, {2, 20}, {3, 12}};
const std::map<int, u8> kAFxyMatch{{0, 8}, {1, 6}, {2, 4}, {3, 2}};
const int kMaxTrafficClass{3};
const int kSlaacPrefixLen{64};
} // namespace

// Interface names (for Linux and VPP)
DEFINE_string (loopback, "lo", "Linux loopback interface");
DEFINE_string (vpp_loopback, "loop0", "VPP loopback interface");
DEFINE_string (main_tap, "vnet0", "Linux main tap interface");
DEFINE_string (vpp_main_tap, "vpp-vnet0", "VPP main tap interface");
DEFINE_string (pop_tap, "tap1", "Linux POP tap interface");
DEFINE_string (pop_loopback, "loop1", "VPP POP loopback interface");
DEFINE_string (vpp_terra, "vpp-terra", "VPP terra interfaces");

// Address transformations
DEFINE_string (vpp_loopback_final_hextet, "0002",
               "The final IPv6 hextet (fully-qualified) to use for the VPP "
               "loopback address");
DEFINE_string (cpe_gw_addr_final_hextet, "0001",
               "The final IPv6 hextet (fully-qualified) to use for the CPE "
               "gateway address");
DEFINE_string (vxlan_endpoint_final_hextet, "0002",
               "The final IPv6 hextet (fully-qualified) to use for the VxLAN "
               "encap/decap address");
DEFINE_string (sr_encap_source_final_hextet, "0002",
               "The final IPv6 hextet (fully-qualified) to use for the SRv6 "
               "encapsulation source address");
DEFINE_string (
    sr_l2_encap_base_hextet, "1001",
    "The base IPv6 hextet to be added to VLAN ID to generate the SRv6 "
    "L2 encapsulation address");
DEFINE_string (
    sr_l2_decap_base_hextet, "2001",
    "The base IPv6 hextet to be added to VLAN ID to generate the SRv6 "
    "L2 decapsulation address");

// Other consts
DEFINE_int32 (vpp_loopback_prefix_length, 128,
              "Prefix length for the VPP loopback interface");
DEFINE_int32 (pop_prefix_length, 64, "Default POP prefix length");
DEFINE_int32 (node_prefix_length, 64, "Prefix length for node local prefix");
DEFINE_uint32 (pop_bridge_domain_id, 1, "POP bridge domain ID in VPP");
DEFINE_uint32 (vxlan_common_bd_id, 100,
               "VxLAN tunnel bridge domain ID in VPP");
DEFINE_uint32 (default_shg, 0, "Default split horizon group ID in VPP");
DEFINE_uint32 (vxlan_shg, 1, "VxLAN tunnel split horizon group ID in VPP");
DEFINE_uint32 (cpe_ra_interval_s, 4,
               "Interval (in seconds) between sending router-advertisement "
               "messages on the CPE interface.");
DEFINE_string (node_mac, "", "Base node MAC");
DEFINE_string (node_prefix, "",
               "Node prefix (e.g. 2001:db8::/63) that is allocated by "
               "controller. VPP uses it to derive CPE interface IP prefix");

namespace facebook
{
namespace terragraph
{
namespace vpp
{

VppConfigManager::VppConfigManager (const std::string &configEnvFile,
                                    const std::string &nodeConfigFile)
{
  boost::filesystem::path nodeConfigEnvFile (configEnvFile);
  if (!boost::filesystem::exists (nodeConfigEnvFile))
    {
      LOG (FATAL) << "Node config environment file does not exist: "
                  << configEnvFile;
    }
  envConfig_ = readConfigEnvFile (configEnvFile);
  readNodeConfigFile (nodeConfigFile);
}

std::unordered_map<std::string, std::string>
VppConfigManager::readConfigEnvFile (const std::string &configEnvFile) const
{
  std::unordered_map<std::string, std::string> config;
  std::ifstream input (configEnvFile);
  if (input.is_open ())
    {
      std::string line;
      while (std::getline (input, line))
        {
          if (line.length () < 1 || line.at (0) == '#') // skip comments
            continue;

          size_t idx = line.find ('=');
          if (idx == std::string::npos)
            continue;

          std::string key = line.substr (0, idx);
          std::string value = line.substr (idx + 1);
          // strip double-quotes
          value.erase (std::remove (value.begin (), value.end (), '\"'),
                       value.end ());

          config[key] = value;
        }
      input.close ();
    }
  return config;
}

std::string VppConfigManager::getEnv (const std::string &key) const
{
  auto iter = envConfig_.find (key);
  return iter == envConfig_.end () ? "" : iter->second;
}

void VppConfigManager::readNodeConfigFile (const std::string &nodeConfigFile)
{
  std::string jsonContents;

  // Read file
  if (!folly::readFile (nodeConfigFile.c_str (), jsonContents))
    {
      LOG (ERROR) << "Cannot read node config JSON file";
      return;
    }

  auto config = folly::parseJson (jsonContents);

  // read kvstoreParams and store
  std::string cfgKey = "kvstoreParams";

  if (config[cfgKey].empty ())
    {
      LOG (ERROR) << "Key '" << cfgKey << "' not present in "
                  << nodeConfigFile;
    }
  else
    {
      kvstoreParams_ = config[cfgKey];
    }

  // Read CPE parameters and store.
  cfgKey = "cpeConfig";
  if (config[cfgKey].empty ())
    {
      LOG (ERROR) << "Key '" << cfgKey << "' not present in "
                  << nodeConfigFile;
    }
  else
    {
      LOG (INFO) << cfgKey << folly::toPrettyJson (config[cfgKey]);
      cpeConfig_ = config[cfgKey];
    }

  // Read tunnel config and store.
  cfgKey = "tunnelConfig";
  if (config[cfgKey].empty ())
    {
      LOG (ERROR) << "Key '" << cfgKey << "' not present in "
                  << nodeConfigFile;
    }
  else
    {
      LOG (INFO) << cfgKey << " " << folly::toPrettyJson (config[cfgKey]);
      tunnelConfig_ = config[cfgKey];
    }

  // Read qos config and store.
  cfgKey = "qosConfig";

  if (config[cfgKey].empty ())
    {
      LOG (ERROR) << "Key '" << cfgKey << "' not present in "
                  << nodeConfigFile;
    }
  else
    {
      qosConfig_ = config[cfgKey];
    }
}

void VppConfigManager::run (VppClient &vppClient)
{
  doSlowPathConfig (vppClient);
  doPopConfig (vppClient);
  doCpeConfig (vppClient);
  doTunnelConfig (vppClient);
  doNat64Config (vppClient);
  doQosConfig (vppClient);
}

void VppConfigManager::doSlowPathConfig (VppClient &vppClient)
{
  LOG (INFO) << "> Applying slow path config...";

  // tgcfg plugin creates loop0 interface with a default MAC address
  // and it's a bit more complicated to read node_info in tgcfg.
  // Set a unique MAC address for loop0 here.
  std::string expectedLoop0Mac =
      NetUtils::generateLoopbackMacAddr (FLAGS_vpp_loopback, FLAGS_node_mac);

  if (expectedLoop0Mac.empty ())
    {
      LOG (ERROR) << FLAGS_vpp_loopback << " using default VPP MAC address";
    }
  else if (expectedLoop0Mac !=
           vppClient.getSwInterfaceMacAddress (FLAGS_vpp_loopback))
    {
      LOG (INFO) << "Setting " << FLAGS_vpp_loopback << " MAC address "
                 << expectedLoop0Mac;
      if (!vppClient.setSwInterfaceMacAddress (FLAGS_vpp_loopback,
                                               expectedLoop0Mac))
        {
          LOG (ERROR) << "Failed to set " << FLAGS_vpp_loopback
                      << " MAC address " << expectedLoop0Mac;
        }
    }

  std::vector<std::string> globalV6Addrs =
      NetUtils::getAllIfIPv6Addresses (FLAGS_loopback, true);
  std::string expectedVppLoopPrefix = "";
  std::string expectedNodeLocalPrefix = "";

  if (globalV6Addrs.size () > 1)
    {
      LOG (INFO) << "Found multiple global IPv6 addresses on '"
                 << FLAGS_loopback
                 << "' interface: " << folly::join (", ", globalV6Addrs);
    }

  // Use newly added address to generate vpp loopback prefix
  if (!globalV6Addrs.empty ())
    {
      expectedVppLoopPrefix = NetUtils::transformIPv6Prefix (
          globalV6Addrs[0] + "/" +
              std::to_string (FLAGS_vpp_loopback_prefix_length),
          "0001", FLAGS_vpp_loopback_final_hextet);

      // generate node local prefix (/64)
      expectedNodeLocalPrefix = NetUtils::transformIPv6Prefix (
          globalV6Addrs[0] + "/" + std::to_string (FLAGS_node_prefix_length),
          "0001", "0000");
    }
  std::string vppLoopPrefix =
      vppClient.getSwInterfacePrefix (FLAGS_vpp_loopback);
  // First index has the new address
  std::vector<std::string> tapV6Addrs =
      NetUtils::getAllIfIPv6Addresses (FLAGS_main_tap, false);
  std::string tapV6 = "";
  if (!tapV6Addrs.empty ())
    tapV6 = tapV6Addrs[0];

  // Assign address to VPP loopback
  if (vppLoopPrefix == expectedVppLoopPrefix)
    VLOG (2) << "Found correct address on VPP loopback interface '"
             << FLAGS_vpp_loopback << "': " << expectedVppLoopPrefix;
  else
    {
      if (!vppLoopPrefix.empty ())
        {
          VLOG (2) << "Deleting old address on " << FLAGS_vpp_loopback << " ("
                   << vppLoopPrefix << ")";
          vppClient.addDelInterfaceAddress (FLAGS_vpp_loopback, vppLoopPrefix,
                                            false);
        }
      if (!expectedVppLoopPrefix.empty ())
        {
          VLOG (2) << "Adding new address on " << FLAGS_vpp_loopback << " ("
                   << expectedVppLoopPrefix << ")";
          vppClient.addDelInterfaceAddress (FLAGS_vpp_loopback,
                                            expectedVppLoopPrefix, true);
        }
    }

  // Clean up old routes on vpp-vnet0 in VPP FIB that are using any prefix
  // not present in globalV6
  std::vector<std::string> staleRoutes =
      vppClient.getFibStaleRoutes (globalV6Addrs, tapV6, FLAGS_vpp_main_tap);
  std::string dropRoute = "";

  for (const std::string &route : staleRoutes)
    {
      VLOG (2) << "Deleting stale route to " << route << " via " << tapV6
               << " " << FLAGS_vpp_main_tap;
      vppClient.addDelRoute (route, tapV6, FLAGS_vpp_main_tap, false);

      dropRoute = NetUtils::transformIPv6Prefix (route, "0000", "0000", "64");
      VLOG (2) << "Deleting stale route to " << route << " via "
               << kVppRouteTypeDrop;
      vppClient.addDelRoute (dropRoute, tapV6, kVppRouteTypeDrop, false);
    }

  // Add route from VPP to linux loopback
  if (globalV6Addrs.empty ())
    LOG (ERROR) << "No global IPv6 address found on Linux loopback interface '"
                << FLAGS_loopback << "'.";
  else
    {
      // add discard route for node local prefix (derived from linux lo GUA) to
      // VPP FIB
      VLOG (2) << "Adding drop route to " << expectedNodeLocalPrefix << " via "
               << kVppRouteTypeDrop;
      vppClient.addDelRoute (expectedNodeLocalPrefix, "", kVppRouteTypeDrop,
                             true);
      // Add routes for all newly added addresses
      for (const std::string &v6Addr : globalV6Addrs)
        {
          if (vppClient.hasRoute (v6Addr, tapV6, FLAGS_vpp_main_tap, true))
            VLOG (2) << "Found existing route to " << v6Addr << " via "
                     << tapV6 << " " << FLAGS_vpp_main_tap;
          else
            {
              VLOG (2) << "Adding route to " << v6Addr << " via " << tapV6
                       << " " << FLAGS_vpp_main_tap;
              vppClient.addDelRoute (v6Addr, tapV6, FLAGS_vpp_main_tap, true);
            }
        }
    }
}

bool VppConfigManager::validatePopConfig ()
{
  return !getEnv ("VPP_ADDR").empty () && !getEnv ("POP_ADDR").empty () &&
         !getEnv ("POP_IFACE").empty ();
}

void VppConfigManager::doPopConfig (VppClient &vppClient)
{
  LOG (INFO) << "> Applying POP config...";
  std::string vppAddr = getEnv ("VPP_ADDR");
  std::string popAddr = getEnv ("POP_ADDR");
  int popPrefixLen = FLAGS_pop_prefix_length;
  std::string popPrefixLenStr = getEnv ("POP_ADDR_PREFIX_LEN");
  if (!popPrefixLenStr.empty ())
    {
      auto maybePopPrefixLen = folly::tryTo<int> (popPrefixLenStr);
      if (maybePopPrefixLen.hasValue ())
        {
          popPrefixLen = maybePopPrefixLen.value ();
        }
      else
        {
          LOG (ERROR) << "Unable to parse POP_ADDR_PREFIX_LEN \""
                      << popPrefixLenStr << "\"";
        }
    }
  std::string popIface = getEnv ("POP_IFACE");
  if (vppAddr.empty () || popAddr.empty () || popIface.empty ())
    {
      LOG (INFO)
          << "Missing one or more configs: [VPP_ADDR, POP_ADDR, POP_IFACE]";
      return;
    }
  std::string popSubIface = "";
  std::string popVlanIDStr = getEnv ("POP_VLAN_ID");
  if (!popVlanIDStr.empty ())
    {
      int popVlanID = std::stoi (popVlanIDStr);
      // Create sub-interface
      popSubIface = vppClient.createSubInterface (popIface, popVlanID);
      if (popSubIface.empty ())
        {
          LOG (ERROR) << "Could not create sub-interface for vlan "
                      << popVlanID << " on " << popIface << " interface.'";
          return;
        }
    }

  // Configure POP loopback interface
  std::string popLoopPrefix = vppAddr + "/" + std::to_string (popPrefixLen);
  std::string popLoopIface = vppClient.getSwInterfaceByPrefix (popLoopPrefix);
  if (popLoopIface.empty ())
    {
      // Create the POP loop1 interface with a unique MAC address.
      std::string popLoop1Mac = NetUtils::generateLoopbackMacAddr (
          FLAGS_pop_loopback, FLAGS_node_mac);
      if (popLoop1Mac.empty ())
        {
          LOG (ERROR) << FLAGS_pop_loopback
                      << " using default VPP MAC address";
        }
      popLoopIface = vppClient.createLoopbackInterface (popLoop1Mac);

      if (popLoopIface.empty ())
        {
          LOG (ERROR) << "Failed to create loopback interface in VPP";
          return;
        }
      if (!vppClient.addDelInterfaceAddress (popLoopIface, popLoopPrefix,
                                             true))
        {
          LOG (ERROR) << "Failed to add prefix " << popLoopPrefix
                      << " to new loopback interface '" << popLoopIface
                      << "' in VPP, deleting the interface.";
          vppClient.delLoopbackInterface (popLoopIface);
          return;
        }
      VLOG (2) << "Created loopback interface '" << popLoopIface
               << "' in VPP with prefix " << popLoopPrefix
               << " and MAC address " << popLoop1Mac;

      // Configure the bridge domain
      if (vppClient.hasBridgeDomain (FLAGS_pop_bridge_domain_id))
        VLOG (2) << "Bridge domain "
                 << std::to_string (FLAGS_pop_bridge_domain_id)
                 << " already exists.";
      else
        {
          VppClient::BridgeDomainConfig bdConfig;
          bdConfig.forward = true;
          bdConfig.learn = true;
          bdConfig.mac_age = 1;
          if (!vppClient.addDelBridgeDomain (FLAGS_pop_bridge_domain_id,
                                             bdConfig, true))
            {
              LOG (ERROR) << "Failed to create bridge domain "
                          << std::to_string (FLAGS_pop_bridge_domain_id)
                          << ", deleting loopback interface '" << popLoopIface
                          << "'";
              vppClient.delLoopbackInterface (popLoopIface);
              return;
            }
          VLOG (2) << "Created bridge domain "
                   << std::to_string (FLAGS_pop_bridge_domain_id);
        }
      if (!vppClient.setInterfaceL2Bridge (FLAGS_pop_bridge_domain_id,
                                           popLoopIface, FLAGS_default_shg,
                                           L2_API_PORT_TYPE_BVI))
        {
          LOG (ERROR) << "Failed to set L2 bridging on loopback interface '"
                      << popLoopIface << "', deleting the interface.";
          vppClient.delLoopbackInterface (popLoopIface);
          return;
        }
      VLOG (2) << "Set L2 bridging on loopback interface '" << popLoopIface
               << "'";
    }
  else
    VLOG (2) << "Found existing loopback interface '" << popLoopIface
             << "' in VPP with prefix " << popLoopPrefix;

  vppClient.enableDisableIp6Interface (popLoopIface, true);

  // Configure POP tap interface
  std::string popTapIface = vppClient.getTapInterface (FLAGS_pop_tap);
  if (popTapIface.empty ())
    {
      // Derive Linux tap interface MAC address from loop1 MAC address
      //
      // If these two MAC addresses are related, their IPv6 link local
      // addresses will also be related.  This allows calculating one link
      // local address from the other.  This is important because on the Linux
      // side, only one link local address is visible:  the one for the tap
      // interface.
      //
      // Because of the relationship between tap and loopback, Open/R, which
      // lives on the Linux domain (or more precisely, openr-fib-vpp) will be
      // able to calculate the link local address of the VPP loopback interface
      // (loopX) from the Linux tap interface.
      //
      // This is critical - as Open/R forms adjacencies over linux interfaces
      // but needs the "nextHop" for Open/R routes to be VPP's loopX.
      const std::string popTapMac = NetUtils::generatePopTapMacAddr (
          vppClient.getSwInterfaceMacAddress (FLAGS_pop_loopback));
      popTapIface = vppClient.connectTap (FLAGS_pop_tap, popTapMac);
      VLOG (2) << "Created POP tap interface '" << popTapIface << "' in VPP"
               << " with MAC address '" << popTapMac << "'.";

      // Add prefix on Linux side
      if (!NetUtils::addIfIPv6Address (FLAGS_pop_tap, popAddr, popPrefixLen))
        {
          LOG (ERROR) << "Failed to add address on Linux POP tap interface '"
                      << FLAGS_pop_tap << "', deleting the VPP interface.";
          vppClient.delTap (popTapIface);
          return;
        }
      VLOG (2) << "Added address on Linux POP tap interface '" << FLAGS_pop_tap
               << "': " << popAddr << "/" << std::to_string (popPrefixLen);

      // Configure L2 bridging
      if (!vppClient.setInterfaceL2Bridge (FLAGS_pop_bridge_domain_id,
                                           popTapIface, FLAGS_default_shg))
        {
          LOG (ERROR) << "Failed to set L2 bridging on POP tap interface '"
                      << popTapIface << "', deleting the interface.";
          vppClient.delTap (popTapIface);
          return;
        }
      // Add sub-interface to POP bridge if VLAN is configured to allow VLAN
      // segregation of control-plane vs data-plane traffic. Otherwise, add the
      // main interface to POP bridge to allow all frames to be forwarded.
      std::string bridgeInterface = popIface;
      if (!popSubIface.empty ())
        {
          bridgeInterface = popSubIface;
        }
      if (vppClient.setInterfaceL2Bridge (FLAGS_pop_bridge_domain_id,
                                          bridgeInterface, FLAGS_default_shg))
        {
          VLOG (2) << "Set L2 bridging on POP interface '" << bridgeInterface
                   << "'.";
        }
      else
        {
          LOG (ERROR) << "Failed to set L2 bridging on POP interface '"
                      << bridgeInterface << "', deleting the tap interface.";
          vppClient.delTap (popTapIface);
          return;
        }
    }
  else
    VLOG (2) << "Found existing POP tap interface '" << popTapIface
             << "' in VPP for device " << FLAGS_pop_tap;

  // Set interfaces up
  VLOG (2) << "Setting flag ADMIN_UP on interfaces: " << popLoopIface << ", "
           << popIface << ", " << popTapIface;
  vppClient.setInterfaceFlags (popLoopIface, true);
  vppClient.setInterfaceFlags (popIface, true);
  vppClient.setInterfaceFlags (popTapIface, true);
  if (!popSubIface.empty ())
    {
      vppClient.setInterfaceFlags (popSubIface, true);
    }

  // e2e-network-prefix is stored in node_config.json the format is
  // xxxx::/plength,allocation_size, we need to strip the part from "," on
  std::string e2eNetworkPrefix = "";
  auto iter = kvstoreParams_.find ("e2e-network-prefix");
  if (iter != kvstoreParams_.items ().end ())
    {
      e2eNetworkPrefix = iter->second.asString ();

      auto idx = e2eNetworkPrefix.find_first_of (',');
      if (idx == std::string::npos)
        {
          e2eNetworkPrefix = "";
        }
      else
        {
          e2eNetworkPrefix.erase (idx);
        }
    }

  // e2e-network-prefix is usually a large, contiuous address block, which is
  // very sparsely utilised. any packet with dst address from the unassigned
  // part of the network prefix will ping-pong between POP(s) and BGP router(s)
  // until hop limit expiration. we need to add a drop route to the VPP FIB in
  // POP nodes to counter this. drop routes are only added, but not removed, as
  // change in the e2e-network-prefix requires reload which wipes FIB anyway
  if (!e2eNetworkPrefix.empty ())
    {
      VLOG (2) << "Adding network prefix drop route for " << e2eNetworkPrefix
               << " via " << kVppRouteTypeDrop;
      vppClient.addDelRoute (e2eNetworkPrefix, "", kVppRouteTypeDrop, true);
    }

  // Static routing config (if needed)
  if (getEnv ("POP_STATIC_ROUTING") == "1")
    {
      std::string gwAddr = getEnv ("GW_ADDR");
      if (gwAddr.empty ())
        {
          LOG (INFO) << "Missing configs for static routing: [GW_ADDR]";
          return;
        }
      VLOG (2) << "Adding POP static default route via " << gwAddr;
      if (getEnv ("NAT64_POP_ENABLED") == "1")
        {
          vppClient.addDelRoute ("0.0.0.0/0", gwAddr, FLAGS_pop_loopback,
                                 true);
        }
      else
        {
          vppClient.addDelRoute ("::/0", gwAddr, "", true);
        }
    }
}

void VppConfigManager::doNat64Config (VppClient &vppClient)
{
  LOG (INFO) << "> Applying NAT64 config on POP...";

  if (!validatePopConfig ())
    {
      LOG (INFO)
          << "Missing POP Config for NAT64: [VPP_ADDR, POP_ADDR, POP_IFACE]";
      return;
    }

  std::string nat64Prefix = getEnv ("NAT64_IPV6_PREFIX");
  std::string ipv4Addr = getEnv ("NAT64_IPV4_ADDR");
  if (getEnv ("NAT64_POP_ENABLED").empty () || nat64Prefix.empty () ||
      ipv4Addr.empty ())
    {
      LOG (INFO) << "Missing one or more configs: [NAT64_POP_ENABLED, "
                    "NAT64_IPV6_PREFIX, NAT64_IPV4_ADDR]";
      return;
    }

  if (getEnv ("NAT64_POP_ENABLED") == "0")
    {
      LOG (INFO) << "NAT64 not enabled on POP.";
      return;
    }

  // IPv4 static routing is required, if POP node needs to connect with
  // external IPv4 network
  if (getEnv ("POP_STATIC_ROUTING").empty () ||
      getEnv ("POP_STATIC_ROUTING") == "0")
    {
      LOG (WARNING) << "NAT64 IPv4 static routing is not enabled on POP.";
    }

  std::string nat64Iface = FLAGS_pop_loopback;

  // Setup NAT64 egress operation on NAT64 interface. This egresses
  // IPv4 packets to external IPv4 network. Usually this is the POP
  // interface itself.

  if (!vppClient.setNat64Interface (nat64Iface, VppNat64Flags::EGRESS_INTF,
                                    true))
    {
      LOG (ERROR) << "Could not enable NAT64 egress operation on '"
                  << nat64Iface << "'.";
      return;
    }
  LOG (INFO) << "Enabled NAT64 egress operation on '" << nat64Iface << "'.";

  // Setup ingress interfaces for NAT64. Currently, all local traffic
  // (vpp-vnet0) and control-plane traffic from other Wi-Gig interfaces
  // (vpp-terraX) need to be ingress interfaces.

  if (!vppClient.setNat64Interface (FLAGS_vpp_main_tap,
                                    VppNat64Flags::INGRESS_INTF, true))
    {
      LOG (ERROR) << "Could not enable NAT64 ingress operation on '"
                  << FLAGS_vpp_main_tap << "'.";
      return;
    }

  auto ifaceMap = vppClient.getIfaceToVppIndexMap ();
  for (const auto &kv : ifaceMap)
    {
      if (kv.first.find (FLAGS_vpp_terra) != std::string::npos)
        {
          if (!vppClient.setNat64Interface (kv.first,
                                            VppNat64Flags::INGRESS_INTF, true))
            {
              LOG (ERROR) << "Could not enable NAT64 ingress operation on '"
                          << kv.first << "'.";
            }
        }
    }

  LOG (INFO) << "Enabled NAT64 ingress operation on '" << FLAGS_vpp_main_tap
             << "' and '" << FLAGS_vpp_terra << "X' interfaces.";

  // Program the NAT64 prefix that will encode the IPv4 addresses. 64:ff9b::/96
  // is a well-known prefix. The resulting IPv6 representaiton of IPv4
  // addresses will be 64:ff9b::198.51.100.2 for example.

  if (!vppClient.addDelNat64TranslationPrefix (nat64Prefix, true))
    {
      LOG (ERROR) << "Could not program NAT64 prefix '" << nat64Prefix << "'.";
      return;
    }

  LOG (INFO) << "Programmed NAT64 prefix '" << nat64Prefix << "'.";

  // Stateful NAT64 only exposes one IPv4 address to the external network so
  // for now let's just put one address in the IPv4 pool.
  std::string poolAddr =
      folly::IPAddress::createNetwork (ipv4Addr, -1, false).first.str ();
  std::string currPoolAddr = vppClient.getNat64PoolRange ();
  if (currPoolAddr != poolAddr)
    {
      // If pool range is changed, delete current pool range before adding the
      // new pool
      if (!currPoolAddr.empty ())
        {
          if (!vppClient.addDelNat64PoolRange (currPoolAddr, currPoolAddr,
                                               false))
            {
              LOG (ERROR) << "Could not delete NAT64 IPv4 pool range '"
                          << currPoolAddr << "' - '" << currPoolAddr << "'.";
              return;
            }
          LOG (INFO) << "Deleted NAT64 IPv4 pool range '" << currPoolAddr
                     << "' - '" << currPoolAddr << "'.";
        }

      if (!vppClient.addDelNat64PoolRange (poolAddr, poolAddr, true))
        {
          LOG (ERROR) << "Could not add NAT64 IPv4 pool range '" << poolAddr
                      << "' - '" << poolAddr << "'.";
          return;
        }
      LOG (INFO) << "Added NAT64 IPv4 pool range '" << poolAddr << "' - '"
                 << poolAddr << "'.";
    }
  else
    LOG (INFO) << "NAT64 IPv4 pool range '" << poolAddr << "' - '" << poolAddr
               << "' is already configured";

  // Use the same IPv4 address in the pool to assign to the NAT64 interface
  // for now.
  std::string intfAddr =
      folly::IPAddress::createNetwork (ipv4Addr, -1, false).first.str ();
  std::string currIntfAddr = vppClient.getInterfaceAddr (nat64Iface, false);
  if (currIntfAddr != intfAddr)
    {
      // If interface address is changed, delete current interface address
      // before adding the new address
      if (!currIntfAddr.empty ())
        {
          if (!vppClient.addDelInterfaceAddress (nat64Iface, currIntfAddr,
                                                 false))
            {
              LOG (ERROR) << "Could not delete IPv4 addresss '" << currIntfAddr
                          << "' on '" << nat64Iface << "'.";
              return;
            }
          LOG (INFO) << "Deleted IPv4 addresss '" << currIntfAddr << "' on '"
                     << nat64Iface << "'.";
        }

      if (!vppClient.addDelInterfaceAddress (nat64Iface, intfAddr, true))
        {
          LOG (ERROR) << "Could not add IPv4 addresss '" << intfAddr
                      << "' on '" << nat64Iface << "'.";
          return;
        }
      LOG (INFO) << "Added IPv4 addresss '" << intfAddr << "' on '"
                 << nat64Iface << "'.";
    }
  else
    LOG (INFO) << "NAT64 IPv4 address '" << intfAddr
               << "' is already configured on '" << nat64Iface << "'.";
}

void VppConfigManager::doCpeConfig (VppClient &vppClient)
{
  if (cpeConfig_.empty ())
    {
      // Use deprecated CPE config if cpeConfig is empty.
      LOG (INFO) << "> Applying single interface CPE config...";
      doCpeInterfaceConfig (vppClient, getEnv ("CPE_INTERFACE"),
                            getEnv ("CPE_IFACE_IP_PREFIX"));
      return;
    }

  LOG (INFO) << "> Applying CPE config...";
  for (const auto &kv : cpeConfig_.items ())
    {
      if (kv.second.isObject ())
        {
          LOG (INFO) << "Configuring '" << kv.first << "' as CPE interface.";
          doCpeInterfaceConfig (vppClient, kv.first.asString (),
                                kv.second["prefix"].asString ());

          if (kv.second.count ("policers"))
            {
              LOG (INFO) << "Adding policers to CPE interface " << kv.first;
              doCpePolicerConfig (vppClient, kv.first.asString (),
                                  kv.second["policers"], true);
            }
          else
            {
              LOG (INFO) << "Deleting policers for CPE interface " << kv.first;
              doCpePolicerConfig (vppClient, kv.first.asString (), 0, false);
            }
          if (kv.second.count ("dhcpRelay"))
            {
              LOG (INFO) << "Configuring DHCPv6 on'" << kv.first << "'.";
              doCpeDhcpv6RelayConfig (vppClient, kv.first.asString (),
                                      kv.second["dhcpRelay"]);
            }
        }
    }
}

std::string VppConfigManager::getFirstCpeInterface ()
{
  std::string cpeIntf;
  if (!cpeConfig_.empty ())
    {
      cpeIntf = cpeConfig_.items ().begin ()->first.asString ();
    }
  else
    {
      cpeIntf = getEnv ("CPE_INTERFACE");
    }
  return cpeIntf;
}

std::string VppConfigManager::deriveCpeIpPrefix (VppClient &vppClient,
                                                 const std::string &interface)
{
  VLOG (2) << "Deriving CPE IP prefix based on node prefix '"
           << FLAGS_node_prefix << "'.";

  folly::CIDRNetwork nodePrefix;
  try
    {
      nodePrefix = folly::IPAddress::createNetwork (FLAGS_node_prefix);
    }
  catch (std::exception &e)
    {
      VLOG (2) << "Invalid node prefix '" << FLAGS_node_prefix << "' ("
               << e.what () << "), skip deriving CEP IP prefix";
      return "";
    }

  if ((int)nodePrefix.second >= kSlaacPrefixLen)
    {
      VLOG (2) << "Could not derive /64 CPE prefix based on node prefix. Node "
                  "prefix has to be at least /63";
      return "";
    }

  int cpePrefixCount =
      kSlaacPrefixLen - nodePrefix.second < (int)sizeof (int) * 8
          ? 1 << (kSlaacPrefixLen - nodePrefix.second)
          : INT32_MAX;
  std::string currGwAddr = vppClient.getSwInterfacePrefix (interface);
  std::string prefix;
  if (NetUtils::inSubnet (nodePrefix, currGwAddr))
    {
      // Leave the CPE prefix unchanged, if current CPE interface already has a
      // prefix configured and the prefix is in the subnet of the node prefix.
      folly::CIDRNetwork currPrefix =
          folly::IPAddress::createNetwork (currGwAddr, kSlaacPrefixLen, true);
      prefix = folly::IPAddress::networkToString (currPrefix);
    }
  else
    {
      // Find the first unused prefix for CPE interface.
      while (cpePrefixIndex_ < cpePrefixCount)
        {
          std::string newPrefix = NetUtils::getNthPrefix (
              nodePrefix, kSlaacPrefixLen, cpePrefixIndex_);
          cpePrefixIndex_++;

          // Check if this prefix is already assigned to an interface.
          std::string gwAddr = NetUtils::transformIPv6Prefix (
              newPrefix, "0000", FLAGS_cpe_gw_addr_final_hextet);
          if (vppClient.getSwInterfaceByPrefix (gwAddr).empty ())
            {
              prefix = newPrefix;
              break;
            }
        }

      if (prefix.empty ())
        {
          VLOG (2) << "Run out of address spaces.";
          return "";
        }
    }
  VLOG (2) << "The derived CPE interface IP prefix is '" << prefix << "'.";

  return prefix;
}

void VppConfigManager::doCpeInterfaceConfig (VppClient &vppClient,
                                             const std::string &interface,
                                             const std::string &prefixCfg)
{
  if (interface.empty ())
    {
      LOG (INFO) << "Missing CPE interface config.";
      return;
    }

  std::vector<std::string> globalV6Addrs =
      NetUtils::getAllIfIPv6Addresses (FLAGS_loopback, true);
  if (globalV6Addrs.empty ())
    {
      LOG (ERROR)
          << "No global IPv6 address found on Linux loopback interface '"
          << FLAGS_loopback << "'. Skipping CPE config...";
      return;
    }

  // If CPE interface IP prefix is missing, derive it based on node prefix
  std::string prefix = prefixCfg.empty ()
                           ? deriveCpeIpPrefix (vppClient, interface)
                           : prefixCfg;
  if (prefix.empty ())
    {
      LOG (INFO) << "Missing CPE IP prefix config.";
      return;
    }

  // Enable the CPE interface
  VLOG (2) << "Enabling CPE interface '" << interface << "' in VPP";
  vppClient.enableDisableIp6Interface (interface, true);

  // Set neighbor discovery parameters
  VLOG (2) << "Setting neighbor discovery parameters on '" << interface << "'";
  VppClient::RaConfig raConfig;
  raConfig.max_interval = FLAGS_cpe_ra_interval_s;
  if (!vppClient.setInterfaceRaConfig (interface, raConfig))
    {
      LOG (ERROR) << "Failed to set RA config on '"
                  << interface << "', disabling the interface.";
      vppClient.enableDisableIp6Interface (interface, false);
      return;
    }
  VppClient::RaPrefixConfig raPrefixConfig;
  raPrefixConfig.val_lifetime = ~0;
  raPrefixConfig.pref_lifetime = ~0;
  if (!vppClient.setInterfaceRaPrefixConfig (interface, prefix,
                                             raPrefixConfig))
    {
      LOG (ERROR) << "Failed to set RA prefix config on '"
                  << interface << "' for prefix " << prefix
                  << ", disabling the interface.";
      vppClient.enableDisableIp6Interface (interface, false);
      return;
    }

  // Assign prefix
  std::string expectedGwAddr = NetUtils::transformIPv6Prefix (
      prefix, "0000", FLAGS_cpe_gw_addr_final_hextet);
  std::string gwAddr = vppClient.getSwInterfacePrefix (interface);
  if (gwAddr == expectedGwAddr)
    VLOG (2) << "Found correct address on CPE interface '"
             << interface << "': " << expectedGwAddr;
  else
    {
      if (!gwAddr.empty ())
        {
          VLOG (2) << "Deleting old address on " << interface << " (" << gwAddr
                   << ")";
          vppClient.addDelInterfaceAddress (interface, gwAddr, false);
        }
      if (!expectedGwAddr.empty ())
        {
          VLOG (2) << "Adding new address on " << interface << " ("
                   << expectedGwAddr << ")";
          vppClient.addDelInterfaceAddress (interface, expectedGwAddr, true);
        }
    }
}

void VppConfigManager::doCpeDhcpv6RelayConfig (
    VppClient &vppClient, const std::string &cpeInterface,
    const folly::dynamic &dhcpRelayConfig)
{
  std::string cpeIfacePrefix = vppClient.getSwInterfacePrefix (cpeInterface);
  std::vector<std::string> requiredDhcpv6Config = {"dhcpServerIp", "enabled"};
  for (const auto &key : requiredDhcpv6Config)
    {
      if (!dhcpRelayConfig.count (key))
        {
          LOG (ERROR) << "Required DHCPv6 key '" << key << "' not found in '"
                      << cpeInterface << "', skipping DHCPv6 config.";
          return;
        }
    }

  std::string dhcpServerIp = dhcpRelayConfig["dhcpServerIp"].asString ();
  if (cpeIfacePrefix.empty () || dhcpServerIp.empty ())
    {
      LOG (ERROR) << "No CPE IP or DHCPv6 server IP found in '" << cpeInterface
                  << "', skipping DHCPv6 config.";
      return;
    }

  bool enabled = dhcpRelayConfig["enabled"].asBool ();

  // Check for optional DHCPv6 options.
  auto key = "dhcpOptionRemoteId";
  bool remoteIdOption =
      dhcpRelayConfig.count (key) ? dhcpRelayConfig[key].asBool () : false;
  key = "enterpriseId";
  u32 enterpriseId =
      dhcpRelayConfig.count (key) ? dhcpRelayConfig[key].asInt () : 0;
  key = "remoteIdMac";
  std::string remoteIdMac =
      dhcpRelayConfig.count (key) ? dhcpRelayConfig[key].asString () : "";
  key = "dhcpOptionRemoteId";
  bool subIdOption =
      dhcpRelayConfig.count (key) ? dhcpRelayConfig[key].asBool () : false;

  if (remoteIdOption)
    {
      if (enterpriseId < 1)
        {
          LOG (INFO)
              << "No Enterprise ID found for DHCPv6 Remote ID option in '"
              << cpeInterface << "'.";
          remoteIdOption = false;
        }
    }

  if (!vppClient.setDhcpv6ProxyConfig (dhcpServerIp, cpeIfacePrefix,
                                       remoteIdOption, subIdOption,
                                       enterpriseId, remoteIdMac, enabled))
    {
      LOG (ERROR) << "Unable to configure DHCPv6 relay on '" << cpeInterface
                  << "' with DHCPv6 server at '" << dhcpServerIp << "'.";
    }
  else
    {
      auto enabledStr = enabled ? "enabled" : "disabled";
      LOG (INFO) << "DHCPv6 relay " << enabledStr << " on '" << cpeIfacePrefix
                 << "' with DHCPv6 server at '" << dhcpServerIp << "'.";
    }
}

bool VppConfigManager::validateTunnelConfig (folly::dynamic &tunnel)
{
  if (tunnel.empty ())
    {
      LOG (ERROR) << "Empty or disabled tunnel. "
                  << folly::toPrettyJson (tunnel);
      return false;
    }

  if (tunnel["dstIp"].empty ())
    {
      LOG (ERROR) << "No tunnel destination IP. "
                  << folly::toPrettyJson (tunnel);
      return false;
    }

  // If localInterface is empty in config, use first CPE interface for now.
  if (tunnel.find ("localInterface") == tunnel.items ().end ())
    {
      tunnel["localInterface"] = getFirstCpeInterface ();
    }

  if (tunnel["localInterface"].empty ())
    {
      LOG (ERROR) << "No CPE tunnel interface. "
                  << folly::toPrettyJson (tunnel);
      return false;
    }

  return true;
}

void VppConfigManager::doTunnelConfig (VppClient &vppClient)
{
  LOG (INFO) << "> Applying tunnel config...";

  // Get current VxLANs configured in VPP
  VxlanTunnelMap currVxlanTunnels = vppClient.getVxlanTunnelDumps ();
  // Get current SRs configured in VPP
  Srv6TunnelMap currSrPolicies = vppClient.getSrPolicyDumps (
      FLAGS_sr_l2_encap_base_hextet, FLAGS_sr_l2_decap_base_hextet);

  for (auto &kv : tunnelConfig_.items ())
    {
      if (!validateTunnelConfig (kv.second))
        {
          continue;
        }

      auto tunnelConfig = kv.second;
      auto type = tunnelConfig["tunnelType"].asString ();

      LOG (INFO) << "Configuring " << type << " tunnel '" << kv.first << "'.";

      if (type == "VXLAN")
        {
          doVxlanTunnelConfig (vppClient, tunnelConfig, currVxlanTunnels);
        }
      else if (type == "SRV6")
        {
          doSrTunnelConfig (vppClient, tunnelConfig, currSrPolicies);
        }
      else
        {
          LOG (ERROR) << "Unsupported tunnel type '" << type << "'.";
        }
    }

  // Clean up the remaining existing VxLAN tunnels
  for (const auto &currVxlanTunnel : currVxlanTunnels)
    {
      std::string tunnelSrcIP = currVxlanTunnel.second.first;
      std::string tunnelDstIP = currVxlanTunnel.first;
      int vni = currVxlanTunnel.second.second;

      vppClient.addDelVxLanTunnel (tunnelSrcIP, tunnelDstIP, vni, false);
      LOG (INFO) << "Cleaned up a VxLAN tunnel '" << vni
                 << "' with source as '" << tunnelSrcIP
                 << "' and destination '" << tunnelDstIP << "'.";
    }

  // Clean up the remaining existing SRv6 tunnels
  for (const auto &currSrPolicy : currSrPolicies)
    {
      int vlanId = currSrPolicy.first;
      std::string bsid = currSrPolicy.second.first;
      std::string sid = currSrPolicy.second.second;

      // Find and delete SR encap policy and decap SID using interface name
      std::string ifName = vppClient.getSrSteeringPolicyIfName (bsid);
      if (ifName.empty ())
        {
          LOG (ERROR)
              << "Cannot find a SRv6 steering policy in VPP that has BSID as "
              << bsid;
          continue;
        }

      std::string localSid = vppClient.getSrLocalSid (ifName);
      if (localSid.empty ())
        {
          LOG (ERROR) << "Cannot find the local SID in VPP that is associated "
                         "with interface "
                      << ifName;
          continue;
        }

      vppClient.deleteSrPolicy (bsid);
      vppClient.addDelSrSteeringPolicy (ifName, bsid, false);
      vppClient.addDelLocalSid (localSid, ifName, false);
      vppClient.deleteSubInterface (ifName);

      LOG (INFO) << "Cleaned up a SRv6 tunnel with BSID '" << bsid
                 << ", next SID '" << sid << "', and VLAN ID '" << vlanId
                 << "'.";
    }
}

void VppConfigManager::doVxlanTunnelConfig (VppClient &vppClient,
                                            const folly::dynamic &tunnelConfig,
                                            VxlanTunnelMap &currTunnels)
{
  std::string dstIP = tunnelConfig["dstIp"].asString ();
  std::string srcIface = tunnelConfig["localInterface"].asString ();
  bool enabled = tunnelConfig["enabled"].asBool ();
  int vni = 100;
  bool isPrimaryTunnel = true;
  if (tunnelConfig.count ("tunnelParams"))
    {
      if (tunnelConfig["tunnelParams"].count ("vlanId"))
        {
          vni = tunnelConfig["tunnelParams"]["vlanId"].asInt ();
        }
      if (tunnelConfig["tunnelParams"].count ("primaryTunnelName"))
        {
          isPrimaryTunnel = false;
          if (tunnelConfig["tunnelParams"]["primaryTunnelName"]
                  .asString ()
                  .empty ())
            {
              return;
            }
        }
    }

  std::vector<std::string> globalV6Addrs =
      NetUtils::getAllIfIPv6Addresses (FLAGS_loopback, true);
  if (globalV6Addrs.empty ())
    {
      LOG (ERROR)
          << "No global IPv6 address found on Linux loopback interface '"
          << FLAGS_loopback << "'.";
      return;
    }
  std::string globalV6 = globalV6Addrs[0];
  // VxLAN endpoint prefixes will be the same prefix as VPP loopback.
  std::string tunnelSrcIP = NetUtils::transformIPv6 (
      globalV6, "0001", FLAGS_vxlan_endpoint_final_hextet);
  std::string tunnelDstIP = NetUtils::transformIPv6 (
      dstIP, "0001", FLAGS_vxlan_endpoint_final_hextet);

  auto currTunnel = currTunnels.find (tunnelDstIP);
  if (currTunnel != currTunnels.end ())
    {
      auto currTunnelParams = currTunnel->second;
      std::string currTunnelSrcIP = currTunnelParams.first;
      std::string currTunnelDstIP = tunnelDstIP;
      int currVni = currTunnelParams.second;

      // Remove tunnel from map, because this tunnel is being processed
      currTunnels.erase (tunnelDstIP);
      // If the tunnel already exists in VPP, do nothing.
      if (enabled && currTunnelSrcIP == tunnelSrcIP && currVni == vni)
        {
          LOG (INFO) << "Found matching VxLAN tunnel '" << vni
                     << "' in VPP with source as '" << tunnelSrcIP
                     << "' and destination '" << tunnelDstIP << "'";
          return;
        }

      vppClient.addDelVxLanTunnel (currTunnelSrcIP, tunnelDstIP, currVni,
                                   false);
      LOG (INFO) << "Deleted a VxLAN tunnel '" << currVni
                 << "' with source as '" << currTunnelSrcIP
                 << "' and destination '" << currTunnelDstIP << "'.";
    }

  if (!enabled)
    {
      return;
    }

  std::string tunnelIface =
      vppClient.addDelVxLanTunnel (tunnelSrcIP, tunnelDstIP, vni, true);
  if (tunnelIface.empty ())
    {
      LOG (INFO) << "Failed to create the VxLAN tunnel '" << vni
                 << "' with source as '" << tunnelSrcIP
                 << "' and destination '" << tunnelDstIP << "'.";
      return;
    }
  LOG (INFO) << "Created a VxLAN tunnel '" << vni << "' with source as '"
             << tunnelSrcIP << "' and destination '" << tunnelDstIP << "'.";

  // TODO(ipurush) - Enable VLAN tag support
  // if (!vppClient.setInterfaceL2VLANTag (srcIface, vni))
  //   {
  //     LOG (ERROR) << "Failed to set L2 VLAN tag on interface '" <<
  //     srcIface
  //                 << "', deleting the tunnel interface.";
  //     vppClient.addDelVxLanTunnel (tunnelSrcIP, tunnelDstIP, vni,
  //                                  false  );
  //     return;
  //   }

  bool success = true;
  if (validatePopConfig ())
    {
      success = doVxLanBridgeConfig (vppClient, srcIface, tunnelIface, dstIP);
    }
  else if (isPrimaryTunnel)
    {
      success = doVxLanXconnectConfig (vppClient, srcIface, tunnelIface);
    }

  // Delete tunnel if srcIface and tunnelIface are not able to be connected
  if (!success)
    {
      vppClient.addDelVxLanTunnel (tunnelSrcIP, tunnelDstIP, vni, false);
    }
}

bool VppConfigManager::doVxLanBridgeConfig (VppClient &vppClient,
                                            const std::string &srcIface,
                                            const std::string &tunnelIface,
                                            const std::string &dstNodeIP)
{
  int vxlanBridgeId = FLAGS_pop_bridge_domain_id;
  // If there is a control-plane VLAN then enable CP/DP
  // separation by putting all VxLAN tunnels on a different bridge
  // while the control-plane VLAN is restricted to the POP bridge.
  if (!getEnv ("POP_VLAN_ID").empty ())
    {
      vxlanBridgeId = FLAGS_vxlan_common_bd_id;
      // Configure the bridge domain for the tunnel
      if (vppClient.hasBridgeDomain (vxlanBridgeId))
        {
          VLOG (2) << "Bridge domain " << std::to_string (vxlanBridgeId)
                   << " already exists.";
        }
      else
        {
          VppClient::BridgeDomainConfig bdConfig;
          bdConfig.forward = true;
          bdConfig.learn = true;
          bdConfig.mac_age = 1;
          if (!vppClient.addDelBridgeDomain (vxlanBridgeId, bdConfig, true))
            {
              LOG (ERROR) << "Failed to create bridge domain "
                          << std::to_string (vxlanBridgeId) << "'";
              return false;
            }
          VLOG (2) << "Created bridge domain "
                   << std::to_string (vxlanBridgeId);
        }

      if (!vppClient.setInterfaceL2Bridge (vxlanBridgeId, srcIface,
                                           FLAGS_default_shg))
        {
          LOG (ERROR)
              << "Failed to set L2 bridging on the wired or CPE interface '"
              << srcIface << "', deleting the tunnel interface.";
          return false;
        }
      LOG (INFO) << "Added CPE interface '" << srcIface
                 << "' to VxLAN bridge '" << vxlanBridgeId << "'.";
    }

  // Add the tunnel interface to the POP bridge only if there is a valid
  // route to the destination node. If there is no valid route, tunnel monitor
  // service will add the tunnel interface to the bridge once the destination
  // node comes online.
  if (vppClient.hasRoute (dstNodeIP, "::1", "", false))
    {
      VLOG (2) << "Found existing route to tunnel destination node '"
               << dstNodeIP << "'.";
      if (!vppClient.setInterfaceL2Bridge (vxlanBridgeId, tunnelIface,
                                           FLAGS_vxlan_shg))
        {
          LOG (ERROR) << "Failed to set L2 bridging on tunnel interface '"
                      << tunnelIface << "', deleting the tunnel interface.";
          return false;
        }
      LOG (INFO) << "Added tunnel interface '" << tunnelIface
                 << "' to VxLAN bridge '" << vxlanBridgeId << "'.";
    }
  else
    {
      LOG (ERROR)
          << "Failed to add tunnel interface '" << tunnelIface
          << "' to POP bridge. No existing route to tunnel destination node "
          << dstNodeIP << ".";
    }

  return true;
}

bool VppConfigManager::doVxLanXconnectConfig (VppClient &vppClient,
                                              const std::string &srcIface,
                                              const std::string &tunnelIface)
{
  // If there is only one CPE interface we can just xconnect
  // the VxLAN tunnel interface and the CPE interface.
  if (!vppClient.setInterfaceL2xConnect (srcIface, tunnelIface))
    {
      LOG (ERROR) << "Failed to xconnect '" << srcIface << "' with '"
                  << tunnelIface << "'.";
      return false;
    }

  if (!vppClient.setInterfaceL2xConnect (tunnelIface, srcIface))
    {
      LOG (ERROR) << "Failed to xconnect '" << tunnelIface << "' with '"
                  << srcIface << "'.";
      return false;
    }

  LOG (INFO) << "Created xconnect between '" << tunnelIface << "' and '"
             << srcIface << "'.";

  return true;
}

void VppConfigManager::doSrTunnelConfig (VppClient &vppClient,
                                         const folly::dynamic &tunnelConfig,
                                         Srv6TunnelMap &currSrPolicies)
{
  if (tunnelConfig.count ("tunnelParams") == 0)
    {
      LOG (ERROR) << "No tunnel parameters. "
                  << folly::toPrettyJson (tunnelConfig);
      return;
    }
  if (tunnelConfig["tunnelParams"].count ("vlanId") == 0)
    {
      LOG (ERROR) << "No VLAN ID. "
                  << folly::toPrettyJson (tunnelConfig["tunnelParams"]);
      return;
    }

  std::string dstIP = tunnelConfig["dstIp"].asString ();
  std::string srcIface = tunnelConfig["localInterface"].asString ();
  bool enabled = tunnelConfig["enabled"].asBool ();
  int vlanId = tunnelConfig["tunnelParams"]["vlanId"].asInt ();

  std::vector<std::string> globalV6Addrs =
      NetUtils::getAllIfIPv6Addresses (FLAGS_loopback, true);
  if (globalV6Addrs.empty ())
    {
      LOG (ERROR)
          << "No global IPv6 address found on Linux loopback interface '"
          << FLAGS_loopback << "'.";
      return;
    }
  std::string globalV6 = globalV6Addrs[0];
  std::string vppSourceAddr = NetUtils::transformIPv6 (
      globalV6, "0001", FLAGS_sr_encap_source_final_hextet);
  // We will use VLANs to generate the final hextet of SRv6 addresses
  // as a POP node can have multiple SRv6 tunnels (per VLAN) and the
  // tunnel endpoint addresses should be known to both ends of the tunnel.
  // Note: these would be global addresses which are used for internal routing
  // within the TG network.
  std::string encapFinalHextet =
      std::to_string (std::stoul (FLAGS_sr_l2_encap_base_hextet) + vlanId);
  std::string decapFinalHextet =
      std::to_string (std::stoul (FLAGS_sr_l2_decap_base_hextet) + vlanId);
  std::string localL2EncapAddr =
      NetUtils::transformIPv6 (globalV6, "0001", encapFinalHextet);
  std::string localL2DecapAddr =
      NetUtils::transformIPv6 (globalV6, "0001", decapFinalHextet);
  std::string dstL2DecapAddr =
      NetUtils::transformIPv6 (dstIP, "0001", decapFinalHextet);

  auto currSrPolicy = currSrPolicies.find (vlanId);
  if (currSrPolicy != currSrPolicies.end ())
    {
      int currVlanId = vlanId;
      std::string currBsid = currSrPolicy->second.first;
      std::string currDstSid = currSrPolicy->second.second;

      // Remove policy from map, because this policy is being processed
      currSrPolicies.erase (currVlanId);
      // If the policy already exists in VPP, do nothing.
      if (enabled && currBsid == localL2EncapAddr &&
          currDstSid == dstL2DecapAddr)
        {
          LOG (INFO) << "Found matching SR policy in VPP with BSID '"
                     << localL2EncapAddr << "', next SID '" << dstL2DecapAddr
                     << "', and VLAN ID '" << vlanId << "'.";
          return;
        }

      std::string currIfName = vppClient.getSrSteeringPolicyIfName (currBsid);
      if (currIfName.empty ())
        {
          LOG (ERROR) << "Cannot find the SRv6 steering policy in VPP that "
                         "has BSID as '"
                      << currBsid << "'.";
        }

      std::string currLocalSid = vppClient.getSrLocalSid (currIfName);
      if (currLocalSid.empty ())
        {
          LOG (ERROR) << "Cannot find the local SID in VPP that is associated "
                         "with interface '"
                      << currIfName << "'.";
        }

      vppClient.deleteSrPolicy (currBsid);
      vppClient.addDelSrSteeringPolicy (currIfName, currBsid, false);
      vppClient.addDelLocalSid (currLocalSid, currIfName, false);
      vppClient.deleteSubInterface (currIfName);

      LOG (INFO) << "Deleted a SR policy with BSID '" << currBsid
                 << "', next SID '" << currDstSid << "', and VLAN ID '"
                 << currVlanId << "'.";
    }

  if (!enabled)
    {
      return;
    }

  // Set the encapsulation source
  if (!vppClient.setSrEncapsulationSource (vppSourceAddr))
    {
      LOG (ERROR) << "Failed to set the SRv6 encapsulation source address to '"
                  << vppSourceAddr << "'.";
      return;
    }

  // Next SRv6 segment (SID) is the decap addr on the destination node.
  std::vector<std::string> sids;
  sids.push_back (dstL2DecapAddr);
  VppClient::SrPolicyConfig srPolicyConfig;
  srPolicyConfig.is_encap = 1;
  if (!vppClient.addSrPolicy (localL2EncapAddr, sids, srPolicyConfig))
    {
      LOG (ERROR) << "Failed to add SRv6 policy with BSID '"
                  << localL2EncapAddr << "' and SID '" << dstL2DecapAddr
                  << "'.";
      return;
    }
  LOG (INFO) << "Added SRv6 policy with BSID '" << localL2EncapAddr
             << "' and SID '" << dstL2DecapAddr << "'.";

  // Create sub-interace for the tunnel and only traffic tagged with
  // this VLAN ID will be tunneled via this SRv6 policy.
  std::string subIntf = vppClient.createSubInterface (srcIface, vlanId);
  if (subIntf.empty ())
    {
      LOG (ERROR) << "Could not create the sub-interface '" << vlanId
                  << "' on '" << srcIface << "' interface.";

      vppClient.deleteSrPolicy (localL2EncapAddr);
      return;
    }
  LOG (INFO) << "Created the sub-interface '" << vlanId << "' on '" << srcIface
             << "' interface.";

  // Steer L2 traffic on this VLAN/subIntf to this SR policy.
  if (!vppClient.addDelSrSteeringPolicy (subIntf, localL2EncapAddr, true))
    {
      LOG (ERROR)
          << "Failed to configure steering of L2 traffic on interface '"
          << subIntf << "' via BSID '" << localL2EncapAddr << "'.";

      vppClient.deleteSrPolicy (localL2EncapAddr);
      vppClient.deleteSubInterface (subIntf);
      return;
    }
  LOG (INFO) << "Added SRv6 steering policy on interface '" << subIntf
             << "' via BSID '" << localL2EncapAddr << "'.";

  // Add local SID for SRv6 decap function of the reverse L2 tunnel.
  if (!vppClient.addDelLocalSid (localL2DecapAddr, subIntf, true))
    {
      LOG (ERROR) << "Failed to add L2 local SID '" << localL2DecapAddr
                  << "' on interface '" << subIntf << "'.";

      vppClient.deleteSrPolicy (localL2EncapAddr);
      vppClient.addDelSrSteeringPolicy (subIntf, localL2EncapAddr, false);
      vppClient.deleteSubInterface (subIntf);
      return;
    }
  LOG (INFO) << "Added L2 local SID '" << localL2DecapAddr
             << "' for SRv6 decap on interface '" << subIntf << "'.";
}

void VppConfigManager::doQosMapping (VppClient &vppClient,
                                     const folly::dynamic &qos)
{
  std::string ifacePrefix = kHqosIfPrefix;
  auto map = vppClient.ifacePrefixToVppIndex (ifacePrefix);
  for (const auto &dscpEntry : qos["dscpEntries"].items ())
    {
      u32 dscp = (u32)~0;
      auto maybeDscp = folly::tryTo<u32> (dscpEntry.first.asString ());
      if (maybeDscp.hasValue ())
        {
          dscp = maybeDscp.value ();
        }
      try
        {
          for (const auto &kv : map)
            {
              setTctblEntry (vppClient, kv.second, dscp, dscpEntry.second);
            }
        }
      catch (const std::exception &e)
        {
          LOG (ERROR) << "Invalid DSCP entry " << dscpEntry.first << ": "
                      << " " << dscpEntry.second << " " << e.what ()
                      << " skipping...";
        }
    }
}

void VppConfigManager::setTctblEntry (VppClient &vppClient,
                                      const u32 sw_if_index, const u32 dscp,
                                      const folly::dynamic dscpParams)
{
  u32 tc = (u32)~0, queue = (u32)~0, color = (u32)~0;
  std::string colorStr;
  for (const auto &kv : dscpParams.items ())
    {
      // Parse attributes
      if (kv.first.asString () == "tc")
        {
          tc = kv.second.asInt ();
        }
      else if (kv.first.asString () == "queue")
        {
          queue = kv.second.asInt ();
        }
      else if (kv.first.asString () == "color")
        {
          colorStr = kv.second.asString ();
          std::transform (colorStr.begin (), colorStr.end (),
                          colorStr.begin (), ::toupper);
        }
      else
        {
          LOG (WARNING) << "Unrecognized key "
                        << folly::sformat ("\"{}\"", kv.first)
                        << " found in QoS classes. Ignoring";
        }
    }

  // Verify attributes
  if (dscp >= kVppTctblSize)
    {
      throw std::invalid_argument (folly::sformat (
          "Invalid DSCP. DSCP must be 0 - {}", kVppTctblSize - 1));
    }
  if (tc >= kVppNumTcClasses)
    {
      throw std::invalid_argument (folly::sformat (
          "Invalid TC. TC must be 0 - {}", kVppNumTcClasses - 1));
    }
  if (queue >= kVppNumQueues)
    {
      throw std::invalid_argument (folly::sformat (
          "Invalid queue. Queue must be 0 - {}", kVppNumQueues - 1));
    }
  if (colorStr != "R" && colorStr != "G" && colorStr != "Y")
    {
      throw std::invalid_argument (
          "Invalid TC Color. Color must be 'R', 'G', or 'Y'");
    }
  else
    {
      color = (colorStr == "R" ? 3 : 0) + (colorStr == "G" ? 1 : 0) +
              (colorStr == "Y" ? 2 : 0);
    }

  // Set tctbl
  vppClient.hqosTctbl (sw_if_index, dscp, tc, queue, color);
}

void VppConfigManager::doCpePolicerConfig (VppClient &vppClient,
                                           const std::string &interface,
                                           const folly::dynamic &policers,
                                           bool isAdd)

{
  u32 interfaceIndex = (u32)~0;
  auto ifaceMap = vppClient.getIfaceToVppIndexMap ();
  auto ifaceMapIter = ifaceMap.find (interface);
  if (ifaceMapIter == ifaceMap.end ())
    {
      LOG (ERROR) << "Invalid interface name for CPE interface policers "
                  << interface << ": " << policers << " "
                  << " skipping...";
      return;
    }
  interfaceIndex = ifaceMapIter->second;

  // The policed interface must be stopped before deleting any old config,
  // otherwise VPP will crash if policed traffic is being passed while the
  // config is deleted.
  bool ifaceStopped = false;
  VppClient::ClassifierTableConfig tableConfig;
  u32 tableId = ~0;

  vppClient.getClassifierByIndex (interfaceIndex, tableId);
  if (tableId != ~0)
    {
      if (!ifaceStopped)
        {
          ifaceStopped = true;
          VLOG (1) << "Stopping interface " << interface
                   << " before removing old policer config";
          vppClient.setInterfaceFlags (interface, false);
        }
      tableConfig.tableIndex = tableId;
      tableConfig.isAdd = 0; // Delete old table
      vppClient.addDelClassifierTable (tableConfig);
    }
  else
    {
      LOG (ERROR) << "Interface does not have classifier table";
    }

  if (isAdd)
    {

      // Create table
      tableConfig.isAdd = 1; // Add
      // Skip 14 bytes for Ethernet header, 4 bits for IPv6 version header;
      // match on full DSCP value contained in next 6 bits
      std::vector<u8> maskVector = {0, 0, 0, 0, 0, 0, 0,    0,
                                    0, 0, 0, 0, 0, 0, 0x0F, 0xC0};
      tableConfig.mask = maskVector.data ();
      vppClient.addDelClassifierTable (tableConfig);
    }
  // Create a policer for every TC. This will allow only packets without DSCPs
  // matching our known TCs to default to TC3.
  for (u8 tc = 0; tc <= kMaxTrafficClass; tc++)
    {
      VppClient::PolicerConfig policerConfig;
      if (isAdd)
        {
          auto policer = policers.find (std::to_string (tc));
          if (policer == policers.items ().end ())
            {
              // If not explicitly configured, create a policer for this TC
              // that does not restrict any traffic
              policerConfig.cir = (u32)~0;
              policerConfig.cb = (u64)~0;
              policerConfig.eir = (u32)~0;
              policerConfig.eb = (u64)~0;
            }
          else
            {
              // Parse attributes
              auto policer_map = policer->second;
              auto rateIter = policer_map.find ("cir");
              if (rateIter != policer_map.items ().end ())
                {
                  policerConfig.cir = rateIter->second.asInt ();
                  policerConfig.cb =
                      policerConfig.cir * 125; // cir in bytes for 1s
                }
              rateIter = policer_map.find ("eir");
              if (rateIter != policer_map.items ().end ())
                {
                  policerConfig.eir = rateIter->second.asInt ();
                  policerConfig.eb =
                      policerConfig.eir * 125; // eir in bytes for 1s
                }
              if (policerConfig.cir == 0)
                {
                  LOG (ERROR) << "CIR is 0 for policer " << policer->first
                              << ": " << policer_map << ", skipping...";
                  continue;
                }
              if (policerConfig.eir == 0)
                {
                  policerConfig.type = SSE2_QOS_POLICER_TYPE_API_1R2C;
                }
              if (policerConfig.type == SSE2_QOS_POLICER_TYPE_API_2R3C_RFC_2698
                  && policerConfig.cir > policerConfig.eir)
                {
                  LOG (ERROR) << "CIR " << policerConfig.cir
                              << " greater than EIR " << policerConfig.eir
                              << " for policer " << policer->first << ": "
                              << policer_map << ", skipping...";
                  continue;
                }
            }
          policerConfig.conformDscp = kAFxyConformDscp.at (tc);
          policerConfig.exceedDscp = kAFxyExcessDscp.at (tc);
        }
      // Create policers
      VppClient::PolicerConfig oldPolicerConfig;
      std::string policerName = interface + "_" + std::to_string (tc);
      memcpy (policerConfig.name, policerName.c_str (),
              sizeof (policerConfig.name));
      u32 policerIndex;
      if (vppClient.getPolicer (policerConfig.name, oldPolicerConfig))
        {
          if (!ifaceStopped)
            {
              ifaceStopped = true;
              VLOG (1) << "Stopping interface "
                       << interface << " before removing old policer config";
              vppClient.setInterfaceFlags (interface, false);
            }
          VLOG (1) << "Remove old policer " << policerConfig.name;
          policerConfig.isAdd = 0; // Delete
          vppClient.addDelPolicer (policerConfig, policerIndex);

          if (!isAdd)
            {
              continue;
            }
        }
      if (isAdd)
        {
          policerConfig.isAdd = 1; // Add
          vppClient.addDelPolicer (policerConfig, policerIndex);

          // Create session matching to AFx1: upstream traffic is expected to
          // be marked with DSCP corresponding to a traffic class with low drop
          // preference (green)
          VppClient::ClassifierSessionConfig sessionConfig;
          std::vector<u8> matchVector = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
              0, 0, kAFxyMatch.at (tc), 0x80};
          sessionConfig.match = matchVector.data ();
          sessionConfig.hitNextIndex = policerIndex;
          sessionConfig.tableIndex = tableConfig.tableIndex;
          vppClient.addDelClassifierSession (sessionConfig);
        }
    }

  if (isAdd)
    {
      // Map table to interface
      vppClient.setClassifierTableNetif (true, interfaceIndex,
                                         tableConfig.tableIndex);
    }
  if (ifaceStopped)
    {
      VLOG (1) << "Restarting interface " << interface;
      vppClient.setInterfaceFlags (interface, true /* up */);
    }
}

void VppConfigManager::doQosConfig (VppClient &vppClient)
{
  if (qosConfig_.empty ())
    {
      LOG (ERROR) << "qosConfig is empty. Skipping QoS config...";
      return;
    }

  if (qosConfig_.count ("dscpEntries"))
    {
      LOG (INFO) << "Setting HQoS Table";
      doQosMapping (vppClient, qosConfig_);
    }
}

} // namespace vpp
} // namespace terragraph
} // namespace facebook
