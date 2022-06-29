#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local config_print_flags = dofile("/usr/sbin/config_print_flags")

local lu = require "luaunit"
require("pl.stringx").import()

TestMain = {}

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertIsFunction(config_print_flags.exportFlags)
end

--- Test execution with good config.
function TestMain:testGoodConfig()
  local config = {flags = {much = "amaze", so = "excite", very = "wow"}}
  local output = config_print_flags.exportFlags(config)
  lu.assertEquals(
    output, "--much='amaze' --so='excite' --very='wow' --undefok=much,so,very"
  )
end

--- Test execution with empty config.
function TestMain:testEmptyConfig()
  lu.assertNil(config_print_flags.exportFlags({}))
  lu.assertEquals(config_print_flags.exportFlags({flags = {}}), "")
end

--- Test invalid characters in flag names.
function TestMain:testInvalidChars()
  local config = {flags = {["!@#$%^&*"] = "OMG!"}}
  local output = config_print_flags.exportFlags(config)
  lu.assertEquals(output, "")
end

--- Test string escaping.
function TestMain:testEscapes()
  local config = {flags = {a = "a'S''d'F", b = "; rm -rf /"}}
  local output = config_print_flags.exportFlags(config)
  lu.assertEquals(
    output, [[--a='a'\''S'\'''\''d'\''F' --b='; rm -rf /' --undefok=a,b]]
  )
end

-- Test group name support.
function TestMain:testGroupNames()
  local config = {flags = {["some.nested.group"] = "1", ["logging.v"] = "2"}}
  local output = config_print_flags.exportFlags(config)
  lu.assertEquals(output, "--group='1' --v='2' --undefok=group,v")
end

os.exit(lu.LuaUnit.run("-v"))
