#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local lu = require "luaunit"
local tg_frr_utils = require "tg.frr_utils"

TestMain = {}

function TestMain:setUp()
  self.nodeConfig = {
    bgpParams = {
      localAsn = 65075,
      neighbors = {
        ["0"] = {
          asn = 65074, ipv6 = "2620:10d:c0be:902b::2", maximumPrefixes = 128
        },
        ["1"] = {
          asn = 65074, ipv6 = "2620:10d:c0be:902b::3", maximumPrefixes = 64
        },
      },
      noPrefixCheck = true,
      specificNetworkPrefixes = "2001::/56,3001:0:0:8000::/57",
    },
    popParams = {
      POP_ADDR = "2001::2",
      VPP_ADDR = "2001::3",
    },
    envParams = {
      DPDK_ENABLED = "1",
    }
  }
  self.routerMac = "1a:2b:3c:4d:5e:6f"
  self.openrNetworkPrefix = "1001:1001::/55"
  self.cpePrefixes = { "9001:123::/64", "9001:abc::/64" }
end

--- Test getNodeBgpInfo() function.
function TestMain:test_getNodeBgpInfo()
  lu.assertIsFunction(tg_frr_utils.getNodeBgpInfo)

  -- Execution with good config
  local staticBgpParams, neighbors, tgPrefixes =
    tg_frr_utils.getNodeBgpInfo(
      self.nodeConfig,
      self.routerMac,
      self.openrNetworkPrefix
    )
  local expectedBgpParams = {
    keepalive = 30,
    localAsn = 65075,
    nextHop = "2001::3",
    routerId = "79.227.243.230"
  }
  lu.assertEquals(staticBgpParams, expectedBgpParams)
  lu.assertEquals(neighbors, self.nodeConfig.bgpParams.neighbors)
  local expectedTgPrefixes = {
    "1001:1001::/55",
    "2001::/56",
    "3001:0:0:8000::/57"
  }
  lu.assertEquals(tgPrefixes, expectedTgPrefixes)

  -- Edge cases
  lu.assertNil(tg_frr_utils.getNodeBgpInfo(nil))
  lu.assertNil(tg_frr_utils.getNodeBgpInfo("not a table"))
  lu.assertNil(tg_frr_utils.getNodeBgpInfo({}))
end

--- Test readBgpParams() function.
function TestMain:test_readBgpParams()
  lu.assertIsFunction(tg_frr_utils.readBgpParams)

  -- Execution with good config
  local staticBgpParams = tg_frr_utils.readBgpParams(
    self.nodeConfig.bgpParams,
    self.nodeConfig.popParams,
    self.nodeConfig.envParams.DPDK_ENABLED
  )
  lu.assertEquals(staticBgpParams.localAsn, 65075)
  lu.assertEquals(staticBgpParams.keepalive, 30)
  lu.assertEquals(staticBgpParams.nextHop, "2001::3")

  -- NextHop missing
  local popParams = self.nodeConfig.popParams
  self.nodeConfig.popParams = nil
  lu.assertNil(
    tg_frr_utils.readBgpParams(
      self.nodeConfig.bgpParams,
      self.nodeConfig.popParams,
      self.nodeConfig.envParams.DPDK_ENABLED
    )
  )
  self.nodeConfig.popParams = ""
  lu.assertNil(
    tg_frr_utils.readBgpParams(
      self.nodeConfig.bgpParams,
      self.nodeConfig.popParams,
      self.nodeConfig.envParams.DPDK_ENABLED
    )
  )
  self.nodeConfig.popParams = popParams
end

--- Test readTgPrefixesFromNodeConfig() function.
function TestMain:test_readTgPrefixesFromNodeConfig()
  lu.assertIsFunction(tg_frr_utils.readTgPrefixesFromNodeConfig)

  -- Execution with good config
  local tgPrefixes = tg_frr_utils.readTgPrefixesFromNodeConfig(
    self.nodeConfig.bgpParams,
    self.openrNetworkPrefix
  )
  lu.assertEquals(
    tgPrefixes,
    {"1001:1001::/55","2001::/56","3001:0:0:8000::/57"}
  )
  tgPrefixes = tg_frr_utils.readTgPrefixesFromNodeConfig(
    self.nodeConfig.bgpParams
  )
  lu.assertEquals(
    tgPrefixes,
    {"2001::/56","3001:0:0:8000::/57"}
  )

  -- Bad IPv6 address
  local specificPrefixes = self.nodeConfig.bgpParams.specificNetworkPrefixes
  self.nodeConfig.bgpParams.specificNetworkPrefixes = "2001::/64,1234::/1234"
  lu.assertNil(
    tg_frr_utils.readTgPrefixesFromNodeConfig(self.nodeConfig.bgpParams)
  )
  self.nodeConfig.bgpParams.specificNetworkPrefixes = specificPrefixes
end

--- Test readNeighbors() function.
function TestMain:test_readNeighbors()
  lu.assertIsFunction(tg_frr_utils.readNeighbors)

  -- Execution with good config
  local neighborsConfig = tg_frr_utils.readNeighbors(self.nodeConfig.bgpParams)
  local expected = {
    ["0"] = {
      asn = 65074, ipv6 = "2620:10d:c0be:902b::2", maximumPrefixes = 128
    },
    ["1"] = {
      asn = 65074, ipv6 = "2620:10d:c0be:902b::3", maximumPrefixes = 64
    }
  }
  lu.assertEquals(neighborsConfig, expected)

  -- Edge cases
  lu.assertNil(tg_frr_utils.readNeighbors(nil))
  lu.assertNil(tg_frr_utils.readNeighbors("not a table"))
  lu.assertNil(tg_frr_utils.readNeighbors({}))
  -- Invalid neighbor
  local ipv6 = self.nodeConfig.bgpParams.neighbors["1"].ipv6
  self.nodeConfig.bgpParams.neighbors["1"].ipv6 = "10.0.0.1"
  lu.assertNil(tg_frr_utils.readNeighbors(self.nodeConfig.bgpParams))
  self.nodeConfig.bgpParams.neighbors["1"].ipv6 = ipv6
