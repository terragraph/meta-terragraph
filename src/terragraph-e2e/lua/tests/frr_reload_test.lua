#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local frrReload = assert(loadfile("/usr/sbin/frr_reload"))

local lu = require "luaunit"
local tablex = require "pl.tablex"
require("pl.stringx").import()

TestMain = {}
TestContextClass = setmetatable({}, {__index = TestMain})
TestConfigClass = setmetatable({}, {__index = TestMain})
TestHelperFunctions = setmetatable({}, {__index = TestMain})

function TestMain:setUp()
  self.Vtysh, self.Context, self.Config, self.C = frrReload()

  self.simpleConfigFile = [[
!
log file /tmp/log.log
service integrated-vtysh-config
!
ipv6 route de:ea:db:ee:ff::/64 reject
!
ipv6 prefix-list TG-PREFIXES-OUT seq 1 permit 1001:1001::/55
ipv6 prefix-list TG-PREFIXES-OUT seq 2 permit 4001::/56
ipv6 prefix-list TG-PREFIXES-IN seq 2 deny 2001::/56
!
end
]]

  self.bgpConfigFile1 = [[
frr version 7.5.1
frr defaults traditional
hostname node-4a-57-dd-05-00-01
log stdout
!
end
router bgp 12345
 bgp router-id 71.3.25.0
 no bgp default ipv4-unicast
 timers bgp 30 90
 neighbor 3001::1 remote-as 54321
!
 address-family ipv6 unicast
  network 2001::/56
  network 2001::/59
  neighbor 3001::1 activate
  neighbor 3001::1 soft-reconfiguration inbound
  neighbor 3001::1 maximum-prefix 128
  neighbor 3001::1 route-map BLOCK-TG-PREFIXES-IN in
  neighbor 3001::1 route-map ALLOW-TG-PREFIXES-OUT out
 exit-address-family
end
!
ipv6 prefix-list TG-PREFIXES-OUT seq 1 permit 2001::/56
ipv6 prefix-list TG-PREFIXES-IN seq 1 deny 2001::/56
ipv6 prefix-list TG-PREFIXES-IN seq 2 deny 2001::/59
ipv6 prefix-list TG-PREFIXES-IN seq 50 permit any
!
end
route-map ALLOW-TG-PREFIXES-OUT permit 30
 match ipv6 address prefix-list TG-PREFIXES-OUT
 set ipv6 next-hop global 3001::6
!
end
route-map BLOCK-TG-PREFIXES-IN permit 10
 match ipv6 address prefix-list TG-PREFIXES-IN
 set ipv6 next-hop prefer-global
!
end
line vty
end
!
]]

  self.bgpConfigFile2 = [[
frr version 7.5.1
frr defaults traditional
hostname node-4a-57-dd-05-00-01
log stdout
!
end
router bgp 12345
 bgp router-id 71.3.25.0
 no bgp default ipv4-unicast
 no bgp network import-check
 timers bgp 30 90
 neighbor 3001::1 remote-as 54321
!
 address-family ipv6 unicast
  neighbor 3001::1 activate
  neighbor 3001::1 soft-reconfiguration inbound
  neighbor 3001::1 maximum-prefix 128
  neighbor 3001::1 route-map BLOCK-TG-PREFIXES-IN in
  neighbor 3001::1 route-map ALLOW-TG-PREFIXES-OUT out
 exit-address-family
end
!
ipv6 prefix-list TG-PREFIXES-IN seq 50 permit any
!
end
route-map ALLOW-TG-PREFIXES-OUT permit 30
 match ipv6 address prefix-list TG-PREFIXES-OUT
 set ipv6 next-hop global 3001::6
!
end
route-map BLOCK-TG-PREFIXES-IN permit 10
 match ipv6 address prefix-list TG-PREFIXES-IN
 set ipv6 next-hop prefer-global
!
end
line vty
end
!
]]
end

--- Test Context.addLines() function
function TestContextClass:test_addLines()
  lu.assertIsFunction(self.Context.addLines)

  -- Create an empty context and check if empty
  local ctx = self.Context.new()
  lu.assertEquals(ctx.lines, {})
  lu.assertEquals(ctx.dlines, {})
  -- Add lines and check
  local lines = { "line1", "line2" }
  self.Context.addLines(ctx, lines)
  lu.assertEquals(ctx.lines, lines)
  lu.assertTrue(ctx.dlines["line1"])
  lu.assertTrue(ctx.dlines["line2"])
  lu.assertNil(ctx.dlines["line3"])
  self.Context.addLines(ctx, { "line3" })
  lu.assertTrue(ctx.dlines["line3"])
end

