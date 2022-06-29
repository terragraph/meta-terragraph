-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Terragraph configuration utilities.
-- @module tg.config_utils

local tg_utils = require "tg.utils"
local logger = require "tg.logger"
local path = require "pl.path"
local dir = require "pl.dir"
require("pl.stringx").import()

local config_utils = {}

local BASE_CONFIG_DIR = "/etc/e2e_config/base_versions/"
local HW_BASE_CONFIG_DIR = BASE_CONFIG_DIR .. "hw_versions/"
local HW_CONFIG_TYPES_FILE = HW_BASE_CONFIG_DIR .. "hw_types.json"
local FW_BASE_CONFIG_DIR = BASE_CONFIG_DIR .. "fw_versions/"

--- Returns the base config file for the latest release.
--
-- This function compares file names using natural sort order.
function config_utils.getLatestBaseConfigFile(files)
  if #files == 0 then
    return nil
  end

  -- Split version string by numbers
  local verSplit = function(s)
    -- Return 0 for invalid table lookups
    local t = setmetatable({}, {__index = function(t, k) return 0 end})
    for w in string.gmatch(s, "%d+") do
      t[#t+1] = tonumber(w)
    end
    return t
  end

  -- Compare version strings
  local verComp = function(a, b)
    local aVer, bVer = verSplit(path.basename(a)), verSplit(path.basename(b))
    for i = 1, math.max(#aVer, #bVer) do
      if aVer[i] < bVer[i] then
        return true
      elseif aVer[i] > bVer[i] then
        return false
      end
    end
    return a < b
  end

  -- Sort files
  table.sort(files, verComp)

  -- Return last in list
  return files[#files]
end

--- Returns (release name, major version, minor version) for a Terragraph
-- software version.
function config_utils.parseReleaseVersion(s)
  -- Try MAJOR_MINOR form first
  local m = {s:match("(RELEASE_M(%d+)_(%d+))")}
  if #m == 3 then
    return m[1], tonumber(m[2]), tonumber(m[3])
  end
  -- Try MAJOR form next
  m = {s:match("(RELEASE_M(%d+))")}
  if #m == 2 then
    return m[1], tonumber(m[2]), 0
  end
  return s:strip(), 0, 0
end

--- Returns a map of hardware board IDs to hardware config types.
--
-- If the map file could not be read, returns nil.
function config_utils.getHardwareTypesMap()
  local hwTypes = tg_utils.readJsonFile(HW_CONFIG_TYPES_FILE)
  if hwTypes == nil then
    return nil
  end
  local d = {}
  for hwType, hwBoardIds in pairs(hwTypes) do
    for _, hwBoardId in ipairs(hwBoardIds) do
      d[hwBoardId] = hwType
    end
  end
  return d
end

--- Returns the hardware-based config subdirectory for the given type.
--
-- If no match is found, returns nil.
function config_utils.getHardwareBasePath(directory, type)
  local hwTypesMap = config_utils.getHardwareTypesMap()
  if next(hwTypesMap) ~= nil and hwTypesMap[type] ~= nil then
    local filePath = path.join(directory, hwTypesMap[type])
    if path.isdir(filePath) then
      return filePath
    end
  end
  return nil
end

--- Returns this machine's hardware board ID, or nil upon error.
function config_utils.getHardwareBoardId()
  local output, ret, code = tg_utils.exec("/usr/sbin/get_hw_info HW_BOARD_ID")
  if output == nil then
    return nil
  else
    return output:strip()
  end
end

--- Returns the firmware-based config file for the given version.
--
-- If no match is found, returns nil.
--
-- Criteria:
--
-- 1. Major versions *must* match.
-- 2. Select the closest minor version *not greater than* the given version.
function config_utils.getFirmwareBaseFile(files, version)
  if #files == 0 then
    return nil
  end

  local major, minor = config_utils.getFirmwareMajorMinorVersion(version)
  local bestFile = nil
  local bestMinor = -1
  for _, f in ipairs(files) do
    local ver, _ext = path.splitext(path.basename(f))
    local maj, min = config_utils.getFirmwareMajorMinorVersion(ver)
    if maj == major then
      if min == minor then
        -- exact match, return now
        return f
      elseif min < minor and min >= bestMinor then
        -- majors match, find closest minor < node's minor
        bestFile = f
        bestMinor = min
      end
    end
  end
  return bestFile
end

--- Returns a (major, minor) version pair for the given version string.
function config_utils.getFirmwareMajorMinorVersion(version)
  local tokens = version:split(".")
  if #tokens == 4 and tonumber(tokens[4]) ~= nil then
    return ("."):join({tokens[1], tokens[2], tokens[3]}), tonumber(tokens[4])
  else
    return version, 0
  end
end

--- Returns this machine's wireless firmware version, or nil upon error.
function config_utils.getFirmwareVersion()
  local output, ret, code = tg_utils.exec("/usr/sbin/get_fw_version")
  if output == nil then
    return nil
  else
    return output:strip()
  end
end

--- Merges base config layers, returning merged result.
function config_utils.getMergedBaseConfig(release)
  -- Read base config
  local baseConfigFile
  if not release then
    baseConfigFile =
      config_utils.getLatestBaseConfigFile(dir.getfiles(BASE_CONFIG_DIR))
  else
    baseConfigFile = path.join(BASE_CONFIG_DIR, release)
  end
  logger.info("Using base config file: %s", baseConfigFile)
  local baseConfig = tg_utils.readJsonFile(baseConfigFile) or {}

  -- Read firmware base config
  local fwVersion = config_utils.getFirmwareVersion()
  if fwVersion then
    logger.info("Current firmware version: %s", fwVersion)
    local fwBaseConfigFile = config_utils.getFirmwareBaseFile(
      dir.getfiles(FW_BASE_CONFIG_DIR), fwVersion
    )
    if fwBaseConfigFile ~= nil then
      logger.info("Using firmware base config file: %s", fwBaseConfigFile)
      local fwBaseConfig = tg_utils.readJsonFile(fwBaseConfigFile) or {}
      tg_utils.tableMerge(baseConfig, fwBaseConfig)
    else
      logger.warning("No matching firmware base config found")
    end
  else
    logger.warning("Could not read firmware version")
  end

  -- Read hardware base config
  local hwBoardId = config_utils.getHardwareBoardId()
  if hwBoardId then
    logger.info("Current hardware board ID: %s", hwBoardId)
    local hwBaseConfigDir =
      config_utils.getHardwareBasePath(HW_BASE_CONFIG_DIR, hwBoardId)
    if hwBaseConfigDir ~= nil then
      local hwBaseConfigFile
      if not release then
        hwBaseConfigFile =
          config_utils.getLatestBaseConfigFile(dir.getfiles(hwBaseConfigDir))
      else
        hwBaseConfigFile = path.join(hwBaseConfigDir, release)
      end
      logger.info("Using hardware base config file: %s", hwBaseConfigFile)
      local hwBaseConfig = tg_utils.readJsonFile(hwBaseConfigFile) or {}
      tg_utils.tableMerge(baseConfig, hwBaseConfig)
    else
      logger.warning("No matching hardware base config found")
    end
  else
    logger.warning("Could not read hardware board ID")
  end

  return baseConfig
end

return config_utils
