#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local lu = require "luaunit"
local tg_platform_utils = require "tg.platform_utils"
local tablex = require "pl.tablex"

TestMain = {}

function TestMain:setUp()
  self._exec = tg_platform_utils._exec
end

function TestMain:tearDown()
  -- Restore "_exec()" implementation
  tg_platform_utils._exec = self._exec
end

--- Test getLoopbackAddress() function.
function TestMain:test_getLoopbackAddress()
  lu.assertIsFunction(tg_platform_utils.getLoopbackAddress)

  tg_platform_utils._exec = function(command, ignoreRetCode)
    return "    inet6 2001:0:0:49::1/128 scope global"
  end
  lu.assertEquals(tg_platform_utils.getLoopbackAddress(), "2001:0:0:49::1")

  tg_platform_utils._exec = function(command, ignoreRetCode) return " " end
  lu.assertNil(tg_platform_utils.getLoopbackAddress())

  tg_platform_utils._exec = function(command, ignoreRetCode) return nil end
  lu.assertNil(tg_platform_utils.getLoopbackAddress())
end

--- Test getOpenrKey() function.
function TestMain:test_getOpenrKey()
  lu.assertIsFunction(tg_platform_utils.getOpenrKey)

  tg_platform_utils._exec = function(command, ignoreRetCode)
    return
[[{
  "e2e-ctrl-url": {
    "hash": "4839721589458941194",
    "originatorId": "puff",
    "ttl": -2147483648,
    "ttlVersion": 0,
    "value": "tcp://[2001::1]:7007",
    "version": 1059
  }
}]]
  end
  lu.assertEquals(
    tg_platform_utils.getOpenrKey("e2e-ctrl-url"),
    "tcp://[2001::1]:7007"
  )

  tg_platform_utils._exec = function(command, ignoreRetCode) return "{}" end
  lu.assertNil(tg_platform_utils.getOpenrKey("asdf"))

  tg_platform_utils._exec = function(command, ignoreRetCode) return nil end
  lu.assertNil(tg_platform_utils.getOpenrKey("asdf"))
end

--- Test getVppUpInt() function.
function TestMain:test_getVppUpInt()
  lu.assertIsFunction(tg_platform_utils.getVppUpInt)

  local OUTPUT = [[
              Name               Idx    State  MTU (L3/IP4/IP6/MPLS)     Counter          Count
TenGigabitEthernet0               3      up          9000/0/0/0
Wigig1/1/0/0                      4      up          9000/0/0/0
local0                            0     down          0/0/0/0       drops                         16
loop0                             2      up          9000/0/0/0
vpp-terra16                       5      up          9000/0/0/0     tx-error                       1
vpp-terra17                       6      up          9000/0/0/0     tx-error                       1
vpp-terra18                       7      up          9000/0/0/0     tx-error                       1
vpp-terra19                       8      up          9000/0/0/0     tx-error                       1
vpp-terra20                       9      up          9000/0/0/0     tx-error                       1
vpp-terra21                       10     up          9000/0/0/0     tx-error                       1
vpp-terra22                       11     up          9000/0/0/0     tx-error                       1
vpp-terra23                       12     up          9000/0/0/0     tx-error                       1
vpp-terra24                       13     up          9000/0/0/0     tx-error                       1
vpp-terra25                       14     up          9000/0/0/0     tx-error                       1
vpp-terra26                       15     up          9000/0/0/0     tx-error                       1
vpp-terra27                       16     up          9000/0/0/0     tx-error                       1
vpp-terra28                       17     up          9000/0/0/0     tx-error                       1
vpp-terra29                       18     up          9000/0/0/0     tx-error                       1
vpp-terra30                       19     up          9000/0/0/0     tx-error                       1
vpp-terra31                       20     up          9000/0/0/0     tx-error                       1
vpp-vnet0                         1      up          9000/0/0/0     rx packets                 16745
                                                                    rx bytes                 1557870
                                                                    tx packets                 22920
                                                                    tx bytes                 1971164
                                                                    drops                      14631
                                                                    ip6                        16745
]]

  tg_platform_utils._exec = function(command, ignoreRetCode) return OUTPUT end
  local expected = {
    TenGigabitEthernet0 = true,
    ["Wigig1/1/0/0"] = true,
    loop0 = true,
    ["vpp-vnet0"] = true,
  }
  for i = 16, 31 do
    expected["vpp-terra" .. i] = true
  end
  lu.assertTrue(tablex.deepcompare(
    tg_platform_utils.getVppUpInt(),
    expected
  ))
  lu.assertTrue(tablex.deepcompare(
    tg_platform_utils.getVppUpInt("TenGigabitEthernet"),
    {TenGigabitEthernet0 = true}
  ))

  tg_platform_utils._exec = function(command, ignoreRetCode) return nil end
  lu.assertNil(tg_platform_utils.getVppUpInt())
end

os.exit(lu.LuaUnit.run("-v"))
