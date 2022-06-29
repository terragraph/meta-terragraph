#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Reads CPE interfaces and prefixes from node config.
-- @script config_get_cpe

local tg_utils = require "tg.utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local tablex = require "pl.tablex"
require("pl.stringx").import()

local C = {}

local CONFIG_DIR = "/data/cfg"
local CONFIG_FILE = CONFIG_DIR .. "/node_config.json"


function C.getSingleCpePrefix(nodeConfig)
  -- Read deprecated cpe config
  return tg_utils.get(
    nodeConfig, "envParams", "CPE_IFACE_IP_PREFIX"
  ) or ""
end

function C.getSingleCpeInterface(nodeConfig)
  -- Read deprecated cpe config
  return tg_utils.get(
    nodeConfig, "envParams", "CPE_INTERFACE"
  ) or ""
end

function C.getCpePrefix(nodeConfig, cpeIntf)
  -- Read new (or deprecated) CPE config.
  local cpeConfig = nodeConfig.cpeConfig
  if cpeConfig == nil then
    return C.getSingleCpePrefix(nodeConfig)
  end
  -- Currently, only per CPE interface prefix is supported.
  if cpeConfig[cpeIntf] ~= nil then
    return cpeConfig[cpeIntf].prefix
  end
  return ""
end

function C.getCpeInterfaces(nodeConfig, cpeIndex)
  local cpeConfig = nodeConfig.cpeConfig
  local cpeIntf = ""
  -- Read deprecated or new CPE config.
  if cpeConfig == nil then
    cpeIntf = C.getSingleCpeInterface(nodeConfig)
  else
    local intfList = tablex.keys(cpeConfig)
    table.sort(intfList)
    if cpeIndex > #intfList then
      logger.error(
        "Index %d is out of range (only %d CPE interface(s) are found)",
        cpeIndex, #intfList
      )
    else
      cpeIntf = intfList[cpeIndex]
    end
  end
  return cpeIntf
end

function C.main()
  local parser = argparse(
    "config_get_cpe", "Reads CPE interfaces and prefixes from node config."
  )
  parser:option(
    "-i --node_config_file", "Path to the node configuration file", CONFIG_FILE
  )

  -- "intf" command
  local intfCmd = parser:command(
    "intf", "Get CPE interfaces from node configuration file"
  )
  intfCmd:option("--index", "CPE interface index"):convert(tonumber)

  -- "prefix" command
  local prefixCmd = parser:command(
    "prefix", "Get IPv6 prefix of the CPE interface"
  )
  prefixCmd:argument("cpe_intf", "The CPE interface name")

  local args = parser:parse()
  -- Read node config file
  local config = tg_utils.readJsonFile(args.node_config_file)
  if config == nil then
    logger.error("Cannot read from %s", args.node_config_file)
    os.exit(1)
  end

  -- Handle commands
  local retval = nil
  if args.intf then
    retval = C.getCpeInterfaces(config, args.index)
  elseif args.prefix then
    retval = C.getCpePrefix(config, args.cpe_intf)
  end
  if retval ~= nil then
    io.write(retval)
  else
    os.exit(1)
  end
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
