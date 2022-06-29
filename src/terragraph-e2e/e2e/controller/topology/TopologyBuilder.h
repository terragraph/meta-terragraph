/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cfloat>
#include <deque>

#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/Topology_types.h"
#include "TopologyWrapper.h"

namespace facebook {
namespace terragraph {

/**
 * The topology discovery algorithm, implemented using broadcast beamforming
 * scans.
 *
 * @see TopologyBuilderApp
 */
class TopologyBuilder {
 public:
  /** Constructor. */
  TopologyBuilder() {}

  /**
   * Process the given topology scan results.
   *
   * For each responder, this finds the strongest beam with the smallest beam
   * angle using the initiator-to-responder LQM (link quality metric) matrix.
   * This also identifies the nearest site in the topology via GPS distance.
   *
   * @param topologyW the TopologyWrapper instance
   * @param scanData the topology scan results
   */
  thrift::StartTopologyScanResp processTopologyScanResults(
      const TopologyWrapper& topologyW, const thrift::ScanData& scanData) const;

  /** The scan type as part of networkTopologyScanLoop(). */
  enum ScanType {
    NETWORK_TOPOLOGY = 1,
    LINK_DISCOVERY,
  };

  /** The action type as part of networkTopologyScanLoop(). */
  enum ActionType {
    /** Start a new topology scan */
    SCAN = 1,
    /** Wait (nothing to do) */
    WAIT,
    /** All scans are complete */
    FINISH,
  };

  /** Represents an action to take as part of networkTopologyScanLoop(). */
  struct Action {
    /** The action type. */
    ActionType type;

    /** The txNode (if actionType == SCAN). */
    std::string txNode;

    /** New nodes that should be added (if any). */
    std::unordered_map<std::string /* macAddr */, thrift::Node> newNodes;

    /** New links that should be added (if any). */
    std::vector<thrift::Link> newLinks;
  };

  /** Returns whether a network-wide topology scan is currently running. */
  bool isRunningNetworkTopologyScan() const;

  /** Initialize structures for a network-wide topology scan. */
  void initNetworkTopologyScan(
      const TopologyWrapper& topologyW,
      const thrift::StartNetworkTopologyScan& req);

  /** Initialize structures for a link discovery scan. */
  void initLinkDiscoveryScan(
      const TopologyWrapper& topologyW,
      const thrift::StartLinkDiscoveryScan& req);

  /**
   * Reset structures from the current scan, if any.
   *
   * If "eraseLogs" is set, all scan records will be cleared as well.
   */
  void resetNetworkTopologyScan(bool eraseLogs = false);

  /** Returns the current `thrift::StartNetworkTopologyScan` request. */
  thrift::StartNetworkTopologyScan getNetworkTopologyScanReq() const;

  /** Returns the network-wide topology scan status. */
  thrift::NetworkTopologyScanStatus getNetworkTopologyScanStatus() const;

  /** Returns the link discovery scan status. */
  thrift::LinkDiscoveryScanStatus getLinkDiscoveryScanStatus() const;

  /** Handle a scan result (from a network-wide topology scan). */
  void handleScanResult(const thrift::StartTopologyScanResp& result);

  /**
   * Advance the network-wide topology scan, returning the next action to take.
   */
  Action networkTopologyScanLoop(
      const TopologyWrapper& topologyW,
      const std::unordered_map<std::string /* macAddr */, int64_t>&
          lastStatusReportMap,
      const std::unordered_map<std::string /* nodeName */, int64_t>&
          lastConfigTimeMap);

 private:
  /** A site entry in siteQueue_ for network-wide topology scans. */
  struct SiteQueueEntry {
    /** This site. */
    thrift::Site site;

    /** The links to other sites that need to be formed. */
    std::unordered_set<std::string> siteLinks;

    /**
     * The radio MACs of nodes on this site, along with the number of scans
     * initiated.
     */
    std::deque<std::pair<std::string, size_t /* scanCount */>> siteRadios;

    /** The radio MAC currently running a topology scan (if any). */
    std::string currentScanRadio;

    /** The topology scan responses per radio (txNode -> all scan responses). */
    std::unordered_map<
        std::string, std::vector<thrift::StartTopologyScanResp>> responses;

    /** Whether this site can contain y-street nodes (default 'no'). */
    bool yStreetAllowed = false;

    /** Number of links from this site to CN sites. */
    size_t cnSiteLinks = 0;
  };

  /** Container storing number of links by type. */
  struct LinkCount {
    /** Number of DN links. */
    size_t dnLinks = 0;
    /** Number of CN links. */
    size_t cnLinks = 0;
  };

  /**
   * Link quality metric for determining the "best" link.
   *
   * link quality := SNR - (penalty * combined beam angle)
   */
  class LinkQuality {
   public:
    /** Empty constructor. */
    LinkQuality() : quality(DBL_MIN), combinedAngle(0.0) {}

    /** Constructor. */
    LinkQuality(const thrift::TopologyScanInfo& info, double beamAnglePenalty) {
      combinedAngle = fabs(info.bestTxAngle) + fabs(info.bestRxAngle);
      quality = info.bestSnr - (beamAnglePenalty * combinedAngle);
    }

