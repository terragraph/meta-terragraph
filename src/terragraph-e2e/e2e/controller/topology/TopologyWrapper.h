/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/IPAddress.h>
#include <folly/experimental/io/FsUtil.h>

#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/Topology_types.h"

namespace facebook {
namespace terragraph {

/**
 * Prefix allocation schemes used on E2E controller.
 */
enum ControllerPrefixAllocScheme {
  NONE = 1,
  CENTRALIZED_PREFIX_ALLOCATION = 2,
  DETERMINISTIC_PREFIX_ALLOCATION = 3,
};

/**
 * Compute line-of-sight (LOS) distance between two Locations.
 *
 * Approximation tested for:
 * max 1% error, Locations upto 50km away, near poles/equator
 */
double approxDistance(const thrift::Location& l1, const thrift::Location& l2);

/** Compute the angle between two locations. */
double computeAngle(const thrift::Location& l1, const thrift::Location& l2);

/**
 * Helper for managing and manipulating Terragraph's network topology.
 */
class TopologyWrapper {
 public:
  /** Empty constructor. */
  TopologyWrapper();

  /** \{ */
  TopologyWrapper(const TopologyWrapper&) = delete;
  TopologyWrapper& operator=(const TopologyWrapper&) = delete;
  /** \} */

  /**
   * Construct TopologyWrapper from the given Thrift topology object.
   *
   * The topology file will be saved in 'topologyDir'.
   *
   * If 'validateTopology' is false, the topology will not be validated
   * (which may cause runtime exceptions for invalid topologies).
   */
  explicit TopologyWrapper(
      const thrift::Topology& topology,
      const std::string& topologyDir = "",
      const bool validateTopology = true,
      const bool createIntrasiteLinks = false);

  /**
   * Construct TopologyWrapper from the given topology file.
   *
   * The topology file will be saved in 'topologyDir'.
   */
  explicit TopologyWrapper(
      const std::string& topologyFile,
      const std::string& topologyDir = "",
      const bool createIntrasiteLinks = false);

  /** Maximum number of nodes on each site. */
  static const size_t kMaxNumOfNodesPerSite;

  /** Maximum number of DN-to-DN links per node. */
  static const size_t kMaxNumOfDnDnLinks;

  /** Maximum number of DN-to-CN links per node. */
  static const size_t kMaxNumOfDnCnLinks;

  /**
   * Returns the link name for the given nodes (without validating that
   * aNodeName < zNodeName lexographically).
   *
   * Format: `link-aNodeName-zNodeName`
   */
  static std::string buildLinkName(
      const std::string& aNodeName, const std::string& zNodeName);

  /** Update the name of the link and swaps the A/Z node names if misordered. */
  static void updateLink(thrift::Link& link);

  /**
   * Reset all node and link statuses to their initial state.
   *
   * Nodes will be marked OFFLINE, and links will be marked "alive" if Ethernet
   * and "dead" if wireless. This also resets the 'linkup_attempts' counter to
   * 0.
   */
  void sanitizeState();

  /**
   * Write the current topology in JSON format to the given output file.
   *
   * Returns true upon success, or false if any error occurred.
   */
  bool writeToFile(const std::string& outputFile) const;

  /**
   * Write the current topology into a timestamped file under topologyDir_.
   *
   * This is called automatically in all add/delete/update functions.
   */
  void writeToTsFile() const;

  /**
   * Completely replace the current topology with the given struct.
   *
   * This will not perform any validation.
   */
  void setTopology(const thrift::Topology& topology);

  /**
   * Completely replace the current topology by reading a new struct from the
   * given file.
   *
   * This will not perform any validation.
   */
  void setTopologyFromFile(const std::string& topologyFile);

  // ------------- //
  //  GET methods  //
  // ------------- //

  /** Returns the current topology. */
  thrift::Topology getTopology() const;

  /** Returns the name of the topology. */
  std::string getTopologyName() const;

  /** Returns a list of all nodes in the topology. */
  std::vector<thrift::Node> getAllNodes() const;

  /** Returns a list of all links in the topology. */
  std::vector<thrift::Link> getAllLinks() const;

  /** Returns a list of all sites in the topology. */
  std::vector<thrift::Site> getAllSites() const;

  /** Returns the total number of nodes in the topology. */
  size_t getNodesCount() const;

