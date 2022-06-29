-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Thrift-related utilities.
-- @module tg.thrift_utils

require "TMemoryBuffer"
require "TCompactProtocol"
require "TBinaryProtocol"
require "TFramedTransport"
require "TSocket"
require "liblualongnumber"
require "tg.TFakeSimpleJSONProtocol"
local cjson_safe = require "cjson.safe"
-- encode all arrays (default is false for "excessively sparse")
cjson_safe.encode_sparse_array(true, 1)
local prettycjson = require "prettycjson"

local thrift_utils = {}

--- Serialize a Thrift structure to JSON (in `SimpleJSONProtocol` format).
function thrift_utils.thriftToJson(thriftData)
  -- HACK! There's no SimpleJSONSerializer, so just convert the object to JSON
  -- directly after stripping unencodable/internal fields.
  -- TODO: This may fail in the following cases:
  --   Numbers might wrongly encode as strings (if not representable in Lua)
  --   Tables might wrongly encode as lists (if keys are consecutive integers)
  local function deepcopy(obj, seen)
    if type(obj) ~= "table" then return obj end
    if seen and seen[obj] then return seen[obj] end
    local s = seen or {}
    local res = setmetatable({}, getmetatable(obj))
    s[obj] = res
    for k, v in pairs(obj) do
      -- Remove __TObject fields etc.
      if k ~= "__parent" and k ~= "__type" and k ~= "__mt" and
         type(v) ~= "function" then
        -- If userdata can be stringified, do it now
        local skip = false
        if type(v) == "userdata" then
          local mt = getmetatable(v)
          if mt ~= nil and type(mt.__tostring) == "function" then
            -- If userdata represents a number (e.g. lualongnumber), return it
            -- in number form when possible, not as a string
            local vNumeric = thrift_utils.lualongnumberToNumber(v)
            if vNumeric ~= nil then
              v = vNumeric
            else
              v = tostring(v)
            end
          else
            skip = true
          end
        end
        if not skip then
          res[deepcopy(k, s)] = deepcopy(v, s)
        end
      end
    end
    return res
  end
  local stripped = deepcopy(thriftData)
  return prettycjson(stripped, "\n", "  ", " ", cjson_safe.encode)
end

--- Deserialize a Thrift structure from JSON (in `SimpleJSONProtocol` format).
function thrift_utils.thriftFromJson(json, thriftData)
  local proto = TFakeSimpleJSONProtocolFactory:getProtocol(json)
  thriftData:read(proto)
end

--- Convert a `liblualongnumber` instance to number form.
--
-- If it is not precisely representable as a Lua (double-precision) number,
-- this returns nil unless `allowImprecise` is set.
--
-- Example:
--    x = liblualongnumber.new("9007199254740993")
--    lualongnumberToNumber(x, false)  => nil
--    lualongnumberToNumber(x, true)   => 9007199254740992
function thrift_utils.lualongnumberToNumber(x, allowImprecise)
  local s = tostring(x)
  local l = tonumber(s)
  if l ~= nil and (allowImprecise or s == string.format("%.0f", l)) then
    return l
  else
    return nil
  end
end

--- Compare two numbers, which can be either primitives or `liblualongnumber`
-- instances or a mix.
--
-- The Thrift v0.12.0 implementation of `liblualongnumber` does not do type
-- conversion on primitive numbers for equality in Lua 5.2, and also does not
-- support comparators on primitive numbers in Lua 5.1.
--
-- Returns 0 if `a == b`, -1 if `a < b`, and 1 if `a > b`.
function thrift_utils.lualongnumberCmp(a, b)
  local aa = type(a) == "userdata" and a or liblualongnumber.new(a)
  local bb = type(b) == "userdata" and b or liblualongnumber.new(b)
  if aa == bb then
    return 0
  elseif aa < bb then
    return -1
  else
    return 1
  end
end

--- Convert a Lua array to a Thrift set.
function thrift_utils.listToThriftSet(l)
  local ret = {}
  for _, x in ipairs(l) do
    ret[tostring(x)] = x
  end
  return ret
end

--- Serialize a Thrift structure using `TCompactProtocol`.
function thrift_utils.serialize(thriftData)
  TMemoryBuffer:resetBuffer("")
  local proto = TCompactProtocolFactory:getProtocol(TMemoryBuffer)
  thriftData:write(proto)
  return TMemoryBuffer:getBuffer()
end

--- Deserialize a Thrift structure using `TCompactProtocol`.
function thrift_utils.deserialize(thriftDataIn, thriftDataOut)
  TMemoryBuffer:resetBuffer(thriftDataIn)
  local proto = TCompactProtocolFactory:getProtocol(TMemoryBuffer)
  thriftDataOut:read(proto)
end

--- Return a Thrift exception as a string.
--
-- This expects a `TException` argument, but also works on other data types
-- (such as strings for when `terror()` is called unexpectedly).
function thrift_utils.exceptionStr(ex)
  if type(ex) == "table" and ex.__tostring then
    return ex:__tostring()
  else
    return tostring(ex)
  end
end

--- Create a Thrift client using `TFramedTransport` and `TBinaryProtocol`.
--
-- Return the (client, nil) upon success, or (nil, errorStr) on error.
function thrift_utils.createClient(clientClass, host, port, timeout)
  -- Create the socket
  local socket = TSocket:new{host = host, port = port}
  if socket == nil then
    return nil, "Failed to create client socket"
  end
  socket:setTimeout(timeout or 5000)

  -- Create the transport and protocol
  local transport = TFramedTransport:new{trans = socket, isServer = false}
  local protocol = TBinaryProtocol:new{trans = transport}
  if protocol == nil then
    return nil, "Failed to create protocol"
  end

  -- Create the client
  local client = clientClass:new{protocol = protocol}
  if client == nil then
    return nil, "Failed to create client"
  end

  -- Open the transport
  local status, err = pcall(transport.open, transport)
  if not status then
    return nil, err
  end

  return client, nil
end

return thrift_utils
