/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "../TopologyWrapper.h"

#include <folly/FileUtil.h>
#include <folly/IPAddress.h>
#include <folly/Memory.h>
#include <folly/system/ThreadName.h>
#include <folly/init/Init.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <thrift/lib/cpp2/Thrift.h>

#include <e2e/common/MacUtils.h>
#include <e2e/common/TestUtils.h>

using namespace std;
using namespace facebook::terragraph;

using std::string;
using std::set;

using thrift::NodeType;
using thrift::LinkType;
using thrift::NodeStatusType;

class TopologyFixture : public ::testing::Test {

 public:
  thrift::Node
  createNode(
      const string& nodeName,
      const string& nodeMac,
      const thrift::NodeType nodeType,
      const bool popNode,
      const thrift::NodeStatusType status,
      const string& siteName = "",
      const string& prefix = "",
      const std::vector<string>& wlanMacs = std::vector<std::string>()) {
    thrift::Node node;
    node.name = nodeName;
    node.node_type = nodeType;
    node.mac_addr = nodeMac;
    node.pop_node = popNode;
    node.status = status;
    node.site_name = siteName;
    node.has_cpe_ref() = false;
    node.prefix_ref() = prefix;
    if (wlanMacs.empty() && !nodeMac.empty()) {
      node.wlan_mac_addrs.push_back(nodeMac);
    } else {
      node.wlan_mac_addrs = wlanMacs;
    }
    try {
      auto mac = MacUtils::standardizeMac(nodeMac);
    } catch (const std::exception &ex) {
      LOG(ERROR) << "Invalid mac: " << nodeMac;
    }
    return node;
  }

  thrift::Link
  createLink(
      const string& linkName,
      const string& aNodeName,
      const string& zNodeName,
      const string& aNodeMac,
      const string& zNodeMac,
      const thrift::LinkType linkType,
      bool isAlive) {
    thrift::Link link;
    link.name = linkName;
    link.a_node_name = aNodeName;
    link.z_node_name = zNodeName;
    link.a_node_mac = aNodeMac;
    link.z_node_mac = zNodeMac;
    link.link_type = linkType;
    link.is_alive = isAlive;
    return link;
  }

  thrift::Site
  createSite(
      const string& siteName,
      const double& latitude,
      const double& longitude,
      const double& altitude,
      const double& accuracy) {
    thrift::Site site;
    site.name = siteName;
    site.location = thrift::Location();
    site.location.latitude = latitude;
    site.location.longitude = longitude;
    site.location.altitude = altitude;
    site.location.accuracy = accuracy;
    return site;
  }

  std::unique_ptr<TopologyWrapper>
  getTopoW(
      const vector<thrift::Node>& nodes,
      const vector<thrift::Link>& links,
      const vector<thrift::Site>& sites) {
    thrift::Topology topology;
    topology.name = "test";
    topology.nodes = nodes;
    topology.links = links;
    topology.sites = sites;

    return std::make_unique<TopologyWrapper>(topology, "", true, false);
  }

