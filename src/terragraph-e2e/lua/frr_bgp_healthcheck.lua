#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Healthcheck monitor for announced FRR BGP routes.
-- @script frr_bgp_healthcheck
require "openr.Network_ttypes"
local argparse = require "argparse"
local cjson_safe = require "cjson.safe"
local tg_utils = require "tg.utils"
local tg_frr_utils = require "tg.frr_utils"
local tg_net_utils = require "tg.net_utils"
local tg_platform_utils = require "tg.platform_utils"
local logger = require "tg.logger"
local unistd = require "posix.unistd"
local tablex = require "pl.tablex"
require("pl.stringx").import()

local BGPD_CONF_FILE = "/var/run/bgpd-current.conf"
local CONFIG_FILE = "/data/cfg/node_config.json"
local NODE_INFO_FILE = "/var/run/node_info"
local KV_NETWORK_PREFIX = "e2e-network-prefix" -- Network prefix key in Open/R
local FRR_BGP_MONITOR_INTERVAL_S = 10

--- Return the localhost ipv6 address (nil if not found).
local function getLoopbackAddress()
  local loopbackAddress = tg_platform_utils.getLoopbackAddress()
  if loopbackAddress == nil then
    logger.error("No global loopback address found")
    return nil
  end
  if not tg_net_utils.isIPv6(loopbackAddress) then
    logger.error("Invalid loopback IPv6 address: %s", loopbackAddress)
    return nil
  end
  logger.debug("Current loopback IP %s", loopbackAddress)
  return loopbackAddress
end

--- Return a list of ipv6 addresses from local fib (nil if error).
local function getFibPrefixes()
  local cmd = "breeze fib list --json"
  local output = tg_utils.exec(cmd, true)
  if output == nil then
    logger.error("Error to execute command: %s", cmd)
    return nil
  end
  -- Remove any message that comes prior to the json and decode table
  output = output:gsub("^[^{]*", "")
  local fib, err = cjson_safe.decode(output)
  if fib == nil then
    logger.error("Error reading JSON fib: %s", err)
    return nil
  end

  local fibPrefixes = {}
  for _, entry in pairs(fib.unicastRoutes) do
    local prefix = entry.dest
    if (prefix ~= nil and tg_net_utils.isIPv6Network(prefix)) then
      table.insert(fibPrefixes, prefix)
    end
  end
  return fibPrefixes
end

--- Return a list of ipv6 CPE prefixes from local fib (nil if error).
local function getCpePrefixes()
  local cmd = "breeze kvstore prefixes --nodes=all --json"
  local output = tg_utils.exec(cmd, true)
  if output == nil then
    logger.error("Error to execute command: %s", cmd)
    return nil
  end
  local prefixesTable, err = cjson_safe.decode(output)
  if prefixesTable == nil then
    logger.error("Error reading JSON prefixes list: %s", err)
    return nil
  end

  local cpePrefixes = {}
  for _, nodeEntry in pairs(prefixesTable) do
    for __, prefixEntry in pairs(nodeEntry.prefixEntries) do
      if prefixEntry.type == PrefixType.CPE then
        table.insert(cpePrefixes, prefixEntry.prefix)
      end
    end
  end
  return cpePrefixes
end

--- Reload frr config to match contents of `bgpdConfFile`.
local function reloadFrrConfig(bgpdConfFile)
  local reloadCmd = string.format(
    "/usr/sbin/frr_reload %s --reload",
    bgpdConfFile
  )
  local output, ret, code = tg_utils.exec(reloadCmd)
  if output == nil then
    logger.error("Error calling frr_reload: %s %d", ret, code)
    return nil
  end
  logger.info("frr_reload output:\n%s", output)
  return true
end

-- Prefix list class
local PrefixList = {}
PrefixList.__index = PrefixList

--- Constructor for PrefixList.
--
-- Order matters in FRR config (translates to sequence numbers), so this class
-- preserves previous order when possible.
function PrefixList.new(prefixes)
  local self = setmetatable({}, PrefixList)
  local prefixMap = {}  -- map(prefixStr, listIndex)
  for idx, prefix in ipairs(prefixes or {}) do
    prefixMap[prefix] = idx
  end
  self.prefixMap = prefixMap
  return self
end

--- Return the number of prefixes in the list.
function PrefixList:size()
  return tablex.size(self.prefixMap)
end

--- Return whether this list is identical to another (raw table) ignoring order.
function PrefixList:equals(prefixes)
  return tablex.compare_no_order(tablex.keys(self.prefixMap), prefixes)
