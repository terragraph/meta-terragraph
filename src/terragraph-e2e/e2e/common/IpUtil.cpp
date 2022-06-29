/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "IpUtil.h"

#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <regex>
#include <sys/socket.h>

#include <folly/Format.h>
#include <glog/logging.h>

namespace {
// ZMQ URI regex
const std::regex kZmqUrlRegex(
    "(inproc://(.{1,256}))|"
    "(ipc://(.{1,107}))|"
    "(tcp://((\\*|[^:]+|\\[[^\\]]+\\]):(\\*|[0-9]{1,5}))"
         "(;((\\*|[^:]+|\\[[^\\]]+\\]):(\\*|[0-9]{1,5})))?)");
}

namespace facebook {
namespace terragraph {

// get globally reachable ipv6 address for given interface name
std::string
IpUtil::getIpv6GlobalAddress(const std::string& ifName) {
  struct ifaddrs *ifaddr, *ifa;
  char host[NI_MAXHOST];
  std::string ipv6Address;

  VLOG(6) << "Get global addressable ipv6 on " << ifName;
  if (getifaddrs(&ifaddr) == -1) {
    LOG(ERROR) << "getifaddrs failed due to " << folly::errnoStr(errno);
    return "";
  }

  for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (strncmp(ifa->ifa_name, ifName.c_str(), ifName.length())) {
      VLOG(6) << "  skip " << ifa->ifa_name << " due to name mismatch";
      continue;
    }
    if (ifa->ifa_addr == nullptr) {
      VLOG(6) << "  skip " << ifa->ifa_name << " due to null ifa_addr";
      continue;
    }
    if (ifa->ifa_addr->sa_family != AF_INET6) {
      VLOG(6) << "  skip " << ifa->ifa_name << " sa_family != AF_INET6";
      continue;
    }
    struct sockaddr_in6* current_addr = (struct sockaddr_in6*)ifa->ifa_addr;
    if (IN6_IS_ADDR_LINKLOCAL(&current_addr->sin6_addr)) {
      VLOG(6) << "  skip " << ifa->ifa_name << " due to link-local";
      continue;
    }
    if (IN6_IS_ADDR_LOOPBACK(&current_addr->sin6_addr)) {
      VLOG(6) << " skip " << ifa->ifa_name << " due to loop-back";
      continue;
    }
    if (getnameinfo(
            ifa->ifa_addr,
            sizeof(struct sockaddr_in6),
            host,
            NI_MAXHOST,
            nullptr,
            0,
            NI_NUMERICHOST) != 0) {
      VLOG(6) << "  skip " << ifa->ifa_name << " due to getnameinfo() failure";
      continue;
    }

    ipv6Address = std::string(host);
    VLOG(6) << "  Found ipv6 global address " << ipv6Address << " for inf "
            << ifa->ifa_name;
    break;
  }

  freeifaddrs(ifaddr);
  return ipv6Address;
}

// get link-local addresses
std::map<std::string, std::string>
IpUtil::getIpv6LinkLocalAddresses() {
  struct ifaddrs *ifaddr, *ifa;
  char host[NI_MAXHOST];
  std::string ipv6Address;
  std::map<std::string, std::string> linkLocalAddresses{};

  VLOG(6) << "Get link-local addresses";
  if (getifaddrs(&ifaddr) == -1) {
    LOG(ERROR) << "getifaddrs failed due to " << folly::errnoStr(errno);
    return linkLocalAddresses;
  }

  for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    struct sockaddr_in6* current_addr = (struct sockaddr_in6*)ifa->ifa_addr;
    if (IN6_IS_ADDR_LINKLOCAL(&current_addr->sin6_addr)) {
      if (getnameinfo(
              ifa->ifa_addr,
              sizeof(struct sockaddr_in6),
              host,
              NI_MAXHOST,
              nullptr,
              0,
              NI_NUMERICHOST) != 0) {
        VLOG(6) << "skip link-local on " << ifa->ifa_name
                << " due to getnameinfo() failure";
        continue;
      }

      ipv6Address = std::string(host);
      auto pos = ipv6Address.find("%");
      if (pos != std::string::npos) {
        linkLocalAddresses[ifa->ifa_name] = ipv6Address.substr(0, pos);
      } else {
        linkLocalAddresses[ifa->ifa_name] = ipv6Address;
      }

      VLOG(6) << "Found link-local ip " << ipv6Address << " for intf "
              << ifa->ifa_name;
    }
  }
  freeifaddrs(ifaddr);
  return linkLocalAddresses;
}


std::string
IpUtil::ipv6Host2String(uint64_t hostId) {
  static const char hexValues[] = "0123456789abcdef";
  std::string result;
  int index = 0;
  while (hostId > 0) {
    if (index++ >= 4) {
      result = ":" + result;
      index = 1;
    }
    result = hexValues[hostId & 0xf] + result;
    hostId >>= 4;
  }
  return result;
}

std::vector<std::string>
IpUtil::getTerraInterfaces() {
  struct if_nameindex *if_ni, *i;
  std::vector<std::string> terraInterfaces{};

  if_ni = if_nameindex();
  if (if_ni == nullptr) {
     LOG(ERROR) << "if_nameindex failed due to " << folly::errnoStr(errno);
     return terraInterfaces;
  }

  for (i = if_ni; ! (i->if_index == 0 && i->if_name == nullptr); i++) {
    auto ifname = i->if_name;
    if (strncmp(ifname, "terra", strlen("terra")) == 0) {
      terraInterfaces.push_back(ifname);
    }
  }

  if_freenameindex(if_ni);
  return terraInterfaces;
}

bool
IpUtil::isValidZmqUri(const std::string& uri) {
  return std::regex_match(uri, kZmqUrlRegex);
}

} // namespace terragraph
} // namespace facebook
