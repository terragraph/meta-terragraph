#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local lu = require "luaunit"
local tg_net_utils = require "tg.net_utils"

TestMain = {}

--- Test isIPv4() function.
function TestMain:test_isIPv4()
  lu.assertIsFunction(tg_net_utils.isIPv4)

  lu.assertTrue(tg_net_utils.isIPv4("127.0.0.1"))
  lu.assertTrue(tg_net_utils.isIPv4("0.0.0.0"))
  lu.assertTrue(tg_net_utils.isIPv4("255.255.255.255"))

  lu.assertFalse(tg_net_utils.isIPv4(""))
  lu.assertFalse(tg_net_utils.isIPv4("..."))
  lu.assertFalse(tg_net_utils.isIPv4("1"))
  lu.assertFalse(tg_net_utils.isIPv4("1.2.3.456"))
  lu.assertFalse(tg_net_utils.isIPv4("1.1.x.1"))
  lu.assertFalse(tg_net_utils.isIPv4("1:2:3:4:a:b:c:d"))
  lu.assertFalse(tg_net_utils.isIPv4("::1"))
end

--- Test isIPv6() function.
function TestMain:test_isIPv6()
  lu.assertIsFunction(tg_net_utils.isIPv6)

  lu.assertTrue(tg_net_utils.isIPv6("1:2:3:4:a:b:c:d"))
  lu.assertTrue(tg_net_utils.isIPv6("1:0:0:0:0:0:0:f"))
  lu.assertTrue(tg_net_utils.isIPv6("1:0:0:00:00:000:000:0000"))
  lu.assertTrue(tg_net_utils.isIPv6("1::1"))
  lu.assertTrue(tg_net_utils.isIPv6("1::2:3"))
  lu.assertTrue(tg_net_utils.isIPv6("::1"))
  lu.assertTrue(tg_net_utils.isIPv6("::"))

  lu.assertFalse(tg_net_utils.isIPv6(""))
  lu.assertFalse(tg_net_utils.isIPv6(":"))
  lu.assertFalse(tg_net_utils.isIPv6("1!2@3#4$a%b^c&d"))
  lu.assertFalse(tg_net_utils.isIPv6("1::::2"))
  lu.assertFalse(tg_net_utils.isIPv6("1:2:3"))
  lu.assertFalse(tg_net_utils.isIPv6("1:2:3:4:5:6:7:8:9:a"))
  lu.assertFalse(tg_net_utils.isIPv6("i:j:k:l:m:n:o:p"))
  lu.assertFalse(tg_net_utils.isIPv6("1::e::f"))
  lu.assertFalse(tg_net_utils.isIPv6("127.0.0.1"))
  lu.assertFalse(tg_net_utils.isIPv6("0.0.0.0"))
  lu.assertFalse(tg_net_utils.isIPv6("[1:2:3:4:a:b:c:d]"))
  lu.assertFalse(tg_net_utils.isIPv6("[1::1]"))
  lu.assertFalse(tg_net_utils.isIPv6("z::"))
  lu.assertFalse(tg_net_utils.isIPv6("::z"))
  lu.assertFalse(tg_net_utils.isIPv6("1::1aaaaa"))
  lu.assertFalse(tg_net_utils.isIPv6("aaaaaa1::1"))
end

--- Test abbreviateIPv6() function.
function TestMain:test_abbreviateIPv6()
  lu.assertIsFunction(tg_net_utils.abbreviateIPv6)

  lu.assertEquals(tg_net_utils.abbreviateIPv6("2001::"), "2001::")
  lu.assertEquals(tg_net_utils.abbreviateIPv6("2001::1"), "2001::1")
  lu.assertEquals(tg_net_utils.abbreviateIPv6("2001:0:0:0:0:0:0:0"), "2001::")
  lu.assertEquals(tg_net_utils.abbreviateIPv6("2001:0:0:0:0:0:0:1"), "2001::1")
  lu.assertEquals(
    tg_net_utils.abbreviateIPv6("2001:1234:5678:9abc:0000:0000:defe:dcba"),
    "2001:1234:5678:9abc::defe:dcba"
  )
  lu.assertEquals(
    tg_net_utils.abbreviateIPv6("2001:1234:5678:9abc:0:0:defe:dcba"),
    "2001:1234:5678:9abc::defe:dcba"
  )
  lu.assertEquals(
    tg_net_utils.abbreviateIPv6("2001:0:0:9abc:0:0:0:dcba"),
    "2001:0:0:9abc::dcba"
  )
  lu.assertEquals(tg_net_utils.abbreviateIPv6("0:0:0:0:0:0:0:0"), "::")
  lu.assertEquals(tg_net_utils.abbreviateIPv6("0:0:0:0:0:0:0:1"), "::1")
