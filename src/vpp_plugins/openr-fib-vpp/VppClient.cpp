/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * Copyright (c) RADWIN.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "VppClient.h"

#include <chrono>
#include <set>
#include <stdexcept>

#include <folly/Format.h>
#include <folly/String.h>
#include <folly/IPAddress.h>

#include <vapi/interface.api.vapi.hpp>
#include <vapi/ip.api.vapi.hpp>
#include <vapi/vpe.api.vapi.hpp>
#include <vapi/l2.api.vapi.hpp>

#include <vapi/tgcfg.api.vapi.hpp>

#define IPV4_ADDR_SZ 4
#define IPV6_ADDR_SZ 16

DEFINE_string (pop_tap, "tap1", "Linux POP tap interface");
DEFINE_uint32 (pop_bridge_domain_id, 1, "POP bridge domain ID in VPP");

using namespace vapi;

namespace openr
{

namespace
{
const std::chrono::seconds kVppKeepAliveTime{10};
} // namespace

VppClient::VppClient (std::optional<std::string> vppRootPath,
                      int maxOutstandingRequests, int responseQueueSize)
    : vppRootPath_{vppRootPath},
      maxOutstandingRequests_{maxOutstandingRequests}, responseQueueSize_{
                                                           responseQueueSize}
{
}

VppClient::~VppClient ()
{
  if (connected_)
    {
      if (keepAliveTimer_)
        keepAliveTimer_.reset ();

      connection_.disconnect ();
      connected_ = false;
    }
}

bool VppClient::connect ()
{
  if (connected_)
    return true;

  vapi_error_e rv = connection_.connect (
      "openr-fib-vpp",
      vppRootPath_.has_value () ? vppRootPath_->data () : nullptr,
      maxOutstandingRequests_, responseQueueSize_);
  if (rv != VAPI_OK)
    {
      return false;
    }

  connected_ = true;
  return true;
}

void VppClient::init (folly::EventBase *evb)
{
  if (evb_)
    return;

  CHECK_NOTNULL (evb);
  evb_ = evb;

  // Update interface and index mapping
  updateInterfaces ();

  // Schedule keep-alive timer to ensure that connection with VPP stays up
  keepAliveTimer_ = folly::AsyncTimeout::make (*evb_, [this] () noexcept {
    try
      {
        sendKeepAlive ();
      }
    catch (std::exception const &e)
      {
        LOG (FATAL) << "Failed to send keep-alive to vpp: " << e.what ();
      }
    keepAliveTimer_->scheduleTimeout (kVppKeepAliveTime); // re-schedule
  });
  keepAliveTimer_->scheduleTimeout (kVppKeepAliveTime);
}

void VppClient::syncRoutes (
    uint8_t preference, const std::vector<openr::thrift::UnicastRoute> &routes)
{
  // Ensure thread safety
  evb_->checkIsInEventBaseThread ();

  auto &cachedRoutes = unicastRoutes_[preference];

  // Step-1 Dump all vpp routes which go via an interface we know about
  auto oldRoutes = getRoutes (preference);
  cachedRoutes.clear ();
  for (auto const &route : oldRoutes)
    {
      cachedRoutes.emplace (route.dest_ref ().value (),
                            route.nextHops_ref ().value ());
    }

  // Step-2 Add or update new routes. Prepare set of new prefixes
  std::set<thrift::IpPrefix> newPrefixes;
  for (auto &route : routes)
    {
      newPrefixes.insert (route.dest_ref ().value ());
      addRoute (preference, route);
    }

  // Step-3 Remove old routes
  for (auto &route : oldRoutes)
    {
      if (newPrefixes.count (route.dest_ref ().value ()))
        {
          // Route is specified in newRoutes
          continue;
        }
      deleteRoute (preference, route.dest_ref ().value ());
    }
}

std::vector<openr::thrift::UnicastRoute>
VppClient::getRoutes (uint8_t preference)
{
  // Ensure thread safety
  evb_->checkIsInEventBaseThread ();

  std::vector<thrift::UnicastRoute> routes;

  Ip_route_dump req (connection_);
  auto &p = req.get_request ().get_payload ();
  // We only use a single table, table 0
  p.table.table_id = 0;
  p.table.is_ip6 = true;

  if (!executeAndWait (req, "ip_route_dump"))
    return routes;

  auto &rs = req.get_result_set ();
  for (auto &r : rs)
    {
      auto &rp = r.get_payload ();
      thrift::UnicastRoute route;
      route.dest_ref ().value ().prefixAddress_ref ().value ().addr_ref () =
          std::string (
              reinterpret_cast<const char *> (rp.route.prefix.address.un.ip6),
              IPV6_ADDR_SZ);
      route.dest_ref ().value ().prefixLength_ref () = rp.route.prefix.len;
      for (unsigned i = 0; i < rp.route.n_paths; ++i)
        {
          auto &path = rp.route.paths[i];
          if (path.preference != preference)
            {
              // Skip paths of unexpected preference
              continue;
            }
          thrift::NextHopThrift nextHop;
          nextHop.address_ref ().value ().addr_ref () = std::string (
              reinterpret_cast<const char *> (path.nh.address.ip6),
              IPV6_ADDR_SZ);
          if (~static_cast<uint32_t> (0) != path.sw_if_index)
            {
              nextHop.address_ref ().value ().ifName_ref () =
                  vppIndexToIface (path.sw_if_index);
            }
          nextHop.weight_ref () = path.weight;
          route.nextHops_ref ().value ().emplace_back (std::move (nextHop));
        }
      // Add route if it has atleast one nexthop
      if (route.nextHops_ref ().value ().size ())
        {
          std::sort (route.nextHops_ref ().value ().begin (),
                     route.nextHops_ref ().value ().end ());
          routes.emplace_back (std::move (route));
        }
    }
  return routes;
}

uint32_t VppClient::getBVIFromBridgeDomain (uint32_t bdId)
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
              VLOG (1) << "Found VPP Bridge domain id " << bdId
                       << " with BVI index " << rp.bvi_sw_if_index;
              return rp.bvi_sw_if_index;
            }
        }
    }
  return 0;
}