  void
  SetUp() override {
    char tmpFileName[] = "/tmp/terraXXXXXX";
    fd = mkstemp(tmpFileName);
    fileName = tmpFileName;
    LOG(INFO) << "Created temporary file " << fileName;
    prefixAllocParams.seedPrefix = "face:b00c::/56";
    prefixAllocParams.allocPrefixLen = 64;
    nodes = {createNode(
                 "1",
                 "01:01:01:01:01:01",
                 NodeType::DN,
                 true,
                 NodeStatusType::OFFLINE,
                 "pole-mpk16"),
             createNode(
                 "2",
                 "02:02:02:02:02:02",
                 NodeType::DN,
                 true,
                 NodeStatusType::OFFLINE,
                 "pole-mpk16"),
             createNode(
                 "3",
                 "03:03:03:03:03:03",
                 NodeType::DN,
                 false,
                 NodeStatusType::OFFLINE,
                 "pole-mpk17"),
             createNode(
                 "4",
                 "04:04:04:04:04:04",
                 NodeType::DN,
                 false,
                 NodeStatusType::ONLINE,
                 "pole-mpk17"),
             createNode(
                 "5",
                 "05:05:05:05:05:05",
                 NodeType::DN,
                 false,
                 NodeStatusType::OFFLINE,
                 "pole-mpk18"),
             createNode(
                 "6",
                 "06:06:06:06:06:06",
                 NodeType::DN,
                 false,
                 NodeStatusType::OFFLINE,
                 "pole-mpk18"),
             createNode(
                 "7",
                 "07:07:07:07:07:07",
                 NodeType::DN,
                 false,
                 NodeStatusType::ONLINE,
                 "pole-mpk18"),
             createNode(
                 "8",
                 "08:08:08:08:08:08",
                 NodeType::DN,
                 false,
                 NodeStatusType::OFFLINE,
                 "pole-mpk18")};

    for (auto& node : nodes) {
      name2Node[node.name] = &node;
    }

    links = {createLink("link-1-5", "1", "5",
                        "01:01:01:01:01:01", "05:05:05:05:05:05",
                        LinkType::WIRELESS, false),
             createLink("link-2-6", "2", "6",
                        "02:02:02:02:02:02", "06:06:06:06:06:06",
                        LinkType::WIRELESS, false),
             createLink("link-3-7", "3", "7",
                        "03:03:03:03:03:03", "07:07:07:07:07:07",
                        LinkType::WIRELESS, false),
             createLink("link-4-8", "4", "8",
                        "04:04:04:04:04:04", "08:08:08:08:08:08",
                        LinkType::WIRELESS, false),
             createLink("link-1-2", "1", "2", "", "", LinkType::ETHERNET, true),
             createLink("link-2-3", "2", "3", "", "", LinkType::ETHERNET, true),
             createLink("link-3-4", "3", "4", "", "", LinkType::ETHERNET, true),
             createLink("link-1-4", "1", "4", "", "",
                        LinkType::ETHERNET, true)};

    for (auto& link : links) {
      name2Link[link.name] = &link;
    }

    // mkp16 <__ 20m __> mpk17 <__ 120m __> mpk18 <__ 1km __> mpk20
    thrift::Site site_pole_mpk16 =
        createSite("pole-mpk16", 37.485301, -122.148005, -25, 10);

    thrift::Site site_pole_mpk17 =
        createSite("pole-mpk17", 37.485180, -122.147885, -25, 10);

    thrift::Site site_pole_mpk18 =
        createSite("pole-mpk18", 37.485811, -122.146738, -25, 10);

    thrift::Site site_pole_mpk20 =
        createSite("pole-mpk20", 37.481044, -122.157929, -25, 10);

    thrift::Site site_pole_unknown =
        createSite("pole-unknown", 0, 0, 0, 40000000);

    sites = {site_pole_mpk16, site_pole_mpk17, site_pole_mpk18, site_pole_mpk20,
        site_pole_unknown};
    for (auto& site : sites) {
      name2Site[site.name] = &site;
    }

    // wired link: ---
    // wireless link: ===
    //
    //
    //    node5 ==== node1 ---- node2 ==== node6
    //                |         |
    //                |         |
    //                |         |
    //    node8 ==== node4 ---- node3 ==== node7

    jsonTopology = R"config({
      "name" : "topology_wrapper_test",
      "sites": [
          {
              "name": "pole-mpk16",
              "location": {
                "latitude": 37.485301,
                "longitude": -122.148005,
                "altitude": -25,
                "accuracy": 10
              }
          },
          {
              "name": "pole-mpk17",
              "location": {
                "latitude": 37.485180,
                "longitude": -122.147885,
                "altitude": -25,
                "accuracy": 10
              }
          },
          {
              "name": "pole-mpk18",
              "location": {
                "latitude": 37.485811,
                "longitude": -122.146738,
                "altitude": -25,
                "accuracy": 10
              }
          },
          {
              "name": "pole-mpk20",
              "location": {
                "latitude": 37.481044,
                "longitude": -122.157929,
                "altitude": -25,
                "accuracy": 10
              }
          },
          {
              "name": "pole-unknown"
          }
      ],
      "links": [
          {
              "is_alive": false,
              "name": "link-1-5",
              "link_type": 1,
              "linkup_attempts": 0,
              "a_node_name": "1",
              "a_node_mac": "",
              "z_node_name": "5",
              "z_node_mac": ""
          },
          {
              "is_alive": false,
              "name": "link-2-6",
              "link_type": 1,
              "linkup_attempts": 0,
              "a_node_name": "2",
              "a_node_mac": "",
              "z_node_name": "6",
              "z_node_mac": ""
          },
          {
              "is_alive": false,
              "name": "link-3-7",
              "link_type": 1,
              "linkup_attempts": 0,
              "a_node_name": "3",
              "a_node_mac": "",
              "z_node_name": "7",
              "z_node_mac": ""
          },
          {
              "is_alive": false,
              "name": "link-4-8",
              "link_type": 1,
              "linkup_attempts": 0,
              "a_node_name": "4",
              "a_node_mac": "",
              "z_node_name": "8",
              "z_node_mac": ""
          },
          {
              "is_alive": true,
              "name": "link-1-2",
              "link_type": 2,
              "linkup_attempts": 0,
              "a_node_name": "1",
              "a_node_mac": "",
              "z_node_name": "2",
              "z_node_mac": ""
          },
          {
              "is_alive": true,
              "name": "link-2-3",
              "link_type": 2,
              "linkup_attempts": 0,
              "a_node_name": "2",
              "a_node_mac": "",
              "z_node_name": "3",
              "z_node_mac": ""
          },
          {
              "is_alive": true,
              "name": "link-3-4",
              "link_type": 2,
              "linkup_attempts": 0,
              "a_node_name": "3",
              "a_node_mac": "",
              "z_node_name": "4",
              "z_node_mac": ""
          },
          {
              "is_alive": true,
              "name": "link-1-4",
              "link_type": 2,
              "linkup_attempts": 0,
              "a_node_name": "1",
              "a_node_mac": "",
              "z_node_name": "4",
              "z_node_mac": ""
          }
      ],
      "nodes": [
          {
              "status": 1,
              "mac_addr": "1:1:1:1:1:1",
              "wlan_mac_addrs": [],
              "name": "1",
              "node_type": 2,
              "pop_node": true,
              "site_name": "pole-mpk16"
          },
          {
              "status": 1,
              "mac_addr": "2:2:2:2:2:2",
              "wlan_mac_addrs": [],
              "name": "2",
              "node_type": 2,
              "pop_node": true,
              "site_name": "pole-mpk16"
          },
          {
              "status": 1,
              "mac_addr": "3:3:3:3:3:3",
              "wlan_mac_addrs": [],
              "name": "3",
              "node_type": 2,
              "pop_node": false,
              "site_name": "pole-mpk17"
          },
          {
              "status": 2,
              "mac_addr": "4:4:4:4:4:4",
              "wlan_mac_addrs": [],
              "name": "4",
              "node_type": 2,
              "pop_node": false,
              "site_name": "pole-mpk17"
          },
          {
              "status": 1,
              "mac_addr": "5:5:5:5:5:5",
              "wlan_mac_addrs": [],
              "name": "5",
              "node_type": 2,
              "pop_node": false,
              "site_name": "pole-mpk18"
          },
          {
              "status": 1,
              "mac_addr": "6:6:6:6:6:6",
              "wlan_mac_addrs": [],
              "name": "6",
              "node_type": 2,
              "pop_node": false,
              "site_name": "pole-mpk18"
          },
          {
              "status": 2,
              "mac_addr": "7:7:7:7:7:7",
              "wlan_mac_addrs": [],
              "name": "7",
              "node_type": 2,
              "pop_node": false,
              "site_name": "pole-mpk18"
          },
          {
              "status": 1,
              "mac_addr": "8:8:8:8:8:8",
              "wlan_mac_addrs": [],
              "name": "8",
              "node_type": 2,
              "pop_node": false,
              "site_name": "pole-mpk18"
          }
      ]
    })config";
  }

  void
  TearDown() override {
    close(fd);
    // Delete any configs created by tests
    remove("/tmp/node_config_overrides.json");
    remove("/tmp/auto_node_config_overrides.json");
    remove("/tmp/network_config_overrides.json");
  }

  void
  checkNode(const thrift::Node& expected, const thrift::Node& got) {
    EXPECT_EQ(expected.name, got.name);
    EXPECT_EQ(expected.node_type, got.node_type);
    EXPECT_EQ(
        MacUtils::standardizeMac(expected.mac_addr),
        MacUtils::standardizeMac(got.mac_addr));
    EXPECT_EQ(expected.pop_node, got.pop_node);
    EXPECT_EQ(expected.status, got.status);
  }

  void
  checkLink(const thrift::Link& expected, const thrift::Link& got) {
    EXPECT_EQ(expected.name, got.name);
    EXPECT_EQ(expected.a_node_name, got.a_node_name);
    EXPECT_EQ(expected.z_node_name, got.z_node_name);
    EXPECT_EQ(expected.link_type, got.link_type);
    EXPECT_EQ(expected.is_alive, got.is_alive);
    EXPECT_EQ(expected.linkup_attempts, got.linkup_attempts);
  }

  void
  checkLocation(const thrift::Location& expected, const thrift::Location& got) {
    EXPECT_EQ(expected.latitude, got.latitude);
    EXPECT_EQ(expected.longitude, got.longitude);
    EXPECT_EQ(expected.altitude, got.altitude);
    EXPECT_EQ(expected.accuracy, got.accuracy);
  }

  void
  checkSite(const thrift::Site& expected, const thrift::Site& got) {
    EXPECT_EQ(expected.name, got.name);
    checkLocation(expected.location, got.location);
  }

  void
  checkTopology(const std::string& topologyFile) {
    TopologyWrapper topologyW(topologyFile);
    EXPECT_EQ(topologyW.getTopologyName(), topologyName);
    EXPECT_EQ(topologyW.getAllNodes().size(), nodes.size());
    EXPECT_EQ(topologyW.getAllLinks().size(), links.size());
    EXPECT_EQ(topologyW.getAllSites().size(), sites.size());
    for (auto& node : topologyW.getAllNodes()) {
      auto itr = name2Node.find(node.name);
      EXPECT_TRUE(itr != name2Node.end());
      if (itr != name2Node.end()) {
        checkNode(node, *(itr->second));
      }
    }
    for (auto& link : topologyW.getAllLinks()) {
      auto itr = name2Link.find(link.name);
      EXPECT_TRUE(itr != name2Link.end());
      if (itr != name2Link.end()) {
        checkLink(link, *(itr->second));
      }
    }
    for (const auto& site : topologyW.getAllSites()) {
      auto itr = name2Site.find(site.name);
      EXPECT_TRUE(itr != name2Site.end());
      if (itr != name2Site.end()) {
        checkSite(site, *(itr->second));
      }
    }
  }

  int fd{-1};
  std::string fileName;
  std::vector<thrift::Node> nodes;
  std::vector<thrift::Link> links;
  std::vector<thrift::Site> sites;
  std::map<string, thrift::Node*> name2Node;
  std::map<string, thrift::Link*> name2Link;
  std::map<string, thrift::Site*> name2Site;
  std::string jsonTopology;
  std::string topologyName{"topology_wrapper_test"};
  thrift::PrefixAllocParams prefixAllocParams;
};

TEST_F(TopologyFixture, writeTest) {
  thrift::Topology topology;
  topology.name = topologyName;
  topology.nodes = nodes;
  topology.links = links;
  topology.sites = sites;
  TopologyWrapper topologyW(topology);
  topologyW.writeToFile(fileName);
  checkTopology(fileName);
}

TEST_F(TopologyFixture, writeTimeStampedTest) {
  thrift::Topology topology;
  topology.name = topologyName;
  topology.nodes = nodes;
  topology.links = links;
  topology.sites = sites;
  TopologyWrapper topologyW(
      topology, "/tmp/topology_wrapper_test");
  topologyW.writeToTsFile();
  checkTopology("/tmp/topology_wrapper_test/e2e_topology.conf");
}

