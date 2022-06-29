#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local lu = require "luaunit"
local tg_config_utils = require "tg.config_utils"
require("pl.stringx").import()

TestMain = {}

--- Test getMergedBaseConfig() function.
function TestMain:test_getMergedBaseConfig()
  lu.assertIsFunction(tg_config_utils.getMergedBaseConfig)

  local baseConfig = tg_config_utils.getMergedBaseConfig()
  lu.assertIsTable(baseConfig)
  lu.assertIsTable(baseConfig.envParams)
end

--- Test getLatestBaseConfigFile() function.
function TestMain:test_getLatestBaseConfigFile()
  lu.assertIsFunction(tg_config_utils.getLatestBaseConfigFile)

  local d = "/etc/e2e_config/base_versions/"
  local M1   = d .. "RELEASE_M1.json"
  local M11  = d .. "RELEASE_M11.json"
  local M2   = d .. "RELEASE_M2.json"
  local M2_1 = d .. "RELEASE_M2_1.json"
  local M2_2 = d .. "RELEASE_M2_2.json"

  lu.assertNil(tg_config_utils.getLatestBaseConfigFile({}))
  lu.assertEquals(tg_config_utils.getLatestBaseConfigFile({M1}), M1)
  lu.assertEquals(
    tg_config_utils.getLatestBaseConfigFile({M1, M11, M2, M2_1, M2_2}), M11
  )
  lu.assertEquals(
    tg_config_utils.getLatestBaseConfigFile({M2, M2_1, M2_2}), M2_2
  )
end

--- Test getFirmwareBaseFile() function.
function TestMain:test_getFirmwareBaseFile()
  lu.assertIsFunction(tg_config_utils.getFirmwareBaseFile)

  local d = "/etc/e2e_config/base_versions/fw_versions/"
  local V10_6_0 = d .. "10.6.0.json"
  local V10_7_0 = d .. "10.7.0.json"
  local files = {V10_6_0, V10_7_0}

  lu.assertNil(tg_config_utils.getFirmwareBaseFile({}, "whatever"))
  lu.assertNil(tg_config_utils.getFirmwareBaseFile(files, "10.5.0.1"))
  lu.assertEquals(
    tg_config_utils.getFirmwareBaseFile(files, "10.6.0.3"), V10_6_0
  )
  lu.assertEquals(
    tg_config_utils.getFirmwareBaseFile(files, "10.7.0.3"), V10_7_0
  )
  lu.assertEquals(
    tg_config_utils.getFirmwareBaseFile(files, "10.7.0.9"), V10_7_0
  )
  lu.assertNil(tg_config_utils.getFirmwareBaseFile(files, "10.8.0.3"))
end

--- Test getFirmwareMajorMinorVersion() function.
function TestMain:test_getFirmwareMajorMinorVersion()
  lu.assertIsFunction(tg_config_utils.getFirmwareMajorMinorVersion)

  local major, minor = tg_config_utils.getFirmwareMajorMinorVersion("10.6.0.1")
  lu.assertEquals(major, "10.6.0")
  lu.assertEquals(minor, 1)

  major, minor = tg_config_utils.getFirmwareMajorMinorVersion("10.6.0")
  lu.assertEquals(major, "10.6.0")
  lu.assertEquals(minor, 0)

  major, minor = tg_config_utils.getFirmwareMajorMinorVersion("whatever")
  lu.assertEquals(major, "whatever")
  lu.assertEquals(minor, 0)
end

--- Test parseReleaseVersion() function.
function TestMain:test_parseReleaseVersion()
  lu.assertIsFunction(tg_config_utils.parseReleaseVersion)

  local release, major, minor = tg_config_utils.parseReleaseVersion(([[
Facebook Terragraph Release RELEASE_M21 (user@dev12345 Tue Jun 5 16:01:52 PDT 2018)
]]):rstrip())
  lu.assertEquals(release, "RELEASE_M21")
  lu.assertEquals(major, 21)
  lu.assertEquals(minor, 0)

  release, major, minor = tg_config_utils.parseReleaseVersion(([[
Facebook Terragraph Release RELEASE_M20_1 (user@dev12345 Tue Apr 24 09:38:31 PDT 2018)
]]):rstrip())
  lu.assertEquals(release, "RELEASE_M20_1")
  lu.assertEquals(major, 20)
  lu.assertEquals(minor, 1)

  release, major, minor = tg_config_utils.parseReleaseVersion(([[
Facebook Terragraph Release RELEASE_M22_PRE1-83-g5be6d6b-user (user@dev12345 Thu Jun  7 23:10:59 UTC 2018)
]]):rstrip())
  lu.assertEquals(release, "RELEASE_M22")
  lu.assertEquals(major, 22)
  lu.assertEquals(minor, 0)

  release, major, minor = tg_config_utils.parseReleaseVersion(" asdf  ")
  lu.assertEquals(release, "asdf")
  lu.assertEquals(major, 0)
  lu.assertEquals(minor, 0)
end

os.exit(lu.LuaUnit.run("-v"))
