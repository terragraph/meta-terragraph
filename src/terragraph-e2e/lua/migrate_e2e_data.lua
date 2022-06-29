#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Migrate E2E data (ex. configuration files) between software versions.
-- @script migrate_e2e_data

local tg_utils = require "tg.utils"
local tg_config_utils = require "tg.config_utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local cjson_safe = require "cjson.safe"
local dir = require "pl.dir"
local path = require "pl.path"
local tablex = require "pl.tablex"
require("pl.stringx").import()

local VERSION_FILE = "/etc/tgversion"
local NETWORK_OVERRIDES_FILE = "/data/cfg/network_config_overrides.json"
local NODE_OVERRIDES_FILE = "/data/cfg/node_config_overrides.json"
local CONTROLLER_CONFIG_FILE = "/data/cfg/controller_config.json"

-- Convert a Lua table to JSON (for comparison purposes only).
local function toJson(t) return cjson_safe.encode(t) end

-- Migrator class
local Migrator = {}
Migrator.__index = Migrator

--- Constructor for Migrator.
function Migrator.new(args)
  local self = setmetatable({}, Migrator)
  self.from_ver_major = args.from_ver_major
  self.from_ver_minor = args.from_ver_minor
  self.from_ver_file = args.from_ver_file
  self.to_ver_major = args.to_ver_major
  self.to_ver_minor = args.to_ver_minor
  self.to_ver_file = args.to_ver_file
  self.network_overrides_file = args.network_overrides_file
  self.node_overrides_file = args.node_overrides_file
  self.controller_config_file = args.controller_config_file
  return self
end

-- -----------------------------------------------------------------------------
-- NOTE!
-- Migration functions MUST be named "migrate_Mxx" or "migrate_Mxx_yy" where
-- "xx" is the target software major version number and "yy" is the minor
-- version number (default minor = 0). These functions take one table argument
-- holding parsed contents for each data file:
-- {
--   network_overrides = {...},
--   node_overrides = {...},
--   controller_config = {...},
-- }
-- -----------------------------------------------------------------------------

--- Migrate to release M77.
function Migrator:migrate_M77(data)
  -- eapolParams moved from "radioParamsBase.wsecParams.eapolParams" to
  -- top-level "eapolParams". Copy the old config to the new location.
  local migrateFn = function(obj, str)
    local oldEapolParams =
      tg_utils.get(obj, "radioParamsBase", "wsecParams", "eapolParams")
    if oldEapolParams == nil then
      logger.info("%s: empty radioParamsBase.wsecParams.eapolParams", str)
      return
    end
    if obj.eapolParams ~= nil then
      logger.info("%s: already has eapolParams, not overwriting", str)
      return
    end
    logger.info(
      "%s: copying radioParamsBase.wsecParams.eapolParams to eapolParams", str
    )
    obj.eapolParams = oldEapolParams
  end

  -- Migrate network + node overrides
  migrateFn(data.network_overrides, "network_overrides")
  for nodeName, obj in pairs(data.node_overrides) do
    migrateFn(obj, "node_overrides[" .. nodeName .. "]")
  end
end

--- Read the major and minor version from the given tgversion file.
--
-- Returns the major and minor versions as two integers, or nil upon error.
function Migrator:readVersion(f)
  if f == nil or f == "" then
    logger.error("Missing version information.")
    return nil, nil
  end
  if not path.isfile(f) then
    logger.error("Version file does not exist: %s", f)
    return nil, nil
  end
  local verStr = tg_utils.readFile(f)
  if verStr == nil then
    return nil, nil
  end
  if verStr == "" then
    logger.error("Version file is empty: %s", f)
    return nil, nil
  end
  local release, major, minor = tg_config_utils.parseReleaseVersion(verStr)
  return major, minor
end

