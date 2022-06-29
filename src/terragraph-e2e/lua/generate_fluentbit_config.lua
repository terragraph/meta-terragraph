#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Generates fluent-bit configuration from node configuration.
-- @script generate_fluentbit_config

local tg_utils = require "tg.utils"
local tg_net_utils = require "tg.net_utils"
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
local FLUENTBIT_CONFIG_DIR = "/var/run/fluent-bit"
local FLUENTBIT_CONFIG_FILENAME = "fluent-bit.conf"
local FLUENTBIT_OUTPUT_CONFIG_FILENAME = "fluent-bit-output.conf"
local FLUENTBIT_PARSER_CONFIG_FILENAME = "parsers_stack.conf"

-- Template strings
local FLUENTBIT_CONFIG_FORMAT = Template([[
# THIS FILE IS AUTO-GENERATED, DO NOT EDIT

${service_conf}
${input_conf_list}
${filter_conf}
]])
local SERVICE_CONFIG_FORMAT = Template([[
[SERVICE]
    Log_File ${log_file}
    Log_Level ${log_level}
    parsers_file ${parser_config_file}
]])

local FLUENTBIT_LOG_FILE = "/var/log/fluent-bit/current"
local FLUENTBIT_LOG_LEVEL = "info"
local INPUT_CONFIG_FORMAT = Template([[
[INPUT]
    Name tail
    Path ${path}
    Tag ${tag}
    DB ${db}
    Refresh_Interval ${refresh_interval}
    Read_from_Head true
    multiline.parser ${parser_name}
${mem_buf_limit_field}
]])
local INPUT_MEM_BUF_LIMIT_FORMAT = Template("    Mem_Buf_Limit ${val}")

local INPUT_TAG_FORMAT = Template("log.node.${name}")
local INPUT_DB = Template("${dir}/fluent-bit-tail.db")
local INPUT_REFRESH_INTERVAL_S = 5
local FILTER_MODIFY_CONFIG_FORMAT = Template([[
[FILTER]
    Name modify
    Match *
${set_value_list}
]])
local FILTER_SET_VALUE_FORMAT = Template("    Set ${key} ${val}")

local OUTPUT_CONFIG_FORMAT = Template("-o forward://${host}:${port} -p match=*")

-- GLOG based applications have a unique datetime format while VPP, Exabgp etc
-- have a different format.
local PARSER_REGEX_CONFIG = {
  glog = {
    name = "glog",
    startRegex = '"/(([A-Z]\\d{4} \\d{2}:\\d{2}:\\d{2}).(\\d+) (.*))/"',
    contRegex = '"/^\\s+.*/"'
  },
  other = {
    name = "other",
    startRegex = '"/(\\[?\\d{4}-\\d{2}-\\d{2}.*)/"',
    contRegex = '"/^\\s+.*/"'
  }
}

-- Create multiline parsers to enable log forwarding of stack traces
local PARSER_CONFIG_FORMAT = Template([[
[MULTILINE_PARSER]
    name          ${parser_name}
    type          regex
    flush_timeout 1000
    #
    # Regex rules for multiline parsing
    # ---------------------------------
    #
    # configuration hints:
    #
    #  - first state always has the name: start_state
    #  - every field in the rule must be inside double quotes
    #
    # rules |   state name  | regex pattern                  | next state
    # ------|---------------|--------------------------------------------
    rule      "start_state"   ${start_line_regex}  "cont"
    rule      "cont"          ${cont_line_regex}  "cont"

]])

local PARSER_CONFIG_FILE_FORMAT = Template([[
# THIS FILE IS AUTO-GENERATED, DO NOT EDIT

${parser_list}
]])


function C.buildFluentBitConfig(serviceConf, inputConfList, filterConf)
  return FLUENTBIT_CONFIG_FORMAT:substitute{
    service_conf = serviceConf,
    input_conf_list = ("\n"):join(inputConfList),
    filter_conf = filterConf
  }
