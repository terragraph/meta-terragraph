-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Routing family netlink library.
-- @module tg.rtnl

local Reader = require "tg.Reader"
local tg_net_utils = require "tg.net_utils"
local logger = require "tg.logger"
local tablex = require "pl.tablex"

local rtnl = {}

--- Routing family netlink message types
rtnl.RtmType = {
  RTM_NEWLINK = 16,
  RTM_DELLINK = 17,
  RTM_GETLINK = 18,
  RTM_SETLINK = 19,
  RTM_NEWADDR = 20,
  RTM_DELADDR = 21,
  RTM_GETADDR = 22,
  RTM_NEWROUTE = 24,
  RTM_DELROUTE = 25,
  RTM_GETROUTE = 26,
  RTM_NEWNEIGH = 28,
  RTM_DELNEIGH = 29,
  RTM_GETNEIGH = 30,
  RTM_NEWRULE = 32,
  RTM_DELRULE = 33,
  RTM_GETRULE = 34,
  RTM_NEWQDISC = 36,
  RTM_DELQDISC = 37,
  RTM_GETQDISC = 38,
  RTM_NEWTCLASS = 40,
  RTM_DELTCLASS = 41,
  RTM_GETTCLASS = 42,
  RTM_NEWTFILTER = 44,
  RTM_DELTFILTER = 45,
  RTM_GETTFILTER = 46,
  RTM_NEWACTION = 48,
  RTM_DELACTION = 49,
  RTM_GETACTION = 50,
  RTM_NEWPREFIX = 52,
  RTM_GETMULTICAST = 58,
  RTM_GETANYCAST = 62,
  RTM_NEWNEIGHTBL = 64,
  RTM_GETNEIGHTBL = 66,
  RTM_SETNEIGHTBL = 67,
  RTM_NEWNDUSEROPT = 68,
  RTM_NEWADDRLABEL = 72,
  RTM_DELADDRLABEL = 73,
  RTM_GETADDRLABEL = 74,
  RTM_GETDCB = 78,
  RTM_SETDCB = 79,
  RTM_NEWNETCONF = 80,
  RTM_DELNETCONF = 81,
  RTM_GETNETCONF = 82,
  RTM_NEWMDB = 84,
  RTM_DELMDB = 85,
  RTM_GETMDB = 86,
  RTM_NEWNSID = 88,
  RTM_DELNSID = 89,
  RTM_GETNSID = 90,
  RTM_NEWSTATS = 92,
  RTM_GETSTATS = 94,
  RTM_NEWCACHEREPORT = 96,
  RTM_NEWLINKPROP = 108,
  RTM_DELLINKPROP = 109,
  RTM_GETLINKPROP = 110,
}

--- Routing family netlink attribute types
--
-- NOTE: Skipping "RTA_UNSPEC" (0) to hide 1-indexed arrays.
rtnl.RtaType = {
  {"RTA_DST",        "addr"},
  {"RTA_SRC",        "addr"},
  {"RTA_IIF",        "int"},
  {"RTA_OIF",        "int"},
  {"RTA_GATEWAY",    "addr"},
  {"RTA_PRIORITY",   "int"},
  {"RTA_PREFSRC",    "addr"},
  {"RTA_METRICS",    "int"},
  {"RTA_MULTIPATH",  nil},
  {"RTA_PROTOINFO",  nil},
  {"RTA_FLOW",       "int"},
  {"RTA_CACHEINFO",  nil},
  {"RTA_SESSION",    nil},
  {"RTA_MP_ALGO",    nil},
  {"RTA_TABLE",      "int"},
  {"RTA_MARK",       "int"},
  {"RTA_MFC_STATS",  nil},
  {"RTA_VIA",        nil},
  {"RTA_NEWDST",     "addr"},
  {"RTA_PREF",       "byte"},
  {"RTA_ENCAP_TYPE", "word"},
  {"RTA_ENCAP",      nil},
  {"RTA_EXPIRES",    "int"},
}

--- Parse a single rtnetlink message from the given data (byte array) and
-- return the data as a table.
function rtnl.parse(data)
  -- Message is expected to be little-endian
  local reader = Reader.new(data, true)
  local t = {header = {}, attrs = {}}

  -- Netlink header (struct nlmsghdr)
  t.header.length = reader:readUInt()
  t.header.type = reader:readUWord()
  t.header.flags = reader:readUWord()
  t.header.seq = reader:readUInt()
  t.header.pid = reader:readUInt()

  -- Message-specific data
  if t.header.type == rtnl.RtmType.RTM_NEWROUTE or
     t.header.type == rtnl.RtmType.RTM_DELROUTE or
     t.header.type == rtnl.RtmType.RTM_GETROUTE then
    -- struct rtmsg
    t.family = reader:readByte()
    t.dst_len = reader:readByte()
    t.src_len = reader:readByte()
    t.tos = reader:readByte()
    t.table = reader:readByte()
    t.protocol = reader:readByte()
    t.scope = reader:readByte()
    t.type = reader:readByte()
    t.flags = reader:readUInt()
  else
    logger.warning(
      "Skipping unimplemented rtnetlink message type: %s", t.header.type
    )
    return t
  end

  -- Attributes (struct rtattr)
  while reader:position() < t.header.length + 1 do
    local alen = reader:readUWord()
    local atype = reader:readUWord()
    local dataLen = alen - 4
    local rtaType = rtnl.RtaType[atype] or {}

    local key = rtaType[1] or string.format("unknown-%d", tablex.size(#t.attrs))
    local val

    if rtaType[2] == "int" then
      val = reader:readInt()
    elseif rtaType[2] == "word" then
      val = reader:readWord()
    elseif rtaType[2] == "byte" then
      val = reader:readByte()
    elseif rtaType[2] == "addr" then
      local adata = reader:readBytes(dataLen)
      local strBytes = tablex.imap(function(v) return string.char(v) end, adata)
      val = tg_net_utils.binaryAddressToString(table.concat(strBytes))
    else
      val = reader:readBytes(dataLen)
    end

    t.attrs[key] = val
  end

  return t
end

return rtnl
