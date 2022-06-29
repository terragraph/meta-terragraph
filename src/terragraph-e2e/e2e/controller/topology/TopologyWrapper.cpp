/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TopologyWrapper.h"

#include <algorithm>
#include <cmath>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/gen/Base.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <time.h>

#include "e2e/common/JsonUtils.h"
#include "e2e/common/MacUtils.h"

using apache::thrift::detail::TEnumMapFactory;
using std::invalid_argument;
using std::string;
using std::unordered_set;
using std::vector;

namespace {
// prefix of timestamped topology file
const string kTopoTsFilePrefix{"e2e_topology"};

// maximum lengths of ethernet/wireless links in meters (for warnings only)
const int kMaxEthLenM{50};
const int kMaxRfLenM{500};

void
createDir(const string& dir) {
  if (dir.empty()) {
    throw invalid_argument(folly::sformat("`{}` is empty", dir));
  }
  if (folly::fs::exists(dir) && !folly::fs::is_directory(dir)) {
    throw invalid_argument(folly::sformat("`{}` is not a directory", dir));
  }
  if (folly::fs::exists(dir)) {
    return;
  }
  try {
    folly::fs::create_directories(dir);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not create directory " << dir;
    throw;
  }
}
}; // namespace

namespace facebook {
namespace terragraph {

// -- CONSTANTS -- //
const size_t TopologyWrapper::kMaxNumOfNodesPerSite = 4;
const size_t TopologyWrapper::kMaxNumOfDnDnLinks = 2;
const size_t TopologyWrapper::kMaxNumOfDnCnLinks = 15;

double
approxDistance(const thrift::Location& l1, const thrift::Location& l2) {
  // https://en.wikipedia.org/wiki/Earth
  // Circumference 40,075.017 km (24,901.461 mi) (equatorial)
  const int earthCircumference = 40075017;
  const double pi = 3.14159265;
  const int deg = 360;
  const double rad = 2 * pi;
  const int lengthPerDeg = earthCircumference / deg;
  double avgLatitudeRadian = ((l1.latitude + l2.latitude) / 2) * (rad / deg);
  // calculate distance across latitude change
  double dLat = fabs(l1.latitude - l2.latitude) * lengthPerDeg;
  // calculate distance across longitude change
  // take care of links across 180 meridian and effect of different latitudes
  double dLong = fabs(l1.longitude - l2.longitude);
  if (dLong > (deg / 2))
    dLong = deg - dLong;
  dLong *= lengthPerDeg * cos(avgLatitudeRadian);
  // calculate distance across altitude change
  double dAlt = fabs(l1.altitude - l2.altitude);
  // assume orthogonality over small distance
  double dist = sqrt((dLat * dLat) + (dLong * dLong) + (dAlt * dAlt));

  return dist;
}

double
computeAngle(const thrift::Location& l1, const thrift::Location& l2) {
  double lon = l1.longitude;
  double lat = l1.latitude;
  double lonRef = l2.longitude;
  double latRef = l2.latitude;

  double R = 6371000.0;  // Earth's radius (in meters)
  double deg_in_rad = M_PI / 180.0;
  double x =
      R * (lon - lonRef) * deg_in_rad * cos((lat + latRef) * deg_in_rad / 2.0);
  double y = R * ((lat - latRef) * deg_in_rad);
  return atan2(y, x) / M_PI * 180;
}

TopologyWrapper::TopologyWrapper() : validateTopology_(false) {
}

TopologyWrapper::TopologyWrapper(
    const thrift::Topology& topology,
    const std::string& topologyDir,
    const bool validateTopology,
    const bool createIntrasiteLinks)
    : topology_(topology),
      topologyDir_(topologyDir),
      validateTopology_(validateTopology),
      createIntrasiteLinks_(createIntrasiteLinks) {
  // Create new topology directory for saving files (optional)
  if (!topologyDir.empty()) {
    createDir(topologyDir);
  }

  populateMaps(validateTopology);

  if (createIntrasiteLinks_) {
    createSiteLinks(validateTopology);
  }
}

TopologyWrapper::TopologyWrapper(
    const std::string& topologyFile,
    const std::string& topologyDir,
    const bool createIntrasiteLinks)
    : topologyFile_(topologyFile),
      topologyDir_(topologyDir),
      validateTopology_(true),
      createIntrasiteLinks_(createIntrasiteLinks) {
  // Read topology file from disk
  string contents;
  readTopologyFile(topologyFile, topology_, contents);
  try {
    contents = JsonUtils::toSortedPrettyJson(contents);
  } catch (const std::exception&ex) {
    LOG(ERROR) << "Could not prettify JSON: " << folly::exceptionStr(ex);
  }

  // Back up original topology file
  string originalTopologyFile = folly::sformat("{}.orgin", topologyFile);
  if (!folly::writeFile(contents, originalTopologyFile.c_str())) {
    throw invalid_argument(folly::sformat(
        "Could not back up topology file: `{}`", originalTopologyFile));
  }

  // Create new topology directory for saving files (optional)
  if (!topologyDir.empty()) {
    createDir(topologyDir);
  }

  populateMaps(true /* validate */);

  if (createIntrasiteLinks_) {
    createSiteLinks(true /* validate */);
  }

  // Write a timestamped file immediately
  writeToTsFile();
}

void
TopologyWrapper::setTopology(const thrift::Topology& topology) {
  topology_ = topology;
  populateMaps(false /* validate */);
}

void
TopologyWrapper::setTopologyFromFile(const string& topologyFile) {
  string contents;
  readTopologyFile(topologyFile, topology_, contents);
  populateMaps(false /* validate */);
}

void
TopologyWrapper::readTopologyFile(
    const string& topologyFile, thrift::Topology& topology, string& contents) {
  if (topologyFile.empty()) {
    throw invalid_argument("Topology filename is empty");
  }

  // Read the file
  if (!folly::readFile(topologyFile.c_str(), contents)) {
    throw invalid_argument(
        folly::sformat("Could not read topology file: `{}`", topologyFile));
  }

  // Allow empty files (return an empty struct)
  if (folly::trimWhitespace(contents).empty()) {
    LOG(INFO) << "Reading empty topology file: " << topologyFile;
    topology = thrift::Topology();
    return;
  }

  // Deserialize the topology struct
  apache::thrift::SimpleJSONSerializer jsonSerializer;
  try {
    jsonSerializer.deserialize<thrift::Topology>(contents, topology);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not parse topology struct " << folly::exceptionStr(ex);
    throw;
  }
}

void
TopologyWrapper::standardizeNodeMacs(thrift::Node& node) {
  if (!node.mac_addr.empty()) {
    node.mac_addr = MacUtils::standardizeMac(node.mac_addr);
  }
  for (auto& macAddr : node.wlan_mac_addrs) {
    macAddr = MacUtils::standardizeMac(macAddr);
  }
}

void
TopologyWrapper::standardizeLinkMacs(thrift::Link& link) {
  if (!link.a_node_mac.empty()) {
    link.a_node_mac = MacUtils::standardizeMac(link.a_node_mac);
  }
  if (!link.z_node_mac.empty()) {
    link.z_node_mac = MacUtils::standardizeMac(link.z_node_mac);
  }
}

void
TopologyWrapper::validateNodeName(const std::string& nodeName) const {
  // node name validity check
  if (nodeName.empty()) {
    throw invalid_argument("Node name cannot be empty");
  }
  if (name2Node_.find(nodeName) != name2Node_.end()) {
    throw invalid_argument("Multiple nodes with same name " + nodeName);
  }
}

void
TopologyWrapper::validateNode(thrift::Node& node) const {
  validateNodeName(node.name);

  // NOTE: site validation is done in plugNodeToSite()

  // mac addr validity check
  if (!node.mac_addr.empty()) {
    if (mac2NodeName_.count(node.mac_addr)) {
      throw invalid_argument("Multiple nodes with same mac " + node.mac_addr);
    }
  }

  for (const auto& macAddr : node.wlan_mac_addrs) {
    if (mac2NodeName_.count(macAddr)) {
      throw invalid_argument("Multiple nodes with same mac " + macAddr);
    }
  }

  // node type validity check
  if (static_cast<int>(node.node_type) == 0) {
    LOG(WARNING) << "Node type is not set for " << node.name
                 << ", default to DN";
    node.node_type = thrift::NodeType::DN;
  }
  auto nodeTypeMap = TEnumMapFactory<thrift::NodeType>::makeValuesToNamesMap();
  if (nodeTypeMap.find(node.node_type) == nodeTypeMap.end()) {
    throw invalid_argument(folly::sformat(
        "Invalid node type: {}", static_cast<int>(node.node_type)));
  }

  // node status validity check
  if (static_cast<int>(node.status) == 0) {
    node.status = thrift::NodeStatusType::OFFLINE;
  }
}

void
TopologyWrapper::validateLink(thrift::Link& link) const {
  // node name validity check
  auto aNodeIter = name2Node_.find(link.a_node_name);
  if (aNodeIter == name2Node_.end()) {
    throw invalid_argument(
        folly::sformat("a_node_name `{}` does not exist", link.a_node_name));
  }
  auto zNodeIter = name2Node_.find(link.z_node_name);
  if (zNodeIter == name2Node_.end()) {
    throw invalid_argument(
        folly::sformat("z_node_name `{}` does not exist", link.z_node_name));
  }

  // disallow CN-to-CN links
  if (aNodeIter->second->node_type == thrift::NodeType::CN &&
      zNodeIter->second->node_type == thrift::NodeType::CN) {
    throw invalid_argument("Cannot create CN-to-CN link");
  }

  if (link.link_type == thrift::LinkType::WIRELESS) {
    // we no longer fill a_node_mac / z_node_mac automatically
  } else if (link.link_type == thrift::LinkType::ETHERNET) {
    link.a_node_mac = "";
    link.z_node_mac = "";
  } else {
    throw invalid_argument(
      folly::sformat("Invalid link_type `{}`", (int)link.link_type));
  }

  // node mac validity check
  if (!link.a_node_mac.empty()) {
    auto aNodeNameIter = mac2NodeName_.find(link.a_node_mac);
    if (aNodeNameIter == mac2NodeName_.end()) {
      throw invalid_argument(
          folly::sformat("a_node_mac `{}` does not exist", link.a_node_mac));
    }
    if (aNodeNameIter->second != link.a_node_name) {
      throw invalid_argument(folly::sformat(
          "a_node_mac `{}` belongs to `{}`, but a_node_name is `{}`",
          link.a_node_mac, aNodeNameIter->second, link.a_node_name));
    }
  }
  if (!link.z_node_mac.empty()) {
    auto zNodeNameIter = mac2NodeName_.find(link.z_node_mac);
    if (zNodeNameIter == mac2NodeName_.end()) {
      throw invalid_argument(
          folly::sformat("z_node_mac `{}` does not exist", link.z_node_mac));
    }
    if (zNodeNameIter->second != link.z_node_name) {
      throw invalid_argument(folly::sformat(
          "z_node_mac `{}` belongs to `{}`, but z_node_name is `{}`",
          link.z_node_mac, zNodeNameIter->second, link.z_node_name));
    }
  }

  // link name validity check
  if (link.a_node_name >= link.z_node_name) {
    throw invalid_argument(folly::sformat(
        "a_node_name `{}` should be lexicographically smaller than "
        "z_node_name `{}`",
        link.a_node_name,
        link.z_node_name));
  }
  const std::string expectedLinkName = buildLinkName(
      link.a_node_name, link.z_node_name);
  if (link.name.empty()) {
    // fill out link name if left empty
    link.name = expectedLinkName;
  } else {
    if (link.name != expectedLinkName) {
      throw invalid_argument(folly::sformat(
          "Invalid link name `{}`, expected format: "
          "`link-[a_node_name]-[z_node_name]`",
          link.name));
    }
  }
  if (name2Link_.find(link.name) != name2Link_.end()) {
    throw invalid_argument("Multiple links with same name " + link.name);
  }

  validateMaxLinks(link);
  // validate length (location)
  validateLinkDistance(link);
}

string
TopologyWrapper::buildLinkName(
    const string& aNodeName, const string& zNodeName) {
  return folly::sformat("link-{}-{}", aNodeName, zNodeName);
}

void
TopologyWrapper::validateMaxLinks(
    const thrift::Link& newLink) const {
  // validate maximum links for each node
  validateNodeMaxLinks(
      newLink.a_node_name, newLink.z_node_name, newLink);
  validateNodeMaxLinks(
      newLink.z_node_name, newLink.a_node_name, newLink);
}

void
TopologyWrapper::validateNodeMaxLinks(
    const string& nodeName,
    const string& nbrNodeName,
    const thrift::Link& newLink) const {
  if (newLink.link_type != thrift::LinkType::WIRELESS) {
    return;
  }

  auto node = getNode(nodeName);
  auto newNbrNode = getNode(nbrNodeName);
  std::string radioMac;
  if (newLink.a_node_name == nodeName) {
    radioMac = newLink.a_node_mac;
  } else if (newLink.z_node_name == nodeName) {
    radioMac = newLink.z_node_mac;
  }

  if (node->node_type == thrift::NodeType::DN) {
    if (newNbrNode->node_type == thrift::NodeType::DN) {
      // validate max supported DN-DN links
      std::unordered_set<string> dnDnLinks;
      dnDnLinks.insert(newLink.name);
      for (const auto& link : getLinksByNodeName(nodeName)) {
        if (link.link_type != thrift::LinkType::WIRELESS) {
          continue;
        }
        if (!radioMac.empty() &&
            link.a_node_mac != radioMac && link.z_node_mac != radioMac) {
          continue;  // different radio
        }
        auto nbrNode = (link.a_node_name == nodeName)
                           ? getNode(link.z_node_name)
                           : getNode(link.a_node_name);
        if (nbrNode->node_type == thrift::NodeType::DN) {
          dnDnLinks.insert(link.name);
        }
      }
      if (dnDnLinks.size() > kMaxNumOfDnDnLinks) {
        throw invalid_argument(folly::sformat(
            "Exceeded maximum supported DN-DN links ({}) with link `{}`",
            kMaxNumOfDnDnLinks,
            newLink.name));
      }
    } else if (newNbrNode->node_type == thrift::NodeType::CN) {
      // validate max supported DN-CN links (from DN's point of view)
      std::unordered_set<string> dnCnLinks;
      dnCnLinks.insert(newLink.name);
      for (const auto& link : getLinksByNodeName(nodeName)) {
        if (link.link_type != thrift::LinkType::WIRELESS) {
          continue;
        }
        if (!radioMac.empty() &&
            link.a_node_mac != radioMac && link.z_node_mac != radioMac) {
          continue;  // different radio
        }
        auto nbrNode = (link.a_node_name == nodeName)
                           ? getNode(link.z_node_name)
                           : getNode(link.a_node_name);
        if (nbrNode->node_type == thrift::NodeType::CN) {
          dnCnLinks.insert(link.name);
        }
      }
      if (dnCnLinks.size() > kMaxNumOfDnCnLinks) {
        throw invalid_argument(folly::sformat(
            "Exceeded maximum supported DN-CN links ({}) with link `{}`",
            kMaxNumOfDnCnLinks,
            newLink.name));
      }
    }
  } else if (node->node_type == thrift::NodeType::CN) {
    if (newNbrNode->node_type == thrift::NodeType::DN) {
      // validate max supported DN-CN links (from CN's point of view)
      if (!newLink.is_backup_cn_link_ref().has_value()
          || !newLink.is_backup_cn_link_ref().value()) {
        for (const auto& link : getLinksByNodeName(nodeName)) {
          if (link.link_type != thrift::LinkType::WIRELESS) {
            continue;
          }
          if (link.name == newLink.name) {
            continue;
          }
          if (!radioMac.empty() &&
              link.a_node_mac != radioMac && link.z_node_mac != radioMac) {
            continue;  // different radio
          }
          // assume this is a DN-CN link
          if (!link.is_backup_cn_link_ref().has_value() ||
              !link.is_backup_cn_link_ref().value()) {
            throw invalid_argument(folly::sformat(
                "Cannot have multiple primary links to CN `{}` (failed with "
                "link `{}`)",
                node->name,
                newLink.name));
          }
        }
      }
    }
  }

  // verify # of dn + cn links is under the limit
  size_t wirelessLinkCount = 0;
  std::vector<thrift::Link> allLinks = radioMac.empty()
      ? getLinksByNodeName(nodeName)
      : getLinksByRadioMac(radioMac);
  for (const auto& link : allLinks) {
    if (link.link_type == thrift::LinkType::WIRELESS &&
        link.name != newLink.name) {
      wirelessLinkCount++;
    }
  }
  if (wirelessLinkCount >= kMaxNumOfDnCnLinks) {
    throw invalid_argument(folly::sformat(
        "Exceeded maximum supported links ({}) with link `{}`",
        kMaxNumOfDnCnLinks,
        newLink.name));
  }
}

bool
TopologyWrapper::validateLocation(
    const thrift::Location& location, std::string& errMsg) const {
  if (location.latitude < -90 || location.latitude > 90) {
    errMsg = "Invalid latitude (valid range: -90 <= x <= 90)";
    return false;
  }
  if (location.longitude < -180 || location.longitude > 180) {
    errMsg = "Invalid longitude (valid range: -180 <= x <= 180)";
    return false;
  }
  if (location.accuracy < 0) {
    errMsg = "Invalid accuracy (valid range: x >= 0)";
    return false;
  }
  return true;
}

void
TopologyWrapper::validateSite(const thrift::Site& site) const {
  // site name validity check
  if (site.name.empty()) {
    throw invalid_argument("Site name cannot be empty");
  }
  if (name2Site_.find(site.name) != name2Site_.end()) {
    throw invalid_argument("Multiple sites with same name " + site.name);
  }

  // location check
  std::string errMsg;
  if (!validateLocation(site.location, errMsg)) {
    throw invalid_argument(folly::sformat("Site '{}': {}", site.name, errMsg));
  }
}

void
TopologyWrapper::validateLinkDistance(thrift::Link& link) const {
  // get aSite and zSite
  // assume that aNode, zNode, aSite, zSite are valid
  const auto aNode = getNode(link.a_node_name);
  const auto zNode = getNode(link.z_node_name);
  const auto aSite = getSite(aNode->site_name);
  const auto zSite = getSite(zNode->site_name);

  // validate length between aSite and zSite
  // relax validation if accuracy is bad (high error)
  double len = approxDistance(aSite->location, zSite->location);
  double error = aSite->location.accuracy + zSite->location.accuracy;
  double d = len - error;

  if (link.link_type == thrift::LinkType::WIRELESS && d > kMaxRfLenM) {
    LOG(WARNING) << "Wireless link '" << link.name << "' with long length "
                 << std::to_string(len) << "m, combined site accuracy/error = "
                 << std::to_string(error) << "m";
  } else if (link.link_type == thrift::LinkType::ETHERNET && d > kMaxEthLenM) {
    LOG(WARNING) << "Ethernet link '" << link.name << "' with long length "
                 << std::to_string(len) << "m, combined site accuracy/error = "
                 << std::to_string(error) << "m";
  }
}

void
TopologyWrapper::createSiteLinks(
    bool validate, std::vector<thrift::Node> nodes) {
  if (!validate) {
    return;
  }
  for (auto& node : (nodes.empty() ? topology_.nodes : nodes)) {
    for (const auto& assocNodeName : site2AssocNodes_[node.site_name]) {
      if (node.name == assocNodeName) {
        continue;
      }

      auto aNode = node;
      auto zNode = getNode(assocNodeName).value();
      if (aNode.node_type == thrift::NodeType::CN &&
          zNode.node_type == thrift::NodeType::CN) {
        continue;  // don't create CN-to-CN links (not allowed)
      }

      // Add intra-site link
      thrift::Link link;
      link.a_node_name = aNode.name;
      link.z_node_name = zNode.name;
      link.link_type = thrift::LinkType::ETHERNET;
      link.is_alive = true;
      updateLink(link);  // fix a/z + name
      if (name2Link_.count(link.name)) {
        continue;  // already exists, skip
      }
      addLink(link, false /* saveToFile */);
      VLOG(1) << "Added intra-site link: " << link.name
              << " on site: " << node.site_name;
    }
  }
}

void
TopologyWrapper::populateMaps(bool validate) {
  // clear all maps
  name2Node_.clear();
  name2Link_.clear();
  name2Site_.clear();
  mac2NodeName_.clear();
  site2AssocNodes_.clear();

  // topology name verification
  if (validate) {
    if (topology_.name.empty()) {
      LOG(WARNING) << "No name given for topology";
    }
  }

  // site verification
  for (auto& site : topology_.sites) {
    if (validate) {
      validateSite(site);
    }
    name2Site_[site.name] = &site;
  }

  // node verification
  for (auto& node : topology_.nodes) {
    standardizeNodeMacs(node);
    if (validate) {
      validateNode(node);
    }
    if (!node.mac_addr.empty()) {
      mac2NodeName_[node.mac_addr] = node.name;
    }
    for (const auto& macAddr : node.wlan_mac_addrs) {
      mac2NodeName_[macAddr] = node.name;
    }
    name2Node_[node.name] = &node;

    plugNodeToSite(node.name, node.site_name, validate);
  }
  if (validate) {
    if (getPopNodeNames().empty()) {
      LOG(WARNING) << "At least one pop node should be provided.";
    }
  }

  // link verification (in 2 passes)
  // 1st pass: do basic checks only
  // 2nd pass: do complex checks because topology is valid
  for (auto& link : topology_.links) {
    standardizeLinkMacs(link);
    if (validate) {
      validateLink(link);
    }
  }
  for (auto& link : topology_.links) {
    if (validate) {
      validateMaxLinks(link);
    }
    name2Link_[link.name] = &link;
  }
}

void
TopologyWrapper::sanitizeState() {
  for (auto& link : topology_.links) {
    // Sanitize link state.
    // At bring-up assume all wireless links are down, and wired links are up.
    if (link.link_type == thrift::LinkType::WIRELESS) {
      setLinkStatus(link.name, false);
    }
    if (link.link_type == thrift::LinkType::ETHERNET) {
      setLinkStatus(link.name, true);
    }
    link.linkup_attempts = 0;
  }

  // Sanitize node state.
  // At bring-up assume all nodes are down.
  for (auto& node : topology_.nodes) {
    setNodeStatus(node.name, thrift::NodeStatusType::OFFLINE);
  }
}

bool
TopologyWrapper::writeToFile(const string& outputFile) const {
  string contents;
  apache::thrift::SimpleJSONSerializer jsonSerializer;
  try {
    jsonSerializer.serialize(topology_, &contents);
    contents = JsonUtils::toSortedPrettyJson(contents);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not serialize topology " << folly::exceptionStr(ex);
    return false;
  }
  if (!folly::writeFile(contents, outputFile.c_str())) {
    LOG(ERROR) << "Could not write to file " << outputFile;
    return false;
  }
  return true;
}

void
TopologyWrapper::writeToTsFile() const {
  if (topologyDir_.empty() && topologyFile_.empty()) {
    return;
  }

  string contents;
  apache::thrift::SimpleJSONSerializer jsonSerializer;
  try {
    jsonSerializer.serialize(topology_, &contents);
    contents = JsonUtils::toSortedPrettyJson(contents);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not serialize topology " << folly::exceptionStr(ex);
    return;
  }

  time_t currentTime;
  struct tm localTime;
  time(&currentTime);
  localtime_r(&currentTime, &localTime);
  char buf[50];
  strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &localTime);