end

--- Test expandIPv6() function.
function TestMain:test_expandIPv6()
  lu.assertIsFunction(tg_net_utils.expandIPv6)

  lu.assertEquals(tg_net_utils.expandIPv6("::1"), "0:0:0:0:0:0:0:1")
  lu.assertEquals(tg_net_utils.expandIPv6("2001::"), "2001:0:0:0:0:0:0:0")
  lu.assertEquals(tg_net_utils.expandIPv6("2001::1"), "2001:0:0:0:0:0:0:1")
  lu.assertEquals(
    tg_net_utils.expandIPv6("2001:0:0:0:0:0:0:1"), "2001:0:0:0:0:0:0:1"
  )
  lu.assertEquals(
    tg_net_utils.expandIPv6("2001:1234:5678:9abc::defe:dcba"),
    "2001:1234:5678:9abc:0:0:defe:dcba"
  )
  lu.assertEquals(tg_net_utils.expandIPv6("::"), "0:0:0:0:0:0:0:0")
end

--- Test binaryAddressToString() function.
function TestMain:test_binaryAddressToString()
  lu.assertIsFunction(tg_net_utils.binaryAddressToString)

  lu.assertEquals(
    tg_net_utils.binaryAddressToString(
      "\32\1\170\187\204\221\238\255\0\0\0\0\0\0\0\1"
    ),
    "2001:aabb:ccdd:eeff::1"
  )
  lu.assertEquals(tg_net_utils.binaryAddressToString("\127\0\0\1"), "127.0.0.1")
end

--- Test stringToBinaryAddress() function.
function TestMain:test_stringToBinaryAddress()
  lu.assertIsFunction(tg_net_utils.stringToBinaryAddress)

  lu.assertEquals(
    tg_net_utils.stringToBinaryAddress("2001:aabb:ccdd:eeff::1"),
    "\32\1\170\187\204\221\238\255\0\0\0\0\0\0\0\1"
  )
  lu.assertEquals(tg_net_utils.stringToBinaryAddress("::"), ("\0"):rep(16))
  lu.assertEquals(tg_net_utils.stringToBinaryAddress("127.0.0.1"), "\127\0\0\1")
end

--- Test isIPv4Network() function.
function TestMain:test_isIPv4Network()
  lu.assertIsFunction(tg_net_utils.isIPv4Network)

  lu.assertTrue(tg_net_utils.isIPv4Network("192.168.100.14/24"))
  lu.assertTrue(tg_net_utils.isIPv4Network("10.0.0.1/8"))
  lu.assertTrue(tg_net_utils.isIPv4Network("123.123.123.123/0"))
  lu.assertTrue(tg_net_utils.isIPv4Network("4.4.4.4/32"))

  lu.assertFalse(tg_net_utils.isIPv4Network("1.2.3.4"))
  lu.assertFalse(tg_net_utils.isIPv4Network("1.2.3.4/"))
  lu.assertFalse(tg_net_utils.isIPv4Network("1.2.3.4/64"))
  lu.assertFalse(tg_net_utils.isIPv4Network("1.2.3.4/-8"))
  lu.assertFalse(tg_net_utils.isIPv4Network("/"))
  lu.assertFalse(tg_net_utils.isIPv4Network("/24"))
  lu.assertFalse(tg_net_utils.isIPv4Network("2001:db8::/48"))
  lu.assertFalse(tg_net_utils.isIPv4Network("::1/128"))
end

--- Test isIPv6Network() function.
function TestMain:test_isIPv6Network()
  lu.assertIsFunction(tg_net_utils.isIPv6Network)

  lu.assertTrue(tg_net_utils.isIPv6Network("2001:db8::/48"))
  lu.assertTrue(tg_net_utils.isIPv6Network("::1/128"))
  lu.assertTrue(tg_net_utils.isIPv6Network("1234::/0"))
  lu.assertTrue(tg_net_utils.isIPv6Network("2001:1234:5678:9abc::/64"))

  lu.assertFalse(tg_net_utils.isIPv6Network("2001:db8::"))
  lu.assertFalse(tg_net_utils.isIPv6Network("2001:db8::/"))
  lu.assertFalse(tg_net_utils.isIPv6Network("2001:db8::/129"))
  lu.assertFalse(tg_net_utils.isIPv6Network("2001:db8::/-64"))
  lu.assertFalse(tg_net_utils.isIPv6Network("/"))
  lu.assertFalse(tg_net_utils.isIPv6Network("/64"))
  lu.assertFalse(tg_net_utils.isIPv6Network("192.168.100.14/24"))
  lu.assertFalse(tg_net_utils.isIPv6Network("10.0.0.1/8"))