std::string VppClient::getVppLoopbackLinkLocalAddr (const std::string &tapLLV6)
{
  // Derive VPP loop1 link local address of a node by changing the higher order
  // word in the final hextet of tap link local from "1" to "0".

  if (tapLLV6.empty ())
    {
      LOG (ERROR) << "Link-local addr is empty. ";
      return "";
    }

  folly::IPAddress tapAddr (tapLLV6);
  if (tapAddr.isV6 ())
    {
      std::string ipFull = tapAddr.toFullyQualified ();
      ipFull.replace (ipFull.length () - 4, 1, "0");
      return folly::IPAddress (ipFull).str ();
    }
  else
    {
      LOG (ERROR) << " link-local addr '" << tapAddr << "' is not IPv6.";
    }
  return "";
}

void VppClient::addRoute (uint8_t preference,
                          const thrift::UnicastRoute &route)
{
  // Ensure thread safety
  evb_->checkIsInEventBaseThread ();

  const auto &addr = route.dest_ref ()
                         .value ()
                         .prefixAddress_ref ()
                         .value ()
                         .addr_ref ()
                         .value ();
  const auto &newNextHops = route.nextHops_ref ().value ();
  const auto &oldNextHops = folly::get_default (
      unicastRoutes_[preference], route.dest_ref ().value (),
      std::vector<thrift::NextHopThrift> ());

  // Lambda function to add/del a nexthop from FIB
  auto addDelNextHop = [&] (thrift::NextHopThrift const &nextHop, bool isAdd) {
    LOG (INFO) << (isAdd ? "Adding" : "Deleting") << " path for "
               << toString (route.dest_ref ().value ()) << " with preference "
               << static_cast<int> (preference) << " " << toString (nextHop);
    Ip_route_add_del req (connection_, 1 /* route_paths_array_size */);
    auto &p = req.get_request ().get_payload ();
    memset (&p, 0, sizeof (p));
    p.is_add = isAdd;
    p.is_multipath = true;
    p.route.n_paths = 1;
    p.route.prefix.len =
        route.dest_ref ().value ().prefixLength_ref ().value ();
    bool is_ipv6 = addr.size () == IPV6_ADDR_SZ;
    if (is_ipv6)
      {
        p.route.prefix.address.af = ADDRESS_IP6;
        memcpy (p.route.prefix.address.un.ip6, addr.data (), addr.size ());

        p.route.paths[0].proto = FIB_API_PATH_NH_PROTO_IP6;
        memcpy (p.route.paths[0].nh.address.ip6,
                nextHop.address_ref ().value ().addr_ref ().value ().data (),
                addr.size ());
      }
    else
      {
        p.route.prefix.address.af = ADDRESS_IP4;
        memcpy (p.route.prefix.address.un.ip4, addr.data (), addr.size ());

        p.route.paths[0].proto = FIB_API_PATH_NH_PROTO_IP4;
        memcpy (p.route.paths[0].nh.address.ip4,
                nextHop.address_ref ().value ().addr_ref ().value ().data (),
                addr.size ());
      }
    p.route.paths[0].preference = preference;

    if (nextHop.address_ref ().value ().ifName_ref ().has_value ())
      {
        const std::string &ifName =
            nextHop.address_ref ().value ().ifName_ref ().value ();

        // If it's a POP wired route - program the POP's VPP loopback
        // link-local next-hop instead of tap's link-local. Routing will break
        // otherwise.
        if (ifName == FLAGS_pop_tap)
          {
            auto nextHopTap =
                folly::IPAddress::fromBinary (
                    folly::ByteRange (reinterpret_cast<const unsigned char *> (
                                          nextHop.address_ref ()
                                              .value ()
                                              .addr_ref ()
                                              .value ()
                                              .data ()),
                                      IPV6_ADDR_SZ))
                    .str ();
            std::string nextHopVppStr =
                getVppLoopbackLinkLocalAddr (nextHopTap);
            folly::IPAddress nextHopVpp = folly::IPAddress (nextHopVppStr);

            LOG (INFO) << "POP route '" << nextHopTap
                       << "' changed to VPP loop1 '" << nextHopVppStr << "'.";
            memcpy (p.route.paths[0].nh.address.ip6, nextHopVpp.bytes (),
                    addr.size ());
          }

        p.route.paths[0].sw_if_index = ifaceToVppIndex (
            nextHop.address_ref ().value ().ifName_ref ().value ());
      }
    else
      {
        p.route.paths[0].sw_if_index = ~0;
      }
    p.route.paths[0].weight =
        static_cast<uint8_t> (nextHop.weight_ref ().value ());

    if (executeAndWait (req, "ip_route_add_del"))
      {
        auto &reply = req.get_response ();
        auto &rp = reply.get_payload ();
        if (rp.retval != 0)
          {
            LOG (FATAL) << "ip_route_add_del returned error: " << rp.retval;
          }
      }
  };

  // Delete old nexthops first
  for (const auto &oldNextHop : oldNextHops)
    {
      auto it =
          std::find (newNextHops.begin (), newNextHops.end (), oldNextHop);
      if (it != newNextHops.end ())
        {
          // nexthop should remain programmed - skip removing
          continue;
        }

      addDelNextHop (oldNextHop, false);
    }

  // Add new nexthops
  for (const auto &newNextHop : newNextHops)
    {
      auto it =
          std::find (oldNextHops.begin (), oldNextHops.end (), newNextHop);
      if (it != oldNextHops.end ())
        {
          // next already programmed - skip adding again
          continue;
        }

      addDelNextHop (newNextHop, true);
    }

  // Update local cache
  if (newNextHops.size ())
    {
      unicastRoutes_[preference][route.dest_ref ().value ()] = newNextHops;
    }
  else
    {
      unicastRoutes_[preference].erase (route.dest_ref ().value ());
    }
}

