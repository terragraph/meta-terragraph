-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Common utilities.
-- @module tg.utils

local cjson_safe = require "cjson.safe"
local prettycjson = require "prettycjson"
local lfs = require "lfs"
local stdio = require "posix.stdio"
local unistd = require "posix.unistd"
require("pl.stringx").import()

local utils = {}

--- Wrapper around `io.write(string.format(s, ...))`.
function utils.printf(s, ...)
  return io.write(s:format(...))
end

--- Check if this is a main function (through stack introspection).
function utils.isMain()
  return debug.getinfo(4) == nil
end

--- Abort the program by raising an error (interceptable by `pcall()`).
--
-- The stack trace will be hidden unless `printStackTrace` is set.
function utils.abort(message, printStackTrace, stackLevel)
  if printStackTrace then
    error(message or "", stackLevel or 2)
  else
    if message ~= nil then
      io.stderr:write(message .. "\n")
    end
    error(nil)
  end
end

--- Reads a file to a string, or returns nil upon failure.
function utils.readFile(filename)
  assert(type(filename) == "string")

  local f, err = io.open(filename, "rb")
  if f then
    local s = f:read("*a")
    f:close()
    return s
  else
    io.write(string.format("Error opening file %s: %s\n", filename, err))
    return nil
  end
end

--- Reads a JSON file to a table, or returns nil upon failure.
function utils.readJsonFile(filename)
  assert(type(filename) == "string")

  local s = utils.readFile(filename)
  if s == nil then
    return nil
  else
    local t, err = cjson_safe.decode(s)
    if t == nil then
      io.write(string.format("Error reading JSON file %s: %s\n", filename, err))
    end
    return t
  end
end

--- Writes a string to the given file, returning success or failure.
--
-- This uses lfs.lock() to lock the file first (only among lua processes).
function utils.writeFile(filename, s)
  assert(type(filename) == "string")
  assert(type(s) == "string")

  local f, err = io.open(filename, "w")
  if f then
    while not lfs.lock(f, "w") do end  -- busy wait
    f:write(s)
    f:flush()
    lfs.unlock(f)
    f:close()
    return true
  else
    io.write(string.format("Error opening file %s: %s\n", filename, err))
    return false
  end
end

--- Writes a table to the given file, returning success or failure.
--
-- This uses lfs.lock() to lock the file first (only among lua processes).
function utils.writeJsonFile(filename, t)
  assert(type(filename) == "string")
  assert(type(t) == "table")

  local json = prettycjson(t, "\n", "  ")
  return utils.writeFile(filename, json .. "\n")
end

--- Writes a string to the given file atomically, returning success or failure.
--
-- Specifically, this writes to a temp file, calls fsync(), then renames the
-- temp file.
function utils.writeFileAtomic(filename, s)
  assert(type(filename) == "string")
  assert(type(s) == "string")

  local tmpfile = filename .. "." .. os.time() .. ".tmp"
  local f, err = io.open(tmpfile, "w")
  if f then
    f:write(s)
    f:flush()
    local fd, filenoErr, filenoErrnum = stdio.fileno(f)
    if fd ~= nil then
      local fsyncRet, fsyncErr, fsyncErrnum = unistd.fsync(fd)
      if fsyncRet == nil then
        io.write(
          string.format("fsync() failed: %s (%d)\n", fsyncErr, fsyncErrnum)
        )
      end
    end
    f:close()
    local renameRet, renameErr = os.rename(tmpfile, filename)
    if renameRet == nil then
      io.write(string.format(
        "Failed to rename temp file %s to %s: %s\n",
        tmpfile, filename, renameErr
      ))
      os.remove(tmpfile)
      return false
    end
    return true
  else
    io.write(string.format("Error opening temp file %s: %s\n", tmpfile, err))
    return false
  end
end

--- Execute a shell command and return the response.
--
-- Upon failure, this returns (nil, "{exit|signal}", {number}) as given by
-- `os.execute()` unless `ignoreRetCode` is set.
function utils.exec(command, ignoreRetCode)
  assert(type(command) == "string")

  local fd = io.popen(command)
  local output = fd:read("*a")
  local success, ret, code = fd:close()
  if success == nil and not ignoreRetCode then
    return nil, ret, code
  else
    return output
  end
end

--- Retrieve a field from a table, handling nil at any level.
--
-- Example:
--    get(t, "x", "y", "z") => t[x][y][z]
function utils.get(t, field, ...)
  if t == nil or field == nil then
    return t
  else
    return utils.get(t[field], ...)
  end
end

