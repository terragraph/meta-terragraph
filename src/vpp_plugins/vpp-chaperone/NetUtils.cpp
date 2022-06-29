/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "NetUtils.h"

#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/socket.h>

#include <folly/String.h>
#include <folly/Conv.h>
#include <glog/logging.h>
#include <netlink/netlink.h>
#include <netlink/route/addr.h>

namespace facebook
{
namespace terragraph
{
namespace vpp
{

std::vector<std::string>
NetUtils::getAllIfIPv6Addresses (const std::string &ifName, bool global)
{
  std::vector<std::string> retAddresses;

  struct ifaddrs *ifaddr;
  if (getifaddrs (&ifaddr) == -1)
    {
      return retAddresses;
    }

  char host[NI_MAXHOST];
  for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
      // Find the right interface
      if (strncmp (ifa->ifa_name, ifName.c_str (), ifName.length ()) ||
          ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET6)
        {
          continue;
        }

      // Filter by address type
      struct sockaddr_in6 *current_addr = (struct sockaddr_in6 *)ifa->ifa_addr;
      bool isLinkLocal = IN6_IS_ADDR_LINKLOCAL (&current_addr->sin6_addr);
      if (global &&
          (isLinkLocal || IN6_IS_ADDR_LOOPBACK (&current_addr->sin6_addr)))
        {
          continue;
        }
      else if (!global && !isLinkLocal)
        {
          continue;
        }

      // Get address info
      auto ret = getnameinfo (ifa->ifa_addr, sizeof (struct sockaddr_in6),
                              host, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);
      if (ret != 0)
        {
          continue;
        }

      // Reformat the address if needed
      auto addr = std::string (host);
      size_t idx = addr.find ("%");
      if (idx != std::string::npos)
        {
          addr = addr.substr (0, idx);
        }
      retAddresses.push_back (addr);
    }

