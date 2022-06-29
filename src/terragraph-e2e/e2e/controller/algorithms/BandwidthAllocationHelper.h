/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/properties.hpp>
#include <boost/property_map/property_map.hpp>

#include <e2e/if/gen-cpp2/BWAllocation_types.h>
#include <e2e/if/gen-cpp2/Topology_types.h>

#include "../topology/TopologyWrapper.h"

namespace facebook {
namespace terragraph {

/**
 * Fair airtime allocation algorithm.
 */
class BandwidthAllocationHelper {
 public:
   /** Compute the fair airtime allocation for the topology. */
  static thrift::NetworkAirtime computeAirtimes(
      const TopologyWrapper *topologyW);

 private:
  // ---- Boost graph structures ----
  /** DN boost graph structure. */
  struct dn_t { /**\{*/ typedef boost::vertex_property_tag kind; /**\}*/ };
  /** POP node boost graph structure. */
  struct pop_t { /**\{*/ typedef boost::vertex_property_tag kind; /**\}*/ };
  /** Wireless link boost graph structure. */
  struct wireless_t { /**\{*/ typedef boost::edge_property_tag kind; /**\}*/ };
  /** Boost vertex property. */
  using VertexProperty =
      boost::property<boost::vertex_name_t, std::string,
      boost::property<dn_t, bool,
      boost::property<pop_t, bool>>>;
  /** Boost edge property. */
  using EdgeProperty =
      boost::property<boost::edge_weight_t, float,
      boost::property<wireless_t, bool>>;
  /** Boost graph type. */
  using graph_t = boost::adjacency_list<
      boost::listS, boost::vecS, boost::undirectedS,
      VertexProperty, EdgeProperty>;
  /** Boost vertex type. */
  using vertex_t = boost::graph_traits<graph_t>::vertex_descriptor;
  /** Boost edge type. */
  using edge_t = boost::graph_traits<graph_t>::edge_descriptor;
  /** Boost graph index map. */
  using IndexMap =
      boost::property_map<graph_t, boost::vertex_index_t>::type;
  /** Boost graph name map. */
  using NameMap =
      boost::property_map<graph_t, boost::vertex_name_t>::type;
  /** Boost graph DN map. */
  using DnMap = boost::property_map<graph_t, dn_t>::type;
  /** Boost graph POP node map. */
  using PopMap = boost::property_map<graph_t, pop_t>::type;
  /** Boost graph wireless link map. */
  using WirelessMap = boost::property_map<graph_t, wireless_t>::type;
  /** Boost graph predecessor map. */
  using PredecessorMap = boost::iterator_property_map<
      vertex_t*, IndexMap, vertex_t, vertex_t&>;
  /** Boost graph vertex map. */
  using VertexMap = std::unordered_map<std::string, vertex_t>;

  /**
   * Per-DN struct holding data used in airtime computations.
   *
   * NOTE: This contains floating-point values due to how we implement
   *       rebalancing for downlink/uplink bandwidth contention.
   */
  struct DnAirtimeData {
    /** Sum of local + remote CNs */
    double downstreamCNs = 0;
    /** Number of CNs attached to this DN */
    double localCNs = 0;
    /** Number of remote downstream CNs */
    double remoteCNs = 0;
    /** Number of CNs attached to the neighbor DN */
    double peerCNs = 0;
    /** DN-to-DN fair time percent (1/100%) */
    std::unordered_map<std::string /* nbr */, int16_t /* pct (1/100%) */>
        dnDnFairtimeMap;
    /** Per-CN fair time percent (1/100%) */
    int16_t perCnFairtime = 0;
  };
  /** Map of DNs to airtime. */
  using DnAirtimeDataMap = std::unordered_map<std::string, DnAirtimeData>;

  /** Returns all wireless DN neighbors of a vertex v. */
  static std::vector<std::string> getWirelessDnNbrs(
      graph_t& graph,
      NameMap& nameMap,
      DnMap& dnMap,
      WirelessMap& wirelessMap,
      vertex_t& v);

  /** Returns whether the vertex v represents a Y-street node. */
  static bool isYStreet(
      graph_t& graph,
      vertex_t& v,
      const std::vector<std::string>& wirelessDnNbrs);

  /** Assign equal airtime to all DN sectors (if there are no CNs). */
  static DnAirtimeDataMap assignEqualAirtimeAllSectors(
      graph_t& graph,
      VertexMap& nameToVertex,
      const std::vector<thrift::Node>& dns);

  /** Compute fair airtimes for all DNs in the topology. */
  static DnAirtimeDataMap computeTopologyToAirtime(
      graph_t& graph,
      VertexMap& nameToVertex,
      const std::vector<std::string>& pops,
      const std::vector<thrift::Node>& dns,
      const std::vector<thrift::Node>& users,
      double ulDlRatio);

  /**
   * Build a graph containing the nodes and links in the topology.
   *
   * Writes name-to-vertex and name-to-edge entries into the provided maps.
   */
  static graph_t buildAirtimeGraph(
      const TopologyWrapper *topologyW, VertexMap& nameToVertex);

  /** Returns all neighbor nodes of a vertex v. */
  static std::vector<thrift::Node> getNbrNodes(
      const TopologyWrapper *topologyW,
      graph_t& graph,
      NameMap& nameMap,
      vertex_t& v);

  /** Generate the per-link airtime allocations for the given DN. */
  static thrift::NodeAirtime generateAirtimes(
      const DnAirtimeData& data, const std::vector<thrift::Node>& nbrs);
};

} // namespace terragraph
} // namespace facebook