--- Set a field in a table, handling nil at any level.
--
-- If an intermediate non-table value was found, returns false.
--
-- Example:
--    set(t, {"x", "y", "z"}, 123) => t[x][y][z] = 123
function utils.set(t, keyPath, value)
  if t == nil or type(t) ~= "table" or
     keyPath == nil or type(keyPath) ~= "table" or #keyPath < 1 then
    return false
  end
  local obj = t
  for i = 1, #keyPath - 1 do
    local k = keyPath[i]
    if obj[k] == nil then
      obj[k] = {}
    elseif type(obj[k]) ~= "table" then
      return false
    end
    obj = obj[k]
  end
  obj[keyPath[#keyPath]] = value
  return true
end

--- Recursively merge table `b` into `a`.
function utils.tableMerge(a, b)
  for k, v in pairs(b) do
    if type(v) == "table" and type(a[k] or false) == "table" then
      utils.tableMerge(a[k], v)
    else
      a[k] = v
    end
  end
end

--- Recursively merge table `b` into `a` without overwriting any keys in `a`.
--
-- Returns true if `a` was modified.
function utils.tableMergeAppend(a, b)
  local modified = false
  for k, v in pairs(b) do
    if type(a[k] or false) == "table" then
      modified = (utils.tableMergeAppend(a[k], v) or modified)
    elseif a[k] == nil then
      modified = true
      a[k] = v
    end
  end
  return modified
end

--- Recursively find entries in table `b` that are missing in table `a`.
--
-- This will NOT return keys with different values.
function utils.tableDifference(a, b)
  local result = {}
  for k, v in pairs(b) do
    if a[k] == nil then
      result[k] = v
    elseif type(a[k] or false) == "table" then
      local o = utils.tableDifference(a[k], v)
      if next(o) ~= nil then
        result[k] = o
      end
    end
  end
  return result
end

--- Return an inverted version of table `t`, where the keys and values are
-- swapped.
function utils.invertTable(t)
  local s = {}
  for k, v in pairs(t) do
    s[v] = k
  end
  return s
end

--- Returns the maximum value in the given table, or nil if the table has no
-- numerical values.
function utils.tableMax(t)
  local k = nil
  for _, i in ipairs(t) do
    if type(i) == "number" and (k == nil or i > k) then
      k = i
    end
  end
  return k
end

--- Return a common string form for a given Lua number, avoiding scientific
-- notation for large integers.
function utils.stringifyNumber(x)
  assert(type(x) == "number")

  if x == math.floor(x) then
    -- integer
    return string.format("%.f", x)
  else
    -- float
    return tostring(x)
  end
end

--- Return a human-readable representation of the given number of bytes.
function utils.formatBytes(bytes)
  if bytes < 1024 then
    return string.format("%.1f B", bytes)
  end
  bytes = bytes / 1024
  if bytes < 1024 then
    return string.format("%.1f KB", bytes)
  end
  bytes = bytes / 1024
  if bytes < 1024 then
    return string.format("%.1f MB", bytes)
  end
  bytes = bytes / 1024
  return string.format("%.1f GB", bytes)
end

--- Return a human-readable representation of a time interval in seconds.
function utils.formatTimeInterval(delta)
  if delta > 86400 then  -- 1 day
    local days = math.floor(delta / 86400)
    local hours = math.floor((delta % 86400) / 3600)
    return string.format("%dd%dh", days, hours)
  elseif delta > 3600 then -- 1 hour
    local hours = math.floor(delta / 3600)
    local minutes = math.floor((delta % 3600) / 60)
    return string.format("%dh%dm", hours, minutes)
  else
    local minutes = math.floor(delta / 60)
    local seconds = delta % 60
    return string.format("%dm%ds", minutes, seconds)
  end
end

--- Return a formatted GPS position string given a latitude/longitude.
function utils.formatPositionString(lat, lon)
  local latStr = lat >= 0 and (lat .. " N") or (-lat .. " S")
  local lonStr = lon >= 0 and (lon .. " E") or (-lon .. " W")
  return latStr .. " " .. lonStr
end

--- Read an environment file into a key-value table.
function utils.readEnvFile(filename)
  assert(type(filename) == "string")

  local s = utils.readFile(filename)
  if s == nil then
    return nil
  end
  local info = {}
  local lines = s:splitlines()
  for _, line in ipairs(lines) do
    if line:sub(1, 1) ~= "#" then  -- skip comments
      local parts = line:split("=")
      if #parts == 2 then
        info[parts[1]:strip()] = parts[2]:strip():replace("\"", "")
      end
    end
  end
  return info
end

--- Subtract two `struct timespec` tables and return the result (i.e. `a - b`).
function utils.timespecSub(a, b)
  assert(type(a) == "table")
  assert(type(b) == "table")

  local result = {tv_sec = a.tv_sec - b.tv_sec, tv_nsec = a.tv_nsec - b.tv_nsec}
  if result.tv_nsec < 0 then
    result.tv_sec = result.tv_sec - 1
    result.tv_nsec = result.tv_nsec + 1000000000
  end
  return result
end

return utils