end

--- Test isMacAddr() function.
function TestMain:test_isMacAddr()
  lu.assertIsFunction(tg_net_utils.isMacAddr)

  lu.assertTrue(tg_net_utils.isMacAddr("12:34:56:78:9a:bc"))
  lu.assertTrue(tg_net_utils.isMacAddr("01:23:45:ab:cd:ef"))
  lu.assertTrue(tg_net_utils.isMacAddr("ff:ff:ff:ff:ff:ff"))
  lu.assertTrue(tg_net_utils.isMacAddr("00:00:00:00:00:00"))
  lu.assertFalse(tg_net_utils.isMacAddr(""))
  lu.assertFalse(tg_net_utils.isMacAddr(":::::"))
  lu.assertFalse(tg_net_utils.isMacAddr("12:34:56:78:9a:bc "))
  lu.assertFalse(tg_net_utils.isMacAddr("12:34:56:78:9a:bc:"))
  lu.assertFalse(tg_net_utils.isMacAddr("12:34:-5:78:9a:bc"))
  lu.assertFalse(tg_net_utils.isMacAddr("12-34-56-78-9a-bc"))
  lu.assertFalse(tg_net_utils.isMacAddr("01:23:45:AB:CD:EF"))
  lu.assertFalse(tg_net_utils.isMacAddr("1:2:3:4:5:6"))
end

--- Test isAddrInIPv6Network() function.
function TestMain:test_isAddrInIPv6Network()
  lu.assertIsFunction(tg_net_utils.isAddrInIPv6Network)

  lu.assertTrue(tg_net_utils.isAddrInIPv6Network("1001::", "1001::/64"))
  lu.assertTrue(tg_net_utils.isAddrInIPv6Network("9abc::", "9abc:1::/31"))
  lu.assertTrue(tg_net_utils.isAddrInIPv6Network("db8::", "db8::1/127"))
  lu.assertTrue(tg_net_utils.isAddrInIPv6Network("3001::1", "3001::/127"))

  lu.assertFalse(tg_net_utils.isAddrInIPv6Network("9abc::", "9abc:1::/32"))
  lu.assertFalse(tg_net_utils.isAddrInIPv6Network("db8::", "db8::1/128"))
  lu.assertFalse(tg_net_utils.isAddrInIPv6Network("3001::1", "3001::/128"))
end

--- Test isPrefixInIPv6Network() function.
function TestMain:test_isPrefixInIPv6Network()
  lu.assertIsFunction(tg_net_utils.isPrefixInIPv6Network)

  lu.assertTrue(tg_net_utils.isPrefixInIPv6Network("1001::/128", "1001::/64"))
  lu.assertTrue(tg_net_utils.isPrefixInIPv6Network("9ab::/32", "9ab:1::/31"))
  lu.assertTrue(tg_net_utils.isPrefixInIPv6Network("db8::/127", "db8::1/127"))
  lu.assertTrue(tg_net_utils.isPrefixInIPv6Network("301::1/128", "301::/127"))

  lu.assertFalse(tg_net_utils.isPrefixInIPv6Network("1001::/64", "1001::/128"))
  lu.assertFalse(tg_net_utils.isPrefixInIPv6Network("9ab::/64", "9ab:1::/32"))
  lu.assertFalse(tg_net_utils.isPrefixInIPv6Network("db8::/128", "db8::1/128"))
  lu.assertFalse(tg_net_utils.isPrefixInIPv6Network("301::1/128", "301::/128"))
end

--- Test isAddrLinkLocal() function.
function TestMain:test_isAddrLinkLocal()
  lu.assertIsFunction(tg_net_utils.isAddrLinkLocal)

  lu.assertFalse(tg_net_utils.isAddrLinkLocal("2001::1/128"))
  lu.assertFalse(tg_net_utils.isAddrLinkLocal("1001::1/128"))
  lu.assertFalse(tg_net_utils.isAddrLinkLocal("ff02::1"))

  lu.assertTrue(tg_net_utils.isAddrLinkLocal("fe80::beef:dead:fefe:cafe"))
end

os.exit(lu.LuaUnit.run("-v"))
