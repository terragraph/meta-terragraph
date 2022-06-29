#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local lu = require "luaunit"
local tg_thrift_utils = require "tg.thrift_utils"
require "liblualongnumber"

TestMain = {}

--- Test lualongnumberToNumber() function.
function TestMain:test_lualongnumberToNumber()
  lu.assertIsFunction(tg_thrift_utils.lualongnumberToNumber)

  -- Normal case
  local x = liblualongnumber.new("4294967296")
  lu.assertEquals(tg_thrift_utils.lualongnumberToNumber(x), 4294967296)
  lu.assertEquals(tg_thrift_utils.lualongnumberToNumber(x, true), 4294967296)

  -- No precise double representation
  x = liblualongnumber.new("9007199254740993")
  lu.assertNil(tg_thrift_utils.lualongnumberToNumber(x))
  lu.assertEquals(
    tg_thrift_utils.lualongnumberToNumber(x, true), 9007199254740992
  )
end

--- Test lualongnumberCmp() function.
function TestMain:test_lualongnumberCmp()
  lu.assertIsFunction(tg_thrift_utils.lualongnumberCmp)

  -- Mix of number, liblualongnumber
  local x = liblualongnumber.new(123)
  lu.assertEquals(tg_thrift_utils.lualongnumberCmp(x, 123), 0)
  lu.assertEquals(tg_thrift_utils.lualongnumberCmp(123, x), 0)
  lu.assertEquals(tg_thrift_utils.lualongnumberCmp(x, 456), -1)
  lu.assertEquals(tg_thrift_utils.lualongnumberCmp(456, x), 1)
  lu.assertEquals(tg_thrift_utils.lualongnumberCmp(x, 1), 1)
  lu.assertEquals(tg_thrift_utils.lualongnumberCmp(1, x), -1)

  -- Both liblualongnumber
  lu.assertEquals(tg_thrift_utils.lualongnumberCmp(x, x), 0)
  local xx = liblualongnumber.new(123)
  lu.assertEquals(tg_thrift_utils.lualongnumberCmp(x, xx), 0)
  local y = liblualongnumber.new(456)
  lu.assertEquals(tg_thrift_utils.lualongnumberCmp(x, y), -1)
  lu.assertEquals(tg_thrift_utils.lualongnumberCmp(y, x), 1)

  -- Both number
  lu.assertEquals(tg_thrift_utils.lualongnumberCmp(123, 123), 0)
  lu.assertEquals(tg_thrift_utils.lualongnumberCmp(123, 456), -1)
  lu.assertEquals(tg_thrift_utils.lualongnumberCmp(123, 1), 1)
end

--- Test listToThriftSet() function.
function TestMain:test_listToThriftSet()
  lu.assertIsFunction(tg_thrift_utils.listToThriftSet)

  lu.assertEquals(
    tg_thrift_utils.listToThriftSet({"5"}), {["5"] = "5"}
  )
  lu.assertEquals(
    tg_thrift_utils.listToThriftSet({"tomato"}), {tomato = "tomato"}
  )
  lu.assertEquals(
    tg_thrift_utils.listToThriftSet({"octopus", "potato"}),
    {octopus = "octopus", potato = "potato"}
  )
end

os.exit(lu.LuaUnit.run("-v"))
