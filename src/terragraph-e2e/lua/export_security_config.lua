#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Exports configuration files for wpa_supplicant and hostapd based on node
-- configuration.
--
-- @script export_security_config

local tg_utils = require "tg.utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local dir = require "pl.dir"
local path = require "pl.path"
local Template = (require "pl.text").Template
require("pl.stringx").import()

local C = {}

-- Default template files
local WPA_SUPPLICANT_TEMPLATE = "/etc/wpa_supplicant.template"
local WPA_SUPPLICANT_8021X_TEMPLATE = "/etc/wpa_supplicant-8021x.template"
local HOSTAPD_TEMPLATE = "/etc/hostapd.template"
local HOSTAPD_8021X_TEMPLATE = "/etc/hostapd-8021x.template"
local HOSTAPD_ACTION_TEMPLATE = [[
#!/bin/sh
if [ $2 == "CTRL-EVENT-EAP-SUCCESS2" ]; then
  vppctl classify table acl-miss-next deny mask l2 src
  vppctl classify session acl-hit-next permit table-index 0 match l2 src $3
  vppctl set interface input acl intfc ${vppIfName} ip6-table 0
  vppctl set interface input acl intfc ${vppIfName} ip4-table 0
  vppctl set interface input acl intfc ${vppIfName} l2-table 0

  vppctl set interface eapol-only off ${vppIfName}
fi
]]

-- Default output directories
local WPA_SUPPLICANT_CONF_DIR = "/var/run/wpa_supplicant/"
local HOSTAPD_CONF_DIR = "/var/run/hostapd/"

-- Default wpa_psk_file path
local WPA_PSK_FILE_PATH = "/var/run/hostapd.wpa_psk"

-- Default WPA-PSK passphrase
local DEFAULT_WPA_PASSPHRASE = "psk_test"

-- Node config file
local CONFIG_FILE = "/data/cfg/node_config.json"

--- Generate wpa_supplicant and hostapd config files by filling templates with
-- values from the given node configuration.
--
-- Returns three values: `(success, wpa_supplicant_conf, hostapd_conf)`
function C.buildWsecConfigs(config, ifname, macaddr)
  -- Read wsecEnable
  local wsecEnable = tg_utils.get(
    config, "radioParamsBase", "fwParams", "wsecEnable"
  )

  -- Select template files
  local wpaSupplicantTmplFile, hostapdTmplFile
  if wsecEnable == 0 then
    logger.warning("wsec is not enabled")
    return false, nil, nil
  elseif wsecEnable == 1 then
    wpaSupplicantTmplFile = WPA_SUPPLICANT_TEMPLATE
    hostapdTmplFile = HOSTAPD_TEMPLATE
  elseif wsecEnable == 2 then
    wpaSupplicantTmplFile = WPA_SUPPLICANT_8021X_TEMPLATE
    hostapdTmplFile = HOSTAPD_8021X_TEMPLATE
  else
    logger.error("Unsupported value of wsecEnable: %s", tostring(wsecEnable))
    return false, nil, nil
  end
  logger.info("Using wpa_supplicant template: %s", wpaSupplicantTmplFile)
  logger.info("Using hostapd template: %s", hostapdTmplFile)

  -- Read template files
  local wpaSupplicantTmpl = tg_utils.readFile(wpaSupplicantTmplFile)
  local hostapdTmpl = tg_utils.readFile(hostapdTmplFile)
  if not wpaSupplicantTmpl or not hostapdTmpl then
    return false, nil, nil
  end

  -- Fill in templates
  local vars = tg_utils.get(config, "eapolParams") or {}
  local psk = tg_utils.get(
    config, "radioParamsBase", "wsecParams", "wpaPskParams", "wpa_passphrase"
  )
  if psk == nil then
    psk = DEFAULT_WPA_PASSPHRASE
    if wsecEnable == 1 then
      logger.error(
        "WARNING: Missing 'wpa_passphrase' in config, using default '%s'",
        DEFAULT_WPA_PASSPHRASE
      )
    end
  end
  vars.wpa_passphrase = psk
  vars.ifname = ifname
  vars.macaddr = macaddr
  vars.driver = "nl80211"
  -- Return empty string for missing keys
  setmetatable(vars, {__index = function(t, k)
    logger.error("WARNING: Missing template field '%s', setting as blank...", k)
    return ""
  end})
  local wpaSupplicantConfig = Template(wpaSupplicantTmpl):substitute(vars)
  local hostapdConfig = Template(hostapdTmpl):substitute(vars)
  return true, wpaSupplicantConfig, hostapdConfig
