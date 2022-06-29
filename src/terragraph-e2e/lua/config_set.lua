#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Set values in the local node configuration file.
-- @script config_set

local tg_utils = require "tg.utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local tablex = require "pl.tablex"
require("pl.stringx").import()

local C = {}

local CONFIG_FILE = "/data/cfg/node_config.json"

--- argparse validator (via "convert" function) for integer values.
local function argparse_validateInt(arg)
  local i = tonumber(arg)
  if i ~= nil and math.ceil(i) == i then
    return i
  else
    return nil, "Expecting integer argument"
  end
end

--- argparse validator (via "convert" function) for boolean values.
local function argparse_validateBool(arg)
  if arg == "true" then
    return true
  elseif arg == "false" then
    return false
  else
    return nil, "Expecting boolean argument ('true' or 'false')"
  end
end

--- Modify the input config table using the given parameters.
function C.modifyConfig(
  config, intVals, floatVals, strVals, boolVals, deleteKeys
)
  local delVals = tablex.map(function(v) return {v} end, deleteKeys)
  for _, l in ipairs({intVals, floatVals, strVals, boolVals, delVals}) do
    for __, kv in ipairs(l) do
      local keyPath = kv[1]:split(".")
      if not tg_utils.set(config, keyPath, kv[2]) then
        logger.error("Failed to set key '%s' to '%s'", kv[1], kv[2])
        return false
      end
    end
  end
  return true
end

function C.main()
  local parser = argparse(
    "config_set", "Set values in the local node configuration file."
  )
  parser:option(
    "-n --node_config_file", "Path to the node configuration file", CONFIG_FILE
  )
  parser:option("-i --int_val", "Set integer key-value pairs")
    :args(2)
    :argname({"<key>", "<value>"})
    :convert({tostring, argparse_validateInt})
    :count("*")
  parser:option("-f --float_val", "Set floating-point key-value pairs")
    :args(2)
    :argname({"<key>", "<value>"})
    :convert({tostring, tonumber})
    :count("*")
  parser:option("-s --str_val", "Set string key-value pairs")
    :args(2)
    :argname({"<key>", "<value>"})
    :convert({tostring, tostring})
    :count("*")
  parser:option("-b --bool_val", "Set boolean key-value pairs")
    :args(2)
    :argname({"<key>", "<value>"})
    :convert({tostring, argparse_validateBool})
    :count("*")
  parser:option("-d --delete_key", "Delete existing keys")
    :argname("<key>")
    :count("*")
  local args = parser:parse()
  if next(args.int_val) == nil and
     next(args.float_val) == nil and
     next(args.str_val) == nil and
     next(args.bool_val) == nil and
     next(args.delete_key) == nil then
    logger.info(parser:get_usage())
    os.exit(1)
  end

  -- Read config, make changes, and write them back
  local config = tg_utils.readJsonFile(args.node_config_file)
  if config == nil then
    os.exit(1)
  end
  local success = C.modifyConfig(
    config,
    args.int_val,
    args.float_val,
    args.str_val,
    args.bool_val,
    args.delete_key
  )
  if not success then
    os.exit(1)
  end
  if not tg_utils.writeJsonFile(args.node_config_file, config) then
    os.exit(1)
  end
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