end

--- Reorder this list given values from a different PrefixList instance.
function PrefixList:reorder(list)
  -- Initialize a list of available indices
  local n = tg_utils.tableMax(tablex.values(self.prefixMap)) or 0
  if n == 0 then
    return  -- our list is empty, nothing to do
  end
  local availableIndices = {}
  for i = 1, n do
    availableIndices[i] = true
  end

  -- Use given positions for any existing elements
  local newPrefixMap = {}
  for prefix, idx in pairs(list.prefixMap) do
    if self.prefixMap[prefix] ~= nil then
      newPrefixMap[prefix] = idx
      self.prefixMap[prefix] = nil
      availableIndices[idx] = nil
    end
  end

  -- For unspecified elements, fill in starting at smallest index
  -- (sort indices in descending order so we can quickly pop elements)
  local indices = tablex.keys(availableIndices)
  table.sort(indices, function(a, b) return a > b end)
  for prefix, _ in pairs(self.prefixMap) do
    local idx = table.remove(indices)
    newPrefixMap[prefix] = idx
  end

  self.prefixMap = newPrefixMap
end

--- Return the prefixes as a list (raw table), using empty strings as
-- placeholder elements where needed.
function PrefixList:toList()
  local ret = {}

  -- Fill with empty strings
  local n = tg_utils.tableMax(tablex.values(self.prefixMap)) or 0
  for i = 1, n do
    ret[i] = ""
  end

  -- Fill elements
  for prefix, idx in pairs(self.prefixMap) do
    ret[idx] = prefix
  end

  return ret
end

-- FRR BGP Route Monitor class
local BgpHealthCheck = {}
BgpHealthCheck.__index = BgpHealthCheck

--- Constructor for BgpHealthCheck.
function BgpHealthCheck.new(
  nodeConfig, routerMac, openrKvstorePrefix, bgpdConfFile, monitorInterval
)
  local self = setmetatable({}, BgpHealthCheck)
  self.nodeConfig = nodeConfig
  self.routerMac = routerMac
  self.openrKvstorePrefix = openrKvstorePrefix
  self.bgpdConfFile = bgpdConfFile
  self.monitorInterval = monitorInterval
  -- Get the bgp routing information from node config
  self.staticBgpParams, self.neighbors, self.tgPrefixes =
    tg_frr_utils.getNodeBgpInfo(nodeConfig, routerMac, openrKvstorePrefix)
  return self
end

--- Generate a new bgpd config file and update the frr bgp running config.
function BgpHealthCheck:updateFrrBgpConfig(activeTgPrefixes, activeCpePrefixes)
  local bgpConfig = tg_frr_utils.fillConfigTemplate(
    self.staticBgpParams,
    self.neighbors,
    activeTgPrefixes,
    activeCpePrefixes
  )
  if bgpConfig == nil then
    logger.error("Unable to generate template from active prefixes info")
    return false
  end
  logger.info("Writing new bgpd configuration to file: %s", self.bgpdConfFile)
  if not tg_utils.writeFile(self.bgpdConfFile, bgpConfig) then
    logger.error("Unable to overwrite bgpd.conf file with new config")
    return false
  end
  logger.info("Calling frr_reload...")
  if not reloadFrrConfig(self.bgpdConfFile) then
    logger.error("Unable to reload frr configs")
    return false
  end
  return true
end

