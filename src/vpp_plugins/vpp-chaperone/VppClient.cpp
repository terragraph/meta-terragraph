/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "VppClient.h"

#include <folly/Range.h>
#include <folly/MacAddress.h>
#include <glog/logging.h>
#include <vapi/dhcp.api.vapi.hpp>
#include <vapi/dpdk.api.vapi.hpp>
#include <vapi/interface.api.vapi.hpp>
#include <vapi/ip.api.vapi.hpp>
#include <vapi/ip6_nd.api.vapi.hpp>
#include <vapi/nat64.api.vapi.hpp>
#include <vapi/policer.api.vapi.hpp>
#include <vapi/sr.api.vapi.hpp>
#include <vapi/tap.api.vapi.hpp>
#include <vapi/vpe.api.vapi.hpp>
#include <vapi/vxlan.api.vapi.hpp>

#include "NetUtils.h"

#include <unordered_set>

using namespace vapi;

namespace
{
const u8 kIpv4AddrLen{4};
const u8 kIpv6AddrLen{16};
const u8 kPolicerNameLen{64};
} // namespace

namespace facebook
{
namespace terragraph
{
namespace vpp
{

VppClient::VppClient (int maxOutstandingRequests, int responseQueueSize)
    : maxOutstandingRequests_{maxOutstandingRequests}, responseQueueSize_{
                                                           responseQueueSize}
{
}

VppClient::~VppClient ()
{
  if (connected_)
    {
      connection_.disconnect ();
      connected_ = false;
    }
}

bool VppClient::connect ()
{
  if (connected_)
    return true;

  vapi_error_e rv = connection_.connect (
      "vpp-chaperone", nullptr, maxOutstandingRequests_, responseQueueSize_);
  if (rv != VAPI_OK)
    {
      return false;
    }

  connected_ = true;
  return true;
}

template <class T>
bool VppClient::executeAndWait (T &req, const std::string &apiName)
{
  if (!connected_)
    return false;

  VLOG (3) << "Querying VAPI: " << apiName;
  vapi_error_e rv = req.execute ();
  if (rv != VAPI_OK)
    {
      LOG (ERROR) << apiName << " execution failed (error code " << rv << ")";
      return false;
    }

  do
    {
      rv = connection_.wait_for_response (req);
    }
  while (rv == VAPI_EAGAIN);
  if (rv != VAPI_OK)
    {
      LOG (ERROR) << apiName << " response failed (error code " << rv << ")";
      return false;
    }

  VLOG (3) << apiName << " succeeded.";
  return true;
}

std::unordered_map<std::string, u32> VppClient::getIfaceToVppIndexMap ()
{
  std::unordered_map<std::string, u32> map;
  Sw_interface_dump req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  if (executeAndWait (req, "sw_interface_dump"))
    {
      auto &rs = req.get_result_set ();
      for (auto &r : rs)
        {
          auto &rp = r.get_payload ();
          map[std::string ((const char *)rp.interface_name)] = rp.sw_if_index;
        }
    }
  return map;
}

std::unordered_map<std::string, u32>
VppClient::ifacePrefixToVppIndex (const std::string &ifPrefix)
{
  auto ifaceMap = getIfaceToVppIndexMap ();
  std::unordered_map<std::string, u32> matches;
  for (const auto &kv : ifaceMap)
    {
      if (kv.first.rfind (ifPrefix, 0) != std::string::npos)
        {
          matches.insert (kv);
        }
    }
  return matches;
}

u32 VppClient::ifaceToVppIndex (const std::string &ifName)
{
  auto ifaceMap = getIfaceToVppIndexMap ();
  auto iter = ifaceMap.find (ifName);
  if (iter == ifaceMap.end ())
    {
      VLOG (3) << "No VPP interface found with name: " << ifName;
      return ~0;
    }
  return iter->second;
}

std::string VppClient::vppIndexToIface (u32 index)
{
  auto ifaceMap = getIfaceToVppIndexMap ();
  for (const auto &kv : ifaceMap)
    {
      if (kv.second == index)
        return kv.first;
    }
  VLOG (3) << "No VPP interface found with index: " << std::to_string (index);
  return "";
}

std::string VppClient::getInterfaceAddr (const std::string &ifName,
                                         bool isIPv6)
{
  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return {};
    }

  Ip_address_dump req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;
  p.is_ipv6 = isIPv6;

  if (executeAndWait (req, "ip_address_dump"))
    {
      auto &rs = req.get_result_set ();
      for (auto &r : rs)
        {
          auto &rp = r.get_payload ();
          return isIPv6 ? NetUtils::ip6AddressFromBinary (
                              rp.prefix.address.un.ip6)
                              .str ()
                        : NetUtils::ip4AddressFromBinary (
                              rp.prefix.address.un.ip4)
                              .str ();
        }
    }
  return "";
}

std::string VppClient::getSwInterfacePrefix (const std::string &ifName)
{
  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return "";
    }

  Ip_address_dump req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;
  p.is_ipv6 = true;
  if (executeAndWait (req, "ip_address_dump"))
    {
      auto &rs = req.get_result_set ();
      for (auto &r : rs)
        {
          auto &rp = r.get_payload ();
          std::string ip =
              NetUtils::ip6AddressFromBinary (rp.prefix.address.un.ip6).str ();
          return ip + "/" + std::to_string (rp.prefix.len);
        }
    }
  return "";
}

std::string VppClient::getSwInterfaceByPrefix (const std::string &prefix)
{
  folly::CIDRNetwork network =
      folly::IPAddress::createNetwork (prefix, -1, false /* mask */);

  // Loop over all interfaces...
  auto ifaceMap = getIfaceToVppIndexMap ();
  for (const auto &kv : ifaceMap)
    {
      // Dump IPs on interface
      Ip_address_dump req (connection_);
      auto &p = req.get_request ().get_payload ();
      memset (&p, 0, sizeof (p));
      p.sw_if_index = kv.second;
      p.is_ipv6 = true;
      if (executeAndWait (req, "ip_address_dump"))
        {
          auto &rs = req.get_result_set ();
          for (auto &r : rs)
            {
              auto &rp = r.get_payload ();
              folly::IPAddress ip =
                  NetUtils::ip6AddressFromBinary (rp.prefix.address.un.ip6);
              if (ip == network.first && rp.prefix.len == network.second)
                return kv.first;
            }
        }
    }
  return "";
}

std::string VppClient::getSwInterfaceMacAddress (const std::string &ifName)
{
  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return "";
    }

