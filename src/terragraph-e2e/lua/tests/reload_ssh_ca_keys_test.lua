#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local reload_ssh_ca_keys = dofile("/usr/sbin/reload_ssh_ca_keys")

local lu = require "luaunit"
require("pl.stringx").import()

TestMain = {}

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertIsFunction(reload_ssh_ca_keys.readCAKeysFromConfig)
end

--- Test execution with good config.
function TestMain:testGoodConfig()
  local muchCA = ([[
ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIElugNkCjCdksEV1ctdwV5OoA/5LCKCqdKoiUGfHcCkP much
]]):rstrip()
  local secretCA = ([[
ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFnzYp67ChmPfuSFG84z3yErFAPHHcDGKUg7DgE2Bh7K secret
]]):rstrip()
  local config = {
    sysParams = {
      allowFactoryCA = false,
      sshTrustedUserCAKeys = {much = muchCA, secret = secretCA}
    }
  }
  local caKeys, allowFactoryCA = reload_ssh_ca_keys.readCAKeysFromConfig(config)
  lu.assertIsTable(caKeys)
  lu.assertItemsEquals(caKeys, {muchCA, secretCA})
  lu.assertFalse(allowFactoryCA)
end

--- Test execution with empty config (that we use default CAs).
function TestMain:testEmptyConfig()
  local caKeys, allowFactoryCA = reload_ssh_ca_keys.readCAKeysFromConfig({})
  lu.assertIsTable(caKeys)
  lu.assertItemsEquals(caKeys, {})
  lu.assertTrue(allowFactoryCA)
end

os.exit(lu.LuaUnit.run("-v"))
