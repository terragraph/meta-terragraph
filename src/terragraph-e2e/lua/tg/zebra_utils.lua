-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Utilities related to the Zebra routing daemon.
-- @module tg.zebra_utils

local Reader = require "tg.Reader"
local tg_rtnl = require "tg.rtnl"
local tablex = require "pl.tablex"

local zebra_utils = {}

--- Length (in bytes) of an FPM (Forwarding Plane Manager) header.
zebra_utils.FPM_HEADER_LENGTH = 4

--- Parse an FPM header from the given data (byte array).
local function _parseFpmHeader(data)
  -- FPM header itself is big-endian, while rtnl body is little-endian
  local reader = Reader.new(data, false)
  local t = {}

  t.version = reader:readByte()
  t.type = reader:readByte()
  t.len = reader:readUWord()

  return t
end

--- Parse an FPM message, consisting of an FPM header followed by an rtnetlink
-- message, from the given data (byte array).
local function _parseMessage(data)
  local fpmHeader = _parseFpmHeader(data)
  local nlMessage = tg_rtnl.parse(
    tablex.sub(data, zebra_utils.FPM_HEADER_LENGTH + 1, -1)
  )

  return fpmHeader, nlMessage
end

--- Parse and return all FPM messages from the given data (byte array).
function zebra_utils.parse(message)
  local parts = {}

  -- Convert input string to byte array
  local msgdata = {string.byte(message, 1, -1)}

  -- Input data may contain multiple messages, so keep reading to the end
  while true do
    local fpmHeader, nlMessage = _parseMessage(msgdata)
    parts[#parts+1] = {fpm = fpmHeader, rtnl = nlMessage}
    if zebra_utils.FPM_HEADER_LENGTH + nlMessage.header.length >= #msgdata then
      break
    end
    msgdata = tablex.sub(
      msgdata, zebra_utils.FPM_HEADER_LENGTH + nlMessage.header.length + 1, -1
    )
  end

  return parts
end

return zebra_utils
