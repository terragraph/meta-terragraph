#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Generate the default node configuration.
-- @script config_get_base

local tg_utils = require "tg.utils"
local tg_config_utils = require "tg.config_utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local dir = require "pl.dir"
local path = require "pl.path"

local C = {}

function C.main()
  local parser = argparse(
    "config_get_base", "Generate the default node configuration."
  )
  parser:argument("output_file_path", "Path to the output configuration file")
  local args = parser:parse()

  -- Write the default base config to the given output file path
  local baseConfig = tg_config_utils.getMergedBaseConfig()
  logger.info("Writing config to output file: %s", args.output_file_path)
  dir.makepath(path.dirname(args.output_file_path))
  tg_utils.writeJsonFile(args.output_file_path, baseConfig)
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
