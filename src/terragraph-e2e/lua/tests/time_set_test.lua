#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local time_set = dofile("/usr/sbin/time_set")

local lu = require "luaunit"

TestMain = {}

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertIsFunction(time_set.buildCommand)
end

--- Test execution with good config.
function TestMain:testGoodConfig()
  local config = {
    sysParams = {
      ntpServers = {
        ntp1 = "1.pool.ntp.org",
        ntp2 = "2.pool.ntp.org"
      }
    }
  }
  lu.assertEquals(
    time_set.buildCommand(config),
    "chronyd -q 'server 1.pool.ntp.org iburst' 'server 2.pool.ntp.org iburst'"
  )
end

--- Test execution with default config.
function TestMain:testDefaultConfig()
  lu.assertEquals(
    time_set.buildCommand({}),
    "chronyd -q 'server time.facebook.com iburst'"
  )
end

os.exit(lu.LuaUnit.run("-v"))
