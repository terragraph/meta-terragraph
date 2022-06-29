/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "BandwidthAllocationHelper.h"

#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <boost/graph/iteration_macros.hpp>
#include <gflags/gflags.h>

using std::string;
using std::vector;
using std::unordered_set;
using std::unordered_map;

DEFINE_double(
    airtime_ul_dl_ratio,
    0.2,
    "Percentage of uplink traffic to allow as a fraction of downlink traffic");

namespace {
  // Max value guaranteed larger than all values
  const int MAX_HOPS_FROM_POP = 100;

  // Min/max airtime allocation values (1/100%)
  const int16_t TX_MIN = 200;
  const int16_t TX_MAX = 8000;
  const int16_t RX_MIN = 200;
  const int16_t RX_MAX = 8000;
  const int16_t INVALID_AIRTIME = -1;
}

namespace facebook {
namespace terragraph {

vector<string>
BandwidthAllocationHelper::getWirelessDnNbrs(
    graph_t& graph,
    NameMap& nameMap,
    DnMap& dnMap,
    WirelessMap& wirelessMap,
    vertex_t& v) {
  vector<string> nbrDns;
  graph_t::adjacency_iterator iter, end;
  for (boost::tie(iter, end) = boost::adjacent_vertices(v, graph);
       iter != end;
       ++iter) {
    if (dnMap[*iter]) {
      auto epair = boost::edge(v, *iter, graph);
      if (epair.second && wirelessMap[epair.first]) {
        nbrDns.push_back(nameMap[*iter]);
      }
    }
  }
  return nbrDns;
}

bool
BandwidthAllocationHelper::isYStreet(
    graph_t& /*graph*/,
    vertex_t& /*v*/,
    const vector<string>& wirelessDnNbrs) {
  // Simply check if we have 2 wireless DN neighbors.
  // If so, this is the parent DN.
  // NOTE: Does not handle cascading of Y-streets.
  return (wirelessDnNbrs.size() > 1);
}

BandwidthAllocationHelper::DnAirtimeDataMap
BandwidthAllocationHelper::assignEqualAirtimeAllSectors(
    graph_t& graph,
    VertexMap& nameToVertex,
    const vector<thrift::Node>& dns) {
  VLOG(2) << "Only DNs in topology - allocating 100% airtime to all DN "
               "sectors (except Y-street links)";

  DnAirtimeDataMap dnData;
  NameMap nameMap = boost::get(boost::vertex_name, graph);
  DnMap dnMap = boost::get(dn_t(), graph);
  WirelessMap wirelessMap = boost::get(wireless_t(), graph);

  for (const auto& node : dns) {
    DnAirtimeData& data = dnData[node.name];
    vertex_t v = nameToVertex[node.name];
    auto dnNbrs = getWirelessDnNbrs(graph, nameMap, dnMap, wirelessMap, v);
    if (isYStreet(graph, v, dnNbrs)) {
      VLOG(3) << "Detected Y-street at node: " << node.name;
      for (const auto& dn_node : dnNbrs) {
        data.dnDnFairtimeMap[dn_node] = 10000 / dnNbrs.size();
      }
    } else {
      if (!dnNbrs.empty()) {
        data.dnDnFairtimeMap[dnNbrs[0]] = 10000;
      }
    }
    data.localCNs = 0;
    data.peerCNs = 0;
    data.perCnFairtime = 0;
  }

  return dnData;
}

BandwidthAllocationHelper::DnAirtimeDataMap
BandwidthAllocationHelper::computeTopologyToAirtime(
    graph_t& graph,
    VertexMap& nameToVertex,
    const vector<string>& pops,
    const vector<thrift::Node>& dns,
    const vector<thrift::Node>& users,
    double ulDlRatio) {
  VLOG(2) << "Using DL-UL ratio: " << ulDlRatio;

  // TBD: This is hack for now! Handle No CN elegantly later
  // with knowledge of where WiFi APs hang-off the network.
  // If no CNs exist, assign equal airtime to all DN sectors
  if (users.empty()) {
    return assignEqualAirtimeAllSectors(graph, nameToVertex, dns);
  }

  DnAirtimeDataMap dnData;
  NameMap nameMap = boost::get(boost::vertex_name, graph);
  DnMap dnMap = boost::get(dn_t(), graph);
  PopMap popMap = boost::get(pop_t(), graph);
  WirelessMap wirelessMap = boost::get(wireless_t(), graph);

  // Compute shortest paths
  unordered_map<string /* pop */, unordered_map<
      string /* node */, unordered_set<string> /* path nodes */>> shortestPaths;
  for (const auto& pop : pops) {
    vertex_t v0 = nameToVertex[pop];

    // Compute shortest paths from the pop to all vertices
    vector<vertex_t> predecessors(boost::num_vertices(graph));
    IndexMap indexMap = boost::get(boost::vertex_index, graph);
    PredecessorMap predecessorMap(&predecessors[0], indexMap);
    boost::dijkstra_shortest_paths(
        graph, v0, boost::predecessor_map(predecessorMap));

    // Extract shortest paths for each node from the pop
    BGL_FORALL_VERTICES(v, graph, graph_t) {
      string node = nameMap[v];
      if (popMap[v]) {
        continue;  // skip pops
      }

      // Extract path
      vector<edge_t> path;
      for (auto u = predecessorMap[v]; u != v; v = u, u = predecessorMap[v]) {
        auto epair = boost::edge(u, v, graph);
        path.push_back(epair.first);
      }
      unordered_set<string> pathNodes;  // we don't care about order
      for (auto iter = path.rbegin(); iter != path.rend(); ++iter) {
        pathNodes.insert(nameMap[boost::source(*iter, graph)]);
        pathNodes.insert(nameMap[boost::target(*iter, graph)]);
      }
      shortestPaths[pop][node] = pathNodes;
    }
  }

  // Find best PoP DN for each user
  unordered_map<string /* node */, string /* pop */> bestPops;
  BGL_FORALL_VERTICES(v, graph, graph_t) {
    string node = nameMap[v];
    if (popMap[v]) {
      VLOG(3) << "Skipping " << node << " PoP node from best PoP analysis";
      bestPops[node] = node;
      continue;
    }

    // Max value guaranteed larger than all values
    size_t shortestPathLen = MAX_HOPS_FROM_POP;
    for (const auto& pop : pops) {
      auto pathNodes = shortestPaths[pop][node];
      if (shortestPathLen > pathNodes.size()) {
        bestPops[node] = pop;
        shortestPathLen = pathNodes.size();
      }
    }
  }

  // Count the total number of CNs being served through each DN
  for (const auto& node : dns) {
    DnAirtimeData& data = dnData[node.name];
    data.downstreamCNs = 0;
    for (const auto& user : users) {
      string pop = bestPops[user.name];
      if (shortestPaths[pop][user.name].count(node.name)) {
        data.downstreamCNs++;
      }
    }
  }

  // Count and maintain number of local and remote CNs being served by each DN
  for (const auto& node : dns) {
    DnAirtimeData& data = dnData[node.name];

    // For local CNs, check if edge exists between the DN and CN in the graph
    data.localCNs = 0;
    for (const auto& user : users) {
      auto epair =
          boost::edge(nameToVertex[node.name], nameToVertex[user.name], graph);
      if (epair.second) {
        data.localCNs++;
      }
    }

    // The rest of the downstream CNs are remote
    data.remoteCNs = data.downstreamCNs - data.localCNs;
  }

  // Look for DNs whose CNs have UL shared with DL traffic from upstream DN
  unordered_map<string /* dn */, string /* UL nbr */> dnUplinkNbrMap;
  unordered_map<string /* dn */, string /* DL nbr */> dnDownlinkNbrMap;
  unordered_map<string /* dn */, vector<string> /* nbrs */> yStreetNodes;
  for (const auto& node : dns) {
    vertex_t v = nameToVertex[node.name];

    // PoP DN sector doesn't have uplink CNs that share BW with DL CNs
    if (popMap[v]) {
      continue;
    }

    // Check for Wireless DN neighbors only
    auto dnNbrs = getWirelessDnNbrs(graph, nameMap, dnMap, wirelessMap, v);
    if (isYStreet(graph, v, dnNbrs)) {
      // Note: This is only detecting Y-street when encountering the parent DN
      yStreetNodes[node.name] = dnNbrs;
    } else if (dnNbrs.size() == 1) {
      // Find DN closest to PoP
      string pop = bestPops[node.name];
      if (shortestPaths[pop][node.name].count(dnNbrs[0]) &&
          dnData[node.name].localCNs > 0) {
        VLOG(3) << "DN=" << node.name << " carrying "
                << static_cast<int>(dnData[node.name].localCNs)
                << " CNs that contend with DL traffic";
        dnUplinkNbrMap[node.name] = dnNbrs[0];
        dnDownlinkNbrMap[dnNbrs[0]] = node.name;
      }
    }
  }

  // Adjust BW based on whether UL CNs share BW with DL CNs on remote DN
  unordered_map<string /* dn */, double> adjDownlinkBw;
  unordered_map<string /* dn */, double> uplinkBwReqMap;
  for (const auto& kv : dnUplinkNbrMap) {
    string dn = kv.first;
    string nbrDn = kv.second;
    string pop = bestPops[dn];

    double downlinkBwPctPerCn = 1.0 / dnData[pop].downstreamCNs;
    double uplinkBwPctPerCn =
        downlinkBwPctPerCn / (1.0 - ulDlRatio) * ulDlRatio;
    double totalUplinkBw = dnData[dn].localCNs * uplinkBwPctPerCn;
    double totalNbrDnDownlinkBw = dnData[nbrDn].localCNs * downlinkBwPctPerCn;
    double totalDownlinkBwReq = 1.0 -
        (dnData[pop].downstreamCNs - dnData[nbrDn].downstreamCNs) /
        dnData[pop].downstreamCNs;
    double uplinkBwReq = 1.0 - (totalUplinkBw - totalNbrDnDownlinkBw);

    if (totalNbrDnDownlinkBw < totalUplinkBw) {
      adjDownlinkBw[nbrDn] =
          dnData[nbrDn].remoteCNs / uplinkBwReq - dnData[nbrDn].remoteCNs;
      uplinkBwReqMap[nbrDn] = uplinkBwReq;
      dnData[nbrDn].downstreamCNs += adjDownlinkBw[nbrDn];
      VLOG(3) << "Neighbor DN's BW decreased to accommodate competing UL "
                 "traffic from remote DN's CNs: "
              << (totalUplinkBw - totalNbrDnDownlinkBw);
      VLOG(3) << "Total DL BW req from neighbouring DN (" << totalDownlinkBwReq
              << ") compared to available link capacity (" << uplinkBwReq
              << ")";
    } else {
      VLOG(3) << "Enough DL BW to accommodate competing UL traffic: (DL="
              << totalNbrDnDownlinkBw << ", UL=" << totalUplinkBw << ")";
      VLOG(3) << "DL BW required on node from previous hop compared to "
                 "available capacity on node: (DL=" << totalDownlinkBwReq
              << ", UL=" << uplinkBwReq << ")";
    }
  }

  // Backfill additional dstream CNs that added in prior step
  for (const auto& p : pops) {
    for (const auto& adjNodePair : adjDownlinkBw) {
      string adjNode = adjNodePair.first;
      double bw = adjNodePair.second;
      string pop = bestPops[adjNode];

      double totalDownlinkBwReq = 1.0 -
          (dnData[pop].downstreamCNs - dnData[adjNode].downstreamCNs) /
          dnData[pop].downstreamCNs;

      if (totalDownlinkBwReq > uplinkBwReqMap[adjNode]) {
        for (const auto& node : shortestPaths[p][adjNode]) {
          if (node == adjNode || dnUplinkNbrMap.count(node)) {
            continue;
          }

          dnData[node].remoteCNs += bw;
          dnData[node].downstreamCNs += bw;
          VLOG(3) << "Adj. counts for " << node << ", remote CNs ("
                  << dnData[node].remoteCNs << "), dstream CNs ("
                  << dnData[node].downstreamCNs << ")";
        }
      }
    }
  }

  VLOG(3) << "Representative Downlink BW per CN: "
          << (1.0 / dnData[pops[0]].downstreamCNs);

  // Configure ideal airtimes for UL DN sectors as well
  for (const auto& kv : dnUplinkNbrMap) {
    string dn = kv.first;
    if (dnData[dn].localCNs > 0) {
      string pop = bestPops[dn];
      dnData[dn].remoteCNs =
          dnData[pop].downstreamCNs - dnData[dn].localCNs;
      dnData[dn].downstreamCNs = dnData[pop].downstreamCNs;
    }
  }

  // Print results
  unordered_set<string /* dn */> pctComputed;
  for (const auto& node : dns) {
    if (pctComputed.count(node.name)) {
      continue;
    }
    DnAirtimeData& data = dnData[node.name];

    VLOG(2) << "DN=" << node.name
              << ", Local CNs=" << (data.downstreamCNs - data.remoteCNs)
              << ", Remote CNs=" << data.remoteCNs
              << ", DN-DN (pct)=" << (10000 *
                  data.remoteCNs / std::max(1.0, data.downstreamCNs));

    vertex_t v = nameToVertex[node.name];
    auto dnNbrs = getWirelessDnNbrs(graph, nameMap, dnMap, wirelessMap, v);
    if (isYStreet(graph, v, dnNbrs)) {
      // Special work for Y-street DNs
      // count Y-street DN's local CNs
      double totalDownlinkCns = data.localCNs;
      for (const auto& n : yStreetNodes[node.name]) {
        // count CNs on adj Y-street DNs
        totalDownlinkCns += dnData[n].downstreamCNs;
      }
      for (const auto& n : yStreetNodes[node.name]) {
        data.dnDnFairtimeMap[n] = static_cast<int16_t>(
            10000 * dnData[n].downstreamCNs / totalDownlinkCns);
        vertex_t u = nameToVertex[n];
        auto nbrs = getWirelessDnNbrs(graph, nameMap, dnMap, wirelessMap, u);
        dnData[n].dnDnFairtimeMap[nbrs[0]] = static_cast<int16_t>(
            10000 * dnData[n].downstreamCNs /
            (totalDownlinkCns - data.localCNs));

        // No need for child DNs to consider peerCNs since
        // they are already duty-cycled by parent
        pctComputed.insert(n);
        dnData[n].peerCNs = 0;
      }

      // Assume that Y-street, i.e., splitting BW between 2 DNs affords
      // enough time for child DNs on Y-street to serve their CNs
      data.peerCNs = 0;
    } else {
      // Work for non Y-street DNs
      if (!dnNbrs.empty()) {
        data.dnDnFairtimeMap[dnNbrs[0]] = 10000 *
            data.remoteCNs / std::max(1.0, data.downstreamCNs);
      }

      if (adjDownlinkBw.count(node.name)) {
        data.peerCNs = dnData[dnDownlinkNbrMap[node.name]].localCNs;
      } else {
        data.peerCNs = 0;
      }
    }

    data.perCnFairtime = static_cast<int16_t>(
        10000 / std::max(1.0, dnData[bestPops[node.name]].downstreamCNs));
  }

  return dnData;
}

BandwidthAllocationHelper::graph_t
BandwidthAllocationHelper::buildAirtimeGraph(
    const TopologyWrapper *topologyW, VertexMap& nameToVertex) {
  graph_t g;

  // Add vertices (nodes)
  for (const auto& node : topologyW->getAllNodes()) {
    VertexProperty vprop;
    boost::get_property_value(vprop, boost::vertex_name) = node.name;
    boost::get_property_value(vprop, dn_t()) =
        (node.node_type == thrift::NodeType::DN);
    boost::get_property_value(vprop, pop_t()) = node.pop_node;
    auto v = boost::add_vertex(vprop, g);
    nameToVertex[node.name] = v;
  }

  // Add edges (links)
  for (const auto& link : topologyW->getAllLinks()) {
    EdgeProperty eprop;
    boost::get_property_value(eprop, boost::edge_weight) = 1;
    boost::get_property_value(eprop, wireless_t()) =
        (link.link_type == thrift::LinkType::WIRELESS);
    boost::add_edge(
        nameToVertex[link.a_node_name],
        nameToVertex[link.z_node_name],
        eprop,
        g);
  }

  return g;
}

vector<thrift::Node>
BandwidthAllocationHelper::getNbrNodes(
    const TopologyWrapper *topologyW,
    graph_t& graph,
    NameMap& nameMap,
    vertex_t& v) {
  vector<thrift::Node> nbrs;
  graph_t::adjacency_iterator iter, end;
  for (boost::tie(iter, end) = boost::adjacent_vertices(v, graph);
       iter != end;
       ++iter) {
    string name = nameMap[*iter];
    nbrs.push_back(topologyW->getNode(name).value());
  }
  return nbrs;
}

thrift::NodeAirtime
BandwidthAllocationHelper::generateAirtimes(
    const DnAirtimeData& data, const vector<thrift::Node>& nbrs) {
  thrift::NodeAirtime nodeAirtime;
  int16_t totalDnDnAirtime = 0;
  for (const auto& node : nbrs) {
    thrift::LinkAirtime linkAirtime;
    linkAirtime.macAddress = node.mac_addr;
    if (node.node_type == thrift::NodeType::DN) {
      // DN-DN link airtimes
      auto iter = data.dnDnFairtimeMap.find(node.name);
      if (iter == data.dnDnFairtimeMap.end()) {
        continue;  // skip wired links
      }
      linkAirtime.txIdeal = iter->second;
      totalDnDnAirtime += iter->second;
      linkAirtime.rxIdeal = INVALID_AIRTIME;
    } else if (node.node_type == thrift::NodeType::CN) {
      // DN-CN link airtimes
      linkAirtime.txIdeal = static_cast<int16_t>(
          (10000 - totalDnDnAirtime) / data.localCNs);
      linkAirtime.rxIdeal = data.perCnFairtime;
    }
    linkAirtime.txMin = TX_MIN;
    linkAirtime.txMax = std::max(TX_MAX, linkAirtime.txIdeal);
    linkAirtime.rxMin = RX_MIN;
    linkAirtime.rxMax = std::max(RX_MAX, linkAirtime.rxIdeal);
    nodeAirtime.linkAirtimes.push_back(linkAirtime);
  }
  return nodeAirtime;
}

thrift::NetworkAirtime
BandwidthAllocationHelper::computeAirtimes(const TopologyWrapper *topologyW) {
  // Build graph
  VertexMap nameToVertex;
  graph_t graph = buildAirtimeGraph(topologyW, nameToVertex);
  NameMap nameMap = boost::get(boost::vertex_name, graph);
  auto pops = topologyW->getPopNodeNames();
  auto dns = topologyW->getDNs();
  auto users = topologyW->getCNs();

  // Compute airtimes
  DnAirtimeDataMap dnData = computeTopologyToAirtime(
      graph, nameToVertex, pops, dns, users, FLAGS_airtime_ul_dl_ratio);

  thrift::NetworkAirtime networkAirtime;
  for (const auto& dn : dns) {
    vertex_t v = nameToVertex[dn.name];
    auto nbrs = getNbrNodes(topologyW, graph, nameMap, v);
    networkAirtime.nodeAirtimeMap[dn.name] =
        generateAirtimes(dnData[dn.name], nbrs);
    VLOG(3) << "Completed airtime allocation for node: " << dn.name;
  }

  return networkAirtime;
}

} // namespace terragraph
} // namespace facebook
