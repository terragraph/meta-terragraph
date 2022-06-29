#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local Reader = require "tg.Reader"
local lu = require "luaunit"

TestMain = {}

--- Test basic read operations.
function TestMain:testBasicReads()
  local data = {0xaa, -22, 0x3, 0x44, 0x9, 0x8, 0x7, 0x6}
  local reader = Reader.new(data)
  lu.assertEquals(reader:readByte(), 0xaa)
  lu.assertEquals(reader:readByte(), -22)
  lu.assertEquals(reader:readWord(), 0x344)
  lu.assertEquals(reader:readInt(), 0x9080706)
  lu.assertNil(reader:readByte())
end

--- Test signed vs. unsigned reads.
function TestMain:testSignedness()
  local data = {0xcc, 0xdd, 0xee, 0xff}
  local reader = Reader.new(data)
  lu.assertEquals(reader:readUWord(), 52445)
  reader:reset()
  lu.assertEquals(reader:readWord(), -13091)
  reader:reset()
  lu.assertEquals(reader:readUInt(), 3437096703)
  reader:reset()
  lu.assertEquals(reader:readInt(), -857870593)
end

--- Test big-endian vs. little-endian reads.
function TestMain:testEndianness()
  local data = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}

  -- Big-endian
  local reader = Reader.new(data, false)
  lu.assertEquals(reader:readUWord(), 0xaabb)
  lu.assertEquals(reader:readUInt(), 0xccddeeff)

  -- Little-endian
  reader = Reader.new(data, true)
  lu.assertEquals(reader:readUWord(), 0xbbaa)
  lu.assertEquals(reader:readUInt(), 0xffeeddcc)
end

--- Test seeking operations.
function TestMain:testSeeking()
  local data = {0x11, 0x22, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}
  local reader = Reader.new(data)

  lu.assertEquals(reader:position(), 1)
  lu.assertEquals(reader:available(), 8)

  reader:skip(2)
  lu.assertEquals(reader:position(), 3)
  lu.assertEquals(reader:available(), 6)
  lu.assertEquals(reader:readByte(), 0xaa)

  reader:reset(7)
  lu.assertEquals(reader:position(), 7)
  lu.assertEquals(reader:available(), 2)
  lu.assertEquals(reader:readUWord(), 0xeeff)

  reader:reset()
  lu.assertEquals(reader:position(), 1)
  lu.assertEquals(reader:available(), 8)
  lu.assertEquals(reader:readUInt(), 0x1122aabb)
end

os.exit(lu.LuaUnit.run("-v"))
