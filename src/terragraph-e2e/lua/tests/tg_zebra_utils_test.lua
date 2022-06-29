#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local lu = require "luaunit"
local tg_rtnl = require "tg.rtnl"
local tg_zebra_utils = require "tg.zebra_utils"
local tablex = require "pl.tablex"

TestMain = {}

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertIsFunction(tg_zebra_utils.parse)
end

--- Test RTM_DELROUTE parsing.
function TestMain:testDelRoute()
  local data =
    "\x01\x01\x00\x34\x30\x00\x00\x00\x19\x00\x01\x04\x00\x00\x00\x00\x00" ..
    "\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x14\x00" ..
    "\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" ..
    "\x00"

  local messages = tg_zebra_utils.parse(data)

  lu.assertEquals(#messages, 1)

  lu.assertEquals(messages[1].fpm.len, 52)
  lu.assertEquals(messages[1].fpm.type, 1)
  lu.assertEquals(messages[1].fpm.version, 1)

  lu.assertEquals(messages[1].rtnl.header.length, 48)
  lu.assertEquals(messages[1].rtnl.header.type, tg_rtnl.RtmType.RTM_DELROUTE)
  lu.assertEquals(messages[1].rtnl.header.flags, 1025)
  lu.assertEquals(messages[1].rtnl.header.seq, 0)
  lu.assertEquals(messages[1].rtnl.header.pid, 0)

  lu.assertEquals(messages[1].rtnl.family, 10)
  lu.assertEquals(messages[1].rtnl.dst_len, 0)
  lu.assertEquals(messages[1].rtnl.src_len, 0)
  lu.assertEquals(messages[1].rtnl.tos, 0)
  lu.assertEquals(messages[1].rtnl.table, 0)
  lu.assertEquals(messages[1].rtnl.protocol, 0)
  lu.assertEquals(messages[1].rtnl.scope, 0)
  lu.assertEquals(messages[1].rtnl.type, 0)
  lu.assertEquals(messages[1].rtnl.flags, 0)

  lu.assertEquals(tablex.size(messages[1].rtnl.attrs), 1)
  lu.assertEquals(messages[1].rtnl.attrs.RTA_DST, "::")
end

--- Test RTM_NEWROUTE parsing.
function TestMain:testNewRoute()
  local data =
    "\1\1\0\88\84\0\0\0\24\0\1\4\0\0\0\0\0\0\0\0\10\0\0\0\254\2\0\1\0\0\0" ..
    "\0\20\0\1\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\8\0\6\0\0\4\0\0\20\0\5\0" ..
    "\254\128\0\0\0\0\0\0\0\0\0\0\0\0\0\1\8\0\4\0\17\0\0\0"

  local messages = tg_zebra_utils.parse(data)

  lu.assertEquals(#messages, 1)

  lu.assertEquals(messages[1].fpm.len, 88)
  lu.assertEquals(messages[1].fpm.type, 1)
  lu.assertEquals(messages[1].fpm.version, 1)

  lu.assertEquals(messages[1].rtnl.header.length, 84)
  lu.assertEquals(messages[1].rtnl.header.type, tg_rtnl.RtmType.RTM_NEWROUTE)
  lu.assertEquals(messages[1].rtnl.header.flags, 1025)
  lu.assertEquals(messages[1].rtnl.header.seq, 0)
  lu.assertEquals(messages[1].rtnl.header.pid, 0)

  lu.assertEquals(messages[1].rtnl.family, 10)
  lu.assertEquals(messages[1].rtnl.dst_len, 0)
  lu.assertEquals(messages[1].rtnl.src_len, 0)
  lu.assertEquals(messages[1].rtnl.tos, 0)
  lu.assertEquals(messages[1].rtnl.table, 254)
  lu.assertEquals(messages[1].rtnl.protocol, 2)
  lu.assertEquals(messages[1].rtnl.scope, 0)
  lu.assertEquals(messages[1].rtnl.type, 1)
  lu.assertEquals(messages[1].rtnl.flags, 0)

  lu.assertEquals(tablex.size(messages[1].rtnl.attrs), 4)
  lu.assertEquals(messages[1].rtnl.attrs.RTA_DST, "::")
  lu.assertEquals(messages[1].rtnl.attrs.RTA_PRIORITY, 1024)
  lu.assertEquals(messages[1].rtnl.attrs.RTA_GATEWAY, "fe80::1")
  lu.assertEquals(messages[1].rtnl.attrs.RTA_OIF, 17)
end

--- Test packed RTM_NEWROUTE parsing (7 back-to-back messages).
function TestMain:testNewRoute7()
  local data =
    "\1\1\0\64\60\0\0\0\24\0\1\4\0\0\0\0\0\0\0\0\2\24\0\0\254\2\0\1\0\0\0" ..
    "\0\8\0\1\0\192\168\4\0\8\0\6\0\0\0\0\0\8\0\5\0\192\168\5\2\8\0\4\0\16" ..
    "\0\0\0\1\1\0\56\52\0\0\0\24\0\1\4\0\0\0\0\0\0\0\0\2\32\0\0\254\2\0\1" ..
    "\0\0\0\0\8\0\1\0\192\168\5\2\8\0\6\0\0\0\0\0\8\0\4\0\16\0\0\0\1\1\0" ..
    "\88\84\0\0\0\24\0\1\4\0\0\0\0\0\0\0\0\10\0\0\0\254\2\0\1\0\0\0\0\20" ..
    "\0\1\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\8\0\6\0\0\4\0\0\20\0\5\0\254" ..
    "\128\0\0\0\0\0\0\252\25\80\255\254\1\2\218\8\0\4\0\35\0\0\0\1\1\0\68" ..
    "\64\0\0\0\24\0\1\4\0\0\0\0\0\0\0\0\10\64\0\0\254\2\0\1\0\0\0\0\20\0" ..
    "\1\0\38\32\1\13\192\137\51\128\0\0\0\0\0\0\0\0\8\0\6\0\0\0\0\0\8\0\4" ..
    "\0\35\0\0\0\1\1\0\88\84\0\0\0\24\0\1\4\0\0\0\0\0\0\0\0\10\61\0\0\254" ..
    "\11\0\1\0\0\0\0\20\0\1\0\38\32\1\13\192\137\51\152\0\0\0\0\0\0\0\0\8" ..
    "\0\6\0\0\0\0\0\20\0\5\0\38\32\1\13\192\137\51\128\0\0\0\0\0\0\0\1\8\0" ..
    "\4\0\35\0\0\0\1\1\0\68\64\0\0\0\24\0\1\4\0\0\0\0\0\0\0\0\10\64\0\0" ..
    "\254\2\0\1\0\0\0\0\20\0\1\0\254\128\0\0\0\0\0\0\0\0\0\0\0\0\0\0\8\0" ..
    "\6\0\0\0\0\0\8\0\4\0\17\0\0\0\1\1\0\68\64\0\0\0\24\0\1\4\0\0\0\0\0\0" ..
    "\0\0\10\8\0\0\254\2\0\1\0\0\0\0\20\0\1\0\255\0\0\0\0\0\0\0\0\0\0\0\0" ..
    "\0\0\0\8\0\6\0\0\1\0\0\8\0\4\0\25\0\0\0"

  local messages = tg_zebra_utils.parse(data)

  lu.assertEquals(#messages, 7)

  lu.assertEquals(messages[1].fpm.len, 64)
  lu.assertEquals(messages[2].fpm.len, 56)
  lu.assertEquals(messages[3].fpm.len, 88)
  lu.assertEquals(messages[4].fpm.len, 68)
  lu.assertEquals(messages[5].fpm.len, 88)
  lu.assertEquals(messages[6].fpm.len, 68)
  lu.assertEquals(messages[7].fpm.len, 68)

  lu.assertEquals(tablex.size(messages[1].rtnl.attrs), 4)
  lu.assertEquals(tablex.size(messages[2].rtnl.attrs), 3)
  lu.assertEquals(tablex.size(messages[3].rtnl.attrs), 4)
  lu.assertEquals(tablex.size(messages[4].rtnl.attrs), 3)
  lu.assertEquals(tablex.size(messages[5].rtnl.attrs), 4)
  lu.assertEquals(tablex.size(messages[6].rtnl.attrs), 3)
  lu.assertEquals(tablex.size(messages[7].rtnl.attrs), 3)
end

os.exit(lu.LuaUnit.run("-v"))