  Sw_interface_get_mac_address req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;

  if (executeAndWait (req, "sw_interface_get_mac_address"))
    {
      auto &rs = req.get_response ();
      auto &rp = rs.get_payload ();
      if (rp.retval != 0)
        return "";

      folly::MacAddress mac = folly::MacAddress::fromBinary (
          folly::ByteRange (&rp.mac_address[0], folly::MacAddress::SIZE));
      return mac.toString ();
    }

  return "";
}

bool VppClient::setSwInterfaceMacAddress (const std::string &ifName,
                                          const std::string &macAddr)
{
  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return false;
    }

  Sw_interface_set_mac_address req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;
  if (!macAddr.empty ())
    {
      folly::MacAddress mac (macAddr);
      memcpy (p.mac_address, mac.bytes (), folly::MacAddress::SIZE);
    }

  if (!executeAndWait (req, "sw_interface_set_mac_address"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "sw_interface_set_mac_address returned error: "
                  << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::setInterfaceFlags (const std::string &ifName, bool up)
{
  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return false;
    }

  Sw_interface_set_flags req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;
  p.flags = up ? IF_STATUS_API_FLAG_ADMIN_UP : 0;
  if (!executeAndWait (req, "sw_interface_set_flags"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "sw_interface_set_flags returned error: " << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::enableDisableIp6Interface (const std::string &ifName,
                                           bool enable)
{
  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return false;
    }

  Sw_interface_ip6_enable_disable req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;
  p.enable = enable;
  if (!executeAndWait (req, "sw_interface_ip6_enable_disable"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  /* TODO: remove calls that don't result in change? */
  if (rp.retval == -81 /* VNET_API_ERROR_VALUE_EXIST */)
    {
      VLOG (3) << "Interface " << ifName << " ip6 already enabled";
      return true;
    }
  else if (rp.retval == -62 /* VNET_API_ERROR_IP6_NOT_ENABLED */)
    {
      VLOG (3) << "Interface " << ifName << " ip6 already disabled";
      return true;
    }
  if (rp.retval != 0)
    {
      LOG (ERROR) << "sw_interface_ip6_enable_disable returned error: "
                  << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::addDelInterfaceAddress (const std::string &ifName,
                                        const std::string &prefix, bool add)
{
  folly::CIDRNetwork network =
      folly::IPAddress::createNetwork (prefix, -1, false /* mask */);

  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return false;
    }

  Sw_interface_add_del_address req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;
  p.is_add = add;
  p.del_all = false;
  p.prefix.len = network.second;

  if (network.first.isV6 ())
    {
      p.prefix.address.af = ADDRESS_IP6;
      memcpy (p.prefix.address.un.ip6, network.first.bytes (), kIpv6AddrLen);
    }
  else
    {
      p.prefix.address.af = ADDRESS_IP4;
      memcpy (p.prefix.address.un.ip4, network.first.bytes (), kIpv4AddrLen);
    }

  if (!executeAndWait (req, "sw_interface_add_del_address"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "sw_interface_add_del_address returned error: "
                  << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::addDelRoute (const std::string &dstPrefix,
                             const std::string &nextHopAddr,
                             const std::string &nextHopIfName, bool add)
{
  auto const ret =
      folly::IPAddress::tryCreateNetwork (dstPrefix, -1, false /* mask */);

  folly::CIDRNetwork dstNetwork;

  if (!ret.hasValue ())
    {
      LOG (ERROR) << "cannot add/del route for " << dstPrefix
                  << " parse error:";
      return false;
    }
  else
    {
      dstNetwork = ret.value ();
    }

  u32 nextHopSwIfIndex;

  if (!nextHopIfName.empty () && nextHopIfName != kVppRouteTypeDrop)
    {
      nextHopSwIfIndex = ifaceToVppIndex (nextHopIfName);
      if (nextHopSwIfIndex == (u32)~0)
        {
          LOG (ERROR) << "Interface '" << nextHopIfName
                      << "' not found in VPP";
          return false;
        }
    }
  else
    nextHopSwIfIndex = ~0;

  Ip_route_add_del req (connection_, 1 /* route_paths_array_size */);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.is_multipath = true;
  p.is_add = add;
  p.route.n_paths = 1;
  p.route.prefix.len = dstNetwork.second;
  p.route.paths[0].sw_if_index = nextHopSwIfIndex;
  if (dstNetwork.first.isV6 ())
    {
      p.route.prefix.address.af = ADDRESS_IP6;
      memcpy (p.route.prefix.address.un.ip6, dstNetwork.first.bytes (),
              kIpv6AddrLen);

      p.route.paths[0].proto = FIB_API_PATH_NH_PROTO_IP6;

      // drop route with no next hop IP address, we don't want multipath
      if (nextHopIfName == kVppRouteTypeDrop)
        {
          p.route.paths[0].type = FIB_API_PATH_TYPE_DROP;
          p.is_multipath = false;
        }
      else
        {
          memcpy (p.route.paths[0].nh.address.ip6,
                  folly::IPAddress (nextHopAddr).bytes (), kIpv6AddrLen);
        }
    }
  else if (dstNetwork.first.isV4 ())
    {
      p.route.prefix.address.af = ADDRESS_IP4;
      memcpy (p.route.prefix.address.un.ip4, dstNetwork.first.bytes (),
              kIpv4AddrLen);

      p.route.paths[0].proto = FIB_API_PATH_NH_PROTO_IP4;
      memcpy (p.route.paths[0].nh.address.ip4,
              folly::IPAddress (nextHopAddr).bytes (), kIpv4AddrLen);
    }
  else
    {
      // not a v4 or v6 route
      LOG (ERROR) << " prefix " << dstNetwork.first
                  << " is not a v4/v6 prefix";
      return false;
    }

  if (executeAndWait (req, "ip_route_add_del"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "ip_route_add_del returned error: " << rp.retval;
      return false;
    }
  return true;
}

std::vector<std::string>
VppClient::getFibStaleRoutes (std::vector<std::string> &curPrefixes,
                              const std::string &nextHopAddr,
                              const std::string &nextHopIfName)
{
  // Create a set of CIDR-format prefixes from the Prefix string
  // for fast lookup
  std::unordered_set<folly::CIDRNetwork> dstNetworks;

  for (const auto &curPrefix : curPrefixes)
    {
      dstNetworks.insert (
          folly::IPAddress::createNetwork (curPrefix, -1, false /* mask */));
    }

  folly::IPAddress nextHopIp = folly::IPAddress (nextHopAddr);
  std::vector<std::string> stalePrefixes;

  u32 nextHopSwIfIndex = ifaceToVppIndex (nextHopIfName);
  if (nextHopSwIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << nextHopIfName << "' not found in VPP";
      return stalePrefixes;
    }

  Ip_route_dump req (connection_);
  auto &p = req.get_request ().get_payload ();
  // We only use a single table, table 0
  p.table.table_id = 0;
  p.table.is_ip6 = true;

  if (!executeAndWait (req, "ip_route_dump"))
    return stalePrefixes;

  auto &rs = req.get_result_set ();
  for (auto &r : rs)
    {
      auto &rp = r.get_payload ();
      folly::IPAddress routeDstIp =
          NetUtils::ip6AddressFromBinary (rp.route.prefix.address.un.ip6);

      auto fibNetwork = std::make_pair (routeDstIp, rp.route.prefix.len);

      if (!dstNetworks.count (fibNetwork))
        {
          // Found a prefix that is absent in linux loopback interface.
          // If the route and interface matches, it is a stale route using
          // an old prefix. Collect all stale routes for cleanup later.
          for (unsigned i = 0; i < rp.route.n_paths; ++i)
            {
              vapi_type_fib_path *path = &rp.route.paths[i];
              folly::IPAddress nhIp =
                  NetUtils::ip6AddressFromBinary (path->nh.address.ip6);
              if (nhIp == nextHopIp && path->sw_if_index == nextHopSwIfIndex)
                {
                  stalePrefixes.push_back (
                      folly::IPAddress::networkToString (fibNetwork));
                }
            }
        }
    }

  return stalePrefixes;
}

bool VppClient::hasRoute (const std::string &dstPrefix,
                          const std::string &nextHopAddr,
                          const std::string &nextHopIfName,
                          bool nextHopRequired)
{
  folly::CIDRNetwork dstNetwork =
      folly::IPAddress::createNetwork (dstPrefix, -1, false /* mask */);
  folly::IPAddress nextHopIp = folly::IPAddress (nextHopAddr);
  u32 nextHopSwIfIndex = 0;

  if (nextHopRequired)
    {
      nextHopSwIfIndex = ifaceToVppIndex (nextHopIfName);
      if (nextHopSwIfIndex == (u32)~0)
        {
          LOG (ERROR) << "Interface '" << nextHopIfName
                      << "' not found in VPP";
          return false;
        }
    }

  Ip_route_lookup req (connection_);
  auto &p = req.get_request ().get_payload ();
  // We only use a single table, table 0
  p.table_id = 0;
  p.exact = 1;
  memcpy (p.prefix.address.un.ip6, dstNetwork.first.bytes (), kIpv6AddrLen);
  p.prefix.address.af = ADDRESS_IP6;
  p.prefix.len = dstNetwork.second;
  if (!executeAndWait (req, "ip_route_lookup"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval == -6 /* VNET_API_ERROR_NO_SUCH_ENTRY */)
    return false;
  else if (rp.retval != 0)
    {
      LOG (ERROR) << "ip_route_lookup returned error: " << rp.retval;
      return false;
    }

  if (rp.route.n_paths > 0 && !nextHopRequired)
    return true;

  for (unsigned i = 0; i < rp.route.n_paths; ++i)
    {
      vapi_type_fib_path *path = &rp.route.paths[i];
      folly::IPAddress nhIp =
          NetUtils::ip6AddressFromBinary (path->nh.address.ip6);
      if (nhIp == nextHopIp && path->sw_if_index == nextHopSwIfIndex)
        return true;
    }
  return false;
}

bool VppClient::setInterfaceLinkLocalAddress (const std::string &ifName,
                                              const std::string &addr)
{
  folly::CIDRNetwork network =
      folly::IPAddress::createNetwork (addr, -1, false /* mask */);

  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP.";
      return false;
    }

  Sw_interface_ip6_set_link_local_address req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;
  memcpy (p.ip, network.first.bytes (), kIpv6AddrLen);
  if (!executeAndWait (req, "sw_interface_ip6_set_link_local_address"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "sw_interface_ip6_set_link_local_address returned error: "
                  << rp.retval;
      return false;
    }
  return true;
}

std::string VppClient::createLoopbackInterface (const std::string &macAddr)
{
  Create_loopback req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  if (!macAddr.empty ())
    {
      folly::MacAddress mac (macAddr);
      memcpy (p.mac_address, mac.bytes (), folly::MacAddress::SIZE);
    }
  if (!executeAndWait (req, "create_loopback"))
    return "";

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "create_loopback returned error: " << rp.retval;
      return "";
    }
  return vppIndexToIface (rp.sw_if_index);
}

bool VppClient::delLoopbackInterface (const std::string &ifName)
{
  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return false;
    }

  Delete_loopback req (connection_);
  auto &p = req.get_request ().get_payload ();
  p.sw_if_index = swIfIndex;
  if (!executeAndWait (req, "delete_loopback"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "delete_loopback returned error: " << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::addDelBridgeDomain (
    u32 bdId, const VppClient::BridgeDomainConfig &bdConfig, bool add)
{
  Bridge_domain_add_del req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.bd_id = bdId;
  if (add)
    {
      p.flood = bdConfig.flood;
      p.uu_flood = bdConfig.uu_flood;
      p.forward = bdConfig.forward;
      p.learn = bdConfig.learn;
    }
  else
    {
      p.flood = 0;
      p.uu_flood = 0;
      p.forward = 0;
      p.learn = 0;
    }
  p.arp_term = bdConfig.arp_term;
  p.mac_age = bdConfig.mac_age;
  p.is_add = add;
  if (!executeAndWait (req, "bridge_domain_add_del"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "bridge_domain_add_del returned error: " << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::hasBridgeDomain (u32 bdId)
{
  Bridge_domain_dump req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.bd_id = bdId;
  // filter by sw_if_index is unimplemented, but field must be set to ~0
  p.sw_if_index = (u32)~0;
  if (executeAndWait (req, "bridge_domain_dump"))
    {
      auto &rs = req.get_result_set ();
      if (rs.size () > 0)
        {
          for (const auto &r : rs)
            {
              auto &rp = r.get_payload ();
              VLOG (1) << "Bridge domain id " << bdId << " with # interfaces "
                       << rp.n_sw_ifs << " and BVI SW index "
                       << rp.bvi_sw_if_index;
            }
          return true;
        }
    }
  return false;
}

bool VppClient::setInterfaceL2Bridge (u32 bdId, const std::string &ifName,
                                      u8 shg, vapi_enum_l2_port_type portType)
{
  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return false;
    }

  Sw_interface_set_l2_bridge req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.rx_sw_if_index = swIfIndex;
  p.bd_id = bdId;
  p.port_type = portType;
  p.shg = shg;
  p.enable = true;
  if (!executeAndWait (req, "sw_interface_set_l2_bridge"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "sw_interface_set_l2_bridge returned error: "
                  << rp.retval;
      return false;
    }
  return true;
}

std::string VppClient::getTapInterface (const std::string &devName)
{
  Sw_interface_tap_dump req (connection_);
  if (executeAndWait (req, "sw_interface_tap_dump"))
    {
      auto &rs = req.get_result_set ();
      for (auto &r : rs)
        {
          auto &rp = r.get_payload ();
          std::string name ((const char *)rp.dev_name);
          if (name == devName)
            return vppIndexToIface (rp.sw_if_index);
        }
    }
  return "";
}

std::string VppClient::connectTap (const std::string &ifName,
                                   const std::string &macAddr)
{
  Tap_connect req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));

  if (ifName.length () >= sizeof (p.tap_name) /* 64 */)
    {
      LOG (ERROR) << "Tap interface name '" << ifName
                  << "' is too long (max length " << (sizeof (p.tap_name) - 1)
                  << ")";
      return "";
    }

  if (!macAddr.empty ())
    {
      p.use_random_mac = false;
      folly::MacAddress mac (macAddr);
      memcpy (p.mac_address, mac.bytes (), folly::MacAddress::SIZE);
      memcpy (p.kernel_mac_address, mac.bytes (), folly::MacAddress::SIZE);
    }
  else
    {
      p.use_random_mac = true;
    }
  memcpy (p.tap_name, ifName.c_str (), ifName.length () + 1);

  if (!executeAndWait (req, "tap_connect"))
    return "";

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "tap_connect returned error: " << rp.retval;
      return "";
    }
  return vppIndexToIface (rp.sw_if_index);
}

bool VppClient::delTap (const std::string &ifName)
{
  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return false;
    }

  Tap_delete req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;
  if (!executeAndWait (req, "tap_delete"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "tap_delete returned error: " << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::setInterfaceRaConfig (const std::string &ifName,
                                      const VppClient::RaConfig &raConfig)
{
  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return false;
    }

  Sw_interface_ip6nd_ra_config req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;
  p.suppress = raConfig.suppress;
  p.managed = raConfig.managed;
  p.other = raConfig.other;
  p.ll_option = raConfig.ll_option;
  p.send_unicast = raConfig.send_unicast;
  p.cease = raConfig.cease;
  p.is_no = raConfig.is_no;
  p.default_router = raConfig.default_router;
  p.max_interval = raConfig.max_interval;
  p.min_interval = raConfig.min_interval;
  p.lifetime = raConfig.lifetime;
  p.initial_count = raConfig.initial_count;
  p.initial_interval = raConfig.initial_interval;
  if (!executeAndWait (req, "sw_interface_ip6nd_ra_config"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "sw_interface_ip6nd_ra_config returned error: "
                  << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::setInterfaceRaPrefixConfig (
    const std::string &ifName, const std::string &prefix,
    const VppClient::RaPrefixConfig &raPrefixConfig)
{
  folly::CIDRNetwork network =
      folly::IPAddress::createNetwork (prefix, -1, false /* mask */);

  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return false;
    }

  Sw_interface_ip6nd_ra_prefix req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;
  memcpy (p.prefix.address.un.ip6, network.first.bytes (), kIpv6AddrLen);
  p.prefix.address.af = ADDRESS_IP6;
  p.prefix.len = network.second;
  p.use_default = raPrefixConfig.use_default;
  p.no_advertise = raPrefixConfig.no_advertise;
  p.off_link = raPrefixConfig.off_link;
  p.no_autoconfig = raPrefixConfig.no_autoconfig;
  p.no_onlink = raPrefixConfig.no_onlink;
  p.is_no = raPrefixConfig.is_no;
  p.val_lifetime = raPrefixConfig.val_lifetime;
  p.pref_lifetime = raPrefixConfig.pref_lifetime;
  if (!executeAndWait (req, "sw_interface_ip6nd_ra_prefix"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "sw_interface_ip6nd_ra_prefix returned error: "
                  << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::setDhcpv6ProxyConfig (const std::string &dhcpServerIp,
                                      const std::string &relayIp,
                                      bool remoteIdOption,
                                      bool subscriberIdOption,
                                      const u32 enterpriseId,
                                      const std::string &remoteIdMac, bool add)
{
  if (dhcpServerIp.empty () || relayIp.empty ())
    {
      LOG (ERROR) << "DHCPv6 Error: empty server or relay IPv6 prefixes";
      return false;
    }

  folly::CIDRNetwork dhcpServerNw =
      folly::IPAddress::createNetwork (dhcpServerIp, -1, false /* mask */);

  folly::CIDRNetwork relayNw =
      folly::IPAddress::createNetwork (relayIp, -1, false /* mask */);

  Dhcp_proxy_config req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.is_add = add;
  p.dhcp_server.af = ADDRESS_IP6;
  p.dhcp_src_address.af = ADDRESS_IP6;
  memcpy (p.dhcp_server.un.ip6, dhcpServerNw.first.bytes (), kIpv6AddrLen);
  memcpy (p.dhcp_src_address.un.ip6, relayNw.first.bytes (), kIpv6AddrLen);
  p.remote_id_option = remoteIdOption;
  p.subscriber_id_option = subscriberIdOption;
  p.enterprise_id = enterpriseId;

  if (!remoteIdMac.empty ())
    {
      folly::MacAddress mac (remoteIdMac);
      memcpy (p.remote_id_mac, mac.bytes (), folly::MacAddress::SIZE);
    }

  if (!executeAndWait (req, "dhcp_proxy_config"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "dhcp_proxy_config returned error: " << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::setSrEncapsulationSource (const std::string &addr)
{
  folly::IPAddress encapsSource (addr);

  Sr_set_encap_source req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  memcpy (p.encaps_source, encapsSource.bytes (), kIpv6AddrLen);
  if (!executeAndWait (req, "sr_set_encap_source"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "sr_set_encap_source returned error: " << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::addSrPolicy (const std::string &bsid,
                             const std::vector<std::string> &sids,
                             const SrPolicyConfig &srPolicyConfig)
{
  folly::IPAddress bsidAddr (bsid);

  Sr_policy_add req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  memcpy (p.bsid_addr, bsidAddr.bytes (), kIpv6AddrLen);
  p.weight = srPolicyConfig.weight; // seems unused... (VPP bug?)
  p.is_encap = srPolicyConfig.is_encap;
  p.is_spray = srPolicyConfig.is_spray;
  p.fib_table = srPolicyConfig.fib_table;
  p.sids.num_sids = sids.size ();
  p.sids.weight = srPolicyConfig.weight;
  for (size_t i = 0; i < sids.size (); i++)
    {
      folly::IPAddress sidAddr (sids[i]);
      memcpy (p.sids.sids[i], sidAddr.bytes (), kIpv6AddrLen);
    }
  if (!executeAndWait (req, "sr_policy_add"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "sr_policy_add returned error: " << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::deleteSrPolicy (const std::string &bsid)
{
  folly::IPAddress bsidAddr (bsid);

  Sr_policy_del req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));

  memcpy (p.bsid_addr, bsidAddr.bytes (), kIpv6AddrLen);

  if (!executeAndWait (req, "sr_policy_del"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "sr_policy_del returned error: " << rp.retval;
      return false;
    }
  return true;
}

Srv6TunnelMap VppClient::getSrPolicyDumps (const std::string &encapBaseHextet,
                                           const std::string &decapBaseHextet)
{
  Sr_policies_dump req (connection_);

  Srv6TunnelMap srPolicies;

  if (executeAndWait (req, "sr_policies_dump"))
    {
      auto &rs = req.get_result_set ();
      for (const auto &r : rs)
        {
          auto &rp = r.get_payload ();
          if (0 == rp.num_sid_lists)
            {
              continue;
            }

          int sidIdx = rp.sid_lists[0].num_sids - 1;
          if (sidIdx < 0)
            {
              LOG (ERROR) << "SRv6 Error: Invalid SID list size "
                          << (int)rp.sid_lists[0].num_sids;
              continue;
            }

          std::string encapAddr =
              NetUtils::ip6AddressFromBinary (rp.bsid).str ();
          std::string decapAddr =
              NetUtils::ip6AddressFromBinary (rp.sid_lists[0].sids[sidIdx])
                  .str ();
          // Parsing VLAN ID from the last 4 hex characters.
          // VLAN ID = Final Hextet - Base Hextet.
          int encapVlan =
              NetUtils::parseVlanFromSrv6IpAddr (encapAddr, encapBaseHextet);
          int decapVlan =
              NetUtils::parseVlanFromSrv6IpAddr (decapAddr, decapBaseHextet);

          if (encapVlan == -1 || decapVlan == -1 || encapVlan != decapVlan)
            {
              LOG (ERROR)
                  << "SRv6 Error: VLAN ID parsed from encap addr ("
                  << encapAddr << ", " << encapVlan
                  << ") is different from VLAN ID parsed from decap addr ("
                  << decapAddr << ", " << decapVlan << ").";
              continue;
            }

          srPolicies[encapVlan] = {encapAddr, decapAddr};
        }
    }

  return srPolicies;
}

std::string VppClient::getSrSteeringPolicyIfName (const std::string &bsid)
{
  folly::IPAddress bsidAddr (bsid);

  Sr_steering_pol_dump req (connection_);
  if (executeAndWait (req, "sr_steering_pol_dump"))
    {
      auto &rs = req.get_result_set ();
      for (auto &r : rs)
        {
          auto &rp = r.get_payload ();
          folly::IPAddress addr = NetUtils::ip6AddressFromBinary (rp.bsid);
          if (addr == bsidAddr)
            {
              return vppIndexToIface (rp.sw_if_index);
            }
        }
    }

  return "";
}

bool VppClient::addDelSrSteeringPolicy (const std::string &ifName,
                                        const std::string &bsid, bool add)
{
  folly::IPAddress bsidAddr (bsid);

  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return false;
    }

  Sr_steering_add_del req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.is_del = !add;
  memcpy (p.bsid_addr, bsidAddr.bytes (), kIpv6AddrLen);
  p.sr_policy_index = ~0;
  p.table_id = ~0;
  p.sw_if_index = swIfIndex;
  p.traffic_type = SR_STEER_API_L2;
  if (!executeAndWait (req, "sr_steering_add_del"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "sr_steering_add_del returned error: " << rp.retval;
      return false;
    }
  return true;
}

std::string VppClient::getSrLocalSid (const std::string &ifName)
{
  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return "";
    }

  Sr_localsids_dump req (connection_);
  if (executeAndWait (req, "sr_localsids_dump"))
    {
      auto &rs = req.get_result_set ();
      for (const auto &r : rs)
        {
          auto &rp = r.get_payload ();
          if (rp.xconnect_iface_or_vrf_table == swIfIndex)
            {
              return NetUtils::ip6AddressFromBinary (rp.addr).str ();
            }
        }
    }

  return "";
}

bool VppClient::addDelLocalSid (const std::string &localSid,
                                const std::string &ifName, bool add)
{
  folly::IPAddress localSidAddr (localSid);

  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return false;
    }

  Sr_localsid_add_del req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.is_del = !add;
  memcpy (p.localsid, localSidAddr.bytes (), kIpv6AddrLen);
  p.end_psp = 0;
  p.behavior = SR_BEHAVIOR_API_DX2;
  p.sw_if_index = swIfIndex;
  p.vlan_index = ~0;
  p.fib_table = 0;
  if (!executeAndWait (req, "sr_localsid_add_del"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "sr_localsid_add_del returned error: " << rp.retval;
      return false;
    }
  return true;
}

std::string VppClient::addDelVxLanTunnel (const std::string &tunnelSrcIP,
                                          const std::string &tunnelDstIP,
                                          int vni, bool add)
{
  if (tunnelSrcIP.empty () || tunnelDstIP.empty ())
    {
      LOG (ERROR) << "VxLAN Error: empty src or dst IPv6 prefixes";
      return "";
    }

  folly::CIDRNetwork tunnelSrcNw =
      folly::IPAddress::createNetwork (tunnelSrcIP, -1, false /* mask */);

  folly::CIDRNetwork tunnelDstNw =
      folly::IPAddress::createNetwork (tunnelDstIP, -1, false /* mask */);

  Vxlan_add_del_tunnel req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));

  p.is_add = add;
  p.vni = vni;
  p.instance = ~0;
  p.decap_next_index = ~0;
  p.src_address.af = ADDRESS_IP6;
  p.dst_address.af = ADDRESS_IP6;
  memcpy (p.src_address.un.ip6, tunnelSrcNw.first.bytes (), kIpv6AddrLen);
  memcpy (p.dst_address.un.ip6, tunnelDstNw.first.bytes (), kIpv6AddrLen);

  if (!executeAndWait (req, "vxlan_add_del_tunnel"))
    {
      LOG (ERROR) << "VxLAN Error: empty src or dst IPv6 prefixes";
      return "";
    }

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "vxlan_add_del_tunnel returned error: " << rp.retval;
      return "";
    }
  return vppIndexToIface (rp.sw_if_index);
}

VxlanTunnelMap VppClient::getVxlanTunnelDumps ()
{
  Vxlan_tunnel_dump req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));

  p.sw_if_index = (u32)~0;

  VxlanTunnelMap vxlanTunnels;

  if (executeAndWait (req, "vxlan_tunnel_dump"))
    {
      auto &rs = req.get_result_set ();
      for (auto &r : rs)
        {
          auto &rp = r.get_payload ();
          std::string srcIpAddr =
              NetUtils::ip6AddressFromBinary (rp.src_address.un.ip6).str ();
          std::string dstIpAddr =
              NetUtils::ip6AddressFromBinary (rp.dst_address.un.ip6).str ();
          vxlanTunnels[dstIpAddr] = {srcIpAddr, (int)rp.vni};
        }
    }

  return vxlanTunnels;
}

std::string VppClient::createSubInterface (const std::string &ifName,
                                           int subId)
{
  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return "";
    }
  if (!subId)
    {
      LOG (ERROR) << "ID is zero - cannot create sub-interface.";
      return "";
    }

  Create_subif req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;
  p.sub_id = subId;
  p.outer_vlan_id = subId;
  p.sub_if_flags = SUB_IF_API_FLAG_EXACT_MATCH | SUB_IF_API_FLAG_ONE_TAG;

  if (!executeAndWait (req, "create_subif"))
    return "";

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "create_subif returned error: " << rp.retval;
      return "";
    }
  return vppIndexToIface (rp.sw_if_index);
}

bool VppClient::deleteSubInterface (const std::string &ifName)
{
  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return false;
    }

  Delete_subif req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));

  p.sw_if_index = swIfIndex;

  if (!executeAndWait (req, "delete_subif"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "delete_subif returned error: " << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::setInterfaceL2VLANTag (const std::string &ifName, int tag)
{
  u32 swIfIndex = ifaceToVppIndex (ifName);
  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "Interface '" << ifName << "' not found in VPP";
      return false;
    }

  L2_interface_vlan_tag_rewrite req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;
  // Assume dot1q VLAN and a single tag - ETHERNET_TYPE_VLAN
  p.push_dot1q = 0x8100;
  p.tag1 = tag;
  if (!executeAndWait (req, "l2_interface_vlan_tag_rewrite"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "l2_interface_vlan_tag_rewrite returned error: "
                  << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::setInterfaceL2xConnect (const std::string &srcIf,
                                        const std::string &dstIf)
{
  u32 srcIfIndex = ifaceToVppIndex (srcIf);
  u32 dstIfIndex = ifaceToVppIndex (dstIf);

  if (srcIfIndex == (u32)~0 || dstIfIndex == (u32)~0)
    {
      LOG (ERROR) << "'" << srcIf << "' or '" << dstIf << "' not found in VPP";
      return false;
    }

  Sw_interface_set_l2_xconnect req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.rx_sw_if_index = srcIfIndex;
  p.tx_sw_if_index = dstIfIndex;
  p.enable = true;
  if (!executeAndWait (req, "sw_interface_set_l2_xconnect"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "sw_interface_set_l2_xconnect returned error: "
                  << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::setNat64Interface (const std::string &natIf,
                                   VppNat64Flags flag, bool add)
{
  u32 swIfIndex = ifaceToVppIndex (natIf);

  if (swIfIndex == (u32)~0)
    {
      LOG (ERROR) << "'" << natIf << "' not found in VPP";
      return false;
    }

  Nat64_add_del_interface req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;
  p.is_add = add;
  // TODO: change VppNat64Flags to use nat_config_flags
  p.flags = flag == VppNat64Flags::INGRESS_INTF ? NAT_IS_INSIDE : 0;
  if (!executeAndWait (req, "nat64_add_del_interface"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "nat64_add_del_interface returned error: " << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::addDelNat64TranslationPrefix (const std::string &nat64Prefix,
                                              bool add)
{
  folly::CIDRNetwork network =
      folly::IPAddress::createNetwork (nat64Prefix, -1, false /* mask */);
  Nat64_add_del_prefix req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.is_add = add;
  memcpy (p.prefix.address, network.first.bytes (), kIpv6AddrLen);
  p.prefix.len = network.second;

  if (!executeAndWait (req, "nat64_add_del_prefix"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "nat64_add_del_interface returned error: " << rp.retval;
      return false;
    }
  return true;
}

std::string VppClient::getNat64PoolRange ()
{
  Nat64_pool_addr_dump req (connection_);
  if (!executeAndWait (req, "nat64_pool_addr_dump"))
    return "";

  auto &rs = req.get_result_set ();
  // Currently, Terragraph will only program 1 IPv4 address in NAT64 pool.
  if (rs.size () > 1)
    {
      LOG (ERROR)
          << "Error: Found " << rs.size ()
          << " IPv4 addresses in NAT64 pool (only expect 1 IPv4 address).";
      return "";
    }

  std::string poolAddr = "";
  if (rs.size () > 0)
    {
      poolAddr =
          NetUtils::ip4AddressFromBinary (rs.begin ()->get_payload ().address)
              .str ();
    }
  return poolAddr;
}

bool VppClient::addDelNat64PoolRange (const std::string &startAddr,
                                      const std::string &endAddr, bool add)
{
  folly::IPAddress startIpAddr (startAddr);
  folly::IPAddress endIpAddr (endAddr);

  Nat64_add_del_pool_addr_range req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.is_add = add;
  memcpy (p.start_addr, startIpAddr.bytes (), kIpv4AddrLen);
  memcpy (p.end_addr, endIpAddr.bytes (), kIpv4AddrLen);

  if (!executeAndWait (req, "nat64_add_del_pool_addr_range"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "nat64_add_del_pool_addr_range returned error: "
                  << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::hqosTctbl (const u32 sw_if_index, const u32 entry,
                           const u32 tc, const u32 queue, const u32 color)
{
  Sw_interface_set_dpdk_hqos_tctbl req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = sw_if_index;
  p.entry = entry;
  p.tc = tc;
  p.queue = queue;
  p.color = color;

  if (!executeAndWait (req, "sw_interface_set_dpdk_hqos_tctbl"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "set_interface_dpdk_interface_hqos_tctbl returned error: "
                  << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::addDelPolicer (const VppClient::PolicerConfig_t &policerConfig,
                               u32 &policerIndex)
{
  Policer_add_del req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.is_add = policerConfig.isAdd;
  memcpy (p.name, policerConfig.name, kPolicerNameLen);
  p.cir = policerConfig.cir;
  p.eir = policerConfig.eir;
  p.cb = policerConfig.cb;
  p.eb = policerConfig.eb;
  p.rate_type = policerConfig.rateType;
  p.round_type = policerConfig.roundType;
  p.type = policerConfig.type;
  p.color_aware = policerConfig.colorAware;
  p.conform_action.type = policerConfig.conformActionType;
  p.conform_action.dscp = policerConfig.conformDscp;
  p.exceed_action.type = policerConfig.exceedActionType;
  p.exceed_action.dscp = policerConfig.exceedDscp;
  p.violate_action.type = policerConfig.violateActionType;
  p.violate_action.dscp = policerConfig.violateDscp;

  if (!executeAndWait (req, "policer_add_del"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "policer_add_del returned error: " << rp.retval;
      return false;
    }
  policerIndex = rp.policer_index;
  return true;
}

bool VppClient::getPolicer (const u8 name[64],
                            VppClient::PolicerConfig_t &policerConfig)
{
  Policer_dump req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  memcpy (p.match_name, name, kPolicerNameLen);
  p.match_name_valid = 1;

  if (!executeAndWait (req, "policer_dump"))
    return false;

  auto &rs = req.get_result_set ();
  if (rs.size () == 0)
    {
      return false;
    }
  if (rs.size () > 1)
    {
      LOG (WARNING) << "policer_dump returned multiple policers when a single "
                       "policer was expected.";
    }

  for (auto &r : rs)
    {
      auto &rp = r.get_payload ();
      policerConfig.cir = rp.cir;
      policerConfig.eir = rp.eir;
      policerConfig.cb = rp.cb;
      policerConfig.eb = rp.eb;
      policerConfig.rateType = rp.rate_type;
      policerConfig.roundType = rp.round_type;
      policerConfig.type = rp.type;
      policerConfig.colorAware = rp.color_aware;
      policerConfig.conformActionType = rp.conform_action.type;
      policerConfig.conformDscp = rp.conform_action.dscp;
      policerConfig.exceedActionType = rp.exceed_action.type;
      policerConfig.exceedDscp = rp.exceed_action.dscp;
      policerConfig.violateActionType = rp.violate_action.type;
      policerConfig.violateDscp = rp.violate_action.dscp;
    }
  return true;
}

bool VppClient::addDelClassifierSession (
    const ClassifierSessionConfig &classifierSessionConfig)
{
  u32 matchArraySize =
      classifierSessionConfig.isAdd ? classifierSessionConfig.matchLen : 0;
  Classify_add_del_session req (connection_, matchArraySize);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.is_add = classifierSessionConfig.isAdd;
  p.table_index = classifierSessionConfig.tableIndex;
  p.hit_next_index = classifierSessionConfig.hitNextIndex;
  p.opaque_index = classifierSessionConfig.opaqueIndex;
  p.advance = classifierSessionConfig.advance;
  p.action = classifierSessionConfig.action;
  p.metadata = classifierSessionConfig.metadata;
  p.match_len = classifierSessionConfig.matchLen;
  if (classifierSessionConfig.isAdd)
    memcpy (p.match, classifierSessionConfig.match,
            classifierSessionConfig.matchLen);

  if (!executeAndWait (req, "classify_add_del_session"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "classify_add_del_session returned error: " << rp.retval;
      return false;
    }
  return true;
}

bool VppClient::getClassifierTableIds (std::vector<u32> &tableIds)
{
  Classify_table_ids req (connection_);
  if (!executeAndWait (req, "classify_table_ids"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "classify_table_ids returned error: " << rp.retval;
      return false;
    }

  for (u32 idIdx = 0; idIdx < rp.count; idIdx++)
    {
      tableIds.push_back (rp.ids[idIdx]);
    }
  return true;
}

bool VppClient::addDelClassifierTable (ClassifierTableConfig &tableConfig)
{
  u32 maskArraySize = tableConfig.isAdd ? tableConfig.maskLen : 0;
  Classify_add_del_table req (connection_, maskArraySize);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.is_add = tableConfig.isAdd;
  p.del_chain = tableConfig.delChain;
  p.table_index = tableConfig.tableIndex;
  p.nbuckets = tableConfig.nbuckets;
  p.memory_size = tableConfig.memorySize;
  p.skip_n_vectors = tableConfig.skipNVectors;
  p.match_n_vectors = tableConfig.matchNVectors;
  p.next_table_index = tableConfig.nextTableIndex;
  p.miss_next_index = tableConfig.missNextIndex;
  p.current_data_flag = tableConfig.currentDataFlag;
  p.current_data_offset = tableConfig.currentDataOffset;
  p.mask_len = tableConfig.maskLen;
  if (tableConfig.isAdd)
    memcpy (p.mask, tableConfig.mask, tableConfig.maskLen);

  if (!executeAndWait (req, "classify_add_del_table"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "classify_add_del_table returned error: " << rp.retval;
      return false;
    }
  tableConfig.tableIndex = rp.new_table_index;
  return true;
}

bool VppClient::getClassifierByIndex (const u32 swIfIndex, u32 &tableId)
{
  Classify_table_by_interface req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = swIfIndex;

  if (!executeAndWait (req, "classify_table_by_interface"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "classify_table_info returned error: " << rp.retval;
      return false;
    }
  tableId = rp.ip6_table_id;
  return true;
}

bool VppClient::setClassifierTableNetif (const u8 isAdd, const u32 swIfIndex,
                                         const u32 ip6TableIndex)
{
  Policer_classify_set_interface req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.is_add = isAdd;
  p.sw_if_index = swIfIndex;
  p.ip6_table_index = ip6TableIndex;
  p.ip4_table_index = (u32)~0; // ~0 indicates skip ip4 table assignment
  p.l2_table_index = (u32)~0;  // ~0 indicates skip l2 table assignment

  if (!executeAndWait (req, "policer_classify_set_interface"))
    return false;

  auto &reply = req.get_response ();
  auto &rp = reply.get_payload ();
  if (rp.retval != 0)
    {
      LOG (ERROR) << "policer_classify_set_interface returned error: "
                  << rp.retval;
      return false;
    }
  return true;
}

} // namespace vpp
} // namespace terragraph
} // namespace facebook

DEFINE_VAPI_MSG_IDS_CLASSIFY_API_JSON
DEFINE_VAPI_MSG_IDS_DHCP_API_JSON
DEFINE_VAPI_MSG_IDS_DPDK_API_JSON
DEFINE_VAPI_MSG_IDS_IP_API_JSON
DEFINE_VAPI_MSG_IDS_IP6_ND_API_JSON
DEFINE_VAPI_MSG_IDS_INTERFACE_API_JSON
DEFINE_VAPI_MSG_IDS_L2_API_JSON
DEFINE_VAPI_MSG_IDS_NAT64_API_JSON
DEFINE_VAPI_MSG_IDS_POLICER_API_JSON
DEFINE_VAPI_MSG_IDS_SR_API_JSON
DEFINE_VAPI_MSG_IDS_TAP_API_JSON
DEFINE_VAPI_MSG_IDS_VXLAN_API_JSON