void VppClient::deleteRoute (uint8_t preference,
                             const thrift::IpPrefix &prefix)
{
  // Ensure thread safety
  evb_->checkIsInEventBaseThread ();

  // add route with empty nexthops to remove all existing paths
  thrift::UnicastRoute route;
  route.dest_ref () = prefix;
  addRoute (preference, route);
}

void VppClient::getCounters (std::map<std::string, int64_t> &counters)
{
  // Ensure thread safety
  evb_->checkIsInEventBaseThread ();

  int64_t numRoutes{0};
  for (auto const &kv : unicastRoutes_)
    {
      auto counterStr = folly::sformat ("fibagent.{}.num_of_routes", kv.first);
      counters[counterStr] = kv.second.size ();
      numRoutes += kv.second.size ();
    }
  counters["fibagent.num_of_routes"] = numRoutes;
}

void VppClient::sendKeepAlive ()
{
  // Ensure thread safety
  evb_->checkIsInEventBaseThread ();

  // Best effort - run largely noop operation against vpp to remind it
  // that we are still alive. Otherwise VPP eventually concludes that
  // we are non-responsive and starts probing with periodic keepalives
  // each resulting in a spammy log output. This call is a best effort
  // only and we do not really care about any of the data returned, we
  // are after its side effect only.
  Show_version req (connection_);
  executeAndWait (req, "show_version");
}

