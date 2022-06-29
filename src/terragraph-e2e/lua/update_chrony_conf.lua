#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Writes a custom chrony config file using parameters from node configuration.
-- @script update_chrony_conf

local tg_utils = require "tg.utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local dir = require "pl.dir"
local path = require "pl.path"
local tablex = require "pl.tablex"
local Template = (require "pl.text").Template
require("pl.stringx").import()

local C = {}

-- File paths
local CONFIG_FILE = "/data/cfg/node_config.json"
local CHRONY_CONF_FILE = "/data/etc/chrony.conf"
local DEFAULT_CHRONY_FILE = "/etc/chrony.conf.default"

-- Default servers (when empty node config)
local DEFAULT_NTP_SERVERS = {"time.facebook.com"}

-- Regex for /dev/ppsX devices
local PPS_DEV_REGEX = "pps%d+"

-- Template strings
local CONF_FILE_FORMAT = Template([[
# THIS FILE IS AUTO-GENERATED, DO NOT EDIT

${server_conf}

# In first three updates step the system clock instead of slew
# if the adjustment is larger than 1 second.
makestep 1.0 3

# Record the rate at which the system clock gains/loses time,
# improving accuracy after reboot
driftfile /var/lib/chrony/drift

# Enable kernel synchronization of the hardware real-time clock (RTC).
rtcsync

# Specify directory for log files.
logdir /var/log/chrony

# Select which information is logged.
log tracking measurements statistics rtc tempcomp

# Change default pidfile to allow running parallel processes in -q mode
pidfile /var/run/chrony/chronyd-main.pid

${gps_source_conf}
]])

-- Using SHM for both or SOCK for both does not work
local GPS_SOURCE_FORMAT = Template([[
# Connect chronyd with NMEA using SHM method
refclock SHM 0 refid NMEA precision 1e-1 offset ${offset}

# Connect chronyd with PPS using SOCK method
refclock SOCK /run/chrony.${pps_dev}.sock refid PPS
]])
local NTP_SERVER_FORMAT = Template("server ${server} iburst")

function C.buildChronyConfig(ntpServers, ppsDevice, nmeaTimeOffset)
  -- Fill in templates
  local serverConf = {}
  for _, server in ipairs(ntpServers) do
    serverConf[#serverConf+1] = NTP_SERVER_FORMAT:substitute{server = server}
  end
  local gpsSourceLines = ""
  if ppsDevice ~= nil and ppsDevice ~= "" then
    gpsSourceLines = GPS_SOURCE_FORMAT:substitute{
      offset = nmeaTimeOffset, pps_dev = ppsDevice
  }
  end
  return CONF_FILE_FORMAT:substitute{
    server_conf = ("\n"):join(serverConf),
    gps_source_conf = gpsSourceLines,
  }
end

function C.processNodeConfig(config)
  -- Parse fields, and fill in defaults if needed
  local ntpServers = tablex.values(
    tg_utils.get(config, "sysParams", "ntpServers") or {}
  )
  if #ntpServers == 0 then
    logger.info(
      "Using default NTP servers: %s", (","):join(DEFAULT_NTP_SERVERS)
    )
    ntpServers = DEFAULT_NTP_SERVERS
  end

  local ppsDevice = tg_utils.get(config, "envParams", "GPSD_PPS_DEVICE")
  local nmeaTimeOffset = 0
  if tg_utils.get(config, "envParams", "GPSD_ENABLED") == "1" and
     ppsDevice ~= nil and ppsDevice ~= "" then
    local match = ppsDevice:match(PPS_DEV_REGEX)
    if match then
      ppsDevice = match
    end
    local offset = tg_utils.get(config, "envParams", "GPSD_NMEA_TIME_OFFSET")
    if offset ~= nil and offset ~= "" then
      nmeaTimeOffset = offset
    end
  end
  return ntpServers, ppsDevice, nmeaTimeOffset
end

function C.main()
  local parser = argparse(
    "update_chrony_conf",
    "Writes a custom chrony config file " ..
    "using parameters from node configuration."
  )
  parser:option(
    "-i --node_config_file", "Path to the node configuration file", CONFIG_FILE
  )
  parser:option(
    "-o --chrony_conf_file",
    "Output chrony configuration file path",
    CHRONY_CONF_FILE
  )
  parser:option(
    "-d --chrony_conf_default",
    "Path to the default chrony configuration file (used if needed)",
    DEFAULT_CHRONY_FILE
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
  logger.info("Writing chrony configuration to file %s", args.chrony_conf_file)
  local config = tg_utils.readJsonFile(args.node_config_file)
  if config == nil then
    dir.copyfile(args.chrony_conf_default, args.chrony_conf_file)
    exit(1)
  end
  dir.makepath(path.dirname(args.chrony_conf_file))
  local newConfig = C.buildChronyConfig(C.processNodeConfig(config))
  if not tg_utils.writeFile(args.chrony_conf_file, newConfig) then
    dir.copyfile(args.chrony_conf_default, args.chrony_conf_file)
    exit(1)
  end

  logger.stopSyslog()
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