--- Run a periodic healthcheck for announced BGP routes.
function BgpHealthCheck:healthCheck()
  -- Initialize lists to hold prefixes that are currently advertised
  local runningTgPrefixList = PrefixList.new(self.tgPrefixes)
  local runningCpePrefixList = PrefixList.new()

  while true do
    logger.info(
      "Currently advertising %d TG / %d CPE prefixes",
      runningTgPrefixList:size(),
      runningCpePrefixList:size()
    )
    -- Initialize tables to hold active prefixes in this round of checking
    local activeTgPrefixesMap = {} -- used as a set
    local activeCpePrefixes = getCpePrefixes()

    -- Run healthcheck on TG network prefixes (node and links) to produce a
    -- list of active prefixees that should be advertised
    local loopbackAddress = getLoopbackAddress()
    local fibPrefixes = getFibPrefixes()
    for _, tgPrefix in pairs(self.tgPrefixes) do
      if (
        loopbackAddress
        and tg_net_utils.isAddrInIPv6Network(loopbackAddress, tgPrefix)
      ) then
        -- If tgPrefix contains the POP node's lo address add it to active list
        logger.debug(
          "Found active loopback address %s within %s prefix",
          loopbackAddress,
          tgPrefix
        )
        activeTgPrefixesMap[tgPrefix] = true
      else
        -- Else, test each prefix in OpenR's fib
        if fibPrefixes ~= nil then
          for __, fibPrefix in pairs(fibPrefixes) do
            if tg_net_utils.isPrefixInIPv6Network(fibPrefix, tgPrefix) then
              -- If tgPrefix contains the fibPrefix, add to active list
              logger.debug(
                "Found an Open/R route or TG link %s within %s prefix",
                fibPrefix,
                tgPrefix
              )
              activeTgPrefixesMap[tgPrefix] = true
              break
            end
          end
        end
      end
    end
    -- Get all sorted unique TG prefixes to advertise
    local activeTgPrefixes = tablex.keys(activeTgPrefixesMap)
    table.sort(activeTgPrefixes)

    -- If the list of active prefixes is different from running prefixes,
    -- update the bgpd config file and reload frr config
    local hasNewTgPrefixes = not runningTgPrefixList:equals(activeTgPrefixes)
    local hasNewCpePrefixes = not runningCpePrefixList:equals(activeCpePrefixes)
    if hasNewTgPrefixes or hasNewCpePrefixes then
      local activeTgPrefixList = PrefixList.new(activeTgPrefixes)
      local activeCpePrefixList = PrefixList.new(activeCpePrefixes)
      activeTgPrefixList:reorder(runningTgPrefixList)
      activeCpePrefixList:reorder(runningCpePrefixList)
      if self:updateFrrBgpConfig(
        activeTgPrefixList:toList(), activeCpePrefixList:toList()
      ) then
        logger.info("Successfully updated running frr bgp config")
        if hasNewTgPrefixes then
          logger.info(
            "Updated list of advertised TG prefixes (%d total): [%s]",
            #activeTgPrefixes,
            (", "):join(activeTgPrefixes)
          )
        end
        if hasNewCpePrefixes then
          logger.info(
            "Updated list of advertised CPE prefixes (%d total): [%s]",
            #activeCpePrefixes,
            (", "):join(activeCpePrefixes)
          )
        end
        runningTgPrefixList = activeTgPrefixList
        runningCpePrefixList = activeCpePrefixList
      else
        logger.error("Failed to update running frr bgp config")
      end
    end

    unistd.sleep(self.monitorInterval)
  end
end

local function main()
  local parser = argparse(
    "frr_bgp_healthcheck",
    "Healthcheck monitor for announced FRR BGP routes"
  )
  parser:option(
    "-t --monitor_interval",
    "Monitor interval (seconds)",
    FRR_BGP_MONITOR_INTERVAL_S
  )
  parser:option(
    "-n --node_config_file",
    "Path to the node configuration file",
    CONFIG_FILE
  )
  parser:option(
    "-i --node_info_file",
    "Path to the node info file",
    NODE_INFO_FILE
  )
  parser:option(
    "-o --bgpd_conf_file",
    "Output bgpd configuration file path",
    BGPD_CONF_FILE
  )
  parser:flag("-d --debug", "Enable logging at debug level", false)
  local args = parser:parse()

  -- Set up logging
  logger.enableDateTimeLogging()
  if args.debug then
    logger.level = "debug"
  end

  -- Read node config file
  local nodeConfig = tg_utils.readJsonFile(args.node_config_file)
  if nodeConfig == nil then
    logger.error("Failed to read node config file %s", args.node_config_file)
    os.exit(1)
  end

  -- Read node info file
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

  local bgpHealthCheck = BgpHealthCheck.new(
    nodeConfig,
    routerMac,
    openrKvstorePrefix,
    args.bgpd_conf_file,
    args.monitor_interval
  )
  logger.info(
    "staticBgpParams = %s",
    cjson_safe.encode(bgpHealthCheck.staticBgpParams)
  )
  logger.info("neighbors = %s", cjson_safe.encode(bgpHealthCheck.neighbors))
  logger.info("tgPrefixes = %s", cjson_safe.encode(bgpHealthCheck.tgPrefixes))
  if (
    bgpHealthCheck.staticBgpParams == nil
    or bgpHealthCheck.neighbors == nil
    or bgpHealthCheck.tgPrefixes == nil
  ) then
    logger.error("Unable to get all necessary information from node config")
    os.exit(1)
  end
  logger.info("Starting FRR BGP announced routes monitor")
  bgpHealthCheck:healthCheck()
end

if tg_utils.isMain() then
  main()
else
  return BgpHealthCheck
end
