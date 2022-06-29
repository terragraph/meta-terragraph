#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Sanitizes the node configuration file and creates an environment file.
-- @script config_read_env

local tg_utils = require "tg.utils"
local tg_config_utils = require "tg.config_utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local path = require "pl.path"
local dir = require "pl.dir"
require("pl.stringx").import()

local C = {}

local CONFIG_DIR = "/data/cfg"
local CONFIG_FILE = CONFIG_DIR .. "/node_config.json"
local CONFIG_FILE_BAK = CONFIG_FILE .. ".bak"
local CONFIG_ENV_FILE = CONFIG_DIR .. "/config"

function C.exportConfig(nodeConfigFile)
  -- NOTE: In a race condition where the config file is modified in between
  -- these two operations, read mtime first to avoid putting a new mtime into an
  -- old env file.
  local lastMod = path.getmtime(nodeConfigFile)
  local config = tg_utils.readJsonFile(nodeConfigFile)
  if config == nil then
    return nil
  end

  -- Read keys from config
  local configList = {
    config.envParams or {}, config.popParams or {}, config.timingParams or {}
  }
  local out = {}
  for _, cfg in ipairs(configList) do
    for k, v in pairs(cfg) do
      if v ~= nil and type(v) ~= "table" then
        out[#out+1] = string.format('%s=%s', k, tostring(v):quote_string())
      end
    end
  end
  table.sort(out)

  -- Append file modification time
  out[#out+1] = string.format('CONFIG_LAST_MOD="%d"', lastMod)

  local s = "##### THIS FILE IS AUTO-GENERATED. DO NOT EDIT. #####\n"
  s = s .. ("\n"):join(out) .. "\n"
  return s
end

function C.copyDefaultConfig(nodeConfigFile)
  local baseConfig = tg_config_utils.getMergedBaseConfig()
  tg_utils.writeJsonFile(nodeConfigFile, baseConfig)
end

function C.sanitize(nodeConfigFile)
  logger.info("Sourcing from %s", nodeConfigFile)

  -- Create config directory if missing
  local nodeConfigDir = path.dirname(nodeConfigFile)
  if not path.isdir(nodeConfigDir) then
    logger.info("Config directory %s missing, creating now...", nodeConfigDir)
    dir.makepath(nodeConfigDir)
  end

  -- Generate latest config file if missing
  if not path.isfile(nodeConfigFile) then
    logger.info("No node config exists, copying default...")
    C.copyDefaultConfig(nodeConfigFile)
  end
end

function C.main()
  local parser = argparse(
    "config_read_env",
    "Sanitizes the node configuration file and creates an environment file.\n"
    .. "This script is invoked by config_get_env.sh as needed."
  )
  parser:option(
    "-i --node_config_file", "Path to the node configuration file", CONFIG_FILE
  )
  parser:option(
    "-b --node_config_bak",
    "Path to the backup node configuration file (created if needed)",
    CONFIG_FILE_BAK
  )
  parser:option(
    "-o --config_env_file", "Output environment file path", CONFIG_ENV_FILE
  )
  parser:flag("-v --verbose", "Enable logging", false)
  local args = parser:parse()

  -- Set up logging
  if not args.verbose then
    logger.level = "critical"
  end
  logger.startSyslog()

  C.sanitize(args.node_config_file)
  local output = C.exportConfig(args.node_config_file)
  if output == nil then
    logger.info(
      "Error processing %s, replacing with default config...",
      args.node_config_file
    )
    if dir.movefile(args.node_config_file, args.node_config_bak) then
      logger.info("Moved %s to %s", args.node_config_file, args.node_config_bak)
    end
    C.copyDefaultConfig(args.node_config_file)
  else
    local ret = tg_utils.writeFile(args.config_env_file, output)
    if ret then
      logger.info("Updated env file: %s", args.config_env_file)
    end
  end

  logger.stopSyslog()
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
