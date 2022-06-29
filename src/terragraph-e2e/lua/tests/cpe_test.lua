#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local config_get_cpe = dofile("/usr/sbin/config_get_cpe")
local cpe_operations = dofile("/usr/sbin/cpe_operations")

local lu = require "luaunit"

TestMain = {}

function TestMain:setUp()
  self.config = {
    cpeConfig = {
      TenGigabitEthernet1 = {
        prefix = "2001:a:b:c::/64"
      },
      TenGigabitEthernet0 = {
        prefix = "2001:a:b:d::/64"
      }
    }
  }
  self.config_deprecated = {
    envParams = {
      CPE_INTERFACE = "TenGigabitEthernet1",
      CPE_IFACE_IP_PREFIX = "2001:a:b:c::/64"
    }
  }
end

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertIsFunction(config_get_cpe.getCpeInterfaces)
  lu.assertIsFunction(config_get_cpe.getCpePrefix)
  lu.assertIsFunction(cpe_operations.runCpeOperations)
end

--- Test reading CPE interfaces from new config.
function TestMain:testGetCpeInterfaces()
  local cpeInterfaces = config_get_cpe.getCpeInterfaces(self.config, 1)
  lu.assertIsString(cpeInterfaces)
  lu.assertStrContains(cpeInterfaces, "TenGigabitEthernet0")
  cpeInterfaces = config_get_cpe.getCpeInterfaces(self.config, 2)
  lu.assertIsString(cpeInterfaces)
  lu.assertStrContains(cpeInterfaces, "TenGigabitEthernet1")
end

--- Test reading CPE interfaces from deprecated config.
function TestMain:testGetCpeInterfaceDeprecated()
  local cpeInterfaces = config_get_cpe.getCpeInterfaces(self.config_deprecated)
  lu.assertIsString(cpeInterfaces)
  lu.assertStrContains(cpeInterfaces, "TenGigabitEthernet1")
end

--- Test reading CPE prefixes.
function TestMain:testGetCpePrefixes()
  local cpePrefix =
    config_get_cpe.getCpePrefix(self.config, "TenGigabitEthernet1")
  lu.assertEquals(cpePrefix, "2001:a:b:c::/64")
end

--- Test reading CPE interfaces from deprecated config.
function TestMain:testGetCpePrefixDeprecated()
  local cpePrefix =
    config_get_cpe.getCpePrefix(self.config_deprecated, "TenGigabitEthernet1")
  lu.assertEquals(cpePrefix, "2001:a:b:c::/64")
end

--- Test execution with bad config and invalid interfaces.
function TestMain:testBadConfig()
  -- Empty config
  local cpeInterfaces = config_get_cpe.getCpeInterfaces({})
  lu.assertEquals(cpeInterfaces, "")

  -- invalid interface
  local cpePrefix = config_get_cpe.getCpePrefix(self.config, "InvalidInterface")
  lu.assertEquals(cpePrefix, "")
end

os.exit(lu.LuaUnit.run("-v"))
