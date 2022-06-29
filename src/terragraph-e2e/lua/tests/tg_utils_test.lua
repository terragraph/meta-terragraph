#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local lu = require "luaunit"
local tg_utils = require "tg.utils"
local tablex = require "pl.tablex"

TestMain = {}

--- Test abort() function.
function TestMain:test_abort()
  lu.assertIsFunction(tg_utils.abort)

  local executionStopped = true
  lu.assertError(function()
    tg_utils.abort("error123")
    executionStopped = false
  end)
  lu.assertTrue(executionStopped)
  lu.assertErrorMsgEquals("blah", function()
    tg_utils.abort("blah", true, 0)
    executionStopped = false
  end)
  lu.assertTrue(executionStopped)
end

--- Test get() function.
function TestMain:test_get()
  lu.assertIsFunction(tg_utils.get)

  -- Normal cases
  local tableA = {a = 1}
  lu.assertEquals(tg_utils.get(tableA, "a"), tableA.a)
  lu.assertError(tg_utils.get, tableA, "a", "b")
  lu.assertNil(tg_utils.get(tableA, "x"))
  local tableB = {a = {b = 1}}
  lu.assertEquals(tg_utils.get(tableB, "a"), tableB.a)
  lu.assertEquals(tg_utils.get(tableB, "a", "b"), tableB.a.b)
  lu.assertError(tg_utils.get, tableB, "a", "b", "c")
  lu.assertNil(tg_utils.get(tableB, "x"))

  -- Edge cases
  lu.assertNil(tg_utils.get(nil, nil))
  lu.assertNil(tg_utils.get(nil, "key"))
  local emptyTable = {}
  lu.assertNil(tg_utils.get(emptyTable, "key"))
  lu.assertEquals(tg_utils.get(emptyTable, nil), emptyTable)
end

--- Test set() function.
function TestMain:test_set()
  lu.assertIsFunction(tg_utils.set)

  -- Normal cases
  local tableA = {}
  lu.assertTrue(tg_utils.set(tableA, {"a"}, "value"))
  lu.assertEquals(tableA.a, "value")
  lu.assertTrue(tg_utils.set(tableA, {"a"}, nil))
  lu.assertEquals(tableA.a, nil)
  local tableB = {}
  lu.assertTrue(tg_utils.set(tableB, {"a", "b", "c"}, 123))
  lu.assertEquals(tableB.a.b.c, 123)
  lu.assertTrue(tg_utils.set(tableB, {"a", "b", "c"}, 456))
  lu.assertEquals(tableB.a.b.c, 456)
  lu.assertFalse(tg_utils.set(tableB, {"a", "b", "c", "d"}, 789))

  -- Edge cases
  lu.assertFalse(tg_utils.set(nil, {}, 123))
  lu.assertFalse(tg_utils.set(nil, {"a"}, nil))
  lu.assertFalse(tg_utils.set({}, nil, 123))
  lu.assertFalse(tg_utils.set({}, {}, 123))
end

--- Test tableMerge() function.
function TestMain:test_tableMerge()
  lu.assertIsFunction(tg_utils.tableMerge)

  local t1 = {a = 1, b = "B", c = {such = "wow", much = "amaze"}}
  local t2 = {a = 56789, c = {much = "wow", very = "wow"}, d = {so = "wow"}}
  tg_utils.tableMerge(t1, t2)

  local expected = {
    a = 56789,
    b = "B",
    c = {such = "wow", much = "wow", very = "wow"},
    d = {so = "wow"}
  }
  lu.assertTrue(tablex.deepcompare(t1, expected))
end

--- Test tableMergeAppend() function.
function TestMain:test_tableMergeAppend()
  lu.assertIsFunction(tg_utils.tableMergeAppend)

  local t1 = {a = 1, b = "B", c = {such = "wow", much = "amaze"}}
  local t2 = {a = 56789, c = {much = "wow", very = "wow"}, d = {so = "wow"}}
  local isModified = tg_utils.tableMergeAppend(t1, t2)

  local expected = {
    a = 1,
    b = "B",
    c = {such = "wow", much = "amaze", very = "wow"},
    d = {so = "wow"}
  }
  lu.assertTrue(isModified)
  lu.assertTrue(tablex.deepcompare(t1, expected))

  local t3 = {x = 1}
  local t4 = {x = 2}
  isModified = tg_utils.tableMergeAppend(t3, t4)
  lu.assertFalse(isModified)
  lu.assertTrue(tablex.deepcompare(t3, {x = 1}))
end

--- Test tableDifference() function.
function TestMain:test_tableDifference()
  lu.assertIsFunction(tg_utils.tableDifference)

  local t1 = {a = 1, b = "B", c = {such = "wow", much = "amaze"}}
  local t2 = {a = 56789, c = {much = "wow", very = "wow"}, d = {so = "wow"}}
  local difference = tg_utils.tableDifference(t1, t2)

  local expected = {c = {very = "wow"}, d = {so = "wow"}}
  lu.assertTrue(tablex.deepcompare(difference, expected))
end

--- Test invertTable() function.
function TestMain:test_invertTable()
  lu.assertIsFunction(tg_utils.invertTable)

  local t = {a = "A", b = "B"}
  local expected = {A = "a", B = "b"}
  lu.assertTrue(tablex.deepcompare(tg_utils.invertTable(t), expected))
end

