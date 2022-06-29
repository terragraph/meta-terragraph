/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * Copyright (c) RADWIN.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <optional>

#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBase.h>
#include <openr/common/NetworkUtil.h>
#include <vapi/vapi.hpp>

namespace openr
{

/**
 * VppClient provides C++ interface for programming routes in VPP. Every public
 * API explosed by this class must be invoked from within `folly::EventBase`
 * passed in init(). This ensures the serialization of all API calls into
 * VPP as well as thread safety of private data members.
 *
 * Routes are organized by preference in this class. Lower value of preference
 * is better and will be used for forwarding. APIs are declarative, e.g.
 * `addRoute` will add or update an existing route to follow new nexthops.
 *
 * Example
 *  client.addRoute(10, Route(dest=prefix1, nextHops=[nh1, nh2]));
 *  -> VPP will forward on `prefix1 -> nh1, nh2`
 *
 *  client.addRoute(10, Route(dest=prefix1, nextHops=[nh2, nh3]));
 *  -> VPP will forward on `prefix1 -> nh2, nh3`
 *
 *  // TODO: VPP don't support multiple client well. nh2/nh3 of preference 20
 *  // will override nh2/nh3 of preference 10. To overcome this problem, we can
 *  // implement preference in VppClient class. VppClient will retain nexthops
 *  // for each preference, but only programs the nexthops with lowest
 *  // preference.
 *  client.addRoute(20, Route(dest=prefix1, nextHops[nh4, nh5]));
 *  -> VPP will forward on `prefix1 -> nh2, nh3` (lower preference better)
 *
 *  client.delRoute(10, prefix1);
 *  -> VPP will forward on `prefix1 -> nh4, nh5` (lower preference takes over)
 *
 *  client.delRoute(20 ,prefix1);
 *  -> VPP will not have entry for prefix1
 *
 * > Note on Preference
 * Lower preference is better. Map a certain ClientID to a particular
 * preference, to ensure the clients don't mess-up with each other's route in
 * HW.
 *
 * > Note on MPLS Support
 * Current implementation doesn't support MPLS, but VPP do support. For adding
 * MPLS support, we will need to support two things. 1) IP->MPLS routes and 2)
 * MPLS routes.
 * For IP->MPLS routes convert `NextHopThrift.mplsAction` attribute to
 * approprriate attributes of `vapi_msg_ip_add_del_route` and adding deleteing
 * route. Convert `vapi_type_fib_path` attributes to `NextHopThrift.mplsAction`
 * if applicable when reading routes from VPP.
 * For MPLS routes, follow same code structure follow same structure as unicast
 * routes (cache, APIs), use appropriate MSG types for adding/updating/reading
 * mpls routes to/from VPP.
 *
 * > References
 *  VPP Source Code: https://github.com/FDio/vpp
 *  VPP APIs: https://docs.fd.io/vpp/17.10/d8/da3/ip_8api_source.html
 *  VPP errno: https://github.com/FDio/vpp/blob/master/src/vnet/api_errno.h
 *
 * > Note on VPP API
 *  VPP uses thrift alike RPC interface. Server is implemented in C++.
 *  Reference: https://github.com/FDio/vpp/tree/master/src/vnet/ip
 *  - `.api` file is RPC interface, `.h` or `.c` is server implementation
 *  - Generated C++ API definitions can be found under build direcotry
 *    `tmp/work/aarch64-poky-linux/vpp/19.01-lsdk-r0/build/vpp-api/vapi`
 */
class VppClient final
{
public:
  VppClient (std::optional<std::string> vppRootPath,
             int maxOutstandingRequests, int responseQueueSize);

  ~VppClient ();

  // Try to connect to VAPI, returning true if successful.
  // Note that vlibmemory/memory_client.c has a hardcoded 10-second timeout.
  bool connect ();

  // Initialize event-related members and interface mappings.
  void init (folly::EventBase *evb);

  void addRoute (uint8_t preference, const openr::thrift::UnicastRoute &route);

  void deleteRoute (uint8_t preference, const openr::thrift::IpPrefix &prefix);

  void syncRoutes (uint8_t preference,
                   const std::vector<openr::thrift::UnicastRoute> &routes);

  std::vector<openr::thrift::UnicastRoute> getRoutes (uint8_t preference);

  void getCounters (std::map<std::string, int64_t> &counters);

  // Return the Bridged VLAN interface index from the bridge domain (if any).
  uint32_t getBVIFromBridgeDomain (uint32_t bdId);

  // Get addresses of a VPP interface.
  std::string getVppSwIfaceAddress (const std::string &ifName);

  // Derive VPP loopback link-local from Tap link-local.
  std::string getVppLoopbackLinkLocalAddr (const std::string &tapLLV6);

private:
  void sendKeepAlive ();

  // Update interface name <-> index mappings
  void updateInterfaces ();

  // Finds a mapping between the given Linux interface name
  // and the corresponding vpp interface index
  int ifaceToVppIndex (const std::string &ifName);

  // Finds a mapping between the given vpp interface index
  // and the corresponding Linux interface name
  std::string vppIndexToIface (int vppIfIndex);

  // Execute a C++ VAPI Request and wait for the response.
  // Return true if no error was encountered.
  template <class T> bool executeAndWait (T &req, const std::string &apiName);

  // Reference to folly event-base
  folly::EventBase *evb_{nullptr};
  std::unique_ptr<folly::AsyncTimeout> keepAliveTimer_;

  // The VAPI connection
  vapi::Connection connection_;

  // Whether a VAPI connection has ever succeeded
  bool connected_{false};

  // VPP root path
  std::optional<std::string> vppRootPath_;

  // Max number of outstanding requests queued by the VAPI client
  const int maxOutstandingRequests_;

  // Size of the VAPI client's response queue
  const int responseQueueSize_;

  // Cache of Linux interface names to vpp interface indexes
  std::unordered_map<std::string, int> ifNameToIndex_;

  // Cache of vpp interface indexes to Linux interface names
  std::unordered_map<int, std::string> ifIndexToName_;

  // Map of cached routes - programmed in VPP
  // We maintain unique set of routes for each preference. Each protocol maps
  // to a particular preference, lower preference wins.
  std::unordered_map<
      uint8_t /* preference aka clientId */,
      std::unordered_map<thrift::IpPrefix, std::vector<thrift::NextHopThrift>>>
      unicastRoutes_;
};

} // namespace openr