TEST_F(TopologyFixture, readTest) {
  folly::writeFile(jsonTopology, fileName.c_str());
  checkTopology(fileName);
}

TEST_F(TopologyFixture, validateTest) {
  // Good config1
  folly::writeFile(jsonTopology, fileName.c_str());
  EXPECT_NO_THROW(TopologyWrapper goodTopoW(fileName));
  // Good config2
  EXPECT_NO_THROW(getTopoW(nodes, links, sites));
  // Good config3 with empty link name
  thrift::Topology topology;
  topology.name = "test";
  topology.nodes = nodes;
  topology.links = links;
  topology.sites = sites;
  auto emptyLinkName = topology;
  emptyLinkName.links[0].name = "";
  EXPECT_NO_THROW(getTopoW(nodes, emptyLinkName.links, sites));
  TopologyWrapper topoW(emptyLinkName);
  EXPECT_EQ(topoW.getTopology(), topology);

  // Bad Configs

  // --- basic node error checks ---

  // Links refer to non-existing nodes
  EXPECT_THROW(getTopoW({}, {links[0]}, {}), std::exception);
  // A empty node name
  auto emptyNodeName = nodes;
  emptyNodeName[0].name = "";
  EXPECT_THROW(getTopoW(emptyNodeName, {}, {}), std::invalid_argument);
  // Multiple nodes with same name
  auto dupNodeNames = nodes;
  dupNodeNames[0].name = "ava";
  dupNodeNames[1].name = "ava";
  EXPECT_THROW(getTopoW(dupNodeNames, {}, {}), std::invalid_argument);
  // Invalid mac address
  auto invalidNodeMac = nodes;
  invalidNodeMac[0].mac_addr = "111:222:333:444";
  EXPECT_THROW(getTopoW(invalidNodeMac, {}, {}), std::invalid_argument);
  // Duplicate mac address
  auto dupNodeMacs = nodes;
  dupNodeMacs[0].mac_addr = "a:a:a:a:a:a";
  dupNodeMacs[1].mac_addr = "A:A:A:A:A:A";
  EXPECT_THROW(getTopoW(dupNodeMacs, {}, {}), std::invalid_argument);
  // Start with particular node marked as up
  auto startNodeAlive = nodes;
  startNodeAlive[0].status = NodeStatusType::ONLINE;
  auto topoW0 = getTopoW(startNodeAlive, links, sites);
  topoW0->sanitizeState();
  EXPECT_EQ(
      NodeStatusType::OFFLINE, topoW0->getNode(startNodeAlive[0].name)->status);
  // invalid node type
  std::string invalidNodeTypeJson = R"config({
    "name" : "invalid_node_type_topology",
    "links": [],
    "nodes": [
        {
            "status": 1,
            "mac_addr": "1:1:1:1:1:1",
            "wlan_mac_addrs": [],
            "name": "1",
            "node_type": 3,
            "pop_node": true,
            "site_name": "pole-mpk16"
        }
    ]
  })config";
  std::string invalidNodeTypeFileName = fileName + "-invalidNodeType";
  folly::writeFile(invalidNodeTypeJson, invalidNodeTypeFileName.c_str());
  EXPECT_THROW(
      TopologyWrapper invalidNodeTypeTopoW(invalidNodeTypeFileName),
      std::invalid_argument);

  // --- basic link error checks ---

  // invalid link name
  auto invalidLinkName = links;
  invalidLinkName[0].name = "ava";
  EXPECT_THROW(getTopoW({}, invalidLinkName, {}), std::invalid_argument);
  // Multiple links with same name
  auto dupLinkNames = links;
  dupLinkNames[0].name = "ava";
  dupLinkNames[1].name = "ava";
  EXPECT_THROW(getTopoW({}, dupLinkNames, {}), std::invalid_argument);
  // Wireless link set to alive
  auto invalidWirelessLink = links;
  invalidWirelessLink[0].is_alive = true;
  auto topoW1 = getTopoW(nodes, invalidWirelessLink, sites);
  topoW1->sanitizeState();
  EXPECT_EQ(topoW1->getLink(invalidWirelessLink[0].name)->is_alive, false);
  // Ethernet link not set to alive
  auto invalidEthernetLink = links;
  invalidEthernetLink[4].is_alive = false;
  auto topoW2 = getTopoW(nodes, invalidEthernetLink, sites);
  topoW2->sanitizeState();
  EXPECT_EQ(topoW2->getLink(invalidEthernetLink[4].name)->is_alive, true);
  // Ethernet link too far away
  auto invalidNodes = nodes;
  invalidNodes[0].site_name = "pole-mpk18";
  EXPECT_THROW(getTopoW(invalidNodes, links, sites), std::invalid_argument);
  // Wireless link too far away
  // We used to throw here, but don't anymore (different limits per hardware)
  invalidNodes = nodes;
  invalidNodes[4].site_name = "pole-mpk20";
  EXPECT_NO_THROW(getTopoW(invalidNodes, links, sites));
  // sites with specified bad location accuracy should not throw
  invalidNodes = nodes;
  invalidNodes[4].site_name = "pole-unknown";
  invalidNodes[0].site_name = "pole-unknown";
  EXPECT_NO_THROW(getTopoW(invalidNodes, links, sites));

    // --- basic site error checks ---

  // overwhelming plugging
  dupNodeNames = nodes;
  dupNodeNames[0].name = "pole-mpk20";
  dupNodeNames[1].name = "pole-mpk20";
  dupNodeNames[2].name = "pole-mpk20";
  dupNodeNames[3].name = "pole-mpk20";
  dupNodeNames[4].name = "pole-mpk20";
  EXPECT_THROW(getTopoW(dupNodeNames, links, sites), std::invalid_argument);
  // invalid node name
  dupNodeNames = nodes;
  dupNodeNames[0].name = "pole-X";
  EXPECT_THROW(getTopoW(dupNodeNames, links, sites), std::invalid_argument);
  // empty site name
  invalidNodes = nodes;
  invalidNodes[0].site_name = "";
  EXPECT_THROW(getTopoW(invalidNodes, links, sites), std::invalid_argument);
  // invalid site name
  invalidNodes = nodes;
  invalidNodes[0].site_name = "pole-X";
  EXPECT_THROW(getTopoW(invalidNodes, links, sites), std::invalid_argument);
}

