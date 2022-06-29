/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <map>
#include <vector>
#include <string>

namespace facebook {
namespace terragraph {

/**
 * IP address-related utilities.
 */
class IpUtil {
 public:
  /**
   * Find the global IPv6 address on the given network interface name.
   *
   * Returns an empty string if not found.
   */
  static std::string getIpv6GlobalAddress(const std::string& ifName);

  /** Find the link-local IPv6 addresses on all network interfaces. */
  static std::map<std::string /*Interface*/, std::string /*Address*/>
      getIpv6LinkLocalAddresses();

  // ????
  /** \{ **/
  static std::string ipv6Host2String(uint64_t hostId);
  /** \} **/

  /** Returns a list of all network interface names beginning with "terra". */
  static std::vector<std::string> getTerraInterfaces();

  /** Validate the given ZMQ URI. */
  static bool isValidZmqUri(const std::string& uri);
};

} // namespace terragraph
} // namespace facebook