end

function C.buildServiceConfig(logFile, logLevel, parserConfigFile)
  return SERVICE_CONFIG_FORMAT:substitute{
    log_file = logFile, log_level = logLevel,
      parser_config_file = parserConfigFile
  }
end

function C.buildInputConfig(logSources, memBufLimit, configDir, refreshInterval)
  local inputList = {}
  local db = INPUT_DB:substitute{dir = configDir}
  local memBufLimitField = ""
  if memBufLimit ~= nil and memBufLimit > 0 then
    memBufLimitField = INPUT_MEM_BUF_LIMIT_FORMAT:substitute{val = memBufLimit}
  end

  -- Iterate in sorted order for cosmetics
  local logSourceKeys = tablex.keys(logSources)
  table.sort(logSourceKeys)
  for _, name in ipairs(logSourceKeys) do
    local source = logSources[name]
    if source.enabled then
      -- Generate fluentd tag (replace any '.' from input name string with '_')
      local tag = INPUT_TAG_FORMAT:substitute{name = name:gsub("\\.", "_")}
      local parserRegexConfig

      if source.filename:find("vnet") or source.filename:find("exabgp") or
          source.filename:find("kern") then
        parserRegexConfig = PARSER_REGEX_CONFIG.other
      else
        parserRegexConfig = PARSER_REGEX_CONFIG.glog
      end

      inputList[#inputList+1] = INPUT_CONFIG_FORMAT:substitute{
        path = source.filename,
        tag = tag,
        db = db,
        refresh_interval = refreshInterval,
        parser_name = parserRegexConfig.name,
        mem_buf_limit_field = memBufLimitField
      }
    end
  end
  return inputList
end

--- Build multi-line or other parsers for different
-- application logs.
function C.buildFluentBitParserConfig()
  local parserList = {}
  -- Sort the parser config for consistency in parser file contents.
  for _, parser in tablex.sort(PARSER_REGEX_CONFIG) do
    parserList[#parserList+1] = PARSER_CONFIG_FORMAT:substitute{
      parser_name = parser.name,
      start_line_regex = parser.startRegex,
      cont_line_regex = parser.contRegex
    }
  end
  return PARSER_CONFIG_FILE_FORMAT:substitute{
    parser_list = ("\n"):join(parserList)
  }
end


function C.buildFilterConfig(macAddr, nodeName, topologyName)
  -- Replace spaces in metadata values since fluent-bit does not support values
  -- with whitespace in them
  local fields = {}
  if macAddr:len() > 0 then
    fields[#fields+1] = FILTER_SET_VALUE_FORMAT:substitute{
      key = "mac_addr", val = macAddr:gsub(" ", "_")
    }
  end
  if nodeName:len() > 0 then
    fields[#fields+1] = FILTER_SET_VALUE_FORMAT:substitute{
      key = "node_name", val = nodeName:gsub(" ", "_")
    }
  end
  if topologyName:len() > 0 then
    fields[#fields+1] = FILTER_SET_VALUE_FORMAT:substitute{
      key = "topology_name", val = topologyName:gsub(" ", "_")
    }
  end

  return FILTER_MODIFY_CONFIG_FORMAT:substitute{
    set_value_list = ("\n"):join(fields)
  }
end

function C.buildFluentBitOutputConfig(endpoints)
  local outputEndpoints = {}
  -- Iterate in sorted order for cosmetics
  local endpointKeys = tablex.keys(endpoints)
  table.sort(endpointKeys)
  for _, key in ipairs(endpointKeys) do
    local endpoint = endpoints[key]
    local host = endpoint.host
    if tg_net_utils.isIPv6(host) then
      host = string.format("[%s]", host)
    end
    outputEndpoints[#outputEndpoints+1] = OUTPUT_CONFIG_FORMAT:substitute{
      host = host, port = endpoint.port
    }
  end
  return (" "):join(outputEndpoints)