TEST_F(TopologyFixture, gettersTest) {
  thrift::Topology topology;
  topology.name = "test";
  topology.nodes = nodes;
  topology.links = links;
  topology.sites = sites;
  TopologyWrapper topologyW(topology);
  std::vector<thrift::Link> result;

  EXPECT_EQ(topologyW.getAllLinks(), links);
  EXPECT_EQ(topologyW.getAllNodes(), topology.nodes);
  EXPECT_EQ(topologyW.getTopology(), topology);

  // getNode
  checkNode(*name2Node["1"], *(topologyW.getNode("1")));
  EXPECT_FALSE(topologyW.getNode("0"));

  // getNodeByMac
  checkNode(*name2Node["2"], *(topologyW.getNodeByMac("2:2:2:2:2:2")));
  EXPECT_FALSE(topologyW.getNodeByMac("0:0:0:0:0:0"));
  EXPECT_FALSE(topologyW.getNodeByMac("k:h:a:l:e:e:s:i"));

  // getNodeNameByMac
  EXPECT_EQ("1", *(topologyW.getNodeNameByMac(name2Node["1"]->mac_addr)));
  EXPECT_FALSE(topologyW.getNodeNameByMac("0:0:0:0:0:0"));
  EXPECT_FALSE(topologyW.getNodeByMac("d:o:t:h:r:a:k:i"));

  // getNodeNamesBySiteName
  EXPECT_EQ(
      (std::unordered_set<std::string>{"1", "2"}),
      *topologyW.getNodeNamesBySiteName("pole-mpk16"));
  EXPECT_FALSE(topologyW.getNodeNamesBySiteName("kings-landing"));

  // getMacByNodeName
  EXPECT_EQ("01:01:01:01:01:01", *(topologyW.getMacByNodeName("1")));
  EXPECT_FALSE(topologyW.getMacByNodeName("0"));

  // getLink
  checkLink(*name2Link["link-1-5"], *(topologyW.getLink("link-1-5")));
  EXPECT_FALSE(topologyW.getLink("link-2-5"));

  // getLinksByNodeName
  result = topologyW.getLinksByNodeName("2");
  EXPECT_EQ(
      set<thrift::Link>({*name2Link["link-1-2"],
                         *name2Link["link-2-3"],
                         *name2Link["link-2-6"]}),
      set<thrift::Link>(result.begin(), result.end()));
  result = topologyW.getLinksByNodeName("4");
  EXPECT_EQ(
      std::set<thrift::Link>({*name2Link["link-3-4"],
                              *name2Link["link-1-4"],
                              *name2Link["link-4-8"]}),
      set<thrift::Link>(result.begin(), result.end()));
  result = topologyW.getLinksByNodeName("6");
  EXPECT_EQ(std::vector<thrift::Link>{*name2Link["link-2-6"]}, result);
  result = topologyW.getLinksByNodeName("8");
  EXPECT_EQ(std::vector<thrift::Link>{*name2Link["link-4-8"]}, result);
  result = topologyW.getLinksByNodeName("xyz");
  EXPECT_EQ(std::vector<thrift::Link>(), result);

  // getLinkName
  EXPECT_EQ("link-1-5", *(topologyW.getLinkName("5", "1")));
  EXPECT_EQ("link-1-5", *(topologyW.getLinkName("1", "5")));
  EXPECT_EQ("link-3-7", *(topologyW.getLinkName("7", "3")));
  EXPECT_EQ("link-3-7", *(topologyW.getLinkName("3", "7")));
  EXPECT_FALSE(topologyW.getLinkName("x", "y"));

  // getNbrNodeName
  EXPECT_EQ("1", *(topologyW.getNbrNodeName("5", "link-1-5")));
  EXPECT_EQ("5", *(topologyW.getNbrNodeName("1", "link-1-5")));
  EXPECT_EQ("1", *(topologyW.getNbrNodeName("2", "link-1-2")));
  EXPECT_EQ("2", *(topologyW.getNbrNodeName("1", "link-1-2")));
  EXPECT_FALSE(topologyW.getNbrNodeName("tg", "gt"));
  EXPECT_FALSE(topologyW.getNbrNodeName("5", "gt"));
  EXPECT_FALSE(topologyW.getNbrNodeName("tg", "link-1-5"));

  // getNbrNode
  checkNode(*name2Node["1"], *(topologyW.getNbrNode("5", "link-1-5")));
  checkNode(*name2Node["5"], *(topologyW.getNbrNode("1", "link-1-5")));
  EXPECT_FALSE(topologyW.getNbrNode("tg", "gt"));
  EXPECT_FALSE(topologyW.getNbrNode("5", "gt"));
  EXPECT_FALSE(topologyW.getNbrNode("tg", "link-1-5"));
  EXPECT_FALSE(topologyW.getNbrNode("2", "link-1-5"));

  // getSiteName
  EXPECT_FALSE(topologyW.getSite("1"));
  auto foundSite = topologyW.getSite("pole-mpk18");
  EXPECT_TRUE((bool)foundSite);
  EXPECT_EQ(37.485811, foundSite->location.latitude);
  EXPECT_EQ(-122.146738, foundSite->location.longitude);
  EXPECT_EQ(-25, foundSite->location.altitude);
}

TEST_F(TopologyFixture, settersTest) {
  thrift::Topology topology;
  topology.name = "test";
  topology.nodes = nodes;
  topology.links = links;
  topology.sites = sites;
  TopologyWrapper topologyW(topology, "");

  // setNodeStatus
  EXPECT_FALSE(topologyW.setNodeStatus("0", NodeStatusType::ONLINE));
  EXPECT_TRUE(topologyW.setNodeStatus("1", NodeStatusType::ONLINE));
  auto node = topologyW.getNode("1");
  EXPECT_EQ(NodeStatusType::ONLINE, (*node).status);
  EXPECT_TRUE(topologyW.setNodeStatus("1", NodeStatusType::ONLINE_INITIATOR));
  node = topologyW.getNode("1");
  EXPECT_EQ(NodeStatusType::ONLINE_INITIATOR, (*node).status);

  // setLinkStatus
  // down -> up
  EXPECT_FALSE(topologyW.setLinkStatus("link-2-1", true));
  EXPECT_TRUE(topologyW.setLinkStatus("link-1-2", true));
  auto link = topologyW.getLink("link-1-2");
  EXPECT_TRUE((*link).is_alive);
  // up -> down
  EXPECT_TRUE(topologyW.setLinkStatus("link-1-2", false));
  link = topologyW.getLink("link-1-2");
  EXPECT_FALSE((*link).is_alive);

  // bumpLinkupAttempts
  EXPECT_FALSE(topologyW.bumpLinkupAttempts("link-1-1"));
  EXPECT_TRUE(topologyW.bumpLinkupAttempts("link-1-2"));
  link = topologyW.getLink("link-1-2");
  EXPECT_EQ((*link).linkup_attempts, 1);
  EXPECT_TRUE(topologyW.bumpLinkupAttempts("link-1-2"));
  link = topologyW.getLink("link-1-2");
  EXPECT_EQ((*link).linkup_attempts, 2);

  // resetLinkupAttempts
  EXPECT_FALSE(topologyW.resetLinkupAttempts("link-1-1"));
  EXPECT_TRUE(topologyW.resetLinkupAttempts("link-1-2"));
  link = topologyW.getLink("link-1-2");
  EXPECT_EQ((*link).linkup_attempts, 0);

  // setNodeMacByName
  std::string mac0 = MacUtils::standardizeMac("0:0:0:0:0:0");
  std::string mac1 = MacUtils::standardizeMac("1:1:1:1:1:1");
  // - set node "1" offline
  EXPECT_TRUE(topologyW.setNodeStatus("1", NodeStatusType::OFFLINE));
  node = topologyW.getNode("1");
  EXPECT_EQ(NodeStatusType::OFFLINE, (*node).status);
  // - try to set mac on nonexistent node "0"
  EXPECT_THROW(
      topologyW.setNodeMacByName("0", mac0),
      std::invalid_argument);
  // up -> down
  EXPECT_TRUE(topologyW.setLinkStatus("link-1-4", false));
  EXPECT_TRUE(topologyW.setLinkStatus("link-1-5", false));
  // - set node "1" mac from "1:1:1:1:1:1" to "0:0:0:0:0:0"
  EXPECT_NO_THROW(topologyW.setNodeMacByName("1", mac0));
  node = topologyW.getNode("1");
  EXPECT_EQ((*node).mac_addr, mac0);
  // - set node "1" mac from "0:0:0:0:0:0" to "1:1:1:1:1:1"
  EXPECT_NO_THROW(topologyW.setNodeMacByName("1", mac1));
  node = topologyW.getNode("1");
  EXPECT_EQ((*node).mac_addr, mac1);

  // add/delete/change wlan_mac_addrs
  std::string wlanMac0 = MacUtils::standardizeMac("a:a:a:a:a:0");
  std::string wlanMac1 = MacUtils::standardizeMac("a:a:a:a:a:1");
  std::string wlanMac2 = MacUtils::standardizeMac("a:a:a:a:a:2");
  // add wlanMac0 and wlanMac1 to node "1"
  EXPECT_NO_THROW(topologyW.addNodeWlanMacs("1", {wlanMac0, wlanMac1}));
  node = topologyW.getNode("1");
  EXPECT_TRUE(
      std::find(node->wlan_mac_addrs.begin(), node->wlan_mac_addrs.end(),
                wlanMac0) != node->wlan_mac_addrs.end() &&
      std::find(node->wlan_mac_addrs.begin(), node->wlan_mac_addrs.end(),
                wlanMac1) != node->wlan_mac_addrs.end());
  // add wlanMac0 to node "2"
  EXPECT_THROW(
      topologyW.addNodeWlanMacs("2", {wlanMac0}), std::invalid_argument);
  // delete wlanMac1 from node "1"
  EXPECT_NO_THROW(topologyW.deleteNodeWlanMacs("1", {wlanMac1}));
  node = topologyW.getNode("1");
  EXPECT_TRUE(std::find(node->wlan_mac_addrs.begin(),
                        node->wlan_mac_addrs.end(),
                        wlanMac1) == node->wlan_mac_addrs.end());
  // add wlanMac1 to node "2"
  EXPECT_NO_THROW(topologyW.addNodeWlanMacs("2", {wlanMac1}));
  node = topologyW.getNode("2");
  EXPECT_TRUE(std::find(node->wlan_mac_addrs.begin(),
                        node->wlan_mac_addrs.end(),
                        wlanMac1) != node->wlan_mac_addrs.end());
  // delete wlanMac1 from node "1"
  EXPECT_THROW(
      topologyW.deleteNodeWlanMacs("1", {wlanMac1}),
      std::invalid_argument);
  // change wlanMac0 on node "1" to wlanMac1
  EXPECT_THROW(
      topologyW.changeNodeWlanMac("1", wlanMac0, wlanMac1),
      std::invalid_argument);
  // change wlanMac0 on node "1" to wlanMac2
  EXPECT_NO_THROW(topologyW.changeNodeWlanMac(
      "1", wlanMac0, wlanMac2));
  node = topologyW.getNode("1");
  EXPECT_TRUE(std::find(node->wlan_mac_addrs.begin(),
                        node->wlan_mac_addrs.end(),
                        wlanMac0) == node->wlan_mac_addrs.end());
  EXPECT_TRUE(std::find(node->wlan_mac_addrs.begin(),
                        node->wlan_mac_addrs.end(),
                        wlanMac2) != node->wlan_mac_addrs.end());

  // setLocation
  auto newNodes = nodes;
  newNodes[0].site_name = "pole-unknown";
  auto mac = newNodes[0].mac_addr;
  auto topoW = getTopoW(newNodes, links, sites);
  thrift::Location mpk14_150;
  mpk14_150.latitude = 37.485133;
  mpk14_150.longitude = -122.149346;
  mpk14_150.altitude = -25;
  mpk14_150.accuracy = 150;

  thrift::Location mpk14_50;
  mpk14_50.latitude = 37.485133;
  mpk14_50.longitude = -122.149346;
  mpk14_50.altitude = -25;
  mpk14_50.accuracy = 50;

  thrift::Location badAcc;
  badAcc.latitude = 37.485133;
  badAcc.longitude = -122.149346;
  badAcc.altitude = -25;
  badAcc.accuracy = 4000;

  thrift::Location invalid;
  invalid.latitude = 12345.6789;
  invalid.longitude = -12345.6789;
  invalid.altitude = 0;
  invalid.accuracy = 1;

  // validate that location gets set
  EXPECT_TRUE(topoW->setLocation(mac, mpk14_150));
  auto site = topoW->getSite("pole-unknown");
  checkLocation(mpk14_150, site->location);
  // validate that location of better accuracy get set
  EXPECT_TRUE(topoW->setLocation(mac, mpk14_50));
  site = topoW->getSite("pole-unknown");
  checkLocation(mpk14_50, site->location);
  // validate that bad accuracy location does not get set
  EXPECT_FALSE(topoW->setLocation(mac, badAcc));
  site = topoW->getSite("pole-unknown");
  checkLocation(mpk14_50, site->location);
  // validate that invalid location does not get set
  EXPECT_FALSE(topoW->setLocation(mac, invalid));
  site = topoW->getSite("pole-unknown");
  checkLocation(mpk14_50, site->location);

}

