-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Terragraph techsupport functions
-- @module tg.techsupport

-- NOTE: These checks are listed in the preferred order of component checking.
-- For example, if `e2e_minion` is not up, there is no point checking `vpp`.
-- (excluding utility functions, which are placed below the check functions)

local tg_utils = require "tg.utils"
local logger = require "tg.logger"

local techsupport = {}

--- Returns if e2e_minion is healthy
function techsupport.check_e2e_minion()
  logger.info("--> Checking if `e2e_minion` is running")
  local out, ret, code = tg_utils.exec("/bin/ps -ef")
  if out == nil then
    logger.info("  - ps -ef FAILED (returned %d)", code)
    return false
  end

  -- This could need to change if we change args passed in SV / SystemD
  if out:match("/usr/sbin/e2e_minion %-node_config_file") then
    logger.info("  + Found a running e2e_minion process")
    return true
  end

  logger.info("  - NO e2e_minion running")
  return false
end

--- Returns if vpp is healthy
function techsupport.check_vpp()
  logger.info("--> Checking if `vpp` has terra interfaces")
  local out, ret, code = tg_utils.exec("/usr/bin/vppctl show interface")
  if out == nil then
    logger.info("  - `vpp show interface` FAILED (returned %d)", code)
    return false
  end

  if out:match("vpp%-terra") then
    logger.info("  + Found vpp-terra interfaces, VPP seems healthy")
    return true
  end

  logger.info("  - VPP has NO vpp-terra interfaces")
  return false
end

--- Returns if linux has a default route + responds to ping6
function techsupport.check_linux_default_route()
  logger.info("--> Checking if Linux has a default over vnet0")
  local cmd = "/sbin/ip -6 route show"
  local out, ret, code = tg_utils.exec(cmd)
  if out == nil then
    logger.info("  - %s FAILED (returned %d)", cmd, code)
    return false
  end

  -- Check if default we want exists
  if not out:match("default via fe80::1 dev vnet0") then
    logger.info("  - No default route over vnet0 via fe80::1 found")
    return false
  end
  logger.info("  + Linux has a default route to vnet0 via fe80::1")

  -- Run ping check
  logger.info("--> Checking if Linux can ping VPP over vnet0")
  cmd = "/bin/ping6 -c 2 -W 0.5 -i 0.1 fe80::1%vnet0"
  out, ret, code = tg_utils.exec(cmd)
  if out == nil then
    logger.info("  - %s FAILED (returned %d)", cmd, code)
    return false
  end
  logger.info("  + Linux can ping VPP over vnet0 to fe80::1")
  return true
end

--- Returns if openr is healthy
function techsupport.check_openr()
  -- TODO: Add thrift calls to check health + adjs
  -- Maybe steal parts from `breeze tech-support`

  logger.info("--> Checking if `openr` is running")
  local out, ret, code = tg_utils.exec("/bin/ps -ef")
  if out == nil then
    logger.info("  - ps -ef FAILED (returned %d)", code)
    return false
  end

  -- This could need to change if we change args passed in SV / SystemD
  if out:match("openr[^\n]+ %-%-config ") then
    logger.info("  + Found a running openr process")
    return true
  end

  logger.info("  - NO openr running")
  return false
end

function techsupport.check_fib_vpp()
  -- TODO: Add thrift calls to check FIB list returns cleanly
  -- Do we want to error on 0 programmed routes?

  logger.info("--> Checking if `fib_vpp` is running")
  local out, ret, code = tg_utils.exec("/bin/ps -ef")
  if out == nil then
    logger.info("  - ps -ef FAILED (returned %d)", code)
    return false
  end

  if out:match("/usr/sbin/fib_vpp") then
    logger.info("  + Found a running fib_vpp process")
    return true
  end

  logger.info("  - NO fib_vpp running")
  return false
end

-- Utility Functions

--- Check node_config to see if we support configuration
function techsupport.supported(node_config)
  -- Only support VPP mode (Puma default)
  -- TODO(wishlist): Add Linux mode support
  local config = tg_utils.readJsonFile(node_config)
  if config == nil then
    return false
  end

  local e2eEnabled = tg_utils.get(
    config, "envParams", "E2E_ENABLED"
  )
  local dpdkVppEnabled = tg_utils.get(
    config, "envParams", "DPDK_ENABLED"
  )

  return e2eEnabled == "1" and dpdkVppEnabled == "1"
end

--- Print component versions to stdout
function techsupport.printVersion(print_uboot)
  -- Uboot version getting is slow - So make optional
  print_uboot = print_uboot or false

  logger.info(
    "Terragraph Version:\n%s", tg_utils.readFile("/etc/tgversion") or "unknown"
  )

  local out, ret, code = tg_utils.exec("/usr/bin/vppctl show version")
  logger.info("Switch Version:\n%s", out or "unknown")

  out, ret, code = tg_utils.exec("/usr/bin/lspci")
  local wireless_vendor = "Qualcomm"
  if not string.match(out, wireless_vendor) then
    wireless_vendor = "Unknown"
  end
  logger.info("Wireless Vendor:\n%s\n", wireless_vendor)

  out, ret, code = tg_utils.exec("/usr/sbin/get_fw_version")
  logger.info("Wireless Firmware Version:\n%s", out)

  if not print_uboot then
    return
  end

  out, ret, code = tg_utils.exec("/usr/bin/strings /dev/mtd0")
  local uboot_version = "unknown"
  for line in string.gmatch(out, "(.-)\n") do
    if string.match(line, "Terragraph Uboot Version") then
      uboot_version = line
      break
    end
  end
  logger.info("U-Boot Version:\n%s", uboot_version)
end

return techsupport