  /**
   * Returns the node with the given name, or std::nullopt if it does not exist.
   */
  std::optional<thrift::Node> getNode(const std::string& nodeName) const;

  /**
   * Returns the link with the given aNodeName and zNodeName, or std::nullopt if
   * it does not exist.
   */
  std::optional<thrift::Link> getLink(
      const std::string& aNodeName, const std::string& zNodeName) const;

  /**
   * Returns the link with the given name, or std::nullopt if it does not exist.
   */
  std::optional<thrift::Link> getLink(const std::string& linkName) const;

  /** Get list of links from same radios as given link. */
  std::vector<thrift::Link> getSameRadioLinks(const thrift::Link& link) const;

  /**
   * Returns the site with the given name, or std::nullopt if it does not exist.
   */
  std::optional<thrift::Site> getSite(const std::string& siteName) const;

  /**
   * Returns the node with the given MAC address (in any format accepted by
   * folly::MacAddress), or std::nullopt if it does not exist.
   *
   * Lookup supports both node IDs (mac_addr) and radio MACs (wlan_mac_addrs).
   */
  std::optional<thrift::Node> getNodeByMac(const std::string& nodeMac) const;

  /**
   * Returns the name of the node with the given MAC address (in any format
   * accepted by folly::MacAddress), or std::nullopt if it does not exist.
   *
   * Lookup supports both node IDs (mac_addr) and radio MACs (wlan_mac_addrs).
   */
  std::optional<std::string> getNodeNameByMac(
      const std::string& macAddr) const;

  /** Returns the full MAC address to node name map. */
  std::map<std::string, std::string> getMac2NodeNameMap() const;

  /**
   * Returns the MAC address of the given node, or std::nullopt if it does not
   * exist.
    */
  std::optional<std::string> getMacByNodeName(
      const std::string& nodeName) const;

  /**
   * Returns a set of all node names in the given site in the topology, or
   * std::nullopt if it does not exist.
   */
  std::optional<std::unordered_set<std::string>> getNodeNamesBySiteName(
      const std::string& siteName) const;

  /**
   * Returns a list of all nodes in the given site in the topology, or an empty
   * list if it does not exist.
   */
  std::vector<thrift::Node> getNodesBySiteName(
      const std::string& siteName) const;

  /** Returns a list of all nodes in the topology with the given node type. */
  std::vector<thrift::Node> getNodesByType(thrift::NodeType nodeType) const;

  /**
   * Returns a list of all node names in the topology with the given node type.
   */
  std::unordered_set<std::string> getNodeNamesByType(
      thrift::NodeType nodeType) const;

  /** Returns a list of all client nodes (CNs) in the topology. */
  std::vector<thrift::Node> getCNs() const;

  /** Returns a list of all distribution nodes (DNs) in the topology. */
  std::vector<thrift::Node> getDNs() const;

  /** Returns a list of all POP nodes in the topology. */
  std::vector<thrift::Node> getPopNodes() const;

  /** Returns a list of all POP node names in the topology. */
  std::vector<std::string> getPopNodeNames() const;

  /** Returns a list of all links in the topology to or from the given node. */
  std::vector<thrift::Link> getLinksByNodeName(
      const std::string& nodeName) const;

  /**
   * Returns a list of all links in the topology to or from the given radio MAC
   * address (which must be non-empty).
   */
  std::vector<thrift::Link> getLinksByRadioMac(
      const std::string& radioMac) const;

  /**
   * Returns a list of all wireless links in the topology to or
   * from the given node.
   */
  std::vector<thrift::Link> getWirelessLinksByNodeName(
      const std::string& nodeName) const;

  /**
   * Returns the name of the link between the given nodes, or std::nullopt if
   * the link does not exist.
   */
  std::optional<std::string> getLinkName(
      const std::string& aNodeName, const std::string& zNodeName) const;

  /**
   * Returns the name of the node on the other end of the given link, or
   * std::nullopt if the origin node or link does not exist.
   */
  std::optional<std::string> getNbrNodeName(
      const std::string& myNodeName, const std::string& linkName) const;

  /**
   * Returns the node on the other end of the given link, or std::nullopt if the
   * origin node or link does not exist.
   */
  std::optional<thrift::Node> getNbrNode(
      const std::string& myNodeName, const std::string& linkName) const;