TEST_F(TopologyFixture, siteModifierTest) {
  thrift::Topology topology;
  topology.name = "test";
  topology.nodes = nodes;
  topology.links = links;
  topology.sites = sites;
  TopologyWrapper topologyW(topology);

  // addSite
  auto newSite = createSite("pole-1", 38.4849, -123.144, 0, 0);
  EXPECT_NO_THROW(topologyW.addSite(newSite));
  auto site = topologyW.getSite("pole-1");
  checkSite(newSite, *site);
  // add same site again
  EXPECT_THROW(topologyW.addSite(newSite), std::invalid_argument);
  // add site with empty name
  newSite = createSite("", 38.4849, -123.144, 0, 0);
  EXPECT_THROW(topologyW.addSite(newSite), std::invalid_argument);

  // delSite
  // delete site that doesn't exist
  EXPECT_THROW(topologyW.delSite("bieberforever"), std::invalid_argument);
  // delete site with nodes still associated to it
  EXPECT_THROW(topologyW.delSite("pole-mpk18"), std::invalid_argument);
  EXPECT_NO_THROW(topologyW.unplugNodeFromSite("5"));
  EXPECT_NO_THROW(topologyW.unplugNodeFromSite("6"));
  EXPECT_NO_THROW(topologyW.unplugNodeFromSite("7"));
  EXPECT_NO_THROW(topologyW.unplugNodeFromSite("8"));
  EXPECT_NO_THROW(topologyW.delSite("pole-mpk18"));

  // editSite (location)
  auto relocatedSite = topologyW.getSite("pole-1");
  relocatedSite->location.latitude = -123;
  relocatedSite->location.longitude = 12345;
  relocatedSite->location.accuracy = -1;
  EXPECT_THROW(
      topologyW.editSite("pole-1", *relocatedSite), std::invalid_argument);
  relocatedSite->location.latitude = -11;
  EXPECT_THROW(
      topologyW.editSite("pole-1", *relocatedSite), std::invalid_argument);
  relocatedSite->location.longitude = 22;
  EXPECT_THROW(
      topologyW.editSite("pole-1", *relocatedSite), std::invalid_argument);
  relocatedSite->location.accuracy = 33;
  EXPECT_NO_THROW(topologyW.editSite("pole-1", *relocatedSite));

  // editSite (rename)
  // rename site that doesn't exist
  EXPECT_THROW(topologyW.editSite("justin", newSite), std::invalid_argument);
  // rename site to itself
  EXPECT_THROW(topologyW.editSite(newSite.name, newSite),
               std::invalid_argument);
  // rename site to an existing site
  thrift::Site newSiteCopy;
  newSiteCopy.name = newSite.name;
  newSiteCopy.location = thrift::Location();
  EXPECT_THROW(topologyW.editSite("pole-mpk17", newSiteCopy),
               std::invalid_argument);
  // rename site
  string newName = "coreSite";
  auto renamedSite = createSite(newName, 37.481044, -122.157929, -25, 10);
  auto allSites = topologyW.getAllSites();
  EXPECT_NO_THROW(topologyW.editSite("pole-mpk20", renamedSite));
  // verify site exists
  site = topologyW.getSite(newName);
  EXPECT_TRUE((bool)site);
  // add a couple nodes to the new site
  auto newNode1 = createNode(
      "pole-1",
      "0:0:0:0:0:10",
      NodeType::DN,
      false,
      NodeStatusType::OFFLINE,
      newName);
  EXPECT_NO_THROW(topologyW.addNode(newNode1));
  auto newNode2 = createNode(
      "pole-2",
      "0:0:0:0:0:11",
      NodeType::DN,
      false,
      NodeStatusType::OFFLINE,
      newName);
  EXPECT_NO_THROW(topologyW.addNode(newNode2));
  // ensure nodes have been updated
  auto nodeNames = topologyW.getNodeNamesBySiteName(newName);
  EXPECT_TRUE((bool)nodeNames);
  // started with 2 nodes attached to site
  EXPECT_EQ(nodeNames->size(), 2);
  for (const auto& nodeName : *nodeNames) {
    auto node = topologyW.getNode(nodeName);
    EXPECT_TRUE((bool)node);
    // make sure site name was updated
    EXPECT_EQ(node->site_name, newName);
  }
}