end

function C.processNodeConfig(config)
  local logSources = tg_utils.get(config, "fluentdParams", "sources") or {}
  local endpoints = tg_utils.get(config, "fluentdParams", "endpoints") or {}
  local memBufLimit = tg_utils.get(config, "fluentdParams", "memBufLimit") or 0
  local nodeName = tg_utils.get(config, "topologyInfo", "nodeName") or ""
  local topologyName =
    tg_utils.get(config, "topologyInfo", "topologyName") or ""

  return logSources, endpoints, memBufLimit, nodeName, topologyName
end

function C.main()
  local parser = argparse(
    "generate_fluentbit_config",
    "Generates fluent-bit configuration from node configuration."
  )
  parser:argument("node_id", "This node's ID (MAC address)")
  parser:option(
    "-i --node_config_file", "Path to the node configuration file", CONFIG_FILE
  )
  parser:option(
    "-o --fluentbit_output_dir",
    "Output directory for fluent-bit configuration files",
    FLUENTBIT_CONFIG_DIR
  )
  parser:option(
    "--fluentbit_log_file",
    "Log file for the fluent-bit service",
    FLUENTBIT_LOG_FILE
  )
  parser:option(
    "--fluentbit_log_level",
    "Log level for the fluent-bit service",
    FLUENTBIT_LOG_LEVEL
  )
  parser:option(
    "--fluentbit_tail_refresh_interval",
    "Refresh interval for watched files, in seconds",
    INPUT_REFRESH_INTERVAL_S
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

  -- Read and parse node config
  local config = tg_utils.readJsonFile(args.node_config_file)
  if config == nil then
    exit(1)
  end
  local logSources, endpoints, memBufLimit, nodeName, topologyName =
    C.processNodeConfig(config)
  if next(logSources) == nil or next(endpoints) == nil then
    logger.info("Incomplete fluent-bit config in node config, exiting now.")
    exit(0)
  end

  -- Build fluent-bit config
  local fluentInputConfig = C.buildInputConfig(
    logSources,
    memBufLimit,
    args.fluentbit_output_dir,
    args.fluentbit_tail_refresh_interval
  )

  local fluentConfig = C.buildFluentBitConfig(
    C.buildServiceConfig(args.fluentbit_log_file,
      args.fluentbit_log_level, FLUENTBIT_PARSER_CONFIG_FILENAME),
      fluentInputConfig,
    C.buildFilterConfig(args.node_id:strip(), nodeName, topologyName)
  )
  local fluentOutputConfig = C.buildFluentBitOutputConfig(endpoints)
  local fluentParserConfig = C.buildFluentBitParserConfig()

  -- Write config
  dir.makepath(args.fluentbit_output_dir)
  local fluentConfigFile =
    path.join(args.fluentbit_output_dir, FLUENTBIT_CONFIG_FILENAME)
  local fluentOutputConfigFile =
    path.join(args.fluentbit_output_dir, FLUENTBIT_OUTPUT_CONFIG_FILENAME)
  local fluentParserConfigFile =
    path.join(args.fluentbit_output_dir, FLUENTBIT_PARSER_CONFIG_FILENAME)

  logger.info("Writing fluent-bit config to %s", fluentConfigFile)
  if not tg_utils.writeFile(fluentConfigFile, fluentConfig) then
    exit(1)
  end
  logger.info("Writing fluent-bit output config to %s", fluentOutputConfigFile)
  if not tg_utils.writeFile(fluentOutputConfigFile, fluentOutputConfig) then
    exit(1)
  end

  logger.info("Writing fluent-bit parser config to %s", fluentParserConfigFile)
  if not tg_utils.writeFile(fluentParserConfigFile, fluentParserConfig) then
    logger.error("Error writing file to %s", fluentParserConfigFile)
    exit(1)
  end

  logger.stopSyslog()
end

if tg_utils.isMain() then
  C.main()
else
  return C
end
