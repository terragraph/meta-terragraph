#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local generate_fluentbit_config = dofile("/usr/sbin/generate_fluentbit_config")

local lu = require "luaunit"
local tablex = require "pl.tablex"
require("pl.stringx").import()

TestMain = {}

function TestMain:setUp()
  self.config = {
    fluentdParams = {
      endpoints = {
        server1 = {
          host = "2001::1",
          port = 24224
        },
        server2 = {
          host = "192.168.0.1",
          port = 55555
        }
      },
      sources = {
        tomato = {
          enabled = false,
          filename = "/var/log/tomato/current"
        },
        vpp_vnet = {
          enabled = true,
          filename = "/var/log/vpp/vnet.log"
        },
        carrot = {
          enabled = true,
          filename = "/var/log/carrot/current"
        }
      },
      memBufLimit = 20000000
    },
    topologyInfo = {
      nodeName = "abc",
      topologyName = "fruits"
    }
  }
end

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertIsFunction(generate_fluentbit_config.processNodeConfig)
  lu.assertIsFunction(generate_fluentbit_config.buildServiceConfig)
  lu.assertIsFunction(generate_fluentbit_config.buildInputConfig)
  lu.assertIsFunction(generate_fluentbit_config.buildFilterConfig)
  lu.assertIsFunction(generate_fluentbit_config.buildFluentBitConfig)
  lu.assertIsFunction(generate_fluentbit_config.buildFluentBitOutputConfig)
end

--- Test execution with good config.
function TestMain:testGoodConfig()
  local logSources, endpoints, memBufLimit, nodeName, topologyName =
    generate_fluentbit_config.processNodeConfig(self.config)

  lu.assertTrue(tablex.deepcompare(
    logSources, self.config.fluentdParams.sources
  ))
  lu.assertTrue(tablex.deepcompare(
    endpoints, self.config.fluentdParams.endpoints
  ))
  lu.assertEquals(memBufLimit, self.config.fluentdParams.memBufLimit)
  lu.assertEquals(nodeName, self.config.topologyInfo.nodeName)
  lu.assertEquals(topologyName, self.config.topologyInfo.topologyName)
end

--- Test execution with empty config.
function TestMain:testEmptyConfig()
  local logSources, endpoints, memBufLimit, nodeName, topologyName =
    generate_fluentbit_config.processNodeConfig({})

  lu.assertIsTable(logSources)
  lu.assertNil(next(logSources))
  lu.assertIsTable(endpoints)
  lu.assertNil(next(endpoints))
  lu.assertEquals(memBufLimit, 0)
  lu.assertEquals(nodeName, "")
  lu.assertEquals(topologyName, "")
end

--- Test generation of fluent-bit config.
function TestMain:testFluentBitConfig()
  local logSources, endpoints, memBufLimit, nodeName, topologyName =
    generate_fluentbit_config.processNodeConfig(self.config)

  local serviceLogFile = "/var/log/fluent-bit/current"
  local serviceLogLevel = "info"
  local parserFileName = "parsers_vegetables.conf"

  local serviceConf = generate_fluentbit_config.buildServiceConfig(
    serviceLogFile, serviceLogLevel, parserFileName
  )
  lu.assertIsString(serviceConf)

  local configDir = "/tmp/fluent-bit"
  local refreshInterval = 2
  local inputConfList = generate_fluentbit_config.buildInputConfig(
    logSources, memBufLimit, configDir, refreshInterval
  )
  lu.assertIsTable(inputConfList)
  lu.assertEquals(#inputConfList, 2)

  local macAddr = "11:22:33:44:55:66"
  local filterConf = generate_fluentbit_config.buildFilterConfig(
    macAddr,
    self.config.topologyInfo.nodeName,
    self.config.topologyInfo.topologyName
  )
  lu.assertIsString(filterConf)

  local fluentConfig = generate_fluentbit_config.buildFluentBitConfig(
    serviceConf, inputConfList, filterConf
  )
  lu.assertEquals(fluentConfig, [[
# THIS FILE IS AUTO-GENERATED, DO NOT EDIT

[SERVICE]
    Log_File /var/log/fluent-bit/current
    Log_Level info
    parsers_file parsers_vegetables.conf

[INPUT]
    Name tail
    Path /var/log/carrot/current
    Tag log.node.carrot
    DB /tmp/fluent-bit/fluent-bit-tail.db
    Refresh_Interval 2
    Read_from_Head true
    multiline.parser glog
    Mem_Buf_Limit 20000000

[INPUT]
    Name tail
    Path /var/log/vpp/vnet.log
    Tag log.node.vpp_vnet
    DB /tmp/fluent-bit/fluent-bit-tail.db
    Refresh_Interval 2
    Read_from_Head true
    multiline.parser other
    Mem_Buf_Limit 20000000

[FILTER]
    Name modify
    Match *
    Set mac_addr 11:22:33:44:55:66
    Set node_name abc
    Set topology_name fruits

]])
end

--- More specific tests for fluent-bit filter config.
function TestMain:testFluentBitFilterConfig()
  -- Omit node/topology names
  local filterConf =
    generate_fluentbit_config.buildFilterConfig("aa:bb:cc:dd:ee:ff", "", "")
  lu.assertEquals(filterConf, [[
[FILTER]
    Name modify
    Match *
    Set mac_addr aa:bb:cc:dd:ee:ff
]])
end

--- Tests for fluent-bit parser config.
function TestMain:testFluentBitParserConfig()
  local parserConf =
    generate_fluentbit_config.buildFluentBitParserConfig()
  lu.assertEquals(parserConf, [[
# THIS FILE IS AUTO-GENERATED, DO NOT EDIT

[MULTILINE_PARSER]
    name          glog
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
    rule      "start_state"   "/(([A-Z]\d{4} \d{2}:\d{2}:\d{2}).(\d+) ]] ..
    [[(.*))/"  "cont"
    rule      "cont"          "/^\s+.*/"  "cont"


[MULTILINE_PARSER]
    name          other
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
    rule      "start_state"   "/(\[?\d{4}-\d{2}-\d{2}.*)/"  "cont"
    rule      "cont"          "/^\s+.*/"  "cont"


]])
end

--- Test generation of fluent-bit output config.
function TestMain:testFluentBitOutputConfig()
  local logSources, endpoints, nodeName, topologyName =
    generate_fluentbit_config.processNodeConfig(self.config)

  local fluentOutputConfig =
    generate_fluentbit_config.buildFluentBitOutputConfig(endpoints)
  lu.assertEquals(fluentOutputConfig, ([[
-o forward://[2001::1]:24224 -p match=* -o forward://192.168.0.1:55555 -p match=*
]]):rstrip())
end

os.exit(lu.LuaUnit.run("-v"))
