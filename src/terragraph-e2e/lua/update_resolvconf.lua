#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Writes a custom resolv.conf file using parameters from node configuration.
-- @script update_resolvconf

local tg_utils = require "tg.utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local dir = require "pl.dir"
local path = require "pl.path"
local tablex = require "pl.tablex"
local Template = (require "pl.text").Template

local C = {}

local CONFIG_FILE = "/data/cfg/node_config.json"
local RESOLVCONF_HEAD_FILE = "/var/run/etc/resolv.conf.d/head"

-- Template strings
local CONF_FILE_FORMAT = Template([[
# THIS FILE IS AUTO-GENERATED, DO NOT EDIT
${nameservers}
]])
local NAMESERVER_FORMAT = Template("nameserver ${server}")

function C.buildResolvconfConfig(config)
  -- Read keys from config
  local dnsServers = tablex.values(
    tg_utils.get(config, "sysParams", "dnsServers") or {}
  )
  table.sort(dnsServers)

  -- Fill in resolv.conf template
  local nameservers = tablex.imap(function(server)
    return NAMESERVER_FORMAT:substitute{server = server}
  end, dnsServers)
  return CONF_FILE_FORMAT:substitute{
    nameservers = table.concat(nameservers, "\n")
  }
end

function C.main()
  local parser = argparse(
    "update_resolvconf",
    "Writes a custom resolv.conf file using parameters from node configuration."
  )
  parser:option(
    "-i --node_config_file", "Path to the node configuration file", CONFIG_FILE
  )
  parser:option(
    "-o --resolvconf_file",
    "Output resolv.conf head file path",
    RESOLVCONF_HEAD_FILE
  )
  parser:flag("-v --verbose", "Enable logging", false)
  local args = parser:parse()

  -- Set up logging
  if not args.verbose then
    logger.level = "critical"
  end
  logger.startSyslog()
  local exit = function(code)
    logger.stopSyslog()
    os.exit(code)
  end

  -- Read node config, process it, and write output config
  local config = tg_utils.readJsonFile(args.node_config_file)
  if config == nil then
    exit(1)
  end
  local output = C.buildResolvconfConfig(config)
  logger.info("Writing resolv.conf head file: %s", args.resolvconf_file)
  dir.makepath(path.dirname(args.resolvconf_file))
  if not tg_utils.writeFile(args.resolvconf_file, output) then
    exit(1)
  end

  logger.stopSyslog()
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
