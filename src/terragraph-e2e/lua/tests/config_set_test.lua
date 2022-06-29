#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local config_set = dofile("/usr/sbin/config_set")

local lu = require "luaunit"
local tablex = require "pl.tablex"

TestMain = {}

function TestMain:setUp()
  self.config = {
    envParams = {
      E2E_ENABLED = "1"
    },
    radioParamsBase = {
      fwParams = {
        txPower = 31
      }
    },
    openrParams = {
      linkMetricConfig = {
        tokenGenRate = 0.1,
      }
    },
    sysParams = {
      managedConfig = true
    },
    test = {
      key1 = "1"
    }
  }
  self.originalConfig = tablex.deepcopy(self.config)
end

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertIsFunction(config_set.modifyConfig)
end

--- Test execution with good arguments.
function TestMain:testGoodArgs()
  local intVals = {}
  local floatVals = {}
  local strVals = {}
  local boolVals = {}
  local deleteKeys = {}

  -- No changes
  lu.assertTrue(config_set.modifyConfig(
    self.config, intVals, floatVals, strVals, boolVals, deleteKeys
  ))
  lu.assertTrue(tablex.deepcompare(self.config, self.originalConfig))

  -- Change all types to different values
  intVals = {{"radioParamsBase.fwParams.txPower", 20}}
  floatVals = {{"openrParams.linkMetricConfig.tokenGenRate", 1.5}}
  strVals = {{"envParams.E2E_ENABLED", "0"}}
  boolVals = {{"sysParams.managedConfig", false}}
  deleteKeys = {"test.key1"}
  lu.assertTrue(config_set.modifyConfig(
    self.config, intVals, floatVals, strVals, boolVals, deleteKeys
  ))
  lu.assertEquals(self.config.radioParamsBase.fwParams.txPower, 20)
  lu.assertEquals(self.config.openrParams.linkMetricConfig.tokenGenRate, 1.5)
  lu.assertEquals(self.config.envParams.E2E_ENABLED, "0")
  lu.assertFalse(self.config.sysParams.managedConfig)
  lu.assertNil(self.config.test.key1)

  -- Add new keys
  intVals = {{"test.key2", 123}, {"test.key3.blah", 456}}
  lu.assertTrue(config_set.modifyConfig(
    self.config, intVals, floatVals, strVals, boolVals, deleteKeys
  ))
  lu.assertEquals(self.config.test.key2, 123)
  lu.assertEquals(self.config.test.key3.blah, 456)

  -- Delete some real and some non-existent keys (this is fine)
  deleteKeys = {"test.key3", "test.key4"}
  lu.assertTrue(config_set.modifyConfig(
    self.config, intVals, floatVals, strVals, boolVals, deleteKeys
  ))
  lu.assertNil(self.config.test.key3)
  lu.assertNil(self.config.test.key4)
end

--- Test execution with bad arguments.
function TestMain:testBadArgs()
  -- Try setting keys in invalid paths
  local intVals = {}
  local floatVals = {}
  local strVals = {{"envParams.E2E_ENABLED.wow.so.wrong", "blah"}}
  local boolVals = {}
  local deleteKeys = {}
  lu.assertFalse(config_set.modifyConfig(
    self.config, intVals, floatVals, strVals, boolVals, deleteKeys
  ))
end

os.exit(lu.LuaUnit.run("-v"))