template <class T>
bool VppClient::executeAndWait (T &req, const std::string &apiName)
{
  // XXX: Convert this to process list of requests at a time to speed up
  // the performance.

  // Ensure thread safety
  evb_->checkIsInEventBaseThread ();

  if (!connected_)
    return false;

  VLOG (3) << "Querying VAPI: " << apiName;
  vapi_error_e rv = req.execute ();
  if (rv != VAPI_OK)
    {
      // NOTE: Being strict about errors
      LOG (FATAL) << apiName << " execution failed (error code " << rv << ")";
      return false;
    }

  do
    {
      rv = connection_.wait_for_response (req);
    }
  while (rv == VAPI_EAGAIN);

  if (rv != VAPI_OK)
    {
      // NOTE: Being strict about errors
      LOG (FATAL) << apiName << " response failed (error code " << rv << ")";
      return false;
    }

  VLOG (3) << apiName << " succeeded.";
  return true;
}

void VppClient::updateInterfaces ()
{
  // Ensure thread safety
  evb_->checkIsInEventBaseThread ();

  {
    Sw_interface_dump req (connection_);
    auto &p = req.get_request ().get_payload ();
    memset (&p, 0, sizeof (p));
    if (executeAndWait (req, "sw_interface_dump"))
      {
        auto &rs = req.get_result_set ();
        for (auto &r : rs)
          {
            auto &rp = r.get_payload ();
            std::string ifName (
                reinterpret_cast<const char *> (rp.interface_name));
            // terraX interfaces have a corresponding vpp interface named
            // vpp-terraX
            if (ifName.find ("vpp-") == 0)
              {
                ifName = ifName.substr (4);
              }
            ifNameToIndex_[ifName] = rp.sw_if_index;
            ifIndexToName_[rp.sw_if_index] = ifName;
            VLOG (1) << "Caching interface index for " << ifName << " with "
                     << rp.sw_if_index;
          }
      }
  }

  // Could be a wired tap, use the tgcfg api to get the corresponding
  // sw_if_index
  {
    Interface_map_dump req (connection_);
    if (executeAndWait (req, "interface_map_dump"))
      {
        auto &rs = req.get_result_set ();
        for (auto &r : rs)
          {
            auto &rp = r.get_payload ();
            std::string ifName (
                reinterpret_cast<const char *> (rp.linux_tap_name));
            ifNameToIndex_[ifName] = rp.sw_if_index;
            ifIndexToName_[rp.sw_if_index] = ifName;
            VLOG (1) << "Caching wired interface index for " << ifName
                     << " with " << rp.sw_if_index;
          }
      }
  }
}