--- Test Config.loadContexts() function
function TestConfigClass:test_loadContexts()
  lu.assertIsFunction(self.Config.loadContexts)

  -- Load contexts and count the number of objects created
  -- Simple config
  local simpleConfig = self.Config.new({})
  self.C.loadLinesFromFile(self.simpleConfigFile, simpleConfig)
  lu.assertEquals(tablex.size(simpleConfig.contexts), 0)
  self.Config.loadContexts(simpleConfig)
  lu.assertEquals(tablex.size(simpleConfig.contexts), 6)
  -- Full bgp config
  local bgpConfig = self.Config.new({})
  self.C.loadLinesFromFile(self.bgpConfigFile1, bgpConfig)
  lu.assertEquals(tablex.size(bgpConfig.contexts), 0)
  self.Config.loadContexts(bgpConfig)
  lu.assertEquals(tablex.size(bgpConfig.contexts), 13)

  -- Check one context to guarantee it was loaded correctly
  -- Simple config
  local keys = { "ipv6 prefix-list TG-PREFIXES-OUT seq 2 permit 4001::/56" }
  local expectedContext = self.Context.new(keys, nil)
  lu.assertEquals(
    self.C.getContext(simpleConfig.contexts, keys),
    expectedContext
  )
  -- Full bgp config
  keys = { "route-map BLOCK-TG-PREFIXES-IN permit 10" }
  local lines = {
    "match ipv6 address prefix-list TG-PREFIXES-IN",
    "set ipv6 next-hop prefer-global"
  }
  expectedContext = self.Context.new(keys, lines)
  lu.assertEquals(
    self.C.getContext(bgpConfig.contexts, keys),
    expectedContext
  )

  -- Check invalid contexts
  lu.assertNil(self.C.getContext(simpleConfig.contexts, { "not a key" }))
  lu.assertNil(self.C.getContext(bgpConfig.contexts, { "not a key" }))
end

--- Test C.anyCtxKeyStartsWith() function
function TestHelperFunctions:test_anyCtxKeyStartsWith()
  lu.assertIsFunction(self.C.anyCtxKeyStartsWith)

  -- Test with valid keys
  local keys = { "router bgp 12345", "address-family ipv6 unicast", "key3"}
  lu.assertTrue(self.C.anyCtxKeyStartsWith(keys, "router bgp"))
  lu.assertTrue(self.C.anyCtxKeyStartsWith(keys, "address-family"))
  lu.assertTrue(self.C.anyCtxKeyStartsWith(keys, "key"))
  lu.assertFalse(self.C.anyCtxKeyStartsWith(keys, "not a key"))

  --Edge cases
  lu.assertFalse(self.C.anyCtxKeyStartsWith({}, "key"))
  lu.assertFalse(self.C.anyCtxKeyStartsWith(nil, "key"))
  lu.assertFalse(self.C.anyCtxKeyStartsWith(keys, ""))
  lu.assertFalse(self.C.anyCtxKeyStartsWith(keys, nil))
end

--- Test C.compareContextObjects() function
function TestHelperFunctions:test_compareContextObjects()
  lu.assertIsFunction(self.C.compareContextObjects)

  -- Create two config objects for comparison
  local bgpConfig1 = self.Config.new({})
  self.C.loadLinesFromFile(self.bgpConfigFile1, bgpConfig1)
  local bgpConfig2 = self.Config.new({})
  self.C.loadLinesFromFile(self.bgpConfigFile2, bgpConfig2)

  -- Before loading contexts
  local linesToAdd, linesToDel = self.C.compareContextObjects(
    bgpConfig1,
    bgpConfig2
  )
  lu.assertEquals(tablex.size(linesToAdd), 0)
  lu.assertEquals(tablex.size(linesToDel), 0)

  -- After loading contexts
  self.Config.loadContexts(bgpConfig1)
  self.Config.loadContexts(bgpConfig2)
  linesToAdd, linesToDel = self.C.compareContextObjects(
    bgpConfig1,
    bgpConfig2
  )
  lu.assertEquals(tablex.size(linesToAdd), 5)
  lu.assertEquals(tablex.size(linesToDel), 1)

  -- Compare to self
  linesToAdd, linesToDel = self.C.compareContextObjects(
    bgpConfig1,
    bgpConfig1
  )
  lu.assertEquals(tablex.size(linesToAdd), 0)
  lu.assertEquals(tablex.size(linesToDel), 0)
  linesToAdd, linesToDel = self.C.compareContextObjects(
    bgpConfig2,
    bgpConfig2
  )
  lu.assertEquals(tablex.size(linesToAdd), 0)
  lu.assertEquals(tablex.size(linesToDel), 0)

  -- Check one linesToAdd and one LinesToDel
  linesToAdd, linesToDel = self.C.compareContextObjects(
    bgpConfig1,
    bgpConfig2
  )
  lu.assertEquals(
    linesToAdd[3],
    {{"ipv6 prefix-list TG-PREFIXES-OUT seq 1 permit 2001::/56"}, true}
  )
  lu.assertEquals(
    linesToDel[1],
    {{"router bgp 12345"}, "no bgp network import-check"}
  )
end

--- Test C.linesToFrrConfig() function
function TestHelperFunctions:test_linesToFrrConfig()
  lu.assertIsFunction(self.C.linesToFrrConfig)

  -- Test with one-line context
  local keys = { "route-map BLOCK-TG-PREFIXES-IN permit 10" }
  lu.assertEquals(
    self.C.linesToFrrConfig(keys, true, false),
    { "route-map BLOCK-TG-PREFIXES-IN permit 10" }
  )
  lu.assertEquals(
    self.C.linesToFrrConfig(keys, true, true),
    { "no route-map BLOCK-TG-PREFIXES-IN permit 10" }
  )

  -- Test with multi-line context
  keys = { "address-family ipv6 unicast" }
  local line = "network 2001::/56"
  lu.assertEquals(
    self.C.linesToFrrConfig(keys, line, false),
    { " address-family ipv6 unicast", " network 2001::/56" }
  )
  lu.assertEquals(
    self.C.linesToFrrConfig(keys, line, true),
    { " address-family ipv6 unicast", " no network 2001::/56" }
  )
end

os.exit(lu.LuaUnit.run("-v"))
