/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TopologyBuilder.h"

#include <algorithm>
#include <chrono>
#include <folly/Format.h>
#include <folly/Random.h>
#include <folly/String.h>
#include <random>

#include "e2e/common/MacUtils.h"
#include "e2e/common/TimeUtils.h"

namespace {
// Amount of time that must elapse since last hearing from a node before
// skipping it in a network-wide topology scan (to account for reboots, etc.).
const std::chrono::seconds kNodeAliveWaitTimeout{90};

// Minimum amount of time between when a node is sent new configurations and
// when we receive a status report from it before initiating a scan request on
// the node. This is because the node is potentially offline (e.g. rebooting).
const std::chrono::seconds kConfigChangeWaitTime{3};
}

namespace facebook {
namespace terragraph {

double
TopologyBuilder::lqmToSnr(size_t lqm) {
  return ((double) lqm - 256) / 8;
}

double
TopologyBuilder::beamIndexToAngle(size_t beamIdx) {
  return (beamIdx * 1.5) - 45;
}

thrift::StartTopologyScanResp
TopologyBuilder::processTopologyScanResults(
    const TopologyWrapper& topologyW, const thrift::ScanData& scanData) const {
  thrift::StartTopologyScanResp resp;
  resp.txNode = scanData.txNode;
  if (scanData.responses.size() != 1) {
    return resp;  // there should only be 1 response (i.e. the tx node)
  }
  resp.txPwrIndex_ref() =
      scanData.responses.begin()->second.txPwrIndex_ref().value();
  auto topoResps = scanData.responses.begin()->second.topoResps_ref();
  if (!topoResps.has_value() || !topoResps.value().size()) {
    return resp;  // no replies from nearby nodes
  }

  // Process results from each responder
  for (const auto& pair : topoResps.value()) {
    const thrift::TopoResponderInfo& responderInfo = pair.second;

    thrift::TopologyScanInfo info;
    info.responderInfo = responderInfo;

    // Find the best beam - with the smallest combined angle
    // We only use "itorLqmMat" because it contains more complete information
    // in the current topology scan implementation.
    uint16_t bestLqm = 0;
    double bestTxBeamAngle, bestRxBeamAngle;
    double bestCombinedAngle = 0.0;
    for (const auto& initiatorPair : responderInfo.itorLqmMat) {
      double txBeamAngle = beamIndexToAngle(initiatorPair.first);
      for (const auto& responderPair : initiatorPair.second) {
        double rxBeamAngle = beamIndexToAngle(responderPair.first);
        double combinedAngle = fabs(txBeamAngle) + fabs(rxBeamAngle);
        uint16_t lqm = responderPair.second;
        if (lqm > bestLqm ||
            (lqm == bestLqm && combinedAngle < bestCombinedAngle)) {
          bestLqm = lqm;
          bestTxBeamAngle = txBeamAngle;
          bestRxBeamAngle = rxBeamAngle;
          bestCombinedAngle = combinedAngle;
        }
      }
    }
    if (bestLqm > 0) {
      info.bestSnr = lqmToSnr(bestLqm);
      info.bestTxAngle = bestTxBeamAngle;
      info.bestRxAngle = bestRxBeamAngle;
    }

    // If the responder is already in the topology, set nearestSite accordingly
    if (auto rxNode = topologyW.getNodeByMac(responderInfo.addr)) {
      info.nearestSite = rxNode->site_name;
      info.nearestSiteDistance = 0;
      if (auto site = topologyW.getSite(rxNode->site_name)) {
        info.responderInfo.pos_ref() = site->location;
      }
    } else if (responderInfo.pos_ref().has_value()) {
      // Find the nearest site
      for (const auto& site : topologyW.getAllSites()) {
        thrift::Location responderPos = responderInfo.pos_ref().value();
        if (site.location.altitude == 0) {
          // If the site altitude is unset, also ignore the responder altitude.
          responderPos.altitude = 0;
        }
        double distance = approxDistance(site.location, responderPos);
        if (info.nearestSite.empty() || distance < info.nearestSiteDistance) {
          info.nearestSite = site.name;
          info.nearestSiteDistance = distance;
        }
      }
    }

    resp.responders.push_back(std::move(info));
  }
  return resp;
}

bool
TopologyBuilder::isRunningNetworkTopologyScan() const {
  return !siteQueue_.empty();
}

void
TopologyBuilder::initNetworkTopologyScan(
    const TopologyWrapper& topologyW,
    const thrift::StartNetworkTopologyScan& req) {
  // Validate some request parameters
  if (req.distanceThreshold < 0) {
    throw std::invalid_argument("The distance threshold cannot be negative");
  }
  if (req.scansPerNode < 1) {
    throw std::invalid_argument("The number of scans per node must be > 0");
  }
  for (const std::string& siteName : req.cnSites) {
    if (!topologyW.getSite(siteName)) {
      throw std::invalid_argument(
          folly::sformat("CN site '{}' does not exist", siteName));
    }
  }
  for (const std::string& siteName : req.yStreetSites) {
    if (!topologyW.getSite(siteName)) {
      throw std::invalid_argument(
          folly::sformat("Y-street site '{}' does not exist", siteName));
    }
  }
  std::unordered_set<std::string> standardizedMacAddrs;
  for (const std::string& macAddr : req.macAddrs) {
    // Try to standardize MAC address, or throw exception otherwise
    standardizedMacAddrs.insert(MacUtils::standardizeMac(macAddr));
  }

  // Create site queue entries
  auto siteQueueEntries = createSiteQueueEntries(topologyW, req);

  // No validation occurs after this point -- erase the previous scan logs
  resetNetworkTopologyScan(true /* eraseLogs */);

  // Move initial site entries to the queue (i.e. any site with nodes)
  sitesToQueue_ = std::move(siteQueueEntries);
  for (auto iter = sitesToQueue_.begin(); iter != sitesToQueue_.end();) {
    if (!iter->second.siteRadios.empty()) {
      siteQueue_.push_back(std::move(iter->second));
      iter = sitesToQueue_.erase(iter);
    } else {
      iter++;
    }
  }

  VLOG(2) << "Initiated network topology scan with " << siteQueue_.size()
          << " initial site(s) queued and " << sitesToQueue_.size()
          << " site(s) to discover";

  startScanReq_ = req;
  startScanReq_.macAddrs = standardizedMacAddrs;
  scanType_ = TopologyBuilder::ScanType::NETWORK_TOPOLOGY;
}

void
TopologyBuilder::initLinkDiscoveryScan(
  const TopologyWrapper& topologyW,
  const thrift::StartLinkDiscoveryScan& req) {
  if (req.scanRadius < 1) {
    throw std::invalid_argument("The scan radius must be >= 1");
  }
  // Try to standardize MAC address, or throw exception otherwise
  const auto targetMac = MacUtils::standardizeMac(req.targetMac);
  const auto node = topologyW.getNodeByMac(targetMac);
  if (!node) {
    throw std::invalid_argument(
      folly::sformat("Node with MAC '{}' does not exist", targetMac));
  }
  // No validation occurs after this point -- erase the previous scan logs
  resetNetworkTopologyScan(true /* eraseLogs */);

  // find all potential initiators within the given scan radius
  const auto responderSite = topologyW.getSite(node->site_name);
  auto scanRadius = req.scanRadius;
  for (const auto& site : topologyW.getAllSites()) {
    if (approxDistance(site.location, responderSite->location) > scanRadius ||
        site.name == responderSite->name) {
      continue;
    }
    TopologyBuilder::SiteQueueEntry siteEntry;
    siteEntry.site = site;
    siteEntry.siteLinks.insert(site.name);
    for (const auto& siteNode : topologyW.getNodesBySiteName(site.name)) {
      if (siteNode.node_type == thrift::NodeType::CN) {
        continue;
      }

      // Add each radio if it does not already have the max # of links
      std::vector<thrift::Link> nodeLinks =
          topologyW.getWirelessLinksByNodeName(siteNode.name);
      for (const std::string& radioMac : siteNode.wlan_mac_addrs) {
        size_t linkCount = 0;
        for (const thrift::Link& link : nodeLinks) {
          if (link.a_node_mac == radioMac || link.z_node_mac == radioMac) {
            linkCount++;
          }
        }
        if (linkCount < TopologyWrapper::kMaxNumOfDnCnLinks) {
          // Add radio entry
          siteEntry.siteRadios.push_back({radioMac, 0});
        }
      }
    }
    if (!siteEntry.siteRadios.empty()) {
      siteQueue_.push_back(std::move(siteEntry));
    }
  }

  VLOG(2) << "Initiated link discovery scan for " << node->name
          << " with " << siteQueue_.size() << " site(s) queued";

  startLinkDiscoveryScan_ = req;
  startLinkDiscoveryScan_.targetMac = targetMac;
  scanType_ = TopologyBuilder::ScanType::LINK_DISCOVERY;
}

std::unordered_map<std::string, TopologyBuilder::SiteQueueEntry>
TopologyBuilder::createSiteQueueEntries(
    const TopologyWrapper& topologyW,
    const thrift::StartNetworkTopologyScan& req) const {
  if (req.siteLinks.empty()) {
    throw std::invalid_argument("No links between sites were specified");
  }

  std::unordered_map<std::string, TopologyBuilder::SiteQueueEntry> sitesToQueue;
  for (const thrift::SiteLink& siteLink : req.siteLinks) {
    if (siteLink.aSite == siteLink.zSite) {
      throw std::invalid_argument(folly::sformat(
          "Cannot form a link between same aSite and zSite '{}'",
          siteLink.aSite));
    }
    addToSiteQueue(
        topologyW, req, sitesToQueue, siteLink.aSite, siteLink.zSite);
    addToSiteQueue(
        topologyW, req, sitesToQueue, siteLink.zSite, siteLink.aSite);
  }
  return sitesToQueue;
}

void
TopologyBuilder::addToSiteQueue(
    const TopologyWrapper& topologyW,
    const thrift::StartNetworkTopologyScan& req,
    std::unordered_map<std::string, TopologyBuilder::SiteQueueEntry>&
        sitesToQueue,
    const std::string& siteName,
    const std::string& oppositeSiteName) const {
  // Validate the site
  auto site = topologyW.getSite(siteName);
  if (!site) {
    throw std::invalid_argument(
        folly::sformat("Site '{}' does not exist", siteName));
  }

  // Initialize structs if needed
  auto& siteEntry = sitesToQueue[siteName];
  if (siteEntry.siteLinks.empty()) {
    siteEntry.site = site.value();
    for (const thrift::Node& node : topologyW.getNodesBySiteName(siteName)) {
      for (const std::string& radioMac : node.wlan_mac_addrs) {
        siteEntry.siteRadios.push_back({radioMac, 0});
      }
    }
    siteEntry.yStreetAllowed = req.yStreetSites.count(siteName);
  }

  // Add the site link
  if (!siteEntry.siteLinks.count(oppositeSiteName)) {
    siteEntry.siteLinks.insert(oppositeSiteName);
    if (req.cnSites.count(oppositeSiteName)) {
      siteEntry.cnSiteLinks++;
    }
  }
}

void
TopologyBuilder::resetNetworkTopologyScan(bool eraseLogs) {
  startScanReq_ = thrift::StartNetworkTopologyScan();
  startLinkDiscoveryScan_ = thrift::StartLinkDiscoveryScan();
  siteQueue_ = {};
  sitesToQueue_ = {};
  if (eraseLogs) {
    lastUpdateTime_ = 0;
    visitedSites_ = {};
    newNodes_ = {};
    newLinks_ = {};
    networkTopologyScanResponders_ = {};
    linkDiscoveryResponders_ = {};
  }
}

void
TopologyBuilder::handleScanResult(const thrift::StartTopologyScanResp& result) {
  if (!isRunningNetworkTopologyScan()) {
    LOG(ERROR) << "Ignoring topology scan result (no network scan in progress)";
    return;
  }

  // Validate result
  auto& siteEntry = siteQueue_.front();
  if (result.txNode != siteEntry.currentScanRadio) {
    LOG(ERROR) << "Scan result for txNode " << result.txNode
               << " is not valid for the current site " << siteEntry.site.name;
    return;
  }

  // Add the result to the current site entry
  siteEntry.responses[result.txNode].push_back(result);
  siteEntry.currentScanRadio.clear();

  // Store a copy of results (if needed)
  if (startScanReq_.storeResults) {
    for (const thrift::TopologyScanInfo& info : result.responders) {
      networkTopologyScanResponders_[result.txNode][info.responderInfo.addr] =
          info;
    }
  }
}

TopologyBuilder::Action
TopologyBuilder::networkTopologyScanLoop(
    const TopologyWrapper& topologyW,
    const std::unordered_map<std::string, int64_t>& lastStatusReportMap,
    const std::unordered_map<std::string, int64_t>& lastConfigTimeMap) {
  TopologyBuilder::Action action;
  if (!isRunningNetworkTopologyScan()) {
    return action;
  }

  VLOG(3) << "................ processing topology scan ................";
  lastUpdateTime_ = std::time(nullptr);
  std::default_random_engine rng(folly::Random::rand32());

  // Find a valid site in the queue
  const int queueSize = siteQueue_.size();
  for (int i = 0; i < queueSize; i++) {
    auto& siteEntry = siteQueue_.front();

    // If no more site links are needed (e.g. added in prior step), discard site
    if (siteEntry.siteLinks.empty()) {
      VLOG(2) << "Discarding site " << siteEntry.site.name
              << " (no more site links needed)";
      visitedSites_.push_back(siteEntry.site.name);
      siteQueue_.pop_front();
      continue;
    }

    // Shuffle all site radios to decrease the likelihood of repeated transient
    // failures (why not?)
    std::shuffle(siteEntry.siteRadios.begin(), siteEntry.siteRadios.end(), rng);

    // Find an online node that still needs to initiate scans
    std::optional<thrift::Node> nextNode;
    std::string nextNodeMac;
    size_t nextNodeScanCount = 0;
    int64_t lastOnlineTs = 0;
    size_t remainingRadiosToScan = 0;
    for (auto iter = siteEntry.siteRadios.begin();
         iter != siteEntry.siteRadios.end();) {
      const std::string& radioMac = iter->first;
      size_t& scanCount = iter->second;

      // Has this node already initiated enough scans?
      if (static_cast<int>(scanCount) >= startScanReq_.scansPerNode) {
        iter++;
        continue;
      }

      auto node = topologyW.getNodeByMac(radioMac);
      if (!node) {
        // Node doesn't exist anymore, so remove it
        VLOG(2) << "Removing MAC " << radioMac << " from site entry "
                << siteEntry.site.name << " (MAC no longer exists)";
        iter = siteEntry.siteRadios.erase(iter);
        continue;
      } else {
        iter++;

        // Skip CN nodes (no need to scan from CNs)
        if (node->node_type == thrift::NodeType::CN) {
          continue;
        }
        remainingRadiosToScan++;

        // Is the node offline?
        // Since the topology state isn't updated frequently, we also look at
        // the last config time and status reports to determine if a node is
        // potentially restarting due to a config change
        auto statusTimeIter = lastStatusReportMap.find(node->mac_addr);
        auto configTimeIter = lastConfigTimeMap.find(node->name);
        if (node->status == thrift::NodeStatusType::OFFLINE ||
            (statusTimeIter != lastStatusReportMap.end() &&
             configTimeIter != lastConfigTimeMap.end() &&
             statusTimeIter->second - configTimeIter->second <
                kConfigChangeWaitTime.count())) {
          // Record the last time we heard from a node on this site
          if (statusTimeIter != lastStatusReportMap.end() &&
              statusTimeIter->second > lastOnlineTs) {
            lastOnlineTs = statusTimeIter->second;
          }
          continue;
        }

        // Node is online, so use it
        nextNode = node;
        nextNodeMac = radioMac;
        nextNodeScanCount = ++scanCount;
        break;
      }
    }

    if (nextNode) {
      // Send next scan request to nextNode
      // TODO Need to scan at both max and min power due to SNR saturation
      VLOG(2) << folly::format(
          "Sending topology scan request #{} to txNode {} : {} (on site {})",
          nextNodeScanCount, nextNodeMac, nextNode->name, siteEntry.site.name);
      siteEntry.currentScanRadio = nextNodeMac;
      action.type = TopologyBuilder::ActionType::SCAN;
      action.txNode = nextNodeMac;
      return action;
    } else {
      // No online nodes remaining
      if (siteEntry.responses.empty()) {
        if (remainingRadiosToScan > 0 &&
            scanType_ == TopologyBuilder::ScanType::NETWORK_TOPOLOGY) {
          // No scan results yet - requeue the site
          VLOG(2) << "Requeueing site " << siteEntry.site.name
                  << " (no nodes online, but " << remainingRadiosToScan
                  << " radios left to scan)";
          auto siteEntryCopy = siteEntry;
          siteQueue_.pop_front();
          siteQueue_.push_back(siteEntryCopy);
          continue;
        } else {
          // This site had no valid nodes to scan (e.g. all CNs) - we're done
          VLOG(2) << "Discarding site " << siteEntry.site.name
                  << " (no nodes to scan)";
          visitedSites_.push_back(siteEntry.site.name);
          siteQueue_.pop_front();
          continue;
        }
      } else {
        // We already have scan results for this site...
        auto now = TimeUtils::getSteadyTimestamp();
        if (now - lastOnlineTs < kNodeAliveWaitTimeout.count()) {
          // If any node was recently online (e.g. rebooting from config), wait
          VLOG(2) << "Unscanned node from site " << siteEntry.site.name
                  << " was last seen " << (now - lastOnlineTs)
                  << "s ago, waiting up to " << kNodeAliveWaitTimeout.count()
                  << "s for it to come back online...";
          action.type = TopologyBuilder::ActionType::WAIT;
          return action;
        } else {
          // Process the results from this site
          VLOG(2) << "Processing results from " << siteEntry.responses.size()
                  << " radio(s) on site " << siteEntry.site.name;
          auto siteEntryCopy = siteEntry;
          siteQueue_.pop_front();
          if (scanType_ == TopologyBuilder::ScanType::NETWORK_TOPOLOGY) {
            processSiteResults(topologyW, siteEntryCopy, action);

            // If all site links are found, we're done - otherwise requeue it
            if (siteEntryCopy.siteLinks.empty()) {
              VLOG(2) << "Finished with site " << siteEntryCopy.site.name
                      << " (all site links formed)";
              visitedSites_.push_back(siteEntryCopy.site.name);
            } else {
              VLOG(2) << "Requeueing site " << siteEntryCopy.site.name
                      << " (" << siteEntryCopy.siteLinks.size()
                      << " site links left to form)";
              // Clear scan data
              siteEntryCopy.responses = {};
              for (auto& pair : siteEntryCopy.siteRadios) {
                pair.second = 0;
              }
              siteQueue_.push_back(siteEntryCopy);
            }
          } else if (scanType_ == TopologyBuilder::ScanType::LINK_DISCOVERY) {
            addLinkDiscoveryResponders(siteEntryCopy);
          }
          continue;
        }
      }
    }
  }

  // If queue is empty, we're done
  if (siteQueue_.size() == 0) {
    VLOG(2) << "Network-wide topology scans finished";
    resetNetworkTopologyScan();
    action.type = TopologyBuilder::ActionType::FINISH;
    return action;
  }

  // Otherwise, nothing can be done right now, so wait
  VLOG(2) << "Waiting - " << siteQueue_.size() << " site(s) remaining...";
  action.type = TopologyBuilder::ActionType::WAIT;
  return action;
}

void
TopologyBuilder::addLinkDiscoveryResponders(
    const TopologyBuilder::SiteQueueEntry& siteEntry) {
  const auto targetMac = startLinkDiscoveryScan_.targetMac;
  for (const auto& kv : siteEntry.responses) {
    const std::string& txNode = kv.first;
    for (const auto& scanResp : kv.second) {
      for (const auto& responder : scanResp.responders) {
        if (responder.responderInfo.addr != targetMac) {
          continue;
        }
        linkDiscoveryResponders_[txNode].push_back(responder);
      }
    }
  }
}

void
TopologyBuilder::processSiteResults(
    const TopologyWrapper& topologyW,
    TopologyBuilder::SiteQueueEntry& siteEntry,
    TopologyBuilder::Action& action) {
  // Remove any site links that already exist in the topology
  // Also, count the number of DN/CN links from each txNode
  auto linkCountMap = cleanUpSiteLinks(topologyW, siteEntry);
  if (siteEntry.siteLinks.empty()) {
    VLOG(2) << "No site links needed for site " << siteEntry.site.name;
    return;
  }
  VLOG(2) << "Searching for link(s) from site " << siteEntry.site.name
          << " to: <" << folly::join(", ", siteEntry.siteLinks) << ">";

  // Group all responders by site:
  // {site1: [(TopologyScanInfo, txNode), ...], site2: ...}
  auto siteToResponders = getSiteToRespondersMap(topologyW, siteEntry, action);
  if (siteToResponders.empty()) {
    VLOG(2) << "No valid responders found";
    return;
  }
  VLOG(2) << "Found " << siteToResponders.size() << " valid responder site(s)";

  // Find the best link to each responder site
  // This picks the highest SNR link among all sites (applying a penalty based
  // on beam angles), adds it, then repeats until no sites are left.
  //
  // Beam angles are penalized because properly-aligned links should normally be
  // boresight. However, P2MP nodes will not form boresight links, so we do not
  // penalize any nodes on possibly-P2MP sites (based on the request inputs).
  //
  // TODO: For unguided discovery of the globally optimal links, this loop
  //       should only add ONE link instead of all possible links.
  bool isMaybeP2MP = siteEntry.yStreetAllowed || siteEntry.cnSiteLinks > 1;
  while (!siteToResponders.empty()) {
    TopologyBuilder::LinkQuality bestLinkQuality;
    std::string site;
    thrift::TopologyScanInfo info;
    std::string txNode;
    for (const auto& siteAndResponders : siteToResponders) {
      const std::string& curSite = siteAndResponders.first;
      for (const auto& pair : siteAndResponders.second) {
        const thrift::TopologyScanInfo& curInfo = pair.first;
        const std::string& curTxNode = pair.second;

        // Check if txNode can form any more links
        TopologyBuilder::LinkCount& linkCount = linkCountMap[curTxNode];
        if (startScanReq_.cnSites.count(curSite)) {
          if (linkCount.cnLinks >= TopologyWrapper::kMaxNumOfDnCnLinks) {
            continue;  // exceeded max DN-to-CN links
          }
        } else {
          if (linkCount.dnLinks >= TopologyWrapper::kMaxNumOfDnDnLinks) {
            continue;  // exceeded max DN-to-DN links
          }
          if (linkCount.dnLinks >= 1 && !siteEntry.yStreetAllowed) {
            continue;  // don't allow y-street unless specified
          }
        }

        VLOG(3) << folly::format(
            "... site {}: txNode={}, responder={} "
            "(SNR={:.2f}dB, txAngle={:.2f}, rxAngle={:.2f})",
            curSite, curTxNode, curInfo.responderInfo.addr,
            curInfo.bestSnr, curInfo.bestTxAngle, curInfo.bestRxAngle);

        // Is this higher than the best quality link we've seen?
        TopologyBuilder::LinkQuality linkQuality(
            curInfo, isMaybeP2MP ? 0.0 : startScanReq_.beamAnglePenalty);
        if (bestLinkQuality < linkQuality) {
          bestLinkQuality = linkQuality;
          site = curSite;
          info = curInfo;
          txNode = curTxNode;
        }
      }
    }
    if (site.empty()) {
      break;  // no responding sites left
    }

    std::string txNodeName = topologyW.getNodeNameByMac(txNode).value_or("");
    VLOG(2) << folly::format(
        "Adding site link from {} (txNode={}, name='{}') to "
        "{} (responder={}, SNR={:.2f}dB, distance={:.2f}m)",
         siteEntry.site.name, txNode, txNodeName,
         site, info.responderInfo.addr, info.bestSnr, info.nearestSiteDistance);

    // Filter adjacencies
    std::vector<std::string> adjs;
    for (const std::string& adjMacAddr : info.responderInfo.adjs) {
      if (!startScanReq_.macAddrs.empty() &&
          !startScanReq_.macAddrs.count(adjMacAddr)) {
        VLOG(3) << folly::format(
            "Skipping adjacency {} (unexpected MAC address)", adjMacAddr);
        continue;  // filtered by MAC address
      }
      adjs.push_back(adjMacAddr);
    }

    // Add responder node
    const std::string& macAddr = info.responderInfo.addr;
    const bool isCnSite = startScanReq_.cnSites.count(site);
    int siteIdx = 1;
    thrift::Node responderNode;
    if (startScanReq_.mergeAdjMacs) {
      // Add all MAC addresses (responder and adjacencies) into wlan_mac_addrs
      // (for multi-radio nodes, e.g. Puma)
      std::vector<std::string> wlanMacAddrs{macAddr};
      wlanMacAddrs.insert(wlanMacAddrs.end(), adjs.begin(), adjs.end());
      // Look for an existing node ID in the topology...
      std::string nodeId;
      for (const std::string& mac : wlanMacAddrs) {
        if (auto maybeNode = topologyW.getNodeByMac(mac)) {
          nodeId = maybeNode->mac_addr;
          break;
        }
      }
      responderNode = addNewNode(
          topologyW, action, nodeId, wlanMacAddrs, site, isCnSite, siteIdx);
    } else {
      // Use responder MAC as node ID, and add all wired adjacencies as separate
      // nodes on the same site (for single-radio nodes)
      responderNode = addNewNode(
          topologyW, action, macAddr, {}, site, isCnSite, siteIdx);
      for (const std::string& adjMacAddr : adjs) {
        // TODO respect TopologyWrapper::kMaxNumOfNodesPerSite (how?)
        addNewNode(
            topologyW, action, adjMacAddr, {}, site, isCnSite, siteIdx);
      }
    }

    // Add link from txNode to responder node
    addNewLink(action, txNodeName, txNode, responderNode.name, macAddr);
    if (isCnSite) {
      linkCountMap[txNode].cnLinks++;
    } else {
      linkCountMap[txNode].dnLinks++;
    }

    // Move new site from sitesToQueue_ to siteQueue_
    // (Note that these won't get processed this loop iteration)
    auto iter = sitesToQueue_.find(site);
    if (iter != sitesToQueue_.end()) {
      TopologyBuilder::SiteQueueEntry& responderSiteEntry = iter->second;
      for (const auto& kv : action.newNodes) {
        if (kv.second.site_name == site) {
          responderSiteEntry.siteRadios.push_back({kv.first, 0});
        }
      }
      siteQueue_.push_back(std::move(responderSiteEntry));
      sitesToQueue_.erase(iter);
    }

    // Remove this responder site entry
    siteEntry.siteLinks.erase(site);
    siteToResponders.erase(site);

    // Remove site link from responder site entry
    for (TopologyBuilder::SiteQueueEntry& otherSiteEntry : siteQueue_) {
      if (otherSiteEntry.site.name == site) {
        otherSiteEntry.siteLinks.erase(siteEntry.site.name);
      }
    }
  }
}

thrift::Node
TopologyBuilder::addNewNode(
    const TopologyWrapper& topologyW,
    TopologyBuilder::Action& action,
    const std::string& nodeId,
    const std::vector<std::string>& wlanMacAddrs,
    const std::string& site,
    const bool isCnSite,
    int& siteIdx) {
  thrift::Node node;
  auto iter = action.newNodes.find(nodeId);
  if (iter != action.newNodes.end()) {
    // Responder node was previously added in same loop iteration
    node = iter->second;
  } else if (auto maybeNode = topologyW.getNodeByMac(nodeId)) {
    // Responder node already present in topology
    node = maybeNode.value();
  } else {
    // Add responder node
    do {
      node.name = folly::sformat("{}.{}", site, siteIdx++);
    } while (topologyW.getNode(node.name));  // loop in case name is taken
    node.mac_addr = nodeId;  // NOTE: might be empty!
    node.wlan_mac_addrs = wlanMacAddrs;
    node.site_name = site;
    node.node_type = isCnSite ? thrift::NodeType::CN : thrift::NodeType::DN;
    node.pop_node = false;  // Assume non-POP

    VLOG(2) << folly::format(
        "Adding new node '{}' (mac_addr: '{}') to site '{}'",
        node.name, node.mac_addr, site);
    action.newNodes[nodeId] = node;
    newNodes_.push_back(node);
  }
  return node;
}

thrift::Link
TopologyBuilder::addNewLink(
    TopologyBuilder::Action& action,
    const std::string& node1Name,
    const std::string& node1Mac,
    const std::string& node2Name,
    const std::string& node2Mac) {
  thrift::Link link;
  link.link_type = thrift::LinkType::WIRELESS;
  link.a_node_name = node1Name;
  link.a_node_mac = node1Mac;
  link.z_node_name = node2Name;
  link.z_node_mac = node2Mac;
  TopologyWrapper::updateLink(link);

  VLOG(2) << folly::format(
      "Adding new link: {} ({}-{})",
      link.name, link.a_node_mac, link.z_node_mac);

  action.newLinks.push_back(link);
  newLinks_.push_back(link);
  return link;
}

std::unordered_map<std::string, TopologyBuilder::LinkCount>
TopologyBuilder::cleanUpSiteLinks(
    const TopologyWrapper& topologyW,
    TopologyBuilder::SiteQueueEntry& siteEntry) const {
  // Find all neighbor sites (e.g. connected via wireless links)
  std::unordered_map<std::string, TopologyBuilder::LinkCount> linkCountMap;
  for (const auto& node : topologyW.getNodesBySiteName(siteEntry.site.name)) {
    std::vector<thrift::Link> links = topologyW.getLinksByNodeName(node.name);
    for (const auto& link : links) {
      if (link.link_type != thrift::LinkType::WIRELESS) {
        continue;  // skip wired links
      }
      const auto& nbrNode = topologyW.getNbrNode(node.name, link);
      if (!nbrNode) {
        continue;  // shouldn't happen
      }

      // Remove site link from entry if it already exists
      auto iter = siteEntry.siteLinks.find(nbrNode->site_name);
      if (iter != siteEntry.siteLinks.end()) {
        VLOG(3) << "Site link from site " << siteEntry.site.name << " to site "
                << nbrNode->site_name << " already exists (" << link.name
                << "), skipping...";
        siteEntry.siteLinks.erase(iter);
      }
    }

    // Count number of links from each radio
    for (const std::string& radioMac : node.wlan_mac_addrs) {
      linkCountMap[radioMac] = countRadioLinks(topologyW, radioMac, links);
    }
  }
  return linkCountMap;
}

TopologyBuilder::LinkCount
TopologyBuilder::countRadioLinks(
    const TopologyWrapper& topologyW,
    const std::string& radioMac,
    const std::vector<thrift::Link>& links) const {
  TopologyBuilder::LinkCount linkCount;
  for (const auto& link : links) {
    if (link.link_type != thrift::LinkType::WIRELESS) {
      continue;  // skip wired links
    }

    // Get neighbor node
    std::string nbrNodeName;
    if (link.a_node_mac == radioMac) {
      nbrNodeName = link.z_node_name;
    } else if (link.z_node_mac == radioMac) {
      nbrNodeName = link.a_node_name;
    } else {
      continue;  // different radio
    }
    const auto& nbrNode = topologyW.getNode(nbrNodeName);
    if (!nbrNode) {
      continue;  // shouldn't happen
    }

    // Increment number of links (by type)
    if (nbrNode->node_type == thrift::NodeType::DN) {
      linkCount.dnLinks++;
    } else if (nbrNode->node_type == thrift::NodeType::CN) {
      if (!link.is_backup_cn_link_ref().has_value() ||
          !link.is_backup_cn_link_ref().value()) {
        linkCount.cnLinks++;
      }
    }
  }
  return linkCount;
}

TopologyBuilder::SiteToRespondersMap
TopologyBuilder::getSiteToRespondersMap(
    const TopologyWrapper& topologyW,
    const TopologyBuilder::SiteQueueEntry& siteEntry,
    const TopologyBuilder::Action& action) const {
  // Loop over all txNode -> scan result entries...
  TopologyBuilder::SiteToRespondersMap siteToResponders;
  for (const auto& pair : siteEntry.responses) {
    const std::string& txNode = pair.first;
    for (const thrift::StartTopologyScanResp& response : pair.second) {
      for (const thrift::TopologyScanInfo& info : response.responders) {
        const std::string& macAddr = info.responderInfo.addr;

        // Check if this responder is valid
        if (info.nearestSite.empty()) {
          VLOG(3) << folly::format(
              "... skipping responder {} for txNode {} "
              "(no location info reported)",
              macAddr, txNode);
          continue;  // no location (i.e. "responderInfo.pos" likely omitted)
        }
        if (!siteEntry.siteLinks.count(info.nearestSite)) {
          VLOG(3) << folly::format(
              "... skipping responder {} for txNode {} "
              "(no link between site {} and responder site {})",
              macAddr, txNode, siteEntry.site.name, info.nearestSite);
          continue;  // not in site links
        }
        if (!startScanReq_.macAddrs.empty() &&
            !startScanReq_.macAddrs.count(macAddr)) {
          VLOG(3) << folly::format(
              "... skipping responder {} for txNode {} "
              "(unexpected MAC address)",
              macAddr, txNode);
          continue;  // filtered by MAC address
        }
        if (info.nearestSiteDistance > startScanReq_.distanceThreshold) {
          // TODO Use site/responder accuracy?
          VLOG(3) << folly::format(
              "... skipping responder {} for txNode {} "
              "({:.2f}m from nearest site {}, threshold is {:.2f}m)",
              macAddr,
              txNode,
              info.nearestSiteDistance,
              info.nearestSite,
              startScanReq_.distanceThreshold);
          continue;  // further than max distance
        }
        if (info.bestSnr < startScanReq_.snrThreshold) {
          VLOG(3) << folly::format(
              "... skipping responder {} for txNode {} "
              "({:.2f}dB SNR is too low, threshold is {:.2f}dB)",
              macAddr, txNode, info.bestSnr, startScanReq_.snrThreshold);
          continue;  // weaker than min SNR
        }
        if (auto maybeTopoNode = topologyW.getNodeByMac(macAddr)) {
          const auto& wlanMacs = maybeTopoNode->wlan_mac_addrs;
          // Is this the same node as txNode?
          if (maybeTopoNode->mac_addr == txNode ||
              std::find(
                  wlanMacs.begin(), wlanMacs.end(), txNode) != wlanMacs.end()) {
            VLOG(3) << folly::format(
                "... skipping responder {} for txNode {} (same node: {})",
                macAddr, txNode, maybeTopoNode->name);
            continue;  // responder is another radio on txNode
          }
          // If MAC is already in the topology, discard unless site matches
          if (maybeTopoNode->site_name != info.nearestSite) {
            VLOG(3) << folly::format(
                "... skipping responder {} for txNode {} "
                "(MAC already in topology on site {}, not reported site {})",
                macAddr, txNode, maybeTopoNode->site_name, info.nearestSite);
            continue;  // inconsistent with existing node with same MAC
          }
          // Did we hit the max number of links already? (assume txNode is DN)
          TopologyBuilder::LinkCount linkCount = countRadioLinks(
              topologyW,
              macAddr,
              topologyW.getLinksByNodeName(maybeTopoNode->name));
          bool hasMaxLinks = false;
          if (maybeTopoNode->node_type == thrift::NodeType::CN) {
            if (linkCount.dnLinks >= 1) {
              hasMaxLinks = true;  // already has a primary CN-to-DN link
            }
          } else if (maybeTopoNode->node_type == thrift::NodeType::DN) {
            if (linkCount.dnLinks >= TopologyWrapper::kMaxNumOfDnDnLinks) {
              hasMaxLinks = true;  // already has max DN-to-DN links
            } else if (linkCount.dnLinks >= 1 &&
                !startScanReq_.yStreetSites.count(maybeTopoNode->site_name)) {
              hasMaxLinks = true;  // don't allow y-street unless specified
            }
          }
          if (hasMaxLinks) {
            VLOG(3) << folly::format(
                "... skipping responder {} for txNode {} "
                "(responder {} already has max links defined)",
                macAddr, txNode, maybeTopoNode->name);
            continue;
          }
        }
        auto iter = action.newNodes.find(macAddr);
        if (iter != action.newNodes.end()) {
          // Discard if MAC was newly added already to a different site
          VLOG(3) << folly::format(
              "... skipping responder {} for txNode {} "
              "(responder already added on site {}, not reported site {})",
              macAddr, txNode, iter->second.site_name, info.nearestSite);
          continue;  // inconsistent with previous info for same responder (??)
        }

        // Add responder to list
        siteToResponders[info.nearestSite].push_back({info, txNode});
      }
    }
  }
  return siteToResponders;
}

thrift::StartNetworkTopologyScan
TopologyBuilder::getNetworkTopologyScanReq() const {
  return startScanReq_;
}

thrift::NetworkTopologyScanStatus
TopologyBuilder::getNetworkTopologyScanStatus() const {
  thrift::NetworkTopologyScanStatus status;
  status.active = isRunningNetworkTopologyScan();
  status.lastUpdateTime = lastUpdateTime_;
  if (isRunningNetworkTopologyScan()) {
    if (!siteQueue_.empty()) {
      status.currentScanRadio = siteQueue_.front().currentScanRadio;
    }
    for (const TopologyBuilder::SiteQueueEntry& siteEntry : siteQueue_) {
      status.queuedSites.push_back(siteEntry.site.name);
    }
    for (const auto& kv : sitesToQueue_) {
      status.emptySites.push_back(kv.first);
    }
  }
  status.visitedSites = visitedSites_;
  status.newNodes = newNodes_;
  status.newLinks = newLinks_;
  status.responses = networkTopologyScanResponders_;
  return status;
}

thrift::LinkDiscoveryScanStatus
TopologyBuilder::getLinkDiscoveryScanStatus() const {
  thrift::LinkDiscoveryScanStatus status;
  status.active = isRunningNetworkTopologyScan();
  status.lastUpdateTime = lastUpdateTime_;
  status.responders = linkDiscoveryResponders_;
  return status;
}

} // namespace terragraph
} // namespace facebook
