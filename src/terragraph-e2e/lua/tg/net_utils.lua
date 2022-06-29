-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Network-related utilities.
-- @module tg.net_utils

local tablex = require "pl.tablex"
require("pl.stringx").import()

local net_utils = {}

--- Return whether the given string is a valid IPv4 address.
function net_utils.isIPv4(s)
  assert(type(s) == "string")

  local m = {s:match("^(%d+)%.(%d+)%.(%d+)%.(%d+)$")}
  if #m ~= 4 then
    return false
  end
  for _, i in ipairs(m) do
    if tonumber(i) > 255 then
      return false
    end
  end
  return true
end

--- Return whether the given string is a valid IPv6 address.
--
-- This does NOT do strict checking and should only be used as an approximation.
function net_utils.isIPv6(s)
  assert(type(s) == "string")

  local addr = net_utils.expandIPv6(s)
  local m = {addr:match("^" .. ("([A-Fa-f0-9]*):"):rep(8):gsub(":$", "$"))}
  if #m == 8 then
    for _, i in ipairs(m) do
      if i:len() > 0 and tonumber(i, 16) > 65535 then
        return false
      end
    end
    return true
  else
    return false
  end
end

--- Abbreviate an IPv6 address. This will not trim any leading zeros.
function net_utils.abbreviateIPv6(s)
  assert(type(s) == "string")

  -- NOTE: code is based on CPython's ipaddress._compress_hextets()
  local hextets = s:split(":")
  local bestDoubleColonStart = -1
  local bestDoubleColonLen = 0
  local doubleColonStart = -1
  local doubleColonLen = 0
  for index, hextet in ipairs(hextets) do
    if hextet:len() == 0 or tonumber(hextet, 16) == 0 then
      doubleColonLen = doubleColonLen + 1
      if doubleColonStart == -1 then
        doubleColonStart = index
      end
      if doubleColonLen > bestDoubleColonLen then
        bestDoubleColonLen = doubleColonLen
        bestDoubleColonStart = doubleColonStart
      end
    else
      doubleColonLen = 0
      doubleColonStart = -1
    end
  end
  if bestDoubleColonLen > 1 then
    local bestDoubleColonEnd = bestDoubleColonStart + bestDoubleColonLen
    if bestDoubleColonEnd == #hextets + 1 then
      hextets[#hextets+1] = ""
    end
    for i = 1, bestDoubleColonLen do
      table.remove(hextets, bestDoubleColonStart)
    end
    table.insert(hextets, bestDoubleColonStart, "")
    if bestDoubleColonStart == 1 then
      table.insert(hextets, 1, "")
    end
  end
  return (":"):join(hextets)
end

--- Expand an IPv6 address. This will not trim any leading zeros.
--
-- If the argument is not a valid IPv6 address, behavior is undefined.
function net_utils.expandIPv6(s)
  assert(type(s) == "string")

  local hextets = s:split(":")
  local abbreviated = s:match("::") and not s:gsub("::", "", 1):match("::")
  if abbreviated then
    local doubleColonIdx = tablex.find(hextets, "")
    if doubleColonIdx ~= nil then
      local missingHextets = 8 - #hextets
      for i = 1, missingHextets do
        table.insert(hextets, doubleColonIdx, "")
      end
    end
  end
  tablex.transform(function(v) return v == "" and "0" or v end, hextets)
  return (":"):join(hextets)
end

--- Convert a binary IP address to its string representation.
function net_utils.binaryAddressToString(s)
  assert(type(s) == "string")

  local t = {}
  if #s == 16 then
    for cc in s:gmatch("..") do
      local i = bit32.lshift(cc:sub(1, 1):byte(), 8) + cc:sub(2, 2):byte()
      t[#t+1] = string.format("%x", i)
    end
    return net_utils.abbreviateIPv6((":"):join(t))
  elseif #s == 4 then
    for c in s:gmatch(".") do
      t[#t+1] = c:byte()
    end
    return ("."):join(t)
  else
    return s
  end
end

--- Convert a string IP address to its binary representation.
function net_utils.stringToBinaryAddress(s)
  assert(type(s) == "string")

  local t = {}
  if net_utils.isIPv4(s) then
    for _, part in ipairs(s:split(".")) do
      t[#t+1] = string.char(tonumber(part))
    end
    return table.concat(t)
  elseif net_utils.isIPv6(s) then
    local hextets = net_utils.expandIPv6(s):split(":")
    for _, hextet in ipairs(hextets) do
      local i = tonumber(hextet, 16)
      t[#t+1] = string.char(bit32.rshift(i, 8))
      t[#t+1] = string.char(bit32.band(i, 255))
    end
    return table.concat(t)
  else
    return s
  end
end

--- Return whether the given string represents a valid IPv4 network (in strict
-- CIDR notation, e.g. "192.168.100.14/24").
function net_utils.isIPv4Network(s)
  local tokens = s:split("/")
  if #tokens < 2 then
    return false
  end
  if not net_utils.isIPv4(tokens[1]) then
    return false
  end
  local bits = tonumber(tokens[2])
  if bits == nil or bits < 0 or bits > 32 then
    return false
  end
  return true
end

--- Return whether the given string represents a valid IPv6 network (in strict
-- CIDR notation, e.g. "2001:db8::/48").
function net_utils.isIPv6Network(s)
  local tokens = s:split("/")
  if #tokens < 2 then
    return false
  end
  if not net_utils.isIPv6(tokens[1]) then
    return false
  end
  local bits = tonumber(tokens[2])
  if bits == nil or bits < 0 or bits > 128 then
    return false
  end
  return true
end

--- Return whether the given string is a valid MAC address.
--
-- This must strictly be in two-digit, lowercase, colon-separated format,
-- e.g. "01:23:45:67:89:ab".
function net_utils.isMacAddr(s)
  assert(type(s) == "string")

  return (s:match("^" .. ("[a-f0-9][a-f0-9]:"):rep(6):gsub(":$", "$")) ~= nil)
end

--- Return whether the given IPv6 string is within an IPv6 network.
--
-- If the arguments are not valid IPv6 address/network, behavior is undefined.
function net_utils.isAddrInIPv6Network(ipv6Str, networkStr)
  -- Split network CIDR notation into IPv6 prefix and mask length
  local prefixStr, maskStr = table.unpack(networkStr:split("/"))

  -- Split hextets and convert to number
  local ipv6 = tablex.imap(
    tonumber,
    net_utils.expandIPv6(ipv6Str):split(":"),
    16
  )
  local prefix = tablex.imap(
    tonumber,
    net_utils.expandIPv6(prefixStr):split(":"),
    16
  )

  -- Compare each hextet starting by MSB until all prefix length is checked
  local remainingLen = tonumber(maskStr)
  for i = 1, 8 do
    local result = bit32.bxor(ipv6[i], prefix[i])
    local mask = bit32.lshift(0xFFFF, math.max(0, 16 - remainingLen))
    if bit32.btest(result, mask) then
      return false
    end

    remainingLen = remainingLen - 16
    if remainingLen <= 0 then
      break
    end
  end
  return true
end

--- Return whether the given IPv6 prefix is within an IPv6 network.
--
-- For a prefix to be within a network, all possible IP addresses within this
-- prefix must also be within the network range. If the arguments are not valid
-- IPv6 prefix/network in CIDR notation, behavior is undefined.
function net_utils.isPrefixInIPv6Network(prefixStr, networkStr)
  local networkMask = networkStr:split("/")[2]
  local prefixAddress, prefixMask = table.unpack(prefixStr:split("/"))
  -- If prefix length is shorter than network length, it can't be contained
  if tonumber(prefixMask) < tonumber(networkMask) then
    return false
  end
  return net_utils.isAddrInIPv6Network(prefixAddress, networkStr)
end

--- Return whether an IPv6 address in string format is link-local.
function net_utils.isAddrLinkLocal(ipv6Str)
  return net_utils.isAddrInIPv6Network(ipv6Str, "fe80::/10")
end

return net_utils
