#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local update_bgpd_conf = dofile("/usr/sbin/update_bgpd_conf")

local lu = require "luaunit"

TestMain = {}

function TestMain:setUp()
  self.config = {
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
  self.openrKvstorePrefix = "1001:1001::/55"
end

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertIsFunction(update_bgpd_conf.buildBgpdConfig)
end

--- Test execution with good config.
function TestMain:testGoodConfig()
  local output = update_bgpd_conf.buildBgpdConfig(
    self.config, self.routerMac, self.openrKvstorePrefix
  )
  lu.assertIsString(output)
end

--- Test execution with bad config.
function TestMain:testBadConfig()
  -- Empty
  lu.assertNil(update_bgpd_conf.buildBgpdConfig({}, ""))

  -- Invalid prefix
  local specificNetworkPrefixes = self.config.bgpParams.specificNetworkPrefixes
  self.config.bgpParams.specificNetworkPrefixes = "2001::/64,1234::/1234"
  lu.assertNil(
    update_bgpd_conf.buildBgpdConfig(
      self.config, self.routerMac, self.openrKvstorePrefix
    )
  )
  self.config.bgpParams.specificNetworkPrefixes = specificNetworkPrefixes

  -- Invalid neighbor config
  local ipv6 = self.config.bgpParams.neighbors["1"].ipv6
  self.config.bgpParams.neighbors["1"].ipv6 = "10.0.0.1"
  lu.assertNil(
    update_bgpd_conf.buildBgpdConfig(
      self.config, self.routerMac, self.openrKvstorePrefix
    )
  )
  self.config.bgpParams.neighbors["1"].ipv6 = ipv6
end

os.exit(lu.LuaUnit.run("-v"))