--- Apply all migrations in order between the given old/new versions.
--
-- Returns the list of version migrations applied as [major, minor] tuples.
function Migrator:applyMigrations(
  fromVerMajor, fromVerMinor, toVerMajor, toVerMinor, data
)
  local MIGRATE_FN_PREFIX = "migrate_M"
  local migrations = {}
  for k, v in tablex.sort(getmetatable(self)) do
    if k:startswith(MIGRATE_FN_PREFIX) then
      local tokens = k:sub(#MIGRATE_FN_PREFIX + 1):split("_")
      local major = tonumber(tokens[1])
      local minor = tokens[2] and tonumber(tokens[2]) or 0
      if (major > fromVerMajor or
          (major == fromVerMajor and minor > fromVerMinor)) and
         (major < toVerMajor or
          (major == toVerMajor and minor <= toVerMinor)) then
        logger.info("\n==> Applying migrations for M%d_%d...", major, minor)
        self[k](self, data)
        migrations[#migrations+1] = {major, minor}
      end
    end
  end
  return migrations
end

--- Back up the given file (copy into the same directory with a given suffix),
-- then overwrite it with the given table.
function Migrator:writeChanges(t, filePath, backupSuffix)
  if backupSuffix and backupSuffix ~= "" then
    local backupPath = filePath .. backupSuffix
    logger.info("Creating backup file: %s", backupPath)
    dir.copyfile(filePath, backupPath)
  end
  logger.info("Overwriting file: %s", filePath)
  tg_utils.writeJsonFile(filePath, t)
end

--- Read all files, run migrations in order, and write new files as needed.
--
-- Returns true upon success.
function Migrator:run()
  -- Get from/to versions
  local fromVerMajor, fromVerMinor, toVerMajor, toVerMinor
  if self.from_ver_major then
    fromVerMajor, fromVerMinor = self.from_ver_major, self.from_ver_minor or 0
  else
    fromVerMajor, fromVerMinor = self:readVersion(self.from_ver_file)
  end
  if self.to_ver_major then
    toVerMajor, toVerMinor = self.to_ver_major, self.to_ver_minor or 0
  else
    toVerMajor, toVerMinor = self:readVersion(self.to_ver_file)
  end
  if fromVerMajor == nil or toVerMajor == nil then
    return false
  end
  if (
    fromVerMajor < 0 or fromVerMinor < 0 or toVerMajor < 0 or toVerMinor < 0
  ) then
    logger.error(
      "Invalid version number(s) provided: from (%d,%d) to (%d,%d)",
      fromVerMajor,
      fromVerMinor,
      toVerMajor,
      toVerMinor
    )
    return false
  end
  logger.info(
    "Migrating data from version M%d_%d to M%d_%d...",
    fromVerMajor,
    fromVerMinor,
    toVerMajor,
    toVerMinor
  )

  -- Read files
  local data = {
    network_overrides =
      tg_utils.readJsonFile(self.network_overrides_file) or {},
    node_overrides = tg_utils.readJsonFile(self.node_overrides_file) or {},
    controller_config =
      tg_utils.readJsonFile(self.controller_config_file) or {},
  }
  local origNetworkOverrides = toJson(data.network_overrides)
  local origNodeOverrides = toJson(data.node_overrides)
  local origControllerConfig = toJson(data.controller_config)

  -- Apply all migrations in order
  local migrations = self:applyMigrations(
    fromVerMajor, fromVerMinor, toVerMajor, toVerMinor, data
  )
  if #migrations == 0 then
    logger.info("\nNo migrations to run. Exiting.")
    return true
  end
  logger.info("\n%d migration(s) finished.\n", #migrations)

  -- If no changes were actually made, exit here
  local networkOverridesChanged =
    (origNetworkOverrides ~= toJson(data.network_overrides))
  local nodeOverridesChanged =
    (origNodeOverrides ~= toJson(data.node_overrides))
  local controllerConfigChanged =
    (origControllerConfig ~= toJson(data.controller_config))
  if (
    not networkOverridesChanged and
    not nodeOverridesChanged and
    not controllerConfigChanged
  ) then
    logger.info("No changes are needed. Exiting.")
    return true
  end

  -- Back up old files (copy into the same directory with a timestamp suffix),
  -- then write overwrite the old files with migrated config
  local backupSuffix = ".bak-" .. os.date("%Y%m%d-%H_%M_%S")
  if networkOverridesChanged then
    self:writeChanges(
      data.network_overrides, self.network_overrides_file, backupSuffix
    )
  end
  if nodeOverridesChanged then
    self:writeChanges(
      data.node_overrides, self.node_overrides_file, backupSuffix
    )
  end
  if controllerConfigChanged then
    self:writeChanges(
      data.controller_config, self.controller_config_file, backupSuffix
    )
  end
  return true
end

local function main()
  local parser = argparse(
    "migrate_e2e_data",
    "Migrate E2E data (ex. configuration files) between software versions."
  )
  parser:option(
    "--from_ver_file", "Path to the previous version file", ""
  )
  parser:option(
    "--to_ver_file", "Path to the next version file", VERSION_FILE
  )
  parser:option(
    "--from_ver_major",
    "The previous software major version number, overriding --from_ver_file"
  ):convert(tonumber)
  parser:option(
    "--from_ver_minor",
    "The previous software minor version number, overriding --from_ver_file",
    "0"
  ):convert(tonumber)
  parser:option(
    "--to_ver_major",
    "The next software major version number, overriding --to_ver_file"
  ):convert(tonumber)
  parser:option(
    "--to_ver_minor",
    "The next software minor version number, overriding --to_ver_file",
    "0"
  ):convert(tonumber)
  parser:option(
    "--network_overrides_file",
    "Path to the network config overrides",
    NETWORK_OVERRIDES_FILE
  )
  parser:option(
    "--node_overrides_file",
    "Path to the node config overrides",
    NODE_OVERRIDES_FILE
  )
  parser:option(
    "--controller_config_file",
    "Path to the controller config",
    CONTROLLER_CONFIG_FILE
  )
  local args = parser:parse()

  local migrator = Migrator.new(args)
  if not migrator:run() then
    os.exit(1)
  end
end

if tg_utils.isMain() then
  main()
else
  return Migrator
end
