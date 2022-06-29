#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Print all POP node IPv6 addresses known to Open/R.
-- @script get_pop_ip

require "openr.Network_ttypes"
local tg_utils = require "tg.utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local cjson_safe = require "cjson.safe"

local C = {}

function C.getPopIp(prefixEntries)
  local popNode = false
  local ip = nil
  for _, entry in ipairs(prefixEntries) do
    if entry.prefix == "::/0" then
      popNode = true
    elseif entry.type == PrefixType.PREFIX_ALLOCATOR then
      ip = entry.prefix
    end
  end
  if popNode and ip then
    return ip:gsub("::/64", "::1")
  end
end

function C.main()
  local parser = argparse(
    "get_pop_ip", "Print all POP node IPv6 addresses known to Open/R."
  )
  parser:parse()

  local command = "breeze kvstore prefixes --nodes=all --json"
  local output, ret, code = tg_utils.exec(command)
  if output == nil then
    logger.error("Error running 'breeze' command")
    os.exit(1)
  end

  local t, err = cjson_safe.decode(output)
  if t == nil then
    logger.error("Error reading JSON output: %s", err)
    os.exit(1)
  end

  for _, prefixDb in pairs(t) do
    local ip = C.getPopIp(prefixDb.prefixEntries)
    if ip ~= nil then
      logger.info(ip)
    end
  end
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
