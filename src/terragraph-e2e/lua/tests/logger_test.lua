#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local lu = require "luaunit"
local logger = require "tg.logger"

TestMain = {}

function TestMain:setUp()
  self.wasIoWriteCalled = false

  -- mock io.write for testing
  io.write = function(...) -- luacheck: ignore only 122
    self.wasIoWriteCalled = true
  end
end

function TestMain:testSameLogLevel()
  logger.level = "info"
  logger.info("hello world")
  lu.assertTrue(self.wasIoWriteCalled)
end

function TestMain:testHigherLogLevel()
  logger.level = "info"
  logger.critical("hello world")
  lu.assertTrue(self.wasIoWriteCalled)
end

function TestMain:testPrintLowerLogLevel()
  logger.level = "info"
  logger.debug("hello world")
  lu.assertFalse(self.wasIoWriteCalled)
end

os.exit(lu.LuaUnit.run("-v"))
