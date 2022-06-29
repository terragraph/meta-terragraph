-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Utilities specific to the Terragraph platform.
-- @module tg.platform_utils

local tg_utils = require "tg.utils"
local logger = require "tg.logger"
local cjson_safe = require "cjson.safe"
local tablex = require "pl.tablex"
require("pl.stringx").import()

local platform_utils = {}

-- allow overriding "exec" call for unit tests
platform_utils._exec = tg_utils.exec

--- Return the global loopback IP address, or nil if not found.
function platform_utils.getLoopbackAddress()
  local cmd = "ip -6 addr show lo | awk '/inet6.+global/'"
  local output = platform_utils._exec(cmd, true)
  if output == nil or output == "" then
    logger.error("No global loopback address assigned")
    return nil
  end

  local splitOutput = output:lstrip():split()
  if tablex.size(splitOutput) < 2 then
    logger.error("Invalid output from command: %s", cmd)
    return nil
  end
  return splitOutput[2]:split("/")[1]
end

--- Return the value of the given Open/R key via the "breeze" CLI.
function platform_utils.getOpenrKey(key)
  assert(type(key) == "string")

  local cmd = string.format("breeze kvstore keyvals %s --json", key)
  local output = platform_utils._exec(cmd, true)
  if output == nil then
    logger.error("Error running 'breeze' command")
    return nil
  end

  local t, err = cjson_safe.decode(output)
  if t == nil then
    logger.error("Error reading JSON output: %s", err)
    return nil
  end
  return tg_utils.get(t, key, "value")
end

--- Return a table of `up` interfaces in VPP, or nil upon error.
--
-- Caller can specify interface `prefix` to filter the list.
--
-- Example:
--    getVppUpInt("TenGigabitEthernet") =>
--    {"TenGigabitEthernet0": true, "TenGigabitEthernet1": true, ...}
function platform_utils.getVppUpInt(prefix)
  local output = platform_utils._exec("vppctl show int")
  if output == nil then
    logger.error("Error running 'vppctl' command")
    return nil
  end

  local ret = {}
  for _, line in ipairs(output:splitlines()) do
    local words = line:split()
    if words[3] == "up" and (prefix == nil or string.find(line, prefix)) then
      ret[words[1]] = true
    end
  end
  return ret
end

return platform_utils
