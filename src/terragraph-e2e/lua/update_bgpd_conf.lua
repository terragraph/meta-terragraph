#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Writes a custom bgpd.conf file for FRR using parameters from node
-- configuration.
-- @script update_bgpd_conf

local tg_utils = require "tg.utils"
local tg_frr_utils = require "tg.frr_utils"
local tg_platform_utils = require "tg.platform_utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local dir = require "pl.dir"
local path = require "pl.path"
require("pl.stringx").import()

local C = {}

local CONFIG_FILE = "/data/cfg/node_config.json"
local NODE_INFO_FILE = "/var/run/node_info"
local BGPD_CONF_FILE = "/var/run/bgpd.conf"

-- Network prefix key in Open/R KvStore
local KV_NETWORK_PREFIX = "e2e-network-prefix"

function C.buildBgpdConfig(config, routerMac, openrKvstorePrefix)
  -- Read and validate keys from node config
  local staticBgpParams, neighbors, tgPrefixes =
    tg_frr_utils.getNodeBgpInfo(config, routerMac, openrKvstorePrefix)

  if (
    staticBgpParams == nil
    or neighbors == nil
    or tgPrefixes == nil
  ) then
    logger.error("Unable to get all necessary information from node config")
    return nil
  end

  -- Fill in bgpd config template
  return tg_frr_utils.fillConfigTemplate(
    staticBgpParams,
    neighbors,
    tgPrefixes
  )
end

function C.main()
  local parser = argparse(
    "update_bgpd_conf",
    "Writes a custom bgpd.conf file for FRR using parameters from node " ..
    "configuration."
  )
  parser:option(
    "-n --node_config_file", "Path to the node configuration file", CONFIG_FILE
  )
  parser:option(
    "-i --node_info_file", "Path to the node info file", NODE_INFO_FILE
  )
  parser:option(
    "-o --bgpd_conf_file", "Output bgpd configuration file path", BGPD_CONF_FILE
  )
  parser:flag("-v --verbose", "Enable logging", false)
  local args = parser:parse()

  -- Set up logging
  if not args.verbose then
    logger.level = "critical"
  end

  -- Read node config and node info file
  local config = tg_utils.readJsonFile(args.node_config_file)
  if config == nil then
    os.exit(1)
  end
  local nodeInfo = tg_utils.readEnvFile(args.node_info_file)
  if nodeInfo == nil then
    logger.error("Failed to read node info file %s", args.node_info_file)
    os.exit(1)
  end
  -- Use node ID as router MAC
  local routerMac = nodeInfo.NODE_ID
  if routerMac == nil then
    logger.error("Missing 'NODE_ID' in node info file!")
  else
    logger.info("Using node ID as router MAC: %s", routerMac)
  end

  -- Read Open/R KvStore prefix
  local openrKvstorePrefix = tg_platform_utils.getOpenrKey(KV_NETWORK_PREFIX)
  if openrKvstorePrefix == nil then
    logger.error("Key '%s' not found in Open/R KvStore", KV_NETWORK_PREFIX)
  else
    openrKvstorePrefix = openrKvstorePrefix:split(",")[1]
    logger.info(
      "Using '%s' from Open/R KvStore: %s",
      KV_NETWORK_PREFIX,
      openrKvstorePrefix
    )
  end

  -- Write output config
  local output = C.buildBgpdConfig(config, routerMac, openrKvstorePrefix)
  if output == nil then
    os.exit(1)
  end
  logger.info("Writing bgpd configuration to file %s", args.bgpd_conf_file)
  dir.makepath(path.dirname(args.bgpd_conf_file))
  if not tg_utils.writeFile(args.bgpd_conf_file, output) then
    os.exit(1)
  end
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
