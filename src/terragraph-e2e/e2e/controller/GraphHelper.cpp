/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "GraphHelper.h"

namespace facebook {
namespace terragraph {

namespace {
// Fake root vertex site name
const std::string kFakeRootSiteName{""};
}

void
GraphHelper::buildGraph(const TopologyWrapper& topologyW) {
  for (const auto& site : topologyW.getAllSites()) {
    auto& siteNodeNames = siteName2nodeNames_[site.name];
    bool updateCandidateSite = false;
    bool addSite = false;
    for (const auto& node : topologyW.getNodesBySiteName(site.name)) {
      if (node.status != thrift::NodeStatusType::OFFLINE) {
        addSite = true;
        siteNodeNames.insert(node.name);
        if (nodesPendingUpdate_.count(node.name)) {
          updateCandidateSite = true;
        }
        if (node.pop_node) {
          popSiteNames_.insert(site.name);
        }
      }
    }
    if (addSite) {
      addVertex(site.name);
    }

    if (updateCandidateSite) {
      sitesPendingUpdate_.insert(site.name);
    }
  }

  // Add links
  for (const auto& link : topologyW.getAllLinks()) {
    if (link.is_alive) {
      auto aNode = topologyW.getNode(link.a_node_name);
      auto zNode = topologyW.getNode(link.z_node_name);
      if (aNode && zNode) {
        addEdge(aNode->site_name, zNode->site_name);
      }
    }
  }
}

void
GraphHelper::addEdge(const std::string& v, const std::string& w) {
  // Add edge if
  // 1- Both vertices exist
  // 2- Edge is between two different vertices

  if (vertices_.count(v) &&
      vertices_.count(w) &&
      v != w) {
    adjacencies_[v].insert(w);
    adjacencies_[w].insert(v);
  }
}

void
GraphHelper::addVertex(const std::string& v) {
  vertices_.insert(v);
}

void
GraphHelper::removeVertex(const std::string& v) {
  auto it = adjacencies_.find(v);
  if (it != adjacencies_.end()) {
    for (const auto& neighbor : it->second) {
      auto itNbr = adjacencies_.find(neighbor);
      if (itNbr != adjacencies_.end()) {
        itNbr->second.erase(v);
      }
    }
    adjacencies_.erase(v);
  }
  vertices_.erase(v);
}

void
GraphHelper::findArticulationPoints(
    const std::string& u,
    std::unordered_set<std::string>& visited,
    std::unordered_map<std::string, int>& discoveryTime,
    std::unordered_map<std::string, int>& lowestDiscoveryTime,
    std::unordered_map<std::string, std::string>& parent,
    std::unordered_map<std::string, std::unordered_set<std::string>>& aps) {

  int numChildren = 0;
  visited.insert(u);

  // Initialize discovery time and lowest discovery time
  discoveryTime[u] = lowestDiscoveryTime[u] = ++discoverTime_;

  for (const auto& neighbor : adjacencies_[u]) {
    if (!visited.count(neighbor)) {
      numChildren++;
      parent[neighbor] = u;
      findArticulationPoints(
          neighbor, visited, discoveryTime, lowestDiscoveryTime, parent, aps);

      // Adjust lowestDiscoveryTime if the subtree has a connection to one of
      // the ancestors of u
      lowestDiscoveryTime[u] =
          std::min(lowestDiscoveryTime[u], lowestDiscoveryTime[neighbor]);

      // u is an articulation point if
      // (1) u is root of DFS tree and has two or more children.
      // (2) u is not root and lowest discovery time value of one of its
      //     children is more than discovery time value of u.
      if (parent.count(u) == 0 && numChildren > 1) {
        // We inject a fake root node. No need to save it as an AP
      } else if (parent.count(u) > 0 &&
          lowestDiscoveryTime[neighbor] >= discoveryTime[u] &&
          sitesPendingUpdate_.count(u)) {
        aps[u].insert(neighbor);
      }
    } else if (neighbor != parent[u]) {
      // Update lowest discovery time value of u.
      lowestDiscoveryTime[u] =
          std::min(lowestDiscoveryTime[u], discoveryTime[neighbor]);
    }
  }
}

std::unordered_set<std::string>
GraphHelper::bfs(
    const std::string& rootSite, const std::string& excludeSite) {

  std::deque<std::string> currSiteNames;
  std::unordered_set<std::string> seenSiteNames;
  std::unordered_set<std::string> visitedSites;

  currSiteNames.push_back(rootSite);
  seenSiteNames.insert(excludeSite);

  while (currSiteNames.size() > 0) {
    std::string currSiteName = currSiteNames.front();
    currSiteNames.pop_front();

    visitedSites.insert(currSiteName);

    auto itCurrentAdj = adjacencies_.find(currSiteName);
    // Ignore invalid nodes.
    if (itCurrentAdj == adjacencies_.end()) {
      continue;
    }

    for (const auto& nbrSiteName : itCurrentAdj->second) {
      if (!seenSiteNames.count(nbrSiteName)) {
        currSiteNames.push_back(nbrSiteName);
        seenSiteNames.insert(nbrSiteName);
      }
    }
  }

  return visitedSites;
}

std::unordered_map<std::string, std::unordered_set<std::string>>
GraphHelper::getApGroups() {
  std::unordered_map<std::string, std::unordered_set<std::string>> apGroups;
  std::unordered_set<std::string> visited;
  std::unordered_map<std::string, int> discoveryTime;
  std::unordered_map<std::string, int> lowestDiscoveryTime;
  std::unordered_map<std::string, std::string> parent;
  std::unordered_map<std::string, std::unordered_set<std::string>> aps;

  // Add fake root site (site name is "") with edges to all pop sites
  addVertex(kFakeRootSiteName);
  for (const auto& popSiteName : popSiteNames_) {
    addEdge(kFakeRootSiteName, popSiteName);
  }

  // Call the recursive helper function to find articulation points
  findArticulationPoints(
      kFakeRootSiteName,
      visited,
      discoveryTime,
      lowestDiscoveryTime,
      parent,
      aps);

  // Create AP groups (AP and all its dependent children)
  // We also filter out sub-groups (APs dependent on other APs)
  std::unordered_set<std::string> allDepSites;
  for (const auto& ap : aps) {
    if (allDepSites.count(ap.first)) {
      // This is a sub-group of already added group.
      continue;
    }
    allDepSites.insert(ap.first);
    for (const auto& child : ap.second) {
      auto dependents = bfs(child, ap.first);
      for (const auto& dependentSite : dependents) {
        if (apGroups.count(dependentSite)) {
          // Remove previously added sub-group
          apGroups.erase(dependentSite);
        }
        apGroups[ap.first].insert(dependentSite);
        allDepSites.insert(dependentSite);
      }
    }
  }
  // Remove the fake root vertex
  removeVertex(kFakeRootSiteName);
  return apGroups;
}

std::unordered_set<std::string>
GraphHelper::getCandidateNodes(
    const std::unordered_map<std::string, std::unordered_set<std::string>>&
        apGroups) {

  std::unordered_set<std::string> nodesToUpdate;

  // Push pop sites first
  // Already updated or excluded pop sites are pushed to the front of
  // UpdatedSitesStack. Thus, we will pick them first in the DFS traversal
  std::deque<std::string> UpdatedSitesStack;
  std::deque<std::string> PendingSitesStack;
  for (const auto& popSiteName : popSiteNames_) {
    if (sitesPendingUpdate_.count(popSiteName)) {
      PendingSitesStack.push_front(popSiteName);
    } else {
      UpdatedSitesStack.push_front(popSiteName);
    }
  }

  std::unordered_set<std::string> seenSiteNames;
  seenSiteNames.insert(PendingSitesStack.begin(), PendingSitesStack.end());
  seenSiteNames.insert(UpdatedSitesStack.begin(), UpdatedSitesStack.end());

  // Limit number of pops in a batch to 1
  // This reduces the impact of updates on the network traffic
  bool popAlreadyAdded = false;

  // DFS starts here
  while (!UpdatedSitesStack.empty() || !PendingSitesStack.empty()) {
    std::string currSiteName;
    if (!UpdatedSitesStack.empty()) {
      currSiteName = UpdatedSitesStack.front();
      UpdatedSitesStack.pop_front();
    } else {
      currSiteName = PendingSitesStack.front();
      PendingSitesStack.pop_front();
    }

    auto itCurrentAdj = adjacencies_.find(currSiteName);
    // Ignore invalid sites.
    if (itCurrentAdj == adjacencies_.end()) {
      continue;
    }

    bool isLeaf = true;
    bool isPop = popSiteNames_.count(currSiteName) != 0;
    auto itApChildren = apGroups.find(currSiteName);
    bool isAp = itApChildren != apGroups.end();

    // Push unseen neighbors of site to stack
    // If the site is not an update candidate, push to the top of
    // UpdatedSitesStack.
    // We do not traverse down an AP group. We stop at the AP group parent.
    for (const auto& nbrSiteName : itCurrentAdj->second) {
      // Don't dive into children of APs
      if (isAp && itApChildren->second.count(nbrSiteName)) {
        continue;
      }

      if (!seenSiteNames.count(nbrSiteName)) {
        if (sitesPendingUpdate_.count(nbrSiteName)) {
          PendingSitesStack.push_front(nbrSiteName);
        } else {
          UpdatedSitesStack.push_front(nbrSiteName);
        }
        seenSiteNames.insert(nbrSiteName);
        isLeaf = false;
      }
    }

    // Pick candidate leaf sites for updates
    // Discard the site if
    //   1- Site is already updated or excluded
    //   2- Site is a pop site and another POP site is picked for updates
    if (isLeaf && sitesPendingUpdate_.count(currSiteName) &&
        (!isPop || !popAlreadyAdded)) {
      std::unordered_set<std::string> candidateNodesToUpdate;

      // Convert candidate leaf site name to candidate node names
      for (const auto& nodeName : siteName2nodeNames_[currSiteName]) {
        if (nodesPendingUpdate_.count(nodeName)) {
          candidateNodesToUpdate.insert(nodeName);
        }
      }

      // If this is an AP site. Add nodes of its children to candidate list
      if (isAp) {
        for (const auto& apChild : itApChildren->second) {
          for (const auto& nodeName : siteName2nodeNames_[apChild]) {
            if (nodesPendingUpdate_.count(nodeName)) {
              candidateNodesToUpdate.insert(nodeName);
            }
          }
        }
      }

      // Add nodes to current batch if we do not exceed the nodes Limit
      // If an AP cluster is bigger than the limit. Add it only if no other
      // nodes were added (This overrides the limit)
      if (nodesToUpdate.size() + candidateNodesToUpdate.size() <=
              batchSizeLimit_ ||
          nodesToUpdate.empty()) {
        nodesToUpdate.insert(
            candidateNodesToUpdate.begin(), candidateNodesToUpdate.end());
        popAlreadyAdded = popAlreadyAdded || isPop;
      }
    }
  }
  return nodesToUpdate;
}

std::unordered_set<std::string>
GraphHelper::getBatch(
    const TopologyWrapper& topologyW,
    const std::unordered_set<std::string>& nodesPendingUpdate,
    int limit) {

  nodesPendingUpdate_ = nodesPendingUpdate;

  // Build graph of sites
  buildGraph(topologyW);
  batchSizeLimit_ = (limit > 0) ? limit : (int) topologyW.getNodesCount();

  // Get all Articulation Points and their dependent children
  auto apGroups = getApGroups();
  LOG(INFO) << apGroups.size() << " articulation point groups exist";
  for (const auto& group : apGroups) {
    VLOG(3) << "AP group " << group.first << " has " << group.second.size()
            << " sites: " << folly::join(", ", group.second);
  }

  auto batch = getCandidateNodes(apGroups);
  return batch;
}

} // namespace terragraph
} // namespace facebook
