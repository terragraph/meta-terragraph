#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Writes keys for trusted certificate authorities to SSH configuration.
-- @script reload_ssh_ca_keys

local tg_utils = require "tg.utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local dir = require "pl.dir"
local path = require "pl.path"
local tablex = require "pl.tablex"
require("pl.stringx").import()

local C = {}

local CONFIG_FILE = "/data/cfg/node_config.json"
local DEFAULT_CA_FILE = "/etc/ssh/tg-CA.bak"
local TG_CA_FILE = "/var/run/ssh/ca_keys"

function C.readCAKeysFromConfig(config)
  local caKeys = {}
  local allowFactoryCA

  -- Read keys from config
  local configFactoryCA = tg_utils.get(config, "sysParams", "allowFactoryCA")
  if configFactoryCA ~= nil and type(configFactoryCA) == "boolean" then
    allowFactoryCA = configFactoryCA
  else
    logger.warning("Error reading 'allowFactoryCA', defaulting to true")
    allowFactoryCA = true
  end
  local configCaKeys = tg_utils.get(config, "sysParams", "sshTrustedUserCAKeys")
  if configCaKeys ~= nil and type(configCaKeys) == "table" then
    caKeys = tablex.values(configCaKeys)
  else
    logger.warning(
      "Error reading 'sshTrustedUserCAKeys', falling back to default CA"
    )
    allowFactoryCA = true
  end

  table.sort(caKeys)
  return caKeys, allowFactoryCA
end

function C.main()
  local parser = argparse(
    "reload_ssh_ca_keys",
    "Writes keys for trusted certificate authorities to SSH configuration."
  )
  parser:option(
    "-i --node_config_file", "Path to the node configuration file", CONFIG_FILE
  )
  parser:option(
    "-d --default_ca_file", "Path to the default CA file", DEFAULT_CA_FILE
  )
  parser:option("-o --output_ca_file", "Output CA file path", TG_CA_FILE)
  parser:flag("-v --verbose", "Enable logging", false)
  local args = parser:parse()

  -- Set up logging
  if not args.verbose then
    logger.level = "critical"
  end
  logger.startSyslog()

  -- Read config file
  local caKeys = {}
  local allowFactoryCA
  logger.info(
    "Reading trusted CA keys from config file %s", args.node_config_file
  )
  local config = tg_utils.readJsonFile(args.node_config_file)
  if config == nil then
    logger.warning("Error reading config file, falling back to default CA")
    allowFactoryCA = true
  else
    caKeys, allowFactoryCA = C.readCAKeysFromConfig(config)
  end

  -- Read default CA (if needed)
  if allowFactoryCA then
    logger.info("Reading default CA keys from %s", args.default_ca_file)
    local defaultCA = tg_utils.readFile(args.default_ca_file)
    if defaultCA ~= nil then
      caKeys[#caKeys+1] = defaultCA
    end
  end

  -- Write to output file
  logger.info("Writing trusted CA keys to %s", args.output_ca_file)
  dir.makepath(path.dirname(args.output_ca_file))
  tg_utils.writeFile(args.output_ca_file, ("\n"):join(caKeys))

  logger.stopSyslog()
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
