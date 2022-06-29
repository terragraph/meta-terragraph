#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Update the node configuration with new keys from this software version.
-- @script config_migrate

local tg_utils = require "tg.utils"
local tg_config_utils = require "tg.config_utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local path = require "pl.path"

local C = {}

local CONFIG_FILE = "/data/cfg/node_config.json"
local LAST_VERSION_FILE = "/data/last_version"
local VERSION_FILE = "/etc/tgversion"

-- Read in the last software version the node was running, and compare it to
-- the current release. This should only run once after a software upgrade.
--
-- If any keys were added in the base configuration for the new release, add
-- them to the node configuration file without overwriting any existing values.
--
-- Returns false only if an error occurs, and true otherwise (even if no changes
-- were made).
function C.migrate(nodeConfigFile, prevVerFile, curVerFile)
  -- Read previous/current version files
  local prevVersion = ""
  local curVersion = ""
  if path.isfile(prevVerFile) then
    prevVersion = tg_utils.readFile(prevVerFile)
  end
  if path.isfile(curVerFile) then
    curVersion = tg_utils.readFile(curVerFile)
  end
  if prevVersion == nil or curVersion == nil then
    return false
  end
  if curVersion:len() == 0 then
    logger.error("Version file empty, exiting.")
    return false
  end

  -- Update last version file
  if curVersion ~= prevVersion then
    logger.info("Writing version string to %s", prevVerFile)
    tg_utils.writeFile(prevVerFile, curVersion)
  end

  if prevVersion:len() == 0 then
    logger.info("Last version file empty, exiting.")
    return true
  end

  -- Parse major/minor releases
  local prevVerStr, prevVerMajor, prevVerMinor =
    tg_config_utils.parseReleaseVersion(prevVersion)
  local curVerStr, curVerMajor, curVerMinor =
    tg_config_utils.parseReleaseVersion(curVersion)
  logger.info("Last version: %s", prevVerStr)
  logger.info("Current version: %s", curVerStr)
  if curVerMajor < prevVerMajor or
     (curVerMajor == prevVerMajor and curVerMinor <= prevVerMinor) then
    logger.info("Current config version is not newer than previous, exiting.")
    return true
  end

  -- Diff base configs
  local prevConfig = tg_config_utils.getMergedBaseConfig(prevVerStr .. ".json")
  local curConfig = tg_config_utils.getMergedBaseConfig(curVerStr .. ".json")
  local additions = tg_utils.tableDifference(prevConfig, curConfig)
  if next(additions) == nil then
    logger.info("No config additions needed, exiting.")
    return true
  end

  -- Load the current config and merge the additional keys into it
  local config = tg_utils.readJsonFile(nodeConfigFile)
  if config == nil then
    return false
  end
  if tg_utils.tableMergeAppend(config, additions) then
    logger.info("Writing updated configuration file: %s", nodeConfigFile)
    tg_utils.writeJsonFile(nodeConfigFile, config)
  else
    logger.info("No changes made to configuration file")
  end
  return true
end

function C.main()
  local parser = argparse(
    "config_migrate",
    "Update the node configuration with new keys from this software version."
  )
  parser:option(
    "-n --node_config_file", "Path to the node configuration file", CONFIG_FILE
  )
  parser:option(
    "--prev_ver_file", "Path to the previous version file", LAST_VERSION_FILE
  )
  parser:option(
    "--cur_ver_file", "Path to the current version file", VERSION_FILE
  )
  local args = parser:parse()

  if not C.migrate(
    args.node_config_file, args.prev_ver_file, args.cur_ver_file
  ) then
    os.exit(1)
  end
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
