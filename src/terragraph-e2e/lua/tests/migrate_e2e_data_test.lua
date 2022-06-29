#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local migrate_e2e_data = assert(loadfile("/usr/sbin/migrate_e2e_data"))

local lu = require "luaunit"

TestMain = {}

function TestMain:setUp()
  self.Migrator = migrate_e2e_data()
  self.migrator = self.Migrator.new({})
end

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertIsFunction(self.migrator.applyMigrations)
end

function TestMain:_emptyData()
  return {
    network_overrides = {},
    node_overrides = {},
    controller_config = {},
  }
end

--- Test mock migrations.
function TestMain:testMockMigrations()
  local emptyFn = function(_self, data) end
  self.Migrator.migrate_M10000_3 = emptyFn
  self.Migrator.migrate_M10001 = emptyFn
  self.Migrator.migrate_M10002_1 = emptyFn

  local migrations, data
  data = self:_emptyData()
  migrations = self.migrator:applyMigrations(9999, 0, 10000, 0, data)
  lu.assertEquals(#migrations, 0)
  migrations = self.migrator:applyMigrations(9999, 0, 10002, 0, data)
  lu.assertEquals(#migrations, 2)
  lu.assertEquals(migrations[1][1], 10000)
  lu.assertEquals(migrations[1][2], 3)
  lu.assertEquals(migrations[2][1], 10001)
  lu.assertEquals(migrations[2][2], 0)
  migrations = self.migrator:applyMigrations(10000, 1, 10002, 0, data)
  lu.assertEquals(#migrations, 2)
  lu.assertEquals(migrations[1][1], 10000)
  lu.assertEquals(migrations[1][2], 3)
  lu.assertEquals(migrations[2][1], 10001)
  lu.assertEquals(migrations[2][2], 0)
  migrations = self.migrator:applyMigrations(10000, 2, 10000, 3, data)
  lu.assertEquals(#migrations, 1)
  lu.assertEquals(migrations[1][1], 10000)
  lu.assertEquals(migrations[1][2], 3)
  migrations = self.migrator:applyMigrations(10000, 3, 10000, 4, data)
  lu.assertEquals(#migrations, 0)
  migrations = self.migrator:applyMigrations(10000, 4, 10001, 0, data)
  lu.assertEquals(#migrations, 1)
  lu.assertEquals(migrations[1][1], 10001)
  lu.assertEquals(migrations[1][2], 0)
  migrations = self.migrator:applyMigrations(10000, 0, 10002, 1, data)
  lu.assertEquals(#migrations, 3)
  lu.assertEquals(migrations[1][1], 10000)
  lu.assertEquals(migrations[1][2], 3)
  lu.assertEquals(migrations[2][1], 10001)
  lu.assertEquals(migrations[2][2], 0)
  lu.assertEquals(migrations[3][1], 10002)
  lu.assertEquals(migrations[3][2], 1)
end

--- Test real migrations.
function TestMain:testRealMigrations()
  local migrations, data

  -- No migrations
  data = self:_emptyData()
  migrations = self.migrator:applyMigrations(74, 0, 75, 0, data)
  lu.assertEquals(#migrations, 0)
  migrations = self.migrator:applyMigrations(77, 0, 77, 1, data)
  lu.assertEquals(#migrations, 0)

  -- migrate_M77()
  migrations = self.migrator:applyMigrations(74, 0, 77, 0, self:_emptyData())
  lu.assertEquals(#migrations, 1)
  lu.assertEquals(migrations[1][1], 77)
  lu.assertEquals(migrations[1][2], 0)
  data = self:_emptyData()
  data.network_overrides = {
    radioParamsBase = {
      wsecParams = {
        eapolParams = {
          radius_server_ip = "2001::1"
        }
      }
    }
  }
  migrations = self.migrator:applyMigrations(74, 0, 79, 0, data)
  lu.assertEquals(#migrations, 1)
  lu.assertEquals(migrations[1][1], 77)
  lu.assertEquals(migrations[1][2], 0)
  lu.assertNotNil(data.network_overrides.eapolParams)
  lu.assertEquals(
    data.network_overrides.eapolParams.radius_server_ip, "2001::1"
  )
end

-- Test migration error cases.
function TestMain:testMigrationError()
  local migrations

  -- Invalid from/to version
  migrations = self.migrator:applyMigrations(79, 0, 74, 0, self:_emptyData())
  lu.assertEquals(#migrations, 0)
end

os.exit(lu.LuaUnit.run("-v"))