  freeifaddrs (ifaddr);
  return retAddresses;
}

bool NetUtils::addIfIPv6Address (const std::string &ifName,
                                 const std::string &ip, int prefixLen)
{
  bool success = false;
  struct nl_sock *nl = nullptr;
  struct rtnl_addr *addr = nullptr;
  struct nl_addr *nlAddr = nullptr;
  int ret;
  unsigned int ifIndex;

  folly::IPAddress ipAddr (ip);
  struct in6_addr in6Addr = ipAddr.asV6 ().toAddr ();

  // Create netlink socket
  nl = nl_socket_alloc ();
  if (!nl)
    {
      LOG (ERROR) << "Failed to allocate nl_sock";
      goto end;
    }
  if (nl_connect (nl, NETLINK_ROUTE) < 0)
    {
      LOG (ERROR) << "Failed to connect netlink socket";
      goto end;
    }

  // Create address
  ifIndex = if_nametoindex (ifName.c_str ());
  if (ifIndex == 0)
    {
      LOG (ERROR) << "Could not resolve interface name '" << ifName
                  << "' to index";
      goto end;
    }
  nlAddr = nl_addr_build (AF_INET6, &in6Addr, sizeof (in6Addr));
  if (!nlAddr)
    {
      LOG (ERROR) << "Failed to construct nl_addr";
      goto end;
    }
  addr = rtnl_addr_alloc ();
  if (!addr)
    {
      LOG (ERROR) << "Failed to allocate rtnl_addr";
      goto end;
    }
  rtnl_addr_set_ifindex (addr, ifIndex);
  rtnl_addr_set_local (addr, nlAddr);
  rtnl_addr_set_prefixlen (addr, prefixLen);

  // Send request
  ret = rtnl_addr_add (nl, addr, 0);
  if (ret == -17)
    {
      VLOG (2) << "Interface '" << ifName << "' already has address " << ip;
      success = true;
    }
  else if (ret >= 0)
    success = true;

end:
  // Free memory
  if (addr)
    rtnl_addr_put (addr);
  if (nlAddr)
    nl_addr_put (nlAddr);
  if (nl)
    {
      nl_close (nl);
      nl_socket_free (nl);
    }
  return success;
}

folly::IPAddressV4 NetUtils::ip4AddressFromBinary (const uint8_t *addr)
{
  return folly::IPAddressV4::fromBinary (
      folly::ByteRange (reinterpret_cast<const unsigned char *> (addr), 4));
}

folly::IPAddress NetUtils::ip6AddressFromBinary (const uint8_t *addr)
{
  return folly::IPAddress::fromBinary (
      folly::ByteRange (reinterpret_cast<const unsigned char *> (addr), 16));
}

std::string NetUtils::transformIPv6 (const std::string &ip,
                                     const std::string &finalHextet,
                                     const std::string &replaceHextet)
{
  folly::IPAddress addr (ip);
  if (addr.isV6 ())
    {
      std::string ipFull = addr.toFullyQualified ();
      if (ipFull.compare (ipFull.length () - 4, 4, finalHextet) == 0)
        {
          ipFull.replace (ipFull.length () - 4, 4, replaceHextet);
          return folly::IPAddress (ipFull).str ();
        }
    }
  return "";
}

std::string NetUtils::transformIPv6Prefix (const std::string &prefix,
                                           const std::string &finalHextet,
                                           const std::string &replaceHextet,
                                           const std::string &newNetmask)
{
  folly::CIDRNetwork network =
      folly::IPAddress::createNetwork (prefix, -1, false /* mask */);
  std::string ip =
      transformIPv6 (network.first.str (), finalHextet, replaceHextet);
  if (ip.empty ())
    return "";

  if (newNetmask.empty ())
    return ip + "/" + std::to_string (network.second);
  else
    return ip + newNetmask;
}

std::string NetUtils::generateLoopbackMacAddr (const std::string &ifName,
                                               const std::string nodeMac)
{
  // Generate unique loopback MAC address by modifying the 5th byte
  // of base node mac address using loop index.
  if (nodeMac.empty ())
    {
      return "";
    }

  std::vector<std::string> macVec;
  folly::split (":", nodeMac, macVec);
  macVec[4] = "0";
  macVec[4] += ifName.back () + 1;
  return folly::join (":", macVec);
}

std::string NetUtils::generatePopTapMacAddr (const std::string loopMac)
{
  // Generate POP Tap MAC address from loop MAC address by changing the
  // higher order nibble of 5th byte from "0" to "1".
  if (loopMac.empty ())
    {
      return "";
    }

  std::vector<std::string> macVec;
  folly::split (":", loopMac, macVec);
  std::string word = "1";
  word += macVec[4].back ();
  macVec[4] = word;
  return folly::join (":", macVec);
}

int NetUtils::parseVlanFromSrv6IpAddr (const std::string &ip,
                                       const std::string &baseHextet)
{
  folly::IPAddress addr (ip);
  if (addr.isV6 ())
    {
      std::string ipFull = addr.toFullyQualified ();
      std::string finalHextet = ipFull.substr (ipFull.length () - 4);

      auto finalHextetInt = folly::tryTo<int> (finalHextet);
      auto baseHextetInt = folly::tryTo<int> (baseHextet);

      if (finalHextetInt.hasValue () && baseHextetInt.hasValue ())
        return finalHextetInt.value () - baseHextetInt.value ();
      else
        VLOG (2) << "Unable to parse VLAN ID due to invalid baseHextet '"
                 << baseHextet << "' or finalHextet '" << finalHextet << "'";
    }
  else
    VLOG (2) << "Unable to parse VLAN ID due to invalid IP address " << ip;

  return -1;
}

std::string NetUtils::getNthPrefix (const folly::CIDRNetwork &seedPrefix,
                                    const uint32_t allocPrefixLen,
                                    const uint32_t prefixIndex)
{
  // get underlying byte array representing IP
  const uint32_t bitCount = seedPrefix.first.bitCount ();
  auto ipBytes =
      std::string (reinterpret_cast<const char *> (seedPrefix.first.bytes ()),
                   seedPrefix.first.byteCount ());

  // host number bit length
  // in seed prefix
  const uint32_t seedHostBitLen = bitCount - seedPrefix.second;
  // in allocated prefix
  const uint32_t allocHostBitLen = bitCount - allocPrefixLen;

  // sanity check
  const int32_t allocBits =
      std::min (32, static_cast<int32_t> (seedHostBitLen - allocHostBitLen));
  if (allocBits < 0)
    {
      throw std::invalid_argument ("Alloc prefix is bigger than seed prefix.");
    }
  if (allocBits < 32 and prefixIndex >= (1u << allocBits))
    {
      throw std::invalid_argument ("Prefix index is out of range.");
    }

  // using bits (seedHostBitLen-allocHostBitLen-1)..0 of @prefixIndex to
  // set bits (seedHostBitLen - 1)..allocHostBitLen of ipBytes
  for (uint8_t i = 0; i < allocBits; ++i)
    {
      // global bit index across bytes
      auto idx = i + allocHostBitLen;
      // byte index: network byte order, i.e., big-endian
      auto byteIdx = bitCount / 8 - idx / 8 - 1;
      // bit index inside the byte
      auto bitIdx = idx % 8;
      if (prefixIndex & (0x1 << i))
        {
          // set
          ipBytes.at (byteIdx) |= (0x1 << bitIdx);
        }
      else
        {
          // clear
          ipBytes.at (byteIdx) &= ~(0x1 << bitIdx);
        }
    }

  // convert back to CIDR
  auto allocPrefixIp = folly::IPAddress::fromBinary (folly::ByteRange (
      reinterpret_cast<const uint8_t *> (ipBytes.data ()), ipBytes.size ()));

  return allocPrefixIp.str () + "/" + std::to_string (allocPrefixLen);
}

bool NetUtils::inSubnet (const folly::CIDRNetwork &subnet,
                         const std::string &gwAddr)
{
  if (gwAddr.empty ())
    {
      return false;
    }

  folly::CIDRNetwork network =
      folly::IPAddress::createNetwork (gwAddr, -1, false);
  folly::IPAddress &ip = network.first;

  return ip.inSubnet (subnet.first, subnet.second);
}

} // namespace vpp
} // namespace terragraph
} // namespace facebook