end

--- Generate hostapd and action config files by filling templates with
-- values from the given node configuration.
--
-- Returns three values: `(success, hostapd_conf, action_conf)`
function C.buildWiredSecurityConfigs(config, ifname, macaddr)
  -- Read wiredSecurityEnable
  local wiredSecurityEnable = false
  local vppIfName = nil
  local cpeConfig = config.cpeConfig
  local ifnameIdx = string.match(ifname, "%d+")
  for intf, ifCfg in pairs(cpeConfig or {}) do
    if string.match(intf, "%d+") == ifnameIdx then
      wiredSecurityEnable = ifCfg.wiredSecurityEnable
      vppIfName = intf
      break
    end
  end

  if wiredSecurityEnable ~= true then
    logger.warning("wired security is not enabled")
    return false, nil, nil
  end

  -- Read template files
  local hostapdTmplFile = HOSTAPD_8021X_TEMPLATE
  logger.info("Using hostapd template: %s", hostapdTmplFile)
  local hostapdTmpl = tg_utils.readFile(hostapdTmplFile)
  local actionTmpl = HOSTAPD_ACTION_TEMPLATE
  if not hostapdTmpl or not actionTmpl then
    return false, nil, nil
  end

  -- Fill in templates
  local vars = tg_utils.get(config, "eapolParams") or {}
  vars.ifname = ifname
  vars.macaddr = macaddr
  vars.driver = "wired"
  -- Return empty string for missing keys
  setmetatable(vars, {__index = function(t, k)
    logger.error("WARNING: Missing template field '%s', setting as blank...", k)
    return ""
  end})
  local hostapdConfig = Template(hostapdTmpl):substitute(vars)
  local actionConfig = actionTmpl:gsub("${vppIfName}", vppIfName)
  return true, hostapdConfig, actionConfig
end

--- Generate and write wpa_supplicant and hostapd config files.
function C.generateConfFiles(
  config, wpaSupplicantConfDir, hostapdConfDir, ifname, macaddr
)
  local success, wpaSupplicantConfig, hostapdConfig, actionConfig
  if ifname:startswith("nic") then
    -- Generate wired security configs
    success, hostapdConfig, actionConfig =
      C.buildWiredSecurityConfigs(config, ifname, macaddr)
    if not success then
      return false
    end
  else
    -- Generate wsec configs
    success, wpaSupplicantConfig, hostapdConfig =
      C.buildWsecConfigs(config, ifname, macaddr)
    if not success then
      return false
    end
  end

  -- Write hostapd output files
  if hostapdConfig ~= nil then
    dir.makepath(path.dirname(hostapdConfDir))
    local hostapdConfFile = path.join(
      hostapdConfDir, string.format("hostapd_%s.conf", ifname)
    )
    logger.info("Writing hostapd config to %s", hostapdConfFile)
    local ret = tg_utils.writeFile(hostapdConfFile, hostapdConfig)
    if not ret then
      logger.error("ERROR: Failed to write hostapd config file")
      return false
    end
  end

  -- Write wpa_supplicant output files
  if wpaSupplicantConfig ~= nil then
    dir.makepath(path.dirname(wpaSupplicantConfDir))
    local wpaSupplicantConfFile = path.join(
      wpaSupplicantConfDir, string.format("wpa_supplicant_%s.conf", ifname)
    )
    logger.info("Writing wpa_supplicant config to %s", wpaSupplicantConfFile)
    local ret = tg_utils.writeFile(wpaSupplicantConfFile, wpaSupplicantConfig)
    if not ret then
      logger.error("ERROR: Failed to write wpa_supplicant config file")
      return false
    end
  end

  -- Write action output files
  if actionConfig ~= nil then
    dir.makepath(path.dirname(hostapdConfDir))
    local actionConfFile = path.join(
      hostapdConfDir, string.format("hostapd_action_%s.sh", ifname)
    )
    logger.info("Writing hostapd action file to %s", actionConfFile)
    local ret = tg_utils.writeFile(actionConfFile, actionConfig)
    if not ret then
      logger.error("ERROR: Failed to write hostapd config file")
      return false
    end
    tg_utils.exec(string.format("chmod +x %s", actionConfFile))
  end

  return true