TEST_F(TopologyFixture, nodeModifierTest) {
  thrift::Topology topology;
  topology.name = "test";
  topology.nodes = nodes;
  topology.links = links;
  topology.sites = sites;
  TopologyWrapper topologyW(topology);

  // addNode
  auto newSite = createSite("pole-1", 38.4849, -123.144, 0, 0);
  EXPECT_NO_THROW(topologyW.addSite(newSite));
  auto newNode = createNode(
      "0",
      "0:0:0:0:0:0",
      NodeType::DN,
      false,
      NodeStatusType::OFFLINE,
      "pole-1");
  EXPECT_NO_THROW(topologyW.addNode(newNode));
  auto node = topologyW.getNode("0");
  checkNode(newNode, *node);
  // add same node again
  EXPECT_THROW(
      topologyW.addNode(newNode), std::invalid_argument);
  // add node with empty name
  newNode = createNode(
      "",
      "0:0:0:0:0:0",
      NodeType::DN,
      false,
      NodeStatusType::OFFLINE,
      "pole-1");
  EXPECT_THROW(
      topologyW.addNode(newNode),
      std::invalid_argument);
  // add node with empty mac address
  newNode = createNode(
      "11",
      "",
      NodeType::DN,
      false,
      NodeStatusType::OFFLINE,
      "pole-1");
  EXPECT_NO_THROW(topologyW.addNode(newNode));
  // add node with invalid mac address
  newNode = createNode(
      "12",
      "111:222:333:444",
      NodeType::DN,
      false,
      NodeStatusType::OFFLINE,
      "pole-1");
  EXPECT_THROW(
      topologyW.addNode(newNode), std::invalid_argument);
  LOG(INFO) << "[nodemodifier] before create 14";
  // attempt to add more than 4 nodes to the same site
  newNode = createNode(
      "14",
      "14:14:14:14:14:14",
      NodeType::DN,
      false,
      NodeStatusType::OFFLINE,
      "pole-mpk18");
  LOG(INFO) << "[nodemodifier] after create 14";
  EXPECT_THROW(
      topologyW.addNode(newNode), std::invalid_argument);
  LOG(INFO) << "[nodemodifier] after add 14";

  // unplug site info
  EXPECT_NO_THROW(topologyW.unplugNodeFromSite("5"));
  LOG(INFO) << "[nodemodifier] after unplug 5";
  EXPECT_THROW(
      topologyW.unplugNodeFromSite("bieberforever"), std::invalid_argument);
  LOG(INFO) << "[nodemodifier] after unplug bieberforever";

  // delNode
  // attempt to delete a node when there are alive links still
  // associated with it
  EXPECT_THROW(
      topologyW.delNode("1", false /*force*/),
      std::invalid_argument);
  LOG(INFO) << "[nodemodifier] after delnode 1 false";
  // force to delete a node with links associated with it alive
  EXPECT_NO_THROW(topologyW.delNode("1", true /*force*/));
  LOG(INFO) << "[nodemodifier] after delnode 1 true";
  // node-4 is still ignited
  EXPECT_THROW(
      topologyW.delNode("4", false /*force*/),
      std::invalid_argument);
  LOG(INFO) << "[nodemodifier] after delnode 4 false";
  // no running links associated with node-6
  EXPECT_NO_THROW(topologyW.delNode("6", false /*force*/));
  LOG(INFO) << "[nodemodifier] after delnode 6 false";
  node = topologyW.getNode("6");
  LOG(INFO) << "[nodemodifier] after getnode 6";
  EXPECT_FALSE(bool(node));
  // delete same node again
  EXPECT_THROW(
      topologyW.delNode("6", false /*force*/),
      std::invalid_argument);
  LOG(INFO) << "[nodemodifier] after delnode 6 false";
  // delete a node that doesn't exist in topology
  EXPECT_THROW(
      topologyW.delNode("bieber", false /*force*/),
      std::invalid_argument);
  LOG(INFO) << "[nodemodifier] after delnode bieber";

  // editNode (rename)
  node = topologyW.getNode("3");
  newNode = createNode("3-new",
                       node->mac_addr,
                       node->node_type,
                       node->pop_node,
                       node->status);
  // rename node with the same name
  EXPECT_THROW(topologyW.editNode("3", *node), std::invalid_argument);
  // rename node that doesn't exist
  EXPECT_THROW(topologyW.editNode("justin", newNode), std::invalid_argument);
  // rename node
  EXPECT_NO_THROW(topologyW.editNode("3", newNode));
}

TEST_F(TopologyFixture, linkModifierTest) {
  thrift::Topology topology;
  topology.name = "test";
  topology.nodes = nodes;
  topology.links = links;
  topology.sites = sites;
  TopologyWrapper topologyW(topology);

  // addLink
  auto newLink = createLink(
      "link-5-8", "5", "8", "5:5:5:5:5:5", "8:8:8:8:8:8",
      LinkType::ETHERNET, false);
  EXPECT_NO_THROW(topologyW.addLink(newLink));
  auto link = topologyW.getLink("link-5-8");
  checkLink(newLink, *link);
  // add same link again
  EXPECT_THROW(
      topologyW.addLink(newLink), std::invalid_argument);
  // end point does not exist
  newLink = createLink(
      "link-0-8", "0", "8", "0:0:0:0:0:0", "8:8:8:8:8:8",
      LinkType::ETHERNET, false);
  EXPECT_THROW(
      topologyW.addLink(newLink), std::invalid_argument);
  // end points are out of order
  newLink = createLink(
      "link-8-5", "8", "5", "8:8:8:8:8:8", "5:5:5:5:5:5",
      LinkType::ETHERNET, false);
  EXPECT_THROW(
      topologyW.addLink(newLink), std::invalid_argument);
  // wrong link name
  newLink = createLink(
      "link-5-8-foo", "5", "8", "5:5:5:5:5:5", "8:8:8:8:8:8",
      LinkType::ETHERNET, false);
  EXPECT_THROW(
      topologyW.addLink(newLink), std::invalid_argument);

  // delLink
  link = topologyW.getLink("link-2-3");
  EXPECT_TRUE(bool(link));
  // attempt to delete an ethernet link
  EXPECT_NO_THROW(topologyW.delLink("2", "3", false /*force*/));
  // link 2-6 is already down
  link = topologyW.getLink("link-2-6");
  EXPECT_TRUE(bool(link));
  EXPECT_NO_THROW(topologyW.delLink("2", "6", false /*force*/));
  // delete same link again
  EXPECT_THROW(
      topologyW.delLink("2", "6", false /*force*/),
      std::invalid_argument);
  // end points of a valid link, but are out of order
  link = topologyW.getLink("link-3-7");
  (*link).is_alive = false;
  EXPECT_THROW(
      topologyW.delLink("7", "3", false /*force*/),
      std::invalid_argument);
}

