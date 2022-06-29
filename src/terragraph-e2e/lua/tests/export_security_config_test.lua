#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local export_security_config = dofile("/usr/sbin/export_security_config")

local lu = require "luaunit"

TestMain = {}

function TestMain:setUp()
  self.config = {
    cpeConfig = {
      TenGigabitEthernet0 = {
        prefix = "2021::/64",
        wiredSecurityEnable = true
      }
    },
    radioParamsBase = {
      fwParams = {
        wsecEnable = 2
      },
      wsecParams = {
        wpaPskParams = {
          wpa_passphrase = "psk_test",
          wpa_passphrase_override = {
            ["aa:bb:cc:dd:ee:ff"] = "totally_secure_passphrase"
          }
        }
      }
    },
    eapolParams = {
      radius_server_ip = "::1",
      radius_server_port = 1812,
      radius_user_identity = "tg",
      ca_cert_path = "/data/secure/keys/ca.pem",
      client_cert_path = "/data/secure/keys/client.pem",
      private_key_path = "/data/secure/keys/client.key",
      secrets = {
        radius_server_shared_secret = "wow",
        radius_user_password = "such",
        private_key_password = "secret"
      }
    }
  }
  self.wirelessIfname = "terra0"
  self.wiredIfname = "nic0"
  self.macaddr = "11:22:33:44:55:66"
end

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertIsFunction(export_security_config.buildWsecConfigs)
end

--- Test 802.1X config (wsecEnable = 2).
function TestMain:test802_1X()
  self.config.radioParamsBase.fwParams.wsecEnable = 2

  local success, wpaSupplicantConfig, hostapdConfig =
    export_security_config.buildWsecConfigs(
      self.config, self.wirelessIfname, self.macaddr
    )
  lu.assertTrue(success)
  lu.assertIsString(wpaSupplicantConfig)
  lu.assertStrContains(wpaSupplicantConfig, 'identity="tg"')
  lu.assertStrContains(
    wpaSupplicantConfig, 'ca_cert="/data/secure/keys/ca.pem"'
  )
  lu.assertStrContains(
    wpaSupplicantConfig, 'client_cert="/data/secure/keys/client.pem"'
  )
  lu.assertStrContains(
    wpaSupplicantConfig, 'private_key="/data/secure/keys/client.key"'
  )
  lu.assertIsString(hostapdConfig)
  lu.assertStrContains(hostapdConfig, "driver=nl80211")
  lu.assertStrContains(hostapdConfig, "auth_server_addr=::1")
  lu.assertStrContains(hostapdConfig, "auth_server_port=1812")
  lu.assertStrContains(
    hostapdConfig,
    string.format("nas_identifier=%s-%s", self.wirelessIfname, self.macaddr)
  )
end

--- Test WPA-PSK config (wsecEnable = 1).
function TestMain:testWpaPsk()
  self.config.radioParamsBase.fwParams.wsecEnable = 1

  local success, wpaSupplicantConfig, hostapdConfig =
    export_security_config.buildWsecConfigs(
      self.config, self.wirelessIfname, self.macaddr
    )
  lu.assertTrue(success)
  lu.assertIsString(wpaSupplicantConfig)
  lu.assertStrContains(
    wpaSupplicantConfig,
    "ctrl_interface=/var/run/wpa_supplicant_" .. self.wirelessIfname
  )
  lu.assertStrContains(wpaSupplicantConfig, 'psk="psk_test"')
  lu.assertIsString(hostapdConfig)
  lu.assertStrContains(
    hostapdConfig, "ctrl_interface=/var/run/hostapd_" .. self.wirelessIfname
  )
  lu.assertStrContains(hostapdConfig, "interface=" .. self.wirelessIfname)

  lu.assertEquals(
    export_security_config.buildWpaPskFile(self.config),
    [[00:00:00:00:00:00 psk_test
aa:bb:cc:dd:ee:ff totally_secure_passphrase]]
  )
end

--- Test wsec disabled (wsecEnable = 0).
function TestMain:testWsecDisabled()
  self.config.radioParamsBase.fwParams.wsecEnable = 0

  local success, wpaSupplicantConfig, hostapdConfig =
    export_security_config.buildWsecConfigs(
      self.config, self.wirelessIfname, self.macaddr
    )
  lu.assertFalse(success)
end

--- Test wired security enabled
function TestMain:testWiredSecurityEnable()
  self.config.cpeConfig.TenGigabitEthernet0.wiredSecurityEnable = true

  local success, hostapdConfig, actionConfig =
    export_security_config.buildWiredSecurityConfigs(
      self.config, self.wiredIfname, self.macaddr
    )
  lu.assertTrue(success)
  lu.assertIsString(actionConfig)
  lu.assertStrContains(
    actionConfig,
    "vppctl set interface input acl intfc TenGigabitEthernet0 ip6-table 0"
  )
  lu.assertStrContains(
    actionConfig,
    "vppctl set interface input acl intfc TenGigabitEthernet0 ip4-table 0"
  )
  lu.assertStrContains(
    actionConfig,
    "vppctl set interface input acl intfc TenGigabitEthernet0 l2-table 0"
  )
  lu.assertStrContains(
    actionConfig,
    "vppctl set interface eapol-only off TenGigabitEthernet0"
  )
  lu.assertIsString(hostapdConfig)
  lu.assertStrContains(hostapdConfig, "driver=wired")
  lu.assertStrContains(hostapdConfig, "auth_server_addr=::1")
  lu.assertStrContains(hostapdConfig, "auth_server_port=1812")
  lu.assertStrContains(
    hostapdConfig,
    string.format("nas_identifier=%s-%s", self.wiredIfname, self.macaddr)
  )
end

--- Test wired security disabled
function TestMain:testWiredSecurityDisabled()
  self.config.cpeConfig.TenGigabitEthernet0.wiredSecurityEnable = false

  local success, hostapdConfig, actionConfig =
    export_security_config.buildWiredSecurityConfigs(
      self.config, self.wiredIfname, self.macaddr
    )
  lu.assertFalse(success)
end

--- Test execution with malformed config.
function TestMain:testBadConfig()
  -- Empty config
  local success, wpaSupplicantConfig, hostapdConfig =
    export_security_config.buildWsecConfigs(
      {}, self.wirelessIfname, self.macaddr
    )
  lu.assertFalse(success)

  -- Bad wsecEnable value
  self.config.radioParamsBase.fwParams.wsecEnable = 123
  success, wpaSupplicantConfig, hostapdConfig =
    export_security_config.buildWsecConfigs(
      self.config, self.wirelessIfname, self.macaddr
    )
  lu.assertFalse(success)

  -- Unexpected config format
  local badConfig = {radioParamsBase = "????"}
  success, wpaSupplicantConfig, hostapdConfig =
    export_security_config.buildWsecConfigs(
      badConfig, self.wirelessIfname, self.macaddr
    )
  lu.assertFalse(success)
end

os.exit(lu.LuaUnit.run("-v"))