    /** Compare by quality first, then break ties with combined angle. */
    inline bool operator<(const LinkQuality& o) const {
      if (quality != o.quality) {
        return quality < o.quality;
      }
      return combinedAngle > o.combinedAngle;
    }

  private:
    /** The link quality value. */
    double quality;

    /** The combined angle, i.e. abs(txAngle) + abs(rxAngle). */
    double combinedAngle;
  };

  /** Map from site name to a responderInfo/txNode pair. */
  using SiteToRespondersMap = std::unordered_map<
      std::string /* siteName */,
      std::vector<
          std::pair<thrift::TopologyScanInfo, std::string /* txNode */>>>;

  /**
   * Convert a link quality metric (LQM) to signal-to-noise ratio (SNR), in dB.
   */
  static double lqmToSnr(size_t lqm);

  /** Convert a beam index to beam angle, in degrees. */
  static double beamIndexToAngle(size_t beamIdx);

  /** Process all results for a site and fills out sites/links to add. */
  void processSiteResults(
      const TopologyWrapper& topologyW,
      SiteQueueEntry& siteEntry,
      Action& action);

  /**
   * Add a new node to "action.newNodes" and increments siteIdx, if not already
   * present in "action" or the current topology.
   *
   * Returns the newly created or existing node object.
   */
  thrift::Node addNewNode(
      const TopologyWrapper& topologyW,
      Action& action,
      const std::string& macAddr,
      const std::vector<std::string>& wlanMacAddrs,
      const std::string& site,
      const bool isCnSite,
      int& siteIdx);

  /**
   * Add a new link to "action.newLinks".
   *
   * Returns the newly created link object.
   */
  thrift::Link addNewLink(
      Action& action,
      const std::string& node1Name,
      const std::string& node1Mac,
      const std::string& node2Name,
      const std::string& node2Mac);

  /**
   * Remove any site links from siteEntry that already exist in the topology.
   *
   * Returns a map of each site radio to the number of existing links it has.
   */
  std::unordered_map<std::string, TopologyBuilder::LinkCount> cleanUpSiteLinks(
      const TopologyWrapper& topologyW, SiteQueueEntry& siteEntry) const;

  /** Return the number of wireless links (by type) from radioMac. */
  LinkCount countRadioLinks(
      const TopologyWrapper& topologyW,
      const std::string& radioMac,
      const std::vector<thrift::Link>& links) const;

  /** Returns a map from site names to responderInfo/txNode pairs. */
  SiteToRespondersMap getSiteToRespondersMap(
      const TopologyWrapper& topologyW,
      const SiteQueueEntry& siteEntry,
      const Action& action) const;

  /** Returns all site queue entries for the given scan request. */
  std::unordered_map<std::string, SiteQueueEntry> createSiteQueueEntries(
      const TopologyWrapper& topologyW,
      const thrift::StartNetworkTopologyScan& req) const;

  /** Add a given site link to sitesToQueue. */
  void addToSiteQueue(
      const TopologyWrapper& topologyW,
      const thrift::StartNetworkTopologyScan& req,
      std::unordered_map<std::string, TopologyBuilder::SiteQueueEntry>&
          sitesToQueue,
      const std::string& siteName,
      const std::string& oppositeSiteName) const;

  /** Store potential responders found in the link discovery scan. */
  void addLinkDiscoveryResponders(
      const TopologyBuilder::SiteQueueEntry& siteEntry);

  /** The current network-wide topology scan request parameters (if any). */
  thrift::StartNetworkTopologyScan startScanReq_;

  /** Queue of sites to traverse. */
  std::deque<SiteQueueEntry> siteQueue_;

  /** Sites that have yet to be queued. */
  std::unordered_map<std::string /* siteName */, SiteQueueEntry> sitesToQueue_;

  /** The last time networkTopologyScanLoop() was run. */
  int64_t lastUpdateTime_{0};

  /** Sites already completely visited (i.e. removed from siteQueue_). */
  std::vector<std::string> visitedSites_;

  /** Newly-added nodes during the last network-wide topology scan. */
  std::vector<thrift::Node> newNodes_;

  /** Newly-added links during the last network-wide topology scan. */
  std::vector<thrift::Link> newLinks_;

  /** The current link discovery request parameters (if any). */
  thrift::StartLinkDiscoveryScan startLinkDiscoveryScan_;

  /** The current link discovery potential responders map keyed by txNode. */
  std::unordered_map<
      std::string /* txNode */,
      std::vector<thrift::TopologyScanInfo>> linkDiscoveryResponders_;

  /**
   * The current network-wide topology scan responders (if any), filled only
   * when "storeResults" is set.
   *
   * This is a two-level map first keyed on "txNode" (initiator) and then on
   * "addr" (responder).
   */
  std::unordered_map<
      std::string /* txNode */,
      std::unordered_map<std::string /* addr */, thrift::TopologyScanInfo>>
          networkTopologyScanResponders_;

  /** The current scan type. */
  ScanType scanType_;
};

} // namespace terragraph
} // namespace facebook
