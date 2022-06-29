#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local update_resolvconf = dofile("/usr/sbin/update_resolvconf")

local lu = require "luaunit"

TestMain = {}

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertIsFunction(update_resolvconf.buildResolvconfConfig)
end

--- Test execution with good config.
function TestMain:testGoodConfig()
  local config = {
    sysParams = {
      dnsServers = {
        dns1 = "2001::1",
        dns2 = "1234:5678:90ab:cdef::2"
      }
    }
  }

  local output = update_resolvconf.buildResolvconfConfig(config)
  lu.assertEquals(output, [[
# THIS FILE IS AUTO-GENERATED, DO NOT EDIT
nameserver 1234:5678:90ab:cdef::2
nameserver 2001::1
]])
end

--- Test execution with empty config.
function TestMain:testEmptyConfig()
  local output = update_resolvconf.buildResolvconfConfig({})
  lu.assertEquals(output, [[
# THIS FILE IS AUTO-GENERATED, DO NOT EDIT

]])
end

os.exit(lu.LuaUnit.run("-v"))
