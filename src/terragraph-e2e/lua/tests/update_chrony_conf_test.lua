#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local update_chrony_conf = dofile("/usr/sbin/update_chrony_conf")

local lu = require "luaunit"
local tablex = require "pl.tablex"

TestMain = {}

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertIsFunction(update_chrony_conf.buildChronyConfig)
  lu.assertIsFunction(update_chrony_conf.processNodeConfig)
end

--- Test execution with good config.
function TestMain:testGoodConfig()
  local config = {
    envParams = {
      GPSD_ENABLED = "1",
      GPSD_NMEA_TIME_OFFSET = "80",
      GPSD_PPS_DEVICE = "/dev/pps1"
    },
    sysParams = {
      ntpServers = {
        ntp1 = "1.pool.ntp.org",
        ntp2 = "2.pool.ntp.org"
      }
    }
  }

  local ntpServers, ppsDevice, nmeaTimeOffset =
    update_chrony_conf.processNodeConfig(config)
  lu.assertIsTable(ntpServers)
  lu.assertItemsEquals(ntpServers, tablex.values(config.sysParams.ntpServers))
  lu.assertEquals(ppsDevice, "pps1")
  lu.assertEquals(nmeaTimeOffset, "80")

  -- Check that templated fields get filled
  local output =
    update_chrony_conf.buildChronyConfig(ntpServers, ppsDevice, nmeaTimeOffset)
  lu.assertIsString(output)
  lu.assertStrContains(output, "server 1.pool.ntp.org iburst")
  lu.assertStrContains(output, "server 2.pool.ntp.org iburst")
  lu.assertStrContains(
    output, "refclock SHM 0 refid NMEA precision 1e-1 offset 80")
  lu.assertStrContains(
    output, "refclock SOCK /run/chrony.pps1.sock refid PPS")
end

--- Test execution with empty config (that we get reasonable defaults).
function TestMain:testDefaultConfig()
  local ntpServers, ppsDevice, nmeaTimeOffset =
    update_chrony_conf.processNodeConfig({})
  lu.assertIsTable(ntpServers)
  lu.assertTrue(#ntpServers >= 1)
  lu.assertNil(ppsDevice)
  lu.assertEquals(nmeaTimeOffset, 0)

  -- Check that templated fields get filled
  local output =
    update_chrony_conf.buildChronyConfig(ntpServers, ppsDevice, nmeaTimeOffset)
  lu.assertIsString(output)
  lu.assertStrContains(output, "server")
end

os.exit(lu.LuaUnit.run("-v"))
