#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Run CPE interface operations.
-- @script cpe_operations

local tg_utils = require "tg.utils"
local tg_platform_utils = require "tg.platform_utils"
local logger = require "tg.logger"
local argparse = require "argparse"
require("pl.stringx").import()
local config_get_cpe = dofile("/usr/sbin/config_get_cpe")

local C = {}

local CONFIG_DIR = "/data/cfg"
local CONFIG_FILE = CONFIG_DIR .. "/node_config.json"

function C.runExecCommand(command)
  local output, ret, code = tg_utils.exec(command)
  if output == nil then
    logger.error("  - %s FAILED (returned %d)", command, code)
    return nil
  end
  return output
end

-- Manage hostapd instance on wired CPE interface
function C.runWiredSecurity(nodeConfig, dpdkVppEnabled)
  if dpdkVppEnabled == "0" then
    return
  end
  -- Start hostapd instance on CPE interface only
  local cpeConfig = nodeConfig.cpeConfig
  if cpeConfig == nil then
    return
  end

  local secret = tg_utils.get(
    nodeConfig,
    "eapolParams",
    "secrets",
    "radius_server_shared_secret"
  )
  local vppUpInt = tg_platform_utils.getVppUpInt("TenGigabitEthernet") or {}

  for intf, ifCfg in pairs(cpeConfig) do
    local ifIdx = string.match(intf, "%d+")
    if ifCfg.wiredSecurityEnable == true and vppUpInt[intf] then
      os.execute(
        "/usr/sbin/start_cpe_security.sh enable " .. ifIdx .. " " .. secret
      )
    else
      os.execute(
        "/usr/sbin/start_cpe_security.sh disable " .. ifIdx
      )
    end
  end
end

function C.setCpeInterfaceState(intf, newState, dpdkVppEnabled)
  -- Only change CPE interface state for kernel mode. In VPP mode,
  -- CPE interface is changed automatically by vpp_chaperone.
  if dpdkVppEnabled == "1" then
    return
  end

  local stateCmd = "ip link show " .. intf .. [[| \
    grep -oP '(?<=state )\w+' | tr '[:upper:]' '[:lower:]']]

  local curState = C.runExecCommand(stateCmd)
  if curState ~= newState then
    C.runExecCommand("sysctl net.ipv6.conf." .. intf .. ".accept_ra=2")
    C.runExecCommand("ip link set " .. intf .. " " .. newState)
  end
end

function C.runCpeOperationsPerInterface(intf, prefix, linuxIp, dpdkVppEnabled)
  if intf == nil or intf == "" then
    return
  end

  -- Disable CPE interface until node has a global prefix
  if linuxIp == nil or linuxIp == "" then
    logger.info(
      "'lo' global prefix not assigned yet. Keep CPE interface '%s' down.", intf
    )
    C.setCpeInterfaceState(intf, "down", dpdkVppEnabled)
    -- Withdraw CPE prefix over Open/R
    C.runExecCommand("breeze prefixmgr withdraw " .. prefix)
  else
    C.setCpeInterfaceState(intf, "up", dpdkVppEnabled)
    -- Advertise CPE prefix over Open/R
    if prefix ~= nil and prefix ~= "" then
      C.runExecCommand(
        "breeze prefixmgr advertise " .. prefix .. " --prefix-type cpe"
      )
    end
  end
end

function C.runCpeOperations(nodeConfig)
  local dpdkVppEnabled = tg_utils.get(
    nodeConfig, "envParams", "DPDK_ENABLED"
  ) or "1"

  local linuxIpCommand = [[ip -6 addr show dev lo | \
    awk '/inet6.+global/ {print substr($2,0,index($2,"/")-1); exit}' \
    | rev | cut -d ':' -f3- | rev]]
  local linuxIp = C.runExecCommand(linuxIpCommand)

  -- Read cpe config
  local cpeConfig = nodeConfig.cpeConfig
  if cpeConfig == nil then
    local intf = config_get_cpe.getCpeInterfaces(nodeConfig)
    local prefix = config_get_cpe.getCpePrefix(nodeConfig, intf)
    C.runCpeOperationsPerInterface(intf, prefix, linuxIp, dpdkVppEnabled)
  else
    for intf, ifCfg in pairs(cpeConfig) do
      C.runCpeOperationsPerInterface(
        intf, ifCfg.prefix, linuxIp, dpdkVppEnabled
      )
    end
  end

  -- Start hostapd on wired interface after loopback interface is assigned a
  -- global prefix.
  if linuxIp ~= nil and linuxIp ~= "" then
    C.runWiredSecurity(nodeConfig, dpdkVppEnabled)
  end
end

function C.main()
  local parser = argparse(
    "cpe_operations", "Run CPE interface operations."
  )
  parser:option(
    "-i --node_config_file", "Path to the node configuration file", CONFIG_FILE
  )
  local args = parser:parse()

  -- Read node config file
  local nodeConfig = tg_utils.readJsonFile(args.node_config_file)
  if nodeConfig == nil then
    os.exit(1)
  end

  C.runCpeOperations(nodeConfig)
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
