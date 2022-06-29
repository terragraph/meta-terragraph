#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local config_read_env = dofile("/usr/sbin/config_read_env")

local lu = require "luaunit"
local tg_utils = require "tg.utils"
require("pl.stringx").import()

TestMain = {}

function TestMain:setUp()
  self.tmpFile = os.tmpname()
end

function TestMain:tearDown()
  os.remove(self.tmpFile)
end

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertIsFunction(config_read_env.exportConfig)
end

--- Test execution with good config.
function TestMain:testGoodConfig()
  local config = {
    envParams = {
      FW_IF2IF = "0",
      DPDK_ENABLED = "1",
      E2E_ENABLED = "1"
    },
    popParams = {
      GW_ADDR = "2001::1",
      POP_ADDR = "2001::2",
      VPP_ADDR = "2001::3",
      POP_IFACE = "TenGigabitEthernet0",
      POP_BGP_ROUTING = "1",
      POP_STATIC_ROUTING = "0"
    },
    timingParams = {
      PPS_TIMESTAMP_SOURCE = "GPS"
    }
  }
  tg_utils.writeJsonFile(self.tmpFile, config)

  local output = config_read_env.exportConfig(self.tmpFile)
  lu.assertIsString(output)
  lu.assertStrContains(output, 'CONFIG_LAST_MOD="%d+"', true)

  -- Need to strip comment lines and CONFIG_LAST_MOD
  local lines = output:splitlines(true)
  local destLines = {}
  lu.assertTrue(#lines > 0)
  for _, line in ipairs(lines) do
    if not line:startswith("#") and not line:startswith("CONFIG_LAST_MOD=") then
      destLines[#destLines+1] = line
    end
  end
  local processedOutput = (""):join(destLines)
  lu.assertEquals(processedOutput, [[
DPDK_ENABLED="1"
E2E_ENABLED="1"
FW_IF2IF="0"
GW_ADDR="2001::1"
POP_ADDR="2001::2"
POP_BGP_ROUTING="1"
POP_IFACE="TenGigabitEthernet0"
POP_STATIC_ROUTING="0"
PPS_TIMESTAMP_SOURCE="GPS"
VPP_ADDR="2001::3"
]])
end

--- Test execution with malformed config.
function TestMain:testBadConfig()
  -- File doesn't exist, should return nil
  lu.assertNil(config_read_env.exportConfig(self.tmpFile))

  -- Empty config, should return some kind of string
  local config = {popParams = {}}
  tg_utils.writeJsonFile(self.tmpFile, config)
  local output = config_read_env.exportConfig(self.tmpFile)
  lu.assertIsString(output)
end

--- Test config that requires string escaping.
function TestMain:testStringEscaping()
  -- Quotes should be escaped
  local config = {envParams = {TEST = '"string"with"quotes""'}}
  tg_utils.writeJsonFile(self.tmpFile, config)
  local output = config_read_env.exportConfig(self.tmpFile)
  lu.assertIsString(output)
  lu.assertStrContains(output, 'TEST="\\"string\\"with\\"quotes\\"\\""')
end

os.exit(lu.LuaUnit.run("-v"))