// unittest for approxDistance()
// pick a few location pairs on earth, which are not too far
// validate if the return distance value is within 1% accuracy
// when compared with trusted source, e.g., google maps
TEST_F(TopologyFixture, approxDistance) {

  double len = 0;
  double expLen = 0;
  const double onePercent = 0.01;
  thrift::Location l1;
  thrift::Location l2;

  // bldg 10 MPK
  l1 = thrift::Location();
  l1.latitude = 37.483462;
  l1.longitude = -122.149796;
  l1.altitude = -25;
  l1.accuracy = 0;
  // bldg 18 MPK
  l2 = thrift::Location();
  l2.latitude = 37.485817;
  l2.longitude = -122.146635;
  l2.altitude = -25;
  l2.accuracy = 0;
  // google map distance = 385m
  expLen = 385;
  len = approxDistance(l1, l2);
  EXPECT_NEAR(expLen, len, onePercent * expLen);

  // MPK
  l1 = thrift::Location();
  l1.latitude = 37.484966;
  l1.longitude = -122.148327;
  l1.altitude = -25;
  l1.accuracy = 0;
  // Palo Alto
  l2 = thrift::Location();
  l2.latitude = 37.427780;
  l2.longitude = -122.170180;
  l2.altitude = -25;
  l2.accuracy = 0;
  // google map distance = 6.65 km
  expLen = 6650;
  len = approxDistance(l1, l2);
  EXPECT_NEAR(expLen, len, onePercent * expLen);

  // MPK
  l1 = thrift::Location();
  l1.latitude = 37.484966;
  l1.longitude = -122.148327;
  l1.altitude = -25;
  l1.accuracy = 0;
  // San Francisco
  l2 = thrift::Location();
  l2.latitude = 37.774489;
  l2.longitude = -122.396150;
  l2.altitude = -25;
  l2.accuracy = 0;
  // google map distance = 38.90 km
  expLen = 38900;
  len = approxDistance(l1, l2);
  EXPECT_NEAR(expLen, len, onePercent * expLen);

  // higher altitude location
  l1 = thrift::Location();
  l1.latitude = 37.484966;
  l1.longitude = -122.148327;
  l1.altitude = -50;
  l1.accuracy = 0;
  // lower altitude location
  l2 = thrift::Location();
  l2.latitude = 37.485966;
  l2.longitude = -122.147327;
  l2.altitude = +50;
  l2.accuracy = 0;
  // distance = 173.26 m
  expLen = 173.26;
  len = approxDistance(l1, l2);
  EXPECT_NEAR(expLen, len, onePercent * expLen);

  // Westchester Lagoon Alaska
  l1 = thrift::Location();
  l1.latitude = 61.208501;
  l1.longitude = -149.922620;
  l1.altitude = 25;
  l1.accuracy = 0;
  // Little Campbell Lake Alaska
  l2 = thrift::Location();
  l2.latitude = 61.163262;
  l2.longitude = -150.024303;
  l2.altitude = 25;
  l2.accuracy = 0;
  // google map distance = 7.41 km
  expLen = 7410;
  len = approxDistance(l1, l2);
  EXPECT_NEAR(expLen, len, onePercent * expLen);

  // Penguin A Antarctica
  l1 = thrift::Location();
  l1.latitude = -84.939853;
  l1.longitude = 173.396147;
  l1.altitude = 2108;
  l1.accuracy = 0;
  // Penguin B Antarctica
  l2 = thrift::Location();
  l2.latitude = -84.909509;
  l2.longitude = 175.407381;
  l2.altitude = 2108;
  l2.accuracy = 0;
  // google map distance = 20.07 km
  expLen = 20070;
  len = approxDistance(l1, l2);
  EXPECT_NEAR(expLen, len, onePercent * expLen);

  // Wrangel Island A
  l1 = thrift::Location();
  l1.latitude = 71.314892;
  l1.longitude = 179.827572;
  l1.altitude = 0;
  l1.accuracy = 0;
  // Wrangel Island B
  l2 = thrift::Location();
  l2.latitude = 71.323622;
  l2.longitude = -179.908053;
  l2.altitude = 0;
  l2.accuracy = 0;
  // google map distance = 9.50km
  expLen = 9500;
  len = approxDistance(l1, l2);
  EXPECT_NEAR(expLen, len, onePercent * expLen);
}

// Check that we can't add more than the max supported DN-DN links
TEST_F(TopologyFixture, maxDnLinkTest) {
  thrift::Topology topology;
  topology.name = "test";
  topology.sites = sites;
  topology.nodes = nodes;
  topology.links = links;
  TopologyWrapper topologyW(topology);

  // add a second DN-DN link
  auto mpLink1 = createLink(
    "link-1-3",
    "1",
    "3",
    "1:1:1:1:1:1",
    "3:3:3:3:3:3",
    LinkType::WIRELESS,
    true
  );
  EXPECT_NO_THROW(topologyW.addLink(mpLink1));

  // add a third DN-DN link
  auto mpLink2 = createLink(
    "link-1-6",
    "1",
    "6",
    "1:1:1:1:1:1",
    "6:6:6:6:6:6",
    LinkType::WIRELESS,
    true
  );
  EXPECT_THROW(
      topologyW.addLink(mpLink2), std::invalid_argument);
}

// Check that we can't add more than the max supported DN-CN links
TEST_F(TopologyFixture, maxCnLinkTest) {
  thrift::Topology topology;
  topology.name = "test";
  // add single dn site/node + cn site
  topology.sites = {
    createSite("dn-1", 37.485301, -122.148005, -25, 10),
    createSite("cn-invalid", 37.485301, -122.148005, -25, 10),
  };
  // create a node to connect all cns + a single cn node past our limit
  topology.nodes = {createNode(
      "dn-1",
      "01:01:01:01:01:01",
      NodeType::DN,
      true,
      NodeStatusType::OFFLINE,
      "dn-1"),
      createNode(
      "cn-invalid",
      "00:01:02:03:04:05",
      NodeType::CN,
      false,
      NodeStatusType::OFFLINE,
      "cn-invalid")};
  topology.links = {};
  TopologyWrapper topologyW(topology);
  for (size_t cnLinkIdx = 0; cnLinkIdx < TopologyWrapper::kMaxNumOfDnCnLinks;
       cnLinkIdx++) {
    // create sites + nodes for cns
    const std::string siteAndNodeName = folly::sformat("cn-{}", cnLinkIdx);
    const std::string macAddr =
        folly::sformat("09:09:09:09:09:{0:02X}", cnLinkIdx);
    auto cnSite = createSite(siteAndNodeName, 37.485301, -122.148005, -25, 10);
    EXPECT_NO_THROW(topologyW.addSite(
        cnSite));
    auto cnNode = createNode(
        siteAndNodeName,
        macAddr,
        NodeType::CN,
        false,
        NodeStatusType::OFFLINE,
        siteAndNodeName);
    EXPECT_NO_THROW(topologyW.addNode(cnNode));
    auto cnLink = createLink(
        folly::sformat("link-{}-{}", siteAndNodeName, "dn-1"),
        siteAndNodeName,
        "dn-1",
        macAddr,
        "01:01:01:01:01:01", // dn-1 mac
        LinkType::WIRELESS,
        false);
    EXPECT_NO_THROW(topologyW.addLink(cnLink));
  }
  // add one more link
  auto cnLink = createLink(
    "link-cn-invalid-dn-1",
    "cn-invalid",
    "dn-1",
    "00:01:02:03:04:05",
    "01:01:01:01:01:01",
    LinkType::WIRELESS,
    true
  );
  EXPECT_THROW(
      topologyW.addLink(cnLink), std::invalid_argument);
}