--- Test tableMax() function.
function TestMain:test_tableMax()
  lu.assertIsFunction(tg_utils.tableMax)

  -- Normal cases
  lu.assertEquals(tg_utils.tableMax({1}), 1)
  lu.assertEquals(tg_utils.tableMax({1, 1, 1}), 1)
  lu.assertEquals(tg_utils.tableMax({1, 1, 3, 3}), 3)
  lu.assertEquals(tg_utils.tableMax({0, 9, 1, 3}), 9)
  lu.assertEquals(tg_utils.tableMax({-3, -1, -2, -4}), -1)
  lu.assertEquals(tg_utils.tableMax({1, 1.1, 1.111}), 1.111)

  -- Edge cases
  lu.assertEquals(tg_utils.tableMax({}), nil)
  lu.assertEquals(tg_utils.tableMax({"not-a-number"}), nil)
  lu.assertEquals(tg_utils.tableMax({1, 2, 3, "not-a-number", 5, 6}), 6)
end

--- Test stringifyNumber() function.
function TestMain:test_stringifyNumber()
  lu.assertIsFunction(tg_utils.stringifyNumber)

  lu.assertEquals(tg_utils.stringifyNumber(1), "1")
  lu.assertEquals(tg_utils.stringifyNumber(1.2), "1.2")
  lu.assertEquals(tg_utils.stringifyNumber(1.23456789), "1.23456789")

  -- No loss of precision
  lu.assertEquals(
    tg_utils.stringifyNumber(1283826948325025), "1283826948325025"
  )

  -- Loss of precision
  lu.assertEquals(
    tg_utils.stringifyNumber(9007199254740993), "9007199254740992"
  )
end

--- Test formatBytes() function.
function TestMain:test_formatBytes()
  lu.assertIsFunction(tg_utils.formatBytes)

  lu.assertEquals(tg_utils.formatBytes(1), "1.0 B")
  lu.assertEquals(tg_utils.formatBytes(123), "123.0 B")
  lu.assertEquals(tg_utils.formatBytes(1234), "1.2 KB")
  lu.assertEquals(tg_utils.formatBytes(1234567), "1.2 MB")
  lu.assertEquals(tg_utils.formatBytes(123456789), "117.7 MB")
  lu.assertEquals(tg_utils.formatBytes(1234567890), "1.1 GB")
end

--- Test formatTimeInterval() function.
function TestMain:test_formatTimeInterval()
  lu.assertIsFunction(tg_utils.formatTimeInterval)

  lu.assertEquals(tg_utils.formatTimeInterval(1), "0m1s")
  lu.assertEquals(tg_utils.formatTimeInterval(123), "2m3s")
  lu.assertEquals(tg_utils.formatTimeInterval(12345), "3h25m")
  lu.assertEquals(tg_utils.formatTimeInterval(1234567), "14d6h")
end

--- Test formatPositionString() function.
function TestMain:test_formatPositionString()
  lu.assertIsFunction(tg_utils.formatPositionString)

  lu.assertEquals(tg_utils.formatPositionString(-11.1, 22.2), "11.1 S 22.2 E")
  lu.assertEquals(
    tg_utils.formatPositionString(40.446, -79.982), "40.446 N 79.982 W"
  )
end

--- Test readEnvFile() function.
function TestMain:test_readEnvFile()
  local infoContents = [[
##### THIS FILE IS AUTO GENERATED. DO NOT EDIT  #####
NODE_ID="12:34:12:34:12:34"
NUM_WLAN_MACS="2"
TG_IF2IF="0"
MAC_0="23:45:23:45:23:45"
BUS_0="0000:01"
GPIO_0="-1"
NVRAM_0="bottom_lvds"
MAC_1="34:56:34:56:34:56"
BUS_1="0001:01"
GPIO_1="-1"
NVRAM_1="bottom_lvds"
PCI_ORDER="0000:01:00.0,0001:01:00.0"
HW_MODEL="NXP TG Board LS1048A (PUMA)"
HW_VENDOR="FB"
HW_BOARD_ID="NXP_LS1048A_PUMA"
HW_REV="1.0"
HW_BATCH="0"
HW_SN="10"
]]

  local infoFile = os.tmpname()
  tg_utils.writeFile(infoFile, infoContents)
  local info = tg_utils.readEnvFile(infoFile)
  local ret = os.remove(infoFile)

  lu.assertNotNil(ret)
  lu.assertIsTable(info)
  lu.assertEquals(tablex.size(info), 18)
  lu.assertEquals(info.NODE_ID, "12:34:12:34:12:34")
  lu.assertEquals(info.NUM_WLAN_MACS, "2")
  lu.assertEquals(tonumber(info.NUM_WLAN_MACS), 2)
end

--- Test timespecSub() function.
function TestMain:test_timespecSub()
  lu.assertIsFunction(tg_utils.timespecSub)

  local result = tg_utils.timespecSub(
    {tv_sec = 10, tv_nsec = 123},
    {tv_sec = 2, tv_nsec = 456}
  )
  lu.assertEquals(result.tv_sec, 7)
  lu.assertEquals(result.tv_nsec, 999999667)

  result = tg_utils.timespecSub(
    {tv_sec = 55555, tv_nsec = 55555},
    {tv_sec = 0, tv_nsec = 0}
  )
  lu.assertEquals(result.tv_sec, 55555)
  lu.assertEquals(result.tv_nsec, 55555)

  result = tg_utils.timespecSub(
    {tv_sec = 0, tv_nsec = 0},
    {tv_sec = 55555, tv_nsec = 55555}
  )
  lu.assertEquals(result.tv_sec, -55556)
  lu.assertEquals(result.tv_nsec, 999944445)
end

os.exit(lu.LuaUnit.run("-v"))
