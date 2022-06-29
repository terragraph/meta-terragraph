#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Compare current node configuration with the base configuration.
-- @script diff_node_config

local config_utils = require "tg.config_utils"
local tg_utils = require "tg.utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local path = require "pl.path"

local C = {}

function C.main()
  local parser = argparse(
    "diff_node_config",
    "Compare current node configuration with the base configuration."
  )
  parser:option(
    "-n --node_config_file",
    "Path to the node configuration file",
    "/data/cfg/node_config.json"
  )
  local args = parser:parse()

  if not path.exists(args.node_config_file) then
    logger.error("!!-> %s does not exist", args.node_config_file)
    os.exit(1)
  end

  local mergedConfigFile = os.tmpname()
  local mergedConfig = config_utils.getMergedBaseConfig()
  if not tg_utils.writeJsonFile(mergedConfigFile, mergedConfig) then
    os.exit(1)
  end

  local command = "diff " .. mergedConfigFile .. " " .. args.node_config_file
  local output, ret, code = tg_utils.exec(command, true)
  os.remove(mergedConfigFile)
  if output == nil then
    os.exit(1)
  elseif output == "" then
    logger.info("==== No differences found ====")
  else
    logger.info(output)
  end
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