// Check that we can't add more than the max supported DN + CN links
TEST_F(TopologyFixture, maxDnAndCnLinkTest) {
  thrift::Topology topology;
  topology.name = "test";
  // add single dn site/node + cn site
  topology.sites = {
    createSite("dn-valid", 37.485301, -122.148005, -25, 10),
    createSite("dn-invalid", 37.485301, -122.148005, -25, 10),
    createSite("cn-invalid", 37.485301, -122.148005, -25, 10),
  };
  // create a node to connect all cns + a single cn node past our limit
  topology.nodes = {createNode(
      "dn-valid",
      "00:00:00:00:00:00",
      NodeType::DN,
      true,
      NodeStatusType::OFFLINE,
      "dn-valid"),
      createNode(
      "dn-invalid",
      "02:02:02:02:02:02",
      NodeType::DN,
      false,
      NodeStatusType::OFFLINE,
      "dn-invalid"),
      createNode(
      "cn-invalid",
      "03:03:03:03:03:03",
      NodeType::CN,
      false,
      NodeStatusType::OFFLINE,
      "cn-invalid")};
  topology.links = {};
  TopologyWrapper topologyW(topology);
  // create maximum dn links
  for (size_t dnLinkIdx = 0; dnLinkIdx < TopologyWrapper::kMaxNumOfDnDnLinks;
       dnLinkIdx++) {
    // create sites + nodes for dns
    const std::string siteAndNodeName = folly::sformat("dn-{}", dnLinkIdx);
    const std::string macAddr =
        folly::sformat("01:01:01:01:01:{0:02X}", dnLinkIdx);
    auto dnSite = createSite(siteAndNodeName, 37.485301, -122.148005, -25, 10);
    EXPECT_NO_THROW(topologyW.addSite(dnSite));
    auto dnNode = createNode(
        siteAndNodeName,
        macAddr,
        NodeType::DN,
        false,
        NodeStatusType::OFFLINE,
        siteAndNodeName);
    EXPECT_NO_THROW(topologyW.addNode(dnNode));
    auto dnLink = createLink(
        folly::sformat("link-{}-{}", siteAndNodeName, "dn-valid"),
        siteAndNodeName,
        "dn-valid",
        macAddr,
        "00:00:00:00:00:00", // dn-valid mac
        LinkType::WIRELESS,
        false);
    EXPECT_NO_THROW(topologyW.addLink(dnLink));
  }
  // create maximum cn links - # of dn links
  for (size_t cnLinkIdx = 0;
       cnLinkIdx < TopologyWrapper::kMaxNumOfDnCnLinks -
                       TopologyWrapper::kMaxNumOfDnDnLinks;
       cnLinkIdx++) {
    // create sites + nodes for cns
    const std::string siteAndNodeName = folly::sformat("cn-{}", cnLinkIdx);
    const std::string macAddr =
        folly::sformat("09:09:09:09:09:{0:02X}", cnLinkIdx);
    auto cnSite = createSite(siteAndNodeName, 37.485301, -122.148005, -25, 10);
    EXPECT_NO_THROW(topologyW.addSite(cnSite));
    auto cnNode = createNode(
        siteAndNodeName,
        macAddr,
        NodeType::CN,
        false,
        NodeStatusType::OFFLINE,
        siteAndNodeName);
    EXPECT_NO_THROW(topologyW.addNode(cnNode));
    auto cnLink = createLink(
        folly::sformat("link-{}-{}", siteAndNodeName, "dn-valid"),
        siteAndNodeName,
        "dn-valid",
        macAddr,
        "00:00:00:00:00:00", // dn-valid mac
        LinkType::WIRELESS,
        false);
    EXPECT_NO_THROW(topologyW.addLink(cnLink));
  }
  // add one dn link past limit
  auto dnLink = createLink(
    "link-dn-invalid-dn-valid",
    "dn-invalid",
    "dn-valid",
    "02:02:02:02:02:02",
    "00:00:00:00:00:00",
    LinkType::WIRELESS,
    true
  );
  EXPECT_THROW(
      topologyW.addLink(dnLink), std::invalid_argument);
  // add one cn link past limit
  auto cnLink = createLink(
    "link-cn-invalid-dn-valid",
    "cn-invalid",
    "dn-valid",
    "03:03:03:03:03:03",
    "00:00:00:00:00:00",
    LinkType::WIRELESS,
    true
  );
  EXPECT_THROW(
      topologyW.addLink(cnLink), std::invalid_argument);
}

TEST_F(TopologyFixture, computeAngleTest) {
  // Tests the computeAngle function.
  double ang = 0;
  double expAng = 0;
  const double expErr = 0.0001;
  thrift::Location l1;
  thrift::Location l2;

  // bldg 10 MPK
  l1 = thrift::Location();
  l1.latitude = 37.483462;
  l1.longitude = -122.149796;
  l1.altitude = -25;
  l1.accuracy = 0;
  // bldg 18 MPK
  l2 = thrift::Location();
  l2.latitude = 37.485817;
  l2.longitude = -122.146635;
  l2.altitude = -25;
  l2.accuracy =0;
  expAng = -136.805533661;
  ang = computeAngle(l1, l2);
  EXPECT_NEAR(expAng, ang, expErr);

  // MPK
  l1 = thrift::Location();
  l1.latitude = 37.484966;
  l1.longitude = -122.148327;
  l1.altitude = -25;
  l1.accuracy = 0;
  // Palo Alto
  l2 = thrift::Location();
  l2.latitude = 37.427780;
  l2.longitude = -122.170180;
  l2.altitude = -25;
  l2.accuracy = 0;
  expAng = 73.1249159234;
  ang = computeAngle(l1, l2);
  EXPECT_NEAR(expAng, ang, expErr);

  // MPK
  l1 = thrift::Location();
  l1.latitude = 37.484966;
  l1.longitude = -122.148327;
  l1.altitude = -25;
  l1.accuracy = 0;
  // San Francisco
  l2 = thrift::Location();
  l2.latitude = 37.774489;
  l2.longitude = -122.396150;
  l2.altitude = -25;
  l2.accuracy = 0;
  expAng = -55.8664547297;
  ang = computeAngle(l1, l2);
  EXPECT_NEAR(expAng, ang, expErr);

  // Westchester Lagoon Alaska
  l1 = thrift::Location();
  l1.latitude = 61.208501;
  l1.longitude = -149.922620;
  l1.altitude = 25;
  l1.accuracy = 0;
  // Little Campbell Lake Alaska
  l2 = thrift::Location();
  l2.latitude =  61.163262;
  l2.longitude = -150.024303;
  l2.altitude = 25;
  l2.accuracy = 0;
  expAng = 42.7098575259;
  ang = computeAngle(l1, l2);
  EXPECT_NEAR(expAng, ang, expErr);

  // Penguin A Antarctica
  l1 = thrift::Location();
  l1.latitude = -84.939853;
  l1.longitude = 173.396147;
  l1.altitude = 2108;
  l1.accuracy = 0;
  // Penguin B Antarctica
  l2 = thrift::Location();
  l2.latitude = -84.909509;
  l2.longitude = 175.407381;
  l2.altitude = 2108;
  l2.accuracy = 0;
  expAng = -170.321638291;
  ang = computeAngle(l1, l2);
  EXPECT_NEAR(expAng, ang, expErr);

  // Wrangel Island A
  l1 = thrift::Location();
  l1.latitude = 71.314892;
  l1.longitude = 179.827572;
  l1.altitude = 0;
  l1.accuracy = 0;
  // Wrangel Island B
  l2 = thrift::Location();
  l2.latitude = 71.323622;
  l2.longitude = -179.908053;
  l2.altitude = 0;
  l2.accuracy = 0;
  expAng = -0.00434113995077;
  ang = computeAngle(l1, l2);
  EXPECT_NEAR(expAng, ang, expErr);
}

TEST_F(TopologyFixture, intraSiteLinks) {
  thrift::Topology topology;
  topology.name = "test";
  topology.sites = sites;
  topology.nodes = nodes;
  topology.links = {};
  TopologyWrapper topologyW(
      topology,
      "",  // topologyDir
      true,   // validateTopology
      true);  // createIntrasiteLinks

  // First site has 2 nodes, so 1 wired link is created
  // Second site has 2 nodes, so 1 wired link is created
  // Third site has 4 nodes, so 6 wired links are created
  EXPECT_EQ(topologyW.getAllLinks().size(), 8);
}

// Build a topology file from scratch, with minimal details specified
TEST_F(TopologyFixture, buildTopologyFromScratch) {
  thrift::Topology topology;
  TopologyWrapper topologyW(topology);

  // Set topology name
  EXPECT_NO_THROW(topologyW.setTopologyName("Test Topology"));

  // Add sites
  auto siteA = createSite("site-A", 37.485301, -122.148005, -25, 10);
  auto siteB = createSite("site-B", 37.485180, -122.147885, -25, 10);
  EXPECT_NO_THROW(topologyW.addSite(siteA));
  EXPECT_NO_THROW(topologyW.addSite(siteB));

  // Add nodes (no MACs, etc.)
  thrift::Node node1;
  node1.name = "node1";
  node1.node_type = NodeType::DN;
  node1.site_name = "site-A";
  thrift::Node node2;
  node2.name = "node2";
  node2.node_type = NodeType::DN;
  node2.site_name = "site-B";
  EXPECT_NO_THROW(topologyW.addNode(node1));
  EXPECT_NO_THROW(topologyW.addNode(node2));

  // Add link (no name, MACs, etc.)
  thrift::Link link12;
  link12.a_node_name = "node1";
  link12.z_node_name = "node2";
  link12.link_type = LinkType::WIRELESS;
  EXPECT_NO_THROW(topologyW.addLink(link12));

  // Set node MACs
  EXPECT_NO_THROW(topologyW.setNodeMacByName(
      "node1", "0:0:0:0:0:1"));
  EXPECT_NO_THROW(topologyW.setNodeMacByName(
      "node2", "0:0:0:0:0:2"));

  // Check that link MACs are still empty
  auto resultLink12 = topologyW.getLink("link-node1-node2");
  EXPECT_TRUE(resultLink12->a_node_mac.empty());
  EXPECT_TRUE(resultLink12->z_node_mac.empty());
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
