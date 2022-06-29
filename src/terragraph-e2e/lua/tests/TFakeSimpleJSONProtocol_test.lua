#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

require "terragraph_thrift.Topology_ttypes"
require "tg.TFakeSimpleJSONProtocol"
local lu = require "luaunit"
local tablex = require "pl.tablex"

TestMain = {}

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertEquals(TFakeSimpleJSONProtocol.__type, "TFakeSimpleJSONProtocol")
  lu.assertEquals(TFakeSimpleJSONProtocol.__parent.__type, "TProtocolBase")
  lu.assertEquals(
    TFakeSimpleJSONProtocolFactory.__type, "TFakeSimpleJSONProtocolFactory"
  )
  lu.assertEquals(
    TFakeSimpleJSONProtocolFactory.__parent.__type, "TProtocolFactory"
  )
end

--- Deserialization tests for 'Topology' structure.
-- Contains: bools, ints, doubles, strings, lists, sets, maps, nested structs
function TestMain:testDeserializeTopology()
  -- Deserialize struct from JSON
  local json = [[
{
  "name": "Carrot Farm",
  "nodes": [
    {
      "name": "carrot-1",
      "node_type": 2,
      "mac_addr": "11:22:33:44:55:66",
      "wlan_mac_addrs": [
        "11:22:33:44:55:66"
      ],
      "pop_node": true,
      "site_name": "A"
    },
    {
      "name": "carrot-2",
      "node_type": 2,
      "mac_addr": "aa:bb:cc:dd:ee:ff",
      "wlan_mac_addrs": [
        "12:34:56:78:90:ab",
        "01:01:01:01:01:01"
      ],
      "pop_node": false,
      "site_name": "B"
    }
  ],
  "links": [
    {
      "name": "link-carrot-1-carrot-2",
      "a_node_name": "carrot-1",
      "a_node_mac": "11:22:33:44:55:66",
      "z_node_name": "carrot-2",
      "z_node_mac": "12:34:56:78:90:ab",
      "link_type": 1
    }
  ],
  "sites": [
    {
      "name": "A",
      "location": {
        "latitude": 37.484946,
        "longitude": -122.1475237,
        "altitude": 10
      }
    },
    {
      "name": "B",
      "location": {
        "latitude": 37.484946,
        "longitude": -122.1472237,
        "altitude": 10
      }
    }
  ],
  "config": {
    "deterministic_prefix_alloc_params": {
      "zones": {
        "A": {
          "node_names": [
            "carrot-1",
            "carrot-2"
          ],
          "zone_prefixes": [
            "2001::/56"
          ]
        }
      }
    }
  },
  "extra_field_that_should_be_ignored": "potato"
}
]]
  local topology = Topology:new{}
  local proto = TFakeSimpleJSONProtocolFactory:getProtocol(json)
  topology:read(proto)

  -- Construct expected result
  local node1 = Node:new{}
  node1.name = "carrot-1"
  node1.node_type = 2
  node1.mac_addr = "11:22:33:44:55:66"
  node1.wlan_mac_addrs = {"11:22:33:44:55:66"}
  node1.pop_node = true
  node1.site_name = "A"
  local node2 = Node:new{}
  node2.name = "carrot-2"
  node2.node_type = 2
  node2.mac_addr = "aa:bb:cc:dd:ee:ff"
  node2.wlan_mac_addrs = {"12:34:56:78:90:ab", "01:01:01:01:01:01"}
  node2.pop_node = false
  node2.site_name = "B"
  local link = Link:new{}
  link.name = "link-carrot-1-carrot-2"
  link.a_node_name = "carrot-1"
  link.a_node_mac = "11:22:33:44:55:66"
  link.z_node_name = "carrot-2"
  link.z_node_mac = "12:34:56:78:90:ab"
  link.link_type = 1
  local site1 = Site:new{}
  site1.name = "A"
  site1.location = Location:new{}
  site1.location.latitude = 37.484946
  site1.location.longitude = -122.1475237
  site1.location.altitude = 10
  local site2 = Site:new{}
  site2.name = "B"
  site2.location = Location:new{}
  site2.location.latitude = 37.484946
  site2.location.longitude = -122.1472237
  site2.location.altitude = 10
  local zone = Zone:new{}
  zone.node_names = {["carrot-1"] = "carrot-1", ["carrot-2"] = "carrot-2"}
  zone.zone_prefixes = {["2001::/56"] = "2001::/56"}
  local config = Config:new{}
  config.deterministic_prefix_alloc_params =
    DeterministicPrefixAllocParams:new{}
  config.deterministic_prefix_alloc_params.zones = {A = zone}
  local t = Topology:new{}
  t.name = "Carrot Farm"
  t.nodes = {node1, node2}
  t.links = {link}
  t.sites = {site1, site2}
  t.config = config

  lu.assertTrue(tablex.deepcompare(topology, t))
end

os.exit(lu.LuaUnit.run("-v"))
