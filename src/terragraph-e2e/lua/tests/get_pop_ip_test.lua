#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local get_pop_ip = dofile("/usr/sbin/get_pop_ip")

local lu = require "luaunit"

TestMain = {}

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertIsFunction(get_pop_ip.getPopIp)
end

--- Test execution with good prefix entries.
function TestMain:testGoodPrefixEntries()
  local prefixEntries = {
    {
      prefix = "::/0",
    },
    {
      prefix = "2001::/64",
      type = 4,
    }
  }

  local output = get_pop_ip.getPopIp(prefixEntries)
  lu.assertEquals(output, "2001::1")
end

--- Test execution with empty prefix entries.
function TestMain:testEmptyPrefixEntries()
  local output = get_pop_ip.getPopIp({})
  lu.assertEquals(output, nil)
end

os.exit(lu.LuaUnit.run("-v"))