  /**
   * Returns the node on the other end of the given link, or std::nullopt if the
   * origin node or link does not exist.
   */
  std::optional<thrift::Node> getNbrNode(
      const std::string& myNodeName, const thrift::Link& link) const;

  /** Return the zones stored in the topology file, if there are any. */
  std::optional<std::unordered_map<std::string, thrift::Zone>>
  getPrefixAllocZones() const;

  /** Returns a mapping from node names to their assigned prefix. */
  std::unordered_map<std::string, std::string> getNodePrefixes() const;

  /**
   * Get current controller prefix allocation scheme. Only valid
   * after setControllerPrefixAllocScheme() is called and does not fetch
   * config.
   */
  ControllerPrefixAllocScheme getControllerPrefixAllocScheme() const;

  // ------------- //
  //  SET methods  //
  // ------------- //

  /**
   * Set the name of the topology.
   *
   * This method will invoke writeToTsFile().
   */
  void setTopologyName(const std::string& name);

  /**
   * Set the liveness of the given link.
   *
   * Returns true if successful, or false if the link does not exist.
   */
  bool setLinkStatus(const std::string& linkName, bool alive);

  /**
   * Change the MAC address (i.e. node ID) for the given node.
   *
   * Setting the 'force' flag will allow the operation even if the node is
   * currently ignited.
   *
   * Throws std::invalid_argument if validation fails (e.g. node does not exist,
   * MAC address did not change, MAC address belongs to other nodes) or if
   * the node is currently ignited and 'force' is false.
   *
   * This method will invoke writeToTsFile() upon success.
   */
  void setNodeMacByName(
      const std::string& nodeName,
      const std::string& macAddr,
      const bool force = false);

  /**
   * Change one WLAN MAC address of the given node.
   *
   * Setting the 'force' flag will allow the operation even if the a link
   * associated with the mac address is alive.
   *
   * Throws std::invalid_argument if validation fails (e.g. node does not exist,
   * MAC address did not change, MAC address belongs to other nodes) or if
   * any affected link is currently alive and 'force' is false.
   *
   * This method will invoke writeToTsFile() upon success.
   */
  void changeNodeWlanMac(
      const std::string& nodeName,
      const std::string& oldMacAddr,
      const std::string& newMacAddr,
      const bool force = false);

  /**
   * Add WLAN MAC addresses to the given node.
   *
   * Throws std::invalid_argument if validation fails (e.g. node does not exist,
   * or MAC addresses belong to other nodes).
   *
   * This method will invoke writeToTsFile() upon success.
   */
  void addNodeWlanMacs(
      const std::string& nodeName,
      const std::vector<std::string>& wlanMacAddrs);

  /**
   * Delete WLAN MAC addresses from the given node.
   *
   * Setting the 'force' flag will allow the operation even if links
   * associated with the mac address are alive.
   *
   * Throws std::invalid_argument if validation fails (e.g. node does not exist,
   * MAC address does not belong to node, MAC address is associated with a
   * link).
   *
   * This method will invoke writeToTsFile() upon success.
   */
  void deleteNodeWlanMacs(
      const std::string& nodeName,
      const std::vector<std::string>& wlanMacAddrs,
      const bool force = false);

  /**
   * Update all links to replace one MAC with another.
   *
   * The caller of this method should invoke writeToTsFile() to save the
   * topology.
   */
  void updateLinksMacs(
      const std::string& nodeName,
      const std::string& oldMac,
      const std::string& newMac,
      const bool force = false);

  /**
   * Remove the given node from its site.
   *
   * Throws std::invalid_argument if the node does not exist.
   */
  void unplugNodeFromSite(const std::string& nodeName);

  /**
   * Set the node status for the given node.
   *
   * Returns true if successful, or false if the node does not exist.
   */
  bool setNodeStatus(
      const std::string& nodeName, thrift::NodeStatusType status);

  /**
   * Increment the link-up attempts for the given link.
   *
   * Returns true if successful, or false if the link does not exist.
   */
  bool bumpLinkupAttempts(const std::string& linkName);

  /**
   * Reset the link-up attempts for the given link.
   *
   * Returns true if successful, or false if the link does not exist.
   */
  bool resetLinkupAttempts(const std::string& linkName);