end

--- Test fillConfigTemplate() function.
function TestMain:test_fillConfigTemplate()
  lu.assertIsFunction(tg_frr_utils.fillConfigTemplate)

  local staticBgpParams, neighbors, tgPrefixes =
    tg_frr_utils.getNodeBgpInfo(
      self.nodeConfig,
      self.routerMac,
      self.openrNetworkPrefix
    )
  local output = tg_frr_utils.fillConfigTemplate(
    staticBgpParams, neighbors, tgPrefixes, self.cpePrefixes
  )
  lu.assertEquals(output, [[
# THIS FILE IS AUTO-GENERATED, DO NOT EDIT
router bgp 65075
 bgp router-id 79.227.243.230
 no bgp default ipv4-unicast
 no bgp network import-check

 neighbor 2620:10d:c0be:902b::2 remote-as 65074
 neighbor 2620:10d:c0be:902b::3 remote-as 65074

 timers bgp 30 90

 address-family ipv6 unicast
  network 1001:1001::/55
  network 2001::/56
  network 3001:0:0:8000::/57
  network 9001:123::/64
  network 9001:abc::/64

  neighbor 2620:10d:c0be:902b::2 activate
  neighbor 2620:10d:c0be:902b::3 activate

  neighbor 2620:10d:c0be:902b::2 soft-reconfiguration inbound
  neighbor 2620:10d:c0be:902b::2 maximum-prefix 128
  neighbor 2620:10d:c0be:902b::2 route-map BLOCK-TG-PREFIXES-IN in
  neighbor 2620:10d:c0be:902b::2 route-map ALLOW-TG-PREFIXES-OUT out

  neighbor 2620:10d:c0be:902b::3 soft-reconfiguration inbound
  neighbor 2620:10d:c0be:902b::3 maximum-prefix 64
  neighbor 2620:10d:c0be:902b::3 route-map BLOCK-TG-PREFIXES-IN in
  neighbor 2620:10d:c0be:902b::3 route-map ALLOW-TG-PREFIXES-OUT out

 exit-address-family

ipv6 prefix-list TG-PREFIXES-OUT seq 1 permit 1001:1001::/55
ipv6 prefix-list TG-PREFIXES-OUT seq 2 permit 2001::/56
ipv6 prefix-list TG-PREFIXES-OUT seq 3 permit 3001:0:0:8000::/57
ipv6 prefix-list TG-PREFIXES-IN seq 1 deny 1001:1001::/55
ipv6 prefix-list TG-PREFIXES-IN seq 2 deny 2001::/56
ipv6 prefix-list TG-PREFIXES-IN seq 3 deny 3001:0:0:8000::/57
ipv6 prefix-list TG-PREFIXES-IN seq 50 permit any

ipv6 prefix-list CPE-PREFIXES-OUT seq 1 permit 9001:123::/64
ipv6 prefix-list CPE-PREFIXES-OUT seq 2 permit 9001:abc::/64
ipv6 prefix-list CPE-PREFIXES-IN seq 1 deny 9001:123::/64
ipv6 prefix-list CPE-PREFIXES-IN seq 2 deny 9001:abc::/64

route-map ALLOW-TG-PREFIXES-OUT permit 10
 match ipv6 address prefix-list CPE-PREFIXES-OUT
 set ipv6 next-hop global 2001::3

route-map ALLOW-TG-PREFIXES-OUT permit 20
 match ipv6 address prefix-list TG-PREFIXES-OUT
 set ipv6 next-hop global 2001::3

route-map BLOCK-TG-PREFIXES-IN permit 10
 match ipv6 address prefix-list CPE-PREFIXES-IN
 set ipv6 next-hop prefer-global

route-map BLOCK-TG-PREFIXES-IN permit 20
 match ipv6 address prefix-list TG-PREFIXES-IN
 set ipv6 next-hop prefer-global

log stdout
]])
end

--- Test computeRouterId() function.
function TestMain:test_computeRouterId()
  lu.assertIsFunction(tg_frr_utils.computeRouterId)

  lu.assertEquals(
    tg_frr_utils.computeRouterId(
      self.nodeConfig.bgpParams.localAsn, self.routerMac
    ),
    "79.227.243.230"
  )
  lu.assertEquals(
    tg_frr_utils.computeRouterId(12345, "aa:bb:cc:dd:ee:ff"),
    "71.3.249.239"
  )
  lu.assertEquals(
    tg_frr_utils.computeRouterId(2 ^ 16 - 1, "ff:ff:ff:ff:ff:ff"),
    "79.255.255.255"
  )
  lu.assertEquals(
    tg_frr_utils.computeRouterId(2 ^ 16 - 1, "ff:ff:ff:ff:ff:00"),
    "79.255.15.240"
  )

  -- Edge cases
  lu.assertNil(tg_frr_utils.computeRouterId(12345, "with space"))
  lu.assertNil(tg_frr_utils.computeRouterId(12345, ""))
  lu.assertNil(tg_frr_utils.computeRouterId(12345, nil))
  lu.assertNil(tg_frr_utils.computeRouterId("12345", "aa:bb:cc:dd:ee:ff"))
  lu.assertNil(tg_frr_utils.computeRouterId(nil, "aa:bb:cc:dd:ee:ff"))
  lu.assertNil(tg_frr_utils.computeRouterId("abcde", "aa:bb:cc:dd:ee:ff"))
end

os.exit(lu.LuaUnit.run("-v"))