end

--- Generate `wpa_psk_file` (for hostapd) using the given node configuration.
function C.buildWpaPskFile(config)
  local lines = {}

  -- Default PSK
  local psk = tg_utils.get(
    config, "radioParamsBase", "wsecParams", "wpaPskParams", "wpa_passphrase"
  )
  if psk == nil then
    psk = DEFAULT_WPA_PASSPHRASE
  end
  lines[#lines+1] = string.format("00:00:00:00:00:00 %s", psk)

  -- Allowed per-link PSKs
  local perLinkPsk = tg_utils.get(
    config,
    "radioParamsBase",
    "wsecParams",
    "wpaPskParams",
    "wpa_passphrase_override"
  )
  if type(perLinkPsk) == "table" then
    for responderMac, linkPsk in pairs(perLinkPsk) do
      lines[#lines+1] = string.format("%s %s", responderMac, linkPsk)
    end
  end

  return ("\n"):join(lines)
end

--- Generate and write hostapd `wpa_psk_file`.
function C.generateWpaPskFile(config, wpaPskFilePath)
  -- Generate wpa_psk_file contents
  local content = C.buildWpaPskFile(config)

  -- Write file
  dir.makepath(path.dirname(wpaPskFilePath))
  logger.info("Writing wpa_psk_file to %s", wpaPskFilePath)
  if not tg_utils.writeFile(wpaPskFilePath, content) then
    logger.error("ERROR: Failed to write wpa_psk_file")
    return false
  end

  return true
end

function C.main()
  local parser = argparse(
    "export_security_config",
    "Exports configuration files for wpa_supplicant and hostapd based on " ..
    "node configuration."
  )
  parser:option(
    "-i --node_config_file", "Path to the node configuration file", CONFIG_FILE
  )
  parser:flag("-v --verbose", "Enable logging", false)
  -- "conf" command
  local confCmd = parser:command(
    "conf", "Generate config files for wpa_supplicant and hostapd"
  )
  confCmd:option(
    "--wpa_supplicant_conf_dir",
    "Output wpa_supplicant configuration directory",
    WPA_SUPPLICANT_CONF_DIR
  )
  confCmd:option(
    "--hostapd_conf_dir",
    "Output hostapd configuration directory",
    HOSTAPD_CONF_DIR
  )
  confCmd:argument("ifname", "The network interface name")
  confCmd:argument("macaddr", "The network interface MAC address")
  -- "wpa_psk_file" command
  local wpaPskFileCmd = parser:command(
    "wpa_psk_file", "Generate wpa_psk_file for hostapd"
  )
  wpaPskFileCmd:option(
    "--wpa_psk_file_path", "Output wpa_psk_file path", WPA_PSK_FILE_PATH
  )

  local args = parser:parse()

  -- Set up logging
  if not args.verbose then
    logger.level = "error"
  end

  -- Read node config file
  local config = tg_utils.readJsonFile(args.node_config_file)
  if config == nil then
    os.exit(1)
  end

  -- Handle user command
  local success = false
  if args.conf then
    success = C.generateConfFiles(
      config,
      args.wpa_supplicant_conf_dir,
      args.hostapd_conf_dir,
      args.ifname,
      args.macaddr
    )
  elseif args.wpa_psk_file then
    success = C.generateWpaPskFile(config, args.wpa_psk_file_path)
  end
  if not success then
    os.exit(1)
  end
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