  const string tsFileName = folly::sformat(
      "{}/{}-{}.conf",
      topologyDir_.string(),
      kTopoTsFilePrefix,
      std::string(buf));

  const string backupTopologyFile =
      topologyFile_.empty()
          ? folly::sformat(
                "{}/{}.conf", topologyDir_.string(), kTopoTsFilePrefix)
          : topologyFile_;

  if (!folly::writeFile(contents, tsFileName.c_str())) {
    LOG(ERROR) << "Could not write to file " << tsFileName;
    return;
  }

  if (!folly::writeFile(contents, backupTopologyFile.c_str())) {
    LOG(ERROR) << "Could not write to file " << topologyFile_;
    return;
  }
}

thrift::Topology
TopologyWrapper::getTopology() const {
  return topology_;
}

vector<thrift::Node>
TopologyWrapper::getAllNodes() const {
  return topology_.nodes;
}

vector<thrift::Link>
TopologyWrapper::getAllLinks() const {
  return topology_.links;
}

vector<thrift::Site>
TopologyWrapper::getAllSites() const {
  return topology_.sites;
}

size_t
TopologyWrapper::getNodesCount() const {
  return topology_.nodes.size();
}

std::optional<thrift::Node>
TopologyWrapper::getNode(const std::string& nodeName) const {
  auto it = name2Node_.find(nodeName);
  if (it != name2Node_.end()) {
    return *(it->second);
  }
  return std::nullopt;
}

std::optional<thrift::Link>
TopologyWrapper::getLink(
    const std::string& aNodeName, const std::string& zNodeName) const {
  return getLink(buildLinkName(aNodeName, zNodeName));
}

std::optional<thrift::Link>
TopologyWrapper::getLink(const std::string& linkName) const {
  auto it = name2Link_.find(linkName);
  if (it != name2Link_.end()) {
    return *(it->second);
  }
  return std::nullopt;
}

std::vector<thrift::Link>
TopologyWrapper::getSameRadioLinks(const thrift::Link& link) const {
  std::vector<thrift::Link> dependentLinks;

  if (link.link_type != thrift::LinkType::WIRELESS || link.a_node_mac.empty() ||
      link.z_node_mac.empty()) {
    return dependentLinks;
  }

  auto aMacLinks = getLinksByRadioMac(link.a_node_mac);
  for (const auto& radioLink : aMacLinks) {
    if (radioLink.link_type == thrift::LinkType::WIRELESS &&
        radioLink.name != link.name && !radioLink.a_node_mac.empty() &&
        !radioLink.z_node_mac.empty()) {
      dependentLinks.push_back(radioLink);
    }
  }

  auto zMacLinks = getLinksByRadioMac(link.z_node_mac);
  for (const auto& radioLink : zMacLinks) {
    if (radioLink.link_type == thrift::LinkType::WIRELESS &&
        radioLink.name != link.name && !radioLink.a_node_mac.empty() &&
        !radioLink.z_node_mac.empty()) {
      dependentLinks.push_back(radioLink);
    }
  }

  return dependentLinks;
}

std::optional<thrift::Site>
TopologyWrapper::getSite(const string& siteName) const {
  auto it = name2Site_.find(siteName);
  if (it != name2Site_.end()) {
    return *(it->second);
  }
  return std::nullopt;
}

std::optional<thrift::Node>
TopologyWrapper::getNodeByMac(const string& nodeMac) const {
  auto nodeName = getNodeNameByMac(nodeMac);
  if (!nodeName) {
    return std::nullopt;
  }
  return *((name2Node_.find(*nodeName))->second);
}

std::optional<string>
TopologyWrapper::getNodeNameByMac(const string& macAddr) const {
  if (macAddr.empty()) {
    return std::nullopt;
  }

  string mac;
  try {
    mac = MacUtils::standardizeMac(macAddr);
  } catch (std::invalid_argument& ex) {
    LOG(ERROR) << "Could not parse MAC: " << folly::exceptionStr(ex);
    return std::nullopt;
  }

  auto it = mac2NodeName_.find(mac);
  if (it == mac2NodeName_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::map<std::string, std::string>
TopologyWrapper::getMac2NodeNameMap() const {
  return mac2NodeName_;
}

std::optional<unordered_set<string>>
TopologyWrapper::getNodeNamesBySiteName(const string& siteName) const {
  auto it = site2AssocNodes_.find(siteName);
  if (it != site2AssocNodes_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<string>
TopologyWrapper::getMacByNodeName(const std::string& nodeName) const {
  auto it = name2Node_.find(nodeName);
  if (it != name2Node_.end()) {
    return it->second->mac_addr;
  }
  return std::nullopt;
}

vector<thrift::Node>
TopologyWrapper::getCNs() const {
  return getNodesByType(thrift::NodeType::CN);
}

vector<thrift::Node>
TopologyWrapper::getDNs() const {
  return getNodesByType(thrift::NodeType::DN);
}

vector<thrift::Node>
TopologyWrapper::getNodesByType(thrift::NodeType nodeType) const {
  return folly::gen::from(topology_.nodes) |
         folly::gen::filter([nodeType](const thrift::Node& node) {
           return (node.node_type == nodeType);
         }) |
         folly::gen::as<std::vector<thrift::Node>>();
}

unordered_set<std::string>
TopologyWrapper::getNodeNamesByType(thrift::NodeType nodeType) const {
  return folly::gen::from(topology_.nodes) |
         folly::gen::filter([nodeType](const thrift::Node& node) {
           return (node.node_type == nodeType);
         }) |
         folly::gen::field(&thrift::Node::name) |
         folly::gen::as<std::unordered_set<std::string>>();
}

vector<thrift::Node>
TopologyWrapper::getNodesBySiteName(const string& siteName) const {
  auto it = site2AssocNodes_.find(siteName);
  if (it != site2AssocNodes_.end()) {
    const auto nodeNames = it->second;
    return folly::gen::from(nodeNames) |
           folly::gen::mapped([&](const string& nodeName) {
             return (*(name2Node_.find(nodeName))->second);
           }) |
           folly::gen::as<std::vector<thrift::Node>>();
  }
  return std::vector<thrift::Node>();
}

vector<thrift::Node>
TopologyWrapper::getPopNodes() const {
  return folly::gen::from(topology_.nodes) |
         folly::gen::filter(
             [](const thrift::Node& node) { return node.pop_node; }) |
         folly::gen::as<std::vector<thrift::Node>>();
}

vector<string>
TopologyWrapper::getPopNodeNames() const {
  return folly::gen::from(topology_.nodes) |
         folly::gen::filter(
             [](const thrift::Node& node) { return node.pop_node; }) |
         folly::gen::field(&thrift::Node::name) |
         folly::gen::as<std::vector<std::string>>();
}

vector<thrift::Link>
TopologyWrapper::getLinksByNodeName(const string& nodeName) const {
  return folly::gen::from(topology_.links) |
         folly::gen::filter([nodeName](const thrift::Link& link) {
           return (
               link.a_node_name == nodeName || link.z_node_name == nodeName);
         }) |
         folly::gen::as<std::vector<thrift::Link>>();
}

vector<thrift::Link>
TopologyWrapper::getLinksByRadioMac(const string& radioMac) const {
  if (radioMac.empty()) {
    return std::vector<thrift::Link>();
  }
  return folly::gen::from(topology_.links) |
         folly::gen::filter([radioMac](const thrift::Link& link) {
           return (
               link.a_node_mac == radioMac || link.z_node_mac == radioMac);
         }) |
         folly::gen::as<std::vector<thrift::Link>>();
}

vector<thrift::Link>
TopologyWrapper::getWirelessLinksByNodeName(const string& nodeName) const {
  return folly::gen::from(topology_.links) |
         folly::gen::filter([nodeName](const thrift::Link& link) {
           return (link.link_type == thrift::LinkType::WIRELESS &&
               (link.a_node_name == nodeName || link.z_node_name == nodeName)
             );
         }) |
         folly::gen::as<std::vector<thrift::Link>>();
}

std::optional<string>
TopologyWrapper::getLinkName(
    const string& aNodeName, const string& zNodeName) const {
  for (const auto& link : topology_.links) {
    if ((link.a_node_name == aNodeName && link.z_node_name == zNodeName) ||
        (link.a_node_name == zNodeName && link.z_node_name == aNodeName)) {
      return link.name;
    }
  }
  return std::nullopt;
}

std::optional<std::string>
TopologyWrapper::getNbrNodeName(
    const std::string& myNodeName, const std::string& linkName) const {
  auto it = name2Link_.find(linkName);
  if (it == name2Link_.end()) {
    return std::nullopt;
  }
  const auto& link = *(it->second);
  if (link.z_node_name == myNodeName) {
    return link.a_node_name;
  }
  if (link.a_node_name == myNodeName) {
    return link.z_node_name;
  }
  return std::nullopt;
}

std::optional<thrift::Node>
TopologyWrapper::getNbrNode(
    const std::string& myNodeName, const std::string& linkName) const {
  auto it = name2Link_.find(linkName);
  if (it == name2Link_.end()) {
    return std::nullopt;
  }
  return getNbrNode(myNodeName, *(it->second));
}

std::optional<thrift::Node>
TopologyWrapper::getNbrNode(
    const std::string& myNodeName, const thrift::Link& link) const {
  if (link.z_node_name == myNodeName) {
    return getNode(link.a_node_name);
  }
  if (link.a_node_name == myNodeName) {
    return getNode(link.z_node_name);
  }
  return std::nullopt;
}

std::string
TopologyWrapper::getTopologyName() const {
  return topology_.name;
}

void
TopologyWrapper::setTopologyName(const std::string& name) {
  topology_.name = name;

  // save the latest topology
  writeToTsFile();
}

bool
TopologyWrapper::setLinkStatus(const string& linkName, bool alive) {
  auto it = name2Link_.find(linkName);
  if (it == name2Link_.end()) {
    return false;
  }
  it->second->is_alive = alive;
  return true;
}

void
TopologyWrapper::setNodeMacByName(
    const std::string& nodeName,
    const std::string& macAddr,
    const bool force) {
  // standardize MAC addresses
  auto newMac = MacUtils::standardizeMac(macAddr);

  // check if node exists
  auto nameIt = name2Node_.find(nodeName);
  if (nameIt == name2Node_.end()) {
    throw invalid_argument("Node `" + nodeName + "` does not exist");
  }
  thrift::Node* node = nameIt->second;

  if (node->mac_addr == newMac) {
    throw invalid_argument("Node MAC address did not change");
  }
  const auto macIt = mac2NodeName_.find(newMac);
  if (macIt != mac2NodeName_.end() && macIt->second != nodeName) {
    throw invalid_argument(
        "MAC address `" + newMac + "` already associated with " +
        macIt->second);
  }

  // check if node is currently alive
  if (!force && node->status != thrift::NodeStatusType::OFFLINE) {
    throw invalid_argument("Node `" + nodeName + "` is still ignited.");
  }

  // erase the old macAddr->nodeName mappings
  mac2NodeName_.erase(node->mac_addr);

  // erase the old macAddr from nodes wlan_mac_addrs (if present)
  node->wlan_mac_addrs.erase(
      std::remove(
          node->wlan_mac_addrs.begin(),
          node->wlan_mac_addrs.end(),
          node->mac_addr),
      node->wlan_mac_addrs.end());

  // Update node
  node->mac_addr = newMac;
  // update mac2NodeName_
  mac2NodeName_[newMac] = nodeName;

  // save the latest topology
  writeToTsFile();
}

void
TopologyWrapper::changeNodeWlanMac(
    const std::string& nodeName,
    const std::string& oldMacAddr,
    const std::string& newMacAddr,
    const bool force) {
  // standardize MAC addresses
  auto oldMac = MacUtils::standardizeMac(oldMacAddr);
  auto newMac = MacUtils::standardizeMac(newMacAddr);

  // check if node exists
  auto nameIt = name2Node_.find(nodeName);
  if (nameIt == name2Node_.end()) {
    throw invalid_argument("Node `" + nodeName + "` does not exist");
  }
  thrift::Node* node = nameIt->second;

  if (oldMac == newMac) {
    throw invalid_argument("WLAN MAC address did not change");
  }

  if (std::find(
          node->wlan_mac_addrs.begin(), node->wlan_mac_addrs.end(), oldMac) ==
      node->wlan_mac_addrs.end()) {
    throw invalid_argument(
        "WLAN MAC address `" + oldMac + "` does not belong to " + nodeName);
  }

  const auto newMacIt = mac2NodeName_.find(newMac);
  if (newMacIt != mac2NodeName_.end()) {
    throw invalid_argument(
        "WLAN MAC address `" + newMac + "` already associated with " +
        newMacIt->second);
  }

  // update the node MAC address on any links
  updateLinksMacs(nodeName, oldMac, newMac, force);

  // Update wlan_mac_addrs
  node->wlan_mac_addrs.erase(
      std::remove(
          node->wlan_mac_addrs.begin(),
          node->wlan_mac_addrs.end(),
          oldMac),
      node->wlan_mac_addrs.end());
  node->wlan_mac_addrs.push_back(newMac);

  // update mac2NodeName_
  mac2NodeName_.erase(oldMac);
  mac2NodeName_[newMac] = nodeName;

  // save the latest topology
  writeToTsFile();
}

void
TopologyWrapper::addNodeWlanMacs(
    const std::string& nodeName,
    const std::vector<std::string>& wlanMacAddrs) {
  // check if node exists
  auto nameIt = name2Node_.find(nodeName);
  if (nameIt == name2Node_.end()) {
    throw invalid_argument("Node `" + nodeName + "` does not exist");
  }
  thrift::Node* node = nameIt->second;

  std::vector<std::string> newMacs;
  for (const std::string& wlanMac : wlanMacAddrs) {
    // standardize MAC address
    std::string stdWlanMac = MacUtils::standardizeMac(wlanMac);

    // Check if mac already belongs to a node
    const auto macIt = mac2NodeName_.find(stdWlanMac);
    if (macIt != mac2NodeName_.end()) {
      throw invalid_argument(
          "WLAN MAC address `" + stdWlanMac + "` is already associated with " +
          macIt->second);
    }

    // Make sure not to add duplicate MACs
    // (not using a set because we want to preserve order)
    auto newMacsIt = std::find(newMacs.begin(), newMacs.end(), stdWlanMac);
    if (newMacsIt == newMacs.end()) {
      newMacs.push_back(stdWlanMac);
    }
  }

  for (const std::string& macAddr : newMacs) {
    // Update wlan_mac_addrs
    node->wlan_mac_addrs.push_back(macAddr);
    // update mac2NodeName_
    mac2NodeName_[macAddr] = nodeName;
  }

  // save the latest topology
  writeToTsFile();
}

void
TopologyWrapper::deleteNodeWlanMacs(
    const std::string& nodeName,
    const std::vector<std::string>& wlanMacAddrs,
    const bool force) {
  // check if node exists
  auto nameIt = name2Node_.find(nodeName);
  if (nameIt == name2Node_.end()) {
    throw invalid_argument("Node `" + nodeName + "` does not exist");
  }
  thrift::Node* node = nameIt->second;

  auto nodeLinks = getLinksByNodeName(nodeName);
  std::vector<std::string> macsToDelete;
  for (const auto& wlanMac : wlanMacAddrs) {
    // standardize MAC address
    auto stdWlanMac = MacUtils::standardizeMac(wlanMac);

    // Check if mac exists
    const auto macIt = mac2NodeName_.find(stdWlanMac);
    if (macIt == mac2NodeName_.end()) {
      throw invalid_argument(
          "WLAN MAC address `" + stdWlanMac + "` does not exist");
    }

    // Check if mac belongs to another node
    if (macIt->second != nodeName) {
      throw invalid_argument(
          "WLAN MAC address `" + stdWlanMac + "` is associated with " +
          macIt->second);
    }

    // Check if mac is associated with any wireless link
    if (!force) {
      for (const auto& link : nodeLinks) {
        if (link.link_type == thrift::LinkType::WIRELESS &&
            link.is_alive &&
            (link.a_node_mac == stdWlanMac || link.z_node_mac == stdWlanMac)) {
          throw invalid_argument(
              "Link `" + link.name + "` associated with MAC `" + stdWlanMac +
              "` is still alive. Bring it down before deleting the MAC, or use"
              " the 'force' option to bypass this check.");
        }
      }
    }
    macsToDelete.push_back(stdWlanMac);
  }

  std::string errorMsg;
  for (const auto& macAddr : macsToDelete) {
    // Update wlan_mac_addrs
    auto wlanMacIt = std::find(
        node->wlan_mac_addrs.begin(), node->wlan_mac_addrs.end(), macAddr);
    if (wlanMacIt != node->wlan_mac_addrs.end()) {
      node->wlan_mac_addrs.erase(wlanMacIt);
    }
    // update mac2NodeName_
    mac2NodeName_.erase(macAddr);

    // delete all links associated with macAddr
    for (const auto& link : nodeLinks) {
      if (link.a_node_mac == macAddr || link.z_node_mac == macAddr) {
        delLink(link.a_node_name, link.z_node_name, force);
      }
    }
  }

  // save the latest topology
  writeToTsFile();
}

void
TopologyWrapper::updateLinksMacs(
    const std::string& nodeName,
    const std::string& oldMac,
    const std::string& newMac,
    const bool force) {

  // find all affected links
  std::vector<std::size_t> affectedLinks;
  for (std::size_t i = 0; i < topology_.links.size(); i++) {
    const auto& link = topology_.links[i];
    if (link.link_type != thrift::LinkType::WIRELESS) {
      continue;  // MAC only applies to wireless links
    }
    if ((link.a_node_name == nodeName && link.a_node_mac == oldMac) ||
        (link.z_node_name == nodeName && link.z_node_mac == oldMac)) {
      // check if we can update mac address on link
      if (!force && link.is_alive) {
        throw invalid_argument(
            "Link `" + link.name + "` associated with MAC `" + oldMac +
            "` is still alive. Bring it down before changing the MAC, or use"
            " the 'force' option to bypass this check.");
      }
      affectedLinks.push_back(i);
    }
  }

  std::string errorMsg;
  for (auto linkIndex : affectedLinks) {
    auto& link = topology_.links[linkIndex];
    if (link.a_node_name == nodeName) {
      link.a_node_mac = newMac;
    } else {
      link.z_node_mac = newMac;
    }
  }
}

void
TopologyWrapper::plugNodeToSite(
    const std::string& nodeName, const std::string& siteName, bool validate) {
  // site validation
  if (validate) {
    if (siteName.empty()) {
      throw invalid_argument("Empty site name for node " + nodeName);
    }
    if (!name2Site_.count(siteName)) {
      throw invalid_argument(
          "Failed in associating node " + nodeName +
          " with unrecognized site " + siteName);
    }
    if (site2AssocNodes_[siteName].size() >= kMaxNumOfNodesPerSite) {
      throw invalid_argument(
          "Trying to associate more than " +
          std::to_string(kMaxNumOfNodesPerSite) + " nodes to site: " +
          siteName);
    }
  }

  // update site2AssocNodes_
  site2AssocNodes_[siteName].insert(nodeName);
}

void
TopologyWrapper::unplugNodeFromSite(const std::string& nodeName) {
  auto it = name2Node_.find(nodeName);
  if (it == name2Node_.end()) {
    throw invalid_argument("Unplug node with invalid node name: " + nodeName);
  }

  // erase the old site->node association
  if (!it->second->site_name.empty()) {
    site2AssocNodes_[it->second->site_name].erase(nodeName);
  }

  // empty site name
  it->second->site_name.clear();
}

bool
TopologyWrapper::setNodeStatus(
    const string& nodeName, thrift::NodeStatusType status) {
  auto it = name2Node_.find(nodeName);
  if (it == name2Node_.end()) {
    return false;
  }
  it->second->status = status;
  return true;
}

bool
TopologyWrapper::bumpLinkupAttempts(const string& linkName) {
  auto link = name2Link_.find(linkName);
  if (link == name2Link_.end()) {
    return false;
  }
  link->second->linkup_attempts++;
  return true;
}

bool
TopologyWrapper::resetLinkupAttempts(const string& linkName) {
  auto link = name2Link_.find(linkName);
  if (link == name2Link_.end()) {
    return false;
  }
  link->second->linkup_attempts = 0;
  return true;
}

void
TopologyWrapper::addNode(thrift::Node& newNode) {
  standardizeNodeMacs(newNode);
  validateNode(newNode);
  plugNodeToSite(newNode.name, newNode.site_name, true);

  if (!newNode.mac_addr.empty()) {
    mac2NodeName_[newNode.mac_addr] = newNode.name;
  }
  for (const auto& macAddr : newNode.wlan_mac_addrs) {
    mac2NodeName_[macAddr] = newNode.name;
  }

  topology_.nodes.push_back(newNode);

  // update all pointers since vector can be resized
  for (auto& node : topology_.nodes) {
    name2Node_[node.name] = &node;
  }

  // save the latest topology
  writeToTsFile();

  if (createIntrasiteLinks_) {
    createSiteLinks(true /* validate */, {newNode});
  }
}

void
TopologyWrapper::delNode(
    const std::string& nodeName, const bool force) {
  const auto it = name2Node_.find(nodeName);
  if (it == name2Node_.end()) {
    throw invalid_argument("Node `" + nodeName + "` does not exist");
  }

  auto node = it->second;
  if (!force && node->status != thrift::NodeStatusType::OFFLINE) {
    throw invalid_argument(
        "Node `" + nodeName +
        "` is still ignited. Use the 'force' option to bypass this check.");
  }

  // check if any links associated with the node are still alive
  const auto links = getLinksByNodeName(nodeName);
  if (!force) {
    for (const auto& link : links) {
      if (!link.is_alive) {
        continue;
      }

      // skip wired intra-site links (these should get automatically deleted)
      if (link.link_type == thrift::LinkType::ETHERNET) {
        auto nbrNode = getNbrNode(nodeName, link);
        if (nbrNode && nbrNode->site_name == node->site_name) {
          continue;
        }
      }

      throw invalid_argument(
          "Link `" + link.name + "` associated with node `" + nodeName +
          "` is still alive. Bring it down before deleting the "
          "node, or use the 'force' option to bypass this check.");
    }
  }

  if (!node->mac_addr.empty()) {
    mac2NodeName_.erase(node->mac_addr);
  }
  for (const auto& macAddr : node->wlan_mac_addrs) {
    mac2NodeName_.erase(macAddr);
  }

  unplugNodeFromSite(nodeName);
  name2Node_.erase(nodeName);

  std::string errorMsg;
  auto& nodes = topology_.nodes;
  // convert raw pointer to iterator first
  nodes.erase(nodes.begin() + (node - &nodes[0]));

  // update all pointers since vector can be resized
  for (auto& node : topology_.nodes) {
    name2Node_[node.name] = &node;
  }
  // delete all links associated with the node
  for (const auto& link : links) {
    delLink(link.a_node_name, link.z_node_name, true);
  }

  // save the latest topology
  writeToTsFile();
}

void
TopologyWrapper::updateLink(thrift::Link& link) {
  if (link.a_node_name > link.z_node_name) {
    // swap names and macs if they're mis-ordered
    std::swap(link.a_node_name, link.z_node_name);
    std::swap(link.a_node_mac, link.z_node_mac);
  }
  link.name = buildLinkName(link.a_node_name, link.z_node_name);
}

void
TopologyWrapper::editNode(
    const std::string& nodeName, const thrift::Node& newNode) {
  // check if node exists
  auto nodeIt = name2Node_.find(nodeName);
  if (nodeIt == name2Node_.end()) {
    throw invalid_argument("Node `" + nodeName + "` does not exist");
  }

  // validate new name
  bool hasNewName = (newNode.name != nodeName);
  if (hasNewName) {
    validateNodeName(newNode.name);
  }

  // for node type changes, do not allow any links to/from the node
  // (too many edge cases to handle)
  bool hasNewType =
      (newNode.node_type != nodeIt->second->node_type &&
       TEnumMapFactory<thrift::NodeType>::makeValuesToNamesMap()
       .count(newNode.node_type));
  if (hasNewType) {
    if (getLinksByNodeName(nodeName).size() > 0) {
      throw invalid_argument(
          "Cannot change node type for `" + nodeName +
          "` until all links are deleted");
    }
  }

  // for backwards compatibility, only change node name if "site_name" matches
  // (old client code sent an empty Node struct instead of the existing struct)
  bool hasMiscProps = (
      newNode.site_name == nodeIt->second->site_name && (
         newNode.pop_node != nodeIt->second->pop_node ||
         newNode.ant_azimuth != nodeIt->second->ant_azimuth ||
         newNode.ant_elevation != nodeIt->second->ant_elevation ||
         newNode.has_cpe_ref().has_value() !=
            nodeIt->second->has_cpe_ref().has_value() ||
         newNode.has_cpe_ref().value_or(false) !=
            nodeIt->second->has_cpe_ref().value_or(false)
      )
    );

  // no changes to make?
  if (!hasNewName && !hasNewType && !hasMiscProps) {
    throw invalid_argument("No changes to make to node `" + nodeName + "`");
  }

  // change misc properties
  if (hasMiscProps) {
    nodeIt->second->pop_node = newNode.pop_node;
    nodeIt->second->ant_azimuth = newNode.ant_azimuth;
    nodeIt->second->ant_elevation = newNode.ant_elevation;
    if (newNode.has_cpe_ref().has_value()) {
      nodeIt->second->has_cpe_ref() = newNode.has_cpe_ref().value();
    } else {
      nodeIt->second->has_cpe_ref().reset();
    }
  }

  // change node type
  if (hasNewType) {
    nodeIt->second->node_type = newNode.node_type;
  }

  // change name
  if (hasNewName) {
    // update mac -> node name
    auto mac2NodeIt = mac2NodeName_.find(nodeIt->second->mac_addr);
    if (mac2NodeIt != mac2NodeName_.end()) {  // we could have an empty mac
      mac2NodeIt->second = newNode.name;
    }
    for (const auto& macAddr : nodeIt->second->wlan_mac_addrs) {
      auto iter = mac2NodeName_.find(macAddr);
      if (iter != mac2NodeName_.end()) {
        iter->second = newNode.name;
      }
    }

    // replace node name in site->node mapping
    auto site2AssocNodesIt = site2AssocNodes_.find(nodeIt->second->site_name);
    if (site2AssocNodesIt != site2AssocNodes_.end()) {
      auto nodeNameIt = site2AssocNodesIt->second.find(nodeName);
      if (nodeNameIt != site2AssocNodesIt->second.end()) {
        site2AssocNodesIt->second.erase(nodeNameIt);
      }
      site2AssocNodesIt->second.insert(newNode.name);
    }

    // rename links associated with the node
    const auto& linksByNode = getLinksByNodeName(nodeName);
    for (const auto& link : linksByNode) {
      auto linkIt = name2Link_.find(link.name);
      if (linkIt == name2Link_.end()) {
        LOG(ERROR) << "Link not found in name2Link_: " << link.name;
        continue;
      }
      // update link a/z node + name + re-order
      if (linkIt->second->a_node_name == nodeName) {
        linkIt->second->a_node_name = newNode.name;
      } else {
        linkIt->second->z_node_name = newNode.name;
      }
      // fix a/z + name
      updateLink(*linkIt->second);
      // update link in mapping
      name2Link_[linkIt->second->name] = linkIt->second;
      name2Link_.erase(linkIt);
    }

    // swap names after links are updated
    nodeIt->second->name = newNode.name;
    name2Node_[newNode.name] = nodeIt->second;
    name2Node_.erase(nodeIt);
  }

  // save the latest topology
  writeToTsFile();
}

void
TopologyWrapper::addLink(
    thrift::Link& newLink, bool saveToFile) {
  standardizeLinkMacs(newLink);
  validateLink(newLink);

  topology_.links.push_back(newLink);

  // update all pointers since vector can be resized
  for (auto& link : topology_.links) {
    name2Link_[link.name] = &link;
  }

  // save the latest topology
  if (saveToFile) {
    writeToTsFile();
  }
}

void
TopologyWrapper::delLink(
    const string& aNodeName,
    const string& zNodeName,
    const bool force) {
  const auto linkName = buildLinkName(aNodeName, zNodeName);

  const auto it = name2Link_.find(linkName);
  if (it == name2Link_.end()) {
    throw invalid_argument("Link `" + linkName + "` does not exist");
  }

  if (!force &&
      it->second->link_type == thrift::LinkType::WIRELESS &&
      it->second->is_alive) {
    throw invalid_argument(
        "Link `" + linkName +
        "` is still alive. Bring it down before deleting it, or use the "
        "'force' option to bypass this check.");
  }

  auto& links = topology_.links;

  std::string errorMsg;
  // convert raw pointer to iterator first
  links.erase(links.begin() + (it->second - &links[0]));
  name2Link_.erase(linkName);

  // update all pointers since vector can be resized
  for (auto& link : topology_.links) {
    name2Link_[link.name] = &link;
  }

  // save the latest topology
  writeToTsFile();
}

void
TopologyWrapper::addSite(const thrift::Site& newSite) {
  validateSite(newSite);

  topology_.sites.push_back(newSite);

  // update all pointers since vector can be resized
  for (auto& site : topology_.sites) {
    name2Site_[site.name] = &site;
  }

  // save the latest topology
  writeToTsFile();
}

void
TopologyWrapper::delSite(const std::string& siteName) {
  const auto it = name2Site_.find(siteName);
  if (it == name2Site_.end()) {
    throw invalid_argument("Site `" + siteName + "` does not exist");
  }

  if (site2AssocNodes_.find(siteName) != site2AssocNodes_.end() &&
      !site2AssocNodes_[siteName].empty()) {
    throw invalid_argument(
        "Site `" + siteName + "` is still associated with nodes! Delete" +
        " nodes and retry");
  }

  const auto site = it->second;
  name2Site_.erase(siteName);

  auto& sites = topology_.sites;
  // convert raw pointer to iterator first
  sites.erase(sites.begin() + (site - &sites[0]));

  // update all pointers since vector can be resized
  for (auto& site : topology_.sites) {
    name2Site_[site.name] = &site;
  }

  // save the latest topology
  writeToTsFile();
}

void
TopologyWrapper::editSite(
    const std::string& siteName, const thrift::Site& newSite) {
  // check if site exists
  const auto name2SiteIt = name2Site_.find(siteName);
  if (name2SiteIt == name2Site_.end()) {
    throw invalid_argument("Site `" + siteName + "` does not exist");
  }

  // validate new name
  bool hasNewName =
      (newSite.name != name2SiteIt->second->name && !newSite.name.empty());
  if (hasNewName) {
    if (name2Site_.count(newSite.name)) {
      throw invalid_argument(
          "New site name `" + newSite.name + "` already exists");
    }
  }

  // validate new location
  // NOTE: we ignore new locations with only default/uninitialized fields
  bool hasNewLocation =
      (newSite.location != name2SiteIt->second->location &&
       newSite.location != thrift::Location());

  // no changes to make?
  if ((!hasNewName && !hasNewLocation) || *name2SiteIt->second == newSite) {
    throw invalid_argument("No changes to make to site `" + siteName + "`");
  }

  // update location
  if (hasNewLocation) {
    // validate location
    std::string errMsg;
    if (!validateLocation(newSite.location, errMsg)) {
      throw invalid_argument(errMsg);
    }

    name2SiteIt->second->location = newSite.location;
    LOG(INFO) << "Updating location of site `" << siteName
              << " to lat/long/alt/acc: " << newSite.location.latitude << "/"
              << newSite.location.longitude << "/" << newSite.location.altitude
              << "/" << newSite.location.accuracy;
  }

  // update name
  if (hasNewName) {
    // update nodes associated with the site
    // only update the site name, not the node names within
    auto site2AssocNodesIt = site2AssocNodes_.find(siteName);
    if (site2AssocNodesIt != site2AssocNodes_.end()) {
      // rename nodes
      auto& nodeList = site2AssocNodesIt->second;
      for (const auto& nodeName : nodeList) {
        auto nodeIt = name2Node_.find(nodeName);
        if (nodeIt != name2Node_.end()) {
          auto node = nodeIt->second;
          node->site_name = newSite.name;
        }
      }
      site2AssocNodes_[newSite.name] = site2AssocNodesIt->second;
      site2AssocNodes_.erase(site2AssocNodesIt);
    }

    // rename site
    name2SiteIt->second->name = newSite.name;

    // update site maps
    name2Site_[newSite.name] = name2SiteIt->second;
    name2Site_.erase(name2SiteIt);
  }

  // save the latest topology
  writeToTsFile();
}

bool
TopologyWrapper::setLocation(
    const std::string& mac, const thrift::Location& location) {
  // validate if node exists, as mac is coming from minion
  auto node = getNodeByMac(mac);
  if (!node) {
    LOG(ERROR) << "Cannot set location for unknown mac " << mac;
    return false;
  }

  // check against current location accuracy
  auto site = name2Site_[node->site_name];
  if (location.accuracy >= site->location.accuracy) {
    LOG(ERROR) << "Ignoring location with worse accuracy: "
               << location.accuracy << " vs " << site->location.accuracy;
    return false;
  }

  // validate location
  std::string errMsg;
  if (!validateLocation(location, errMsg)) {
    LOG(ERROR) << "Ignoring invalid location: " << errMsg;
    return false;
  }

  // update site location
  site->location = location;
  LOG(INFO) << "Location of node with MAC '" << mac
            << "' updated to lat/long/alt/acc: " << location.latitude << "/"
            << location.longitude << "/" << location.altitude << "/"
            << location.accuracy;

  // save the latest topology
  writeToTsFile();
  return true;
}

void
TopologyWrapper::setNodePrefix(
    const std::string& nodeName,
    const std::optional<folly::CIDRNetwork> prefix) {
  auto iter = name2Node_.find(nodeName);
  if (iter == name2Node_.end()) {
    throw std::invalid_argument(
        folly::sformat("Node `{}` does not exist", nodeName));
  }

  auto node = iter->second;
  if (prefix.has_value()) {
    node->prefix_ref() = folly::IPAddress::networkToString(prefix.value());
  } else {
    node->prefix_ref().reset();
  }
  writeToTsFile();
}

std::optional<std::unordered_map<std::string, thrift::Zone>>
TopologyWrapper::getPrefixAllocZones() const {
  auto dpaPrefixAllocParams =
      topology_.config.deterministic_prefix_alloc_params_ref();
  if (dpaPrefixAllocParams.has_value() &&
      dpaPrefixAllocParams->zones_ref().has_value()) {
    return std::make_optional(dpaPrefixAllocParams.value().zones_ref()
        .value());
  }
  return std::nullopt;
}

void
TopologyWrapper::setPrefixZones(
    std::unordered_map<std::string, thrift::Zone>& zones) {
  thrift::DeterministicPrefixAllocParams dpaParams;
  dpaParams.zones_ref() = zones;
  topology_.config.deterministic_prefix_alloc_params_ref() = dpaParams;
  writeToTsFile();
}

std::unordered_map<std::string, std::string>
TopologyWrapper::getNodePrefixes() const {
  std::unordered_map<std::string, std::string> ret;
  for (const auto& node : getAllNodes()) {
    if (node.prefix_ref().has_value()) {
      ret[node.name] = node.prefix_ref().value();
    }
  }
  return ret;
}

void TopologyWrapper::setControllerPrefixAllocScheme(
    ControllerPrefixAllocScheme controllerPrefixAlloc) {
  controllerPrefixAlloc_ = controllerPrefixAlloc;
}

ControllerPrefixAllocScheme
TopologyWrapper::getControllerPrefixAllocScheme() const {
  return controllerPrefixAlloc_;
}

} // namespace terragraph
} // namespace facebook