// Given a Linux interface (e.g. terra2), look up the
// vpp interface index in a cache. If not found, attempt
// to retrieve this info from vpp and update the cache.
// This is necessary so that routes given by OpenR, which
// contain the kernel interface name as a string, can be
// applied to the equivalent vpp interfaces, which are
// specified by an integer index.
int VppClient::ifaceToVppIndex (const std::string &ifName)
{
  // Ensure thread safety
  evb_->checkIsInEventBaseThread ();

  auto it = ifNameToIndex_.find (ifName);
  if (it != ifNameToIndex_.end ())
    return it->second;

  LOG (WARNING) << "Interface index for name " << ifName
                << " unknown, querying VPP";
  updateInterfaces ();

  // POP interface is configured by vpp-chaperone so interfaces
  // may not be synced yet.
  // For the Linux POP tap interface - the VPP mapping is to
  // the BVI interface (loopX) on the POP bridge instead
  // of the tapcli interface.

  if (ifName == FLAGS_pop_tap)
    {
      int vppSwIndex = getBVIFromBridgeDomain (FLAGS_pop_bridge_domain_id);

      if (!vppSwIndex)
        {
          VLOG (1) << "VPP BVI interface not found for POP interface "
                   << ifName;
        }
      else
        {
          ifNameToIndex_[ifName] = vppSwIndex;
          ifIndexToName_[vppSwIndex] = ifName;
          VLOG (1) << "Mapped wired Linux tap " << ifName << " to VPP BVI "
                   << vppIndexToIface (vppSwIndex) << " with idx "
                   << vppSwIndex;
        }
    }

  // If it wasn't found, that means either vpp has no corresponding interface,
  // or openr discovered peers on an interface we didn't expect. Either is bad.
  it = ifNameToIndex_.find (ifName);
  if (it != ifNameToIndex_.end ())
    {
      return it->second;
    }

  throw std::runtime_error (
      folly::sformat ("Could not find vpp interface index for {}", ifName));
}

// Given a vpp index (e.g. 4), return the name of the
// corresponding Linux interface (e.g. terra2). This
// is necessary so that getRoutes can return sensible
// information to OpenR. In theory, vpp won't contain
// any routes that we haven't programmed in ourselves.
// However, a user might query the pre-installed routes
// too on restart.
std::string VppClient::vppIndexToIface (int ifIndex)
{
  // Ensure thread safety
  evb_->checkIsInEventBaseThread ();

  auto it = ifIndexToName_.find (ifIndex);
  if (it != ifIndexToName_.end ())
    return it->second;

  LOG (WARNING) << "Interface name for index " << ifIndex
                << " unknown, querying VPP";
  updateInterfaces ();

  // If it wasn't found, that means either vpp has no corresponding interface,
  // or openr discovered peers on an interface we didn't expect. Either is bad.
  it = ifIndexToName_.find (ifIndex);
  if (it != ifIndexToName_.end ())
    {
      return it->second;
    }

  throw std::runtime_error (
      folly::sformat ("Could not find vpp interface name for {}", ifIndex));
}

// Get addresses of a VPP interface.
std::string VppClient::getVppSwIfaceAddress (const std::string &ifName)
{
  // Ensure thread safety
  evb_->checkIsInEventBaseThread ();

  auto vppSwIndex = ifaceToVppIndex (ifName);

  Ip_address_dump req (connection_);
  auto &p = req.get_request ().get_payload ();
  memset (&p, 0, sizeof (p));
  p.sw_if_index = vppSwIndex;
  p.is_ipv6 = true;
  if (executeAndWait (req, "ip_address_dump"))
    {
      auto &rs = req.get_result_set ();
      for (const auto &r : rs)
        {
          auto &rp = r.get_payload ();

          return folly::IPAddress::fromBinary (
                     folly::ByteRange (
                         reinterpret_cast<const unsigned char *> (
                             rp.prefix.address.un.ip6),
                         16))
              .str ();
        }
    }
  return "";
}

} // namespace openr

DEFINE_VAPI_MSG_IDS_IP_API_JSON
DEFINE_VAPI_MSG_IDS_INTERFACE_API_JSON
DEFINE_VAPI_MSG_IDS_L2_API_JSON
DEFINE_VAPI_MSG_IDS_TGCFG_API_JSON
