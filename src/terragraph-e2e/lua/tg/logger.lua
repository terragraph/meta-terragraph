-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Simple logging module.
-- @module tg.logger

local hasSyslog, syslog = pcall(require, "posix.syslog")
local syslogEnabled = false

local logger = {
  level = "info",
  LEVELS = {
    "debug",
    "info",
    "warning",
    "error",
    "critical"
  },
  dateTimeEnabled = false,
  dateTimeFormat = "!%Y-%m-%dT%TZ",
}

local syslogLevels = {
  syslog.LOG_DEBUG,
  syslog.LOG_INFO,
  syslog.LOG_WARNING,
  syslog.LOG_ERR,
  syslog.LOG_CRIT
}

local levelIndices = {}
for i, level in ipairs(logger.LEVELS) do
  levelIndices[level] = i
end

--- Log a formatted message iff the message is at or above the set logging level
local function logAtlevel(level, ...)
  local loggerLevelIndex = levelIndices[logger.level]
  local levelIndex = levelIndices[level]
  local logEnabled = (levelIndex >= loggerLevelIndex)
  if not logEnabled and not syslogEnabled then
    return
  end

  -- Format the log line
  local logStr = ""
  if logger.dateTimeEnabled then
    local now = os.time()
    logStr = logStr ..
      string.format("%s: ", os.date(logger.dateTimeFormat, now))
  end

  local args = {...}
  if #args > 1 then
    logStr = logStr .. string.format(...)
  elseif #args == 1 then
    logStr = logStr .. tostring(args[1])
  else
    error("log function expected at least 1 argument")
  end

  -- Write log line
  if logEnabled then
    io.write(logStr, "\n")
    io.flush()
  end
  if syslogEnabled then
    syslog.syslog(syslogLevels[levelIndex], logStr)
  end
end

--- Start logging messages to syslog.
--
-- All messages will be written to syslog regardless of the configured logger
-- level.
--
-- Arguments are optional and passed to the `openlog()` syscall.
function logger.startSyslog(ident, option, facility)
  if not hasSyslog then
    error("posix.syslog (from luaposix) is required for syslog")
  end
  if ident == nil then
    -- use program name as identity, otherwise "lua" (default)
    if type(arg) == "table" then
      ident = arg[0]
    else
      ident = "lua"
    end
  end
  syslog.openlog(ident, option, facility)
  syslogEnabled = true
end

--- Stop logging messages to syslog.
--
-- Must be called after `startSyslog()` to close the opened file descriptor.
function logger.stopSyslog()
  if not hasSyslog then
    error("posix.syslog (from luaposix) is required for syslog")
  end
  syslogEnabled = false
  syslog.closelog()
end

--- Prepend timestamps to log messages.
--
-- - format: If given, override the default datetime format (see `os.date()`).
function logger.enableDateTimeLogging(format)
  if format ~= nil then
    logger.dateTimeFormat = format
  end
  logger.dateTimeEnabled = true
end

--- Log a formatted string at "debug" severity.
function logger.debug(...) logAtlevel("debug", ...) end

--- Log a formatted string at "info" severity.
function logger.info(...) logAtlevel("info", ...) end

--- Log a formatted string at "warning" severity.
function logger.warning(...) logAtlevel("warning", ...) end

--- Log a formatted string at "error" severity.
function logger.error(...) logAtlevel("error", ...) end

--- Log a formatted string at "critical" severity.
function logger.critical(...) logAtlevel("critical", ...) end

return logger
