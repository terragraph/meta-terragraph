#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Set system time.
-- @script time_set

local tg_utils = require "tg.utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local unistd = require "posix.unistd"
local tablex = require "pl.tablex"
require("pl.stringx").import()

local C = {}

-- File paths
local CONFIG_FILE = "/data/cfg/node_config.json"
local UPTIME_FILE = "/proc/uptime"

-- Default servers (when empty node config)
local DEFAULT_NTP_SERVERS = {"time.facebook.com"}

function C.buildCommand(config)
  local ntpServers = tablex.values(
    tg_utils.get(config, "sysParams", "ntpServers") or {}
  )
  if #ntpServers == 0 then
    logger.info(
      "Using default NTP servers: %s", (","):join(DEFAULT_NTP_SERVERS)
    )
    ntpServers = DEFAULT_NTP_SERVERS
  end

  table.sort(ntpServers)
  ntpServers = tablex.map(
    function(ntpServer)
      return string.format("\'server %s iburst\'", ntpServer)
    end, ntpServers
  )
  return string.format("chronyd -q %s", (" "):join(ntpServers))
end

function C.getUptime()
  local contents = tg_utils.readFile(UPTIME_FILE)
  if contents == nil then
    return nil
  end
  return math.floor(contents:split()[1])
end

function C.main()
  local parser = argparse("time_set", "Set system time.")
  parser:argument(
    "node_config_file", "Path to the node configuration file", CONFIG_FILE
  )
  parser:option(
    "-u --uptime",
    "Optionally delay script execution until a given system uptime (in seconds)"
  ):convert(tonumber)
  local args = parser:parse()

  -- Fetch current uptime and wait if needed
  if args.uptime then
    local uptime = C.getUptime()
    if uptime == nil then
      logger.warning(
        "Unable to read current system uptime, proceeding immediately."
      )
    elseif uptime >= args.uptime then
      logger.info("Ignoring 'uptime' argument (current uptime: %ds)", uptime)
    else
      local sleepTime = args.uptime - uptime
      logger.info(
        "Delaying script execution by %d seconds (current uptime: %ds)",
        sleepTime, uptime
      )
      if unistd.sleep(sleepTime) > 0 then
        logger.info("Interrupted!")
        -- proceed anyway...
      end
    end
  end

  -- Read node config (for timeserver list)
  local config = tg_utils.readJsonFile(args.node_config_file)
  if config == nil then
    os.exit(1)
  end

  -- Run command
  local command = C.buildCommand(config)
  if command == nil then
    os.exit(1)
  end
  logger.info("Running command: %s", command)
  local output, ret, code = tg_utils.exec(command)
  if output == nil then
    os.exit(1)
  else
    logger.info(output)
  end
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
