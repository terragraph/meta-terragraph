#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Inject all Open/R KvStore keys found in the node configuration.
-- @script config_set_kvstore

local tg_utils = require "tg.utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local cjson_safe = require "cjson.safe"

local C = {}

local CONFIG_FILE = "/data/cfg/node_config.json"
local OPENR_CLI = "/usr/bin/breeze"

function C.injectKeyValue(k, v)
  logger.info("> set %s = %s", k, v)
  os.execute(string.format("%s kvstore set-key %s %s", OPENR_CLI, k, v))
end

function C.main()
  local parser = argparse(
    "config_set_kvstore",
    "Inject all Open/R KvStore keys found in the node configuration."
  )
  parser:option(
    "-i --node_config_file", "Path to the node configuration file", CONFIG_FILE
  )
  local args = parser:parse()

  -- Read node config and inject keys
  local config = tg_utils.readJsonFile(args.node_config_file)
  if config == nil then
    os.exit(1)
  end
  local keyvals = tg_utils.get(config, "kvstoreParams") or {}
  if next(keyvals) == nil then
    logger.info("No KvStore keys found in node configuration")
  else
    for k, v in pairs(keyvals) do
      C.injectKeyValue(k, cjson_safe.encode(v))
    end
  end
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