  /**
   * Add the given node to the topology, after performing validation and
   * automatic parameter assignment (e.g. polarity, prefix).
   *
   * Throws various exceptions if validation fails.
   *
   * This method will invoke writeToTsFile() upon success.
   */
  void addNode(thrift::Node& newNode);

  /**
   * Delete the given node and all associated links from the topology.
   *
   * Setting the 'force' flag will allow the operation even if the node or any
   * associated links are currently ignited.
   *
   * Throws std::invalid_argument if validation fails, or if the node or any
   * associated links are still ignited and 'force' is false.
   *
   * This method will invoke writeToTsFile() upon success.
   */
  void delNode(const std::string& nodeName, const bool force);

  /**
   * Edit the given node in the topology.
   *
   * NOTE: Currently, the only supported changes are "name", "pop_node",
   * "ant_azimuth", "ant_elevation", and "has_cpe".
   *
   * Throws std::invalid_argument if validation fails.
   *
   * This method will invoke writeToTsFile() upon success.
   */
  void editNode(const std::string& nodeName, const thrift::Node& newNode);

  /**
   * Add the given link to the topology, after performing validation and
   * automatic parameter assignment (e.g. polarity, control superframe,
   * Golay index).
   *
   * Throws various exceptions if validation fails.
   *
   * Will invoke writeToTsFile() if successful and 'saveToFile' is true.
   */
  void addLink(thrift::Link& newLink, bool saveToFile = true);

  /**
   * Delete the given link from the topology.
   *
   * Setting the 'force' flag will allow the operation even if the link is
   * currently ignited.
   *
   * Throws std::invalid_argument if validation fails, or if the link is still
   * ignited and 'force' is false.
   *
   * This method will invoke writeToTsFile() upon success.
   */
  void delLink(
      const std::string& aNodename,
      const std::string& zNodeName,
      const bool force);

  /**
   * Add the given site to the topology, after performing validation.
   *
   * Throws std::invalid_argument if validation fails.
   *
   * This method will invoke writeToTsFile() upon success.
   */
  void addSite(const thrift::Site& newSite);

  /**
   * Delete the given site from the topology.
   *
   * Throws std::invalid_argument if validation fails (e.g. site is still
   * associated with nodes).
   *
   * This method will invoke writeToTsFile() upon success.
   */
  void delSite(const std::string& siteName);

  /**
   * Edit the given site in the topology.
   *
   * Throws std::invalid_argument if validation fails.
   *
   * This method will invoke writeToTsFile() upon success.
   */
  void editSite(const std::string& siteName, const thrift::Site& newSite);

  /**
   * Set the location of the node with the given MAC address.
   *
   * Returns true if successful, or false if either the MAC address is invalid,
   * the new location does not improve the accuracy of the current location, or
   * the new location is not valid.
   */
  bool setLocation(const std::string& mac, const thrift::Location& location);

  /**
   * Set node prefix used for prefix allocation.
   *
   * Throws std::invalid_argument if nodeName is invalid.
   *
   * This method will invoke writeToTsFile() upon success.
   */
  void setNodePrefix(
      const std::string& nodeName,
      const std::optional<folly::CIDRNetwork> prefix);

  /**
   * Overwrite the determinisitc_prefix_alloc_params zones.
   *
   * This method will invoke writeToTsFile().
   */
  void setPrefixZones(std::unordered_map<std::string, thrift::Zone>& zones);

  /**
   * Set current controller prefix allocation scheme.
   */
  void setControllerPrefixAllocScheme(
      ControllerPrefixAllocScheme controllerPrefixAlloc);

 private:
  /**
   * Read the given topology file, and set 'topology' to the deserialized
   * struct and 'contents' to the raw file contents.
   */
  void readTopologyFile(
      const std::string& topologyFile,
      thrift::Topology& topology,
      std::string& contents);

  /** Populate all internal map structures using the current topology. */
  void populateMaps(bool validate);

  /**
   * Set the MAC address fields for the given node to a standard format.
   *
   * This method will invoke MacUtils::standardizeMac().
   *
   * Throws std::invalid_argument if any MAC address could not be parsed.
   */
  static void standardizeNodeMacs(thrift::Node& node);

  /**
   * Set the MAC address fields for the given link to a standard format.
   *
   * This method will invoke MacUtils::standardizeMac().
   *
   * Throws std::invalid_argument if any MAC address could not be parsed.
   */
  static void standardizeLinkMacs(thrift::Link& node);

