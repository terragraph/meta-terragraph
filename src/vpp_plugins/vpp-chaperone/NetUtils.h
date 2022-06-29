/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/IPAddress.h>

namespace facebook
{
namespace terragraph
{
namespace vpp
{

//
// Network-related utilities.
//
class NetUtils final
{
public:
  // Find all the IPv6 addresses for the given interface name.
  // If flag 'global' is true, returns only global addresses, else link-locals.
  // Return an empty string if not found. The first index usually has the newly
  // added address.
  static std::vector<std::string>
  getAllIfIPv6Addresses (const std::string &ifName, bool global);

  // Add an IPv6 address to the given interface.
  // Return true upon success.
  static bool addIfIPv6Address (const std::string &ifName,
                                const std::string &ip, int prefixLen);

  // Return a folly::IPAddressV4 instance for the given binary IPv4 address.
  static folly::IPAddressV4 ip4AddressFromBinary (const uint8_t *addr);

  // Return a folly::IPAddress instance for the given binary IPv6 address.
  static folly::IPAddress ip6AddressFromBinary (const uint8_t *addr);

  // For a given IPv6 address, replace the expected final hextet
  //   (fully-qualified, i.e. string of 4 hex characters) with another.
  // If the final hextet differs or the IPv6 address is invalid, return an
  //   empty string.
  static std::string transformIPv6 (const std::string &ip,
                                    const std::string &finalHextet,
                                    const std::string &replaceHextet);

  // For a given IPv6 prefix, replace the expected final hextet
  //   (fully-qualified, i.e. string of 4 hex characters) with another, and
  //   optionaly replace the network mask unless 'newNetMask' is an empty
  //   string, otherwise use the original mask.
  // If the final hextet differs or the IPv6 prefix is invalid, return an
  //   empty string.
  static std::string transformIPv6Prefix (const std::string &prefix,
                                          const std::string &finalHextet,
                                          const std::string &replaceHextet,
                                          const std::string &newNetmask = "");

  // Generate MAC address based on base node MAC address
  // and loopback interface id.
  static std::string generateLoopbackMacAddr (const std::string &ifName,
                                              const std::string nodeMac);

  // Generate POP Tap MAC address based on corresponding loop MAC address.
  static std::string generatePopTapMacAddr (const std::string loopMac);

  // Parse VLAN ID from SRv6 encapsulation or decapsulation IP address.
  static int parseVlanFromSrv6IpAddr (const std::string &ip,
                                      const std::string &baseHextet);

  // Get n-th subprefix of allocated length in seed prefix. This is a copy of
  // openr::getNthPrefix().
  static std::string getNthPrefix (const folly::CIDRNetwork &seedPrefix,
                                   const uint32_t allocPrefixLen,
                                   const uint32_t prefixIndex);

  // Check if a gateway address is in the subnet
  static bool inSubnet (const folly::CIDRNetwork &subnet,
                        const std::string &gwAddr);
};

} // namespace vpp
} // namespace terragraph
} // namespace facebook
