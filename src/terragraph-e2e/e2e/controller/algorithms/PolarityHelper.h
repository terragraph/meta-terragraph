/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <e2e/if/gen-cpp2/Topology_types.h>

#include "../ConfigHelper.h"
#include "../topology/TopologyWrapper.h"
#include "e2e/common/EventClient.h"

namespace facebook {
namespace terragraph {

/**
 * Polarity assignment algorithms.
 */
class PolarityHelper {
 public:
  /** Returns true if the polarity is ODD or HYBRID_ODD. */
  static bool isOddPolarity(thrift::PolarityType polarity);

  /** Returns true if the polarity is EVEN or HYBRID_EVEN. */
  static bool isEvenPolarity(thrift::PolarityType polarity);

  /** Returns true if the polarity is HYBRID_ODD or HYBRID_EVEN. */
  static bool isHybridPolarity(thrift::PolarityType polarity);

  /** Validate if two polarities are compatible across a wireless link. */
  static bool isValidLinkPolarity(
      std::optional<thrift::PolarityType> aPolarity,
      std::optional<thrift::PolarityType> zPolarity);

  /** Validate the given polarity type. */
  static bool isValidPolarityType(thrift::PolarityType polarity);

  /** Map from MAC address to polarity. */
  using MacToPolarity = std::unordered_map<std::string, thrift::PolarityType>;

  /**
   * Perform polarity optimization.
   *
   * Returns true if successful. Otherwise, add error messages to 'errMsgs' and
   * return false. This would fail if a polarity assignment is impossible given
   * the topology at hand (e.g. odd loop exists of only P2MP vertices).
   *
   * @param topologyW the network topology
   * @param userPolarities user configured polarities; the algorithm tries to
   *                       respect these with highest priority
   * @param oldPolarities current polarity assignment; the algorithm tries to
   *                      respect these with best-effort priority
   * @param newPolarities new polarity assignment
   * @param errMsgs list of errors encountered why generating a new polarity
   *                assignment
   */
  static bool optimizePolarity(
      const TopologyWrapper& topologyW,
      MacToPolarity& userPolarities,
      MacToPolarity& oldPolarities,
      MacToPolarity& newPolarities,
      std::vector<std::string>& errMsgs);

  /** Assign polarity to the radio(s) on both ends of a link, if possible. */
  static bool assignLinkPolarity(
      const TopologyWrapper& topologyW,
      ConfigHelper& configHelper,
      const thrift::Link& link) noexcept;

 private:
  /**
   * Returns true if the given node has any wireless links not including the
   * given 'testLink'.
   */
  static bool hasOtherWirelessLinks(
      const TopologyWrapper& topologyW,
      const thrift::Link& testLink,
      const std::string& nodeName);

  /**
   * Returns any polarity assigned to a radio on the given site, if any exists.
   *
   * This will never return a hybrid value.
   */
  static std::optional<thrift::PolarityType> getSitePolarity(
      const TopologyWrapper& topologyW,
      const ConfigHelper& configHelper,
      const std::string& siteName);

  /** Returns the preferred polarity assignment for a MAC address. */
  static thrift::PolarityType getPreferedPolarity(
      const std::string& macAddr,
      MacToPolarity& userPolarities,
      MacToPolarity& oldPolarities,
      MacToPolarity& newPolarities,
      std::unordered_set<std::string>& hybridMacs,
      std::unordered_map<std::string, std::unordered_set<std::string>>&
          site2MacsMap,
      std::unordered_map<std::string, std::string>& mac2SiteNameMap);

  /**
   * Assign polarity to a MAC address.
   *
   * This also propagates the polarity allocation to MAC addresses reachable via
   * wireless links.
   *
   * The caller can request that polarity allocation also propagates over MAC
   * addresses on the same site.
   */
  static bool assignPolarityAndFollow(
      const TopologyWrapper& topologyW,
      const std::string& currMacAddr,
      bool followSameSite,
      std::unordered_set<std::string>& hybridMacs,
      MacToPolarity& userPolarities,
      MacToPolarity& oldPolarities,
      MacToPolarity& newPolarities,
      std::map<std::string, std::string>& mac2NodeName,
      std::unordered_map<std::string, std::unordered_set<std::string>>&
          site2MacsMap,
      std::unordered_map<std::string, std::string>& mac2SiteNameMap,
      std::vector<std::string>& errMsgs);

  /** Allocate all polarities. */
  static bool allocatePolarities(
      const TopologyWrapper& topologyW,
      std::unordered_set<std::string>& hybridMacs,
      MacToPolarity& userPolarities,
      MacToPolarity& oldPolarities,
      MacToPolarity& newPolarities,
      std::map<std::string, std::string>& mac2NodeName,
      std::unordered_map<std::string, std::unordered_set<std::string>>&
          site2MacsMap,
      std::unordered_map<std::string, std::string>& mac2SiteNameMap,
      std::vector<std::string>& errMsgs);

  /** Returns a set of WLAN MAC addresses with Y-street links. */
  static std::unordered_set<std::string> getYStreetMacs(
      const TopologyWrapper& topologyW);
};

} // namespace terragraph
} // namespace facebook