  /**
   * Validate the given node, and modify the node struct if needed.
   *
   * This method will invoke validateNodeName().
   *
   * Throws std::invalid_argument if validation fails (e.g. MAC already exists,
   * invalid node type).
   */
  void validateNode(thrift::Node& node) const;

  /**
   * Validate the given link, and modify the link struct if needed.
   *
   * This method will invoke GolayHelper::validateGolayIdx() and
   * validateLinkDistance().
   *
   * Throws std::invalid_argument if validation fails (e.g. A/Z nodes do not
   * exist, A/Z node MAC addresses are incorrect, link name is incorrect,
   * attempting to create CN-to-CN link).
   */
  void validateLink(thrift::Link& link) const;

  /**
   * Validate the given site.
   *
   * Throws std::invalid_argument if validation fails (e.g. empty site name,
   * site name already exists).
   */
  void validateSite(const thrift::Site& site) const;

  /**
   * Validate the given node name.
   *
   * Throws std::invalid_argument if validation fails (e.g. empty node name,
   * node name already exists).
   */
  void validateNodeName(const std::string& nodeName) const;

  /**
   * Validate the given location (geographic coordinates and accuracy).
   *
   * Returns true if valid, otherwise false and sets `errMsg` accordingly.
   */
  bool validateLocation(
      const thrift::Location& location, std::string& errMsg) const;

  /**
   * Insert the given node-to-site entry into site2AssocNodes_.
   *
   * This will create wired links if any other nodes exist in the site and
   * 'validate' is true.
   *
   * Throws std::invalid_argument if validation fails (e.g. empty site name,
   * site does not exist, too many nodes on the site) and 'validate' is true.
   */
  void plugNodeToSite(
      const std::string& nodeName,
      const std::string& siteName,
      bool validate = true);

  /**
   * Validate the physical distance between the two nodes on the given link.
   *
   * This will log a warning if nodes are too far apart (but does not throw).
   */
  void validateLinkDistance(thrift::Link& link) const;

  /**
   * Validate the number of links on both ends of this new link.
   *
   * This method will invoke validateNodeMaxLinks().
   *
   * Throws std::invalid_argument if validation fails (e.g. A/Z nodes exceed
   * maximum supported number of wireless links).
   */
  void validateMaxLinks(const thrift::Link& newLink) const;

  /**
   * Validate the number of links between the given node and a neighbor along a
   * new link.
   *
   * Throws std::invalid_argument if validation fails (e.g. maximum supported
   * number of DN-DN or DN-CN links would be exceeded, multiple primary links
   * to a CN).
   */
  void validateNodeMaxLinks(
      const std::string& nodeName,
      const std::string& nbrNodeName,
      const thrift::Link& newLink) const;

  /**
   * Create all intrasite ETHERNET links.
   *
   * If 'nodes' is non-empty, it will only create links for those nodes.
   */
  void createSiteLinks(bool validate, std::vector<thrift::Node> nodes = {});

  /** The current working topology. */
  thrift::Topology topology_;

  /** Map of node names to their memory addresses in topology_. */
  std::map<std::string, thrift::Node*> name2Node_;
  /** Map of link names to their memory addresses in topology_. */
  std::map<std::string, thrift::Link*> name2Link_;
  /** Map of site names to their memory addresses in topology_. */
  std::map<std::string, thrift::Site*> name2Site_;

  /** Map of MAC addresses (id or wlan) to node names. */
  std::map<std::string, std::string> mac2NodeName_;

  /** Map of site names to nodes within that site. */
  std::map<std::string, std::unordered_set<std::string>> site2AssocNodes_;

  /** Topology filename for initialization. */
  std::string topologyFile_;

  /** Directory to save timestamped topology file whenever topology changes. */
  const folly::fs::path topologyDir_;

  /** Whether any topology validation should be performed. */
  bool validateTopology_;

  /** Whether or not to automatically create intrasite wired links. */
  bool createIntrasiteLinks_{false};

  /** Current prefix allocation scheme used in controller. */
  ControllerPrefixAllocScheme controllerPrefixAlloc_{
      ControllerPrefixAllocScheme::NONE};

};

} // namespace terragraph
} // namespace facebook
