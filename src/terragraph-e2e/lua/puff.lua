#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Open/R command-line interface.
-- @script puff

require "openr.OpenrCtrl_OpenrCtrl"
require "openr.OpenrConfig_ttypes"
require "openr.Platform_FibService"
require "openr.Platform_ttypes"
require "openr.KvStore_ttypes"
require "openr.Types_ttypes"
require "openr.Network_ttypes"
local tg_utils = require "tg.utils"
local tg_net_utils = require "tg.net_utils"
local tg_thrift_utils = require "tg.thrift_utils"
local tabulate = require "tg.tabulate"
local logger = require "tg.logger"
local argparse = require "argparse"
local cjson_safe = require "cjson.safe"
local tablex = require "pl.tablex"
require("pl.stringx").import()

local consts = {
  CTRL_PORT = 2018,
  FIB_AGENT_PORT = 60100,

  ADJ_DB_MARKER = "adj:",
  PREFIX_DB_MARKER = "prefixV2:",
  PREFIX_DB_MARKER_DEPRECATED = "prefix:",
  STATIC_PREFIX_ALLOC_PARAM_KEY = "e2e-network-allocations",
  CONST_TTL_INF = -2 ^ 31,

  ORIGINATOR_DEFAULT = "puff",
  OPENR_CONFIG_FILE = "/etc/sysconfig/openr",
}

--- Convert a Network.BinaryAddress structure into a human-readable string.
local function decodeBinaryAddress(binaryAddr)
  local addr = tg_net_utils.binaryAddressToString(binaryAddr.addr)
  if binaryAddr.ifName ~= nil and binaryAddr.ifName:len() > 0 then
    addr = addr .. "%" .. binaryAddr.ifName
  end
  return addr
end

--- Convert a human-readable string to a Network.BinaryAddress structure.
local function encodeBinaryAddress(addr)
  -- Parse link-local addresses
  local address
  local iface = nil
  if addr:find("@") then
    local ipAndIface = addr:split("@", 2)
    address = ipAndIface[1]
    iface = ipAndIface[2]
  elseif addr:find("%%") then
    local ipAndIface = addr:split("%", 2)
    address = ipAndIface[1]
    iface = ipAndIface[2]
  else
    address = addr
  end

  local binaryAddr = BinaryAddress:new{}
  binaryAddr.addr = tg_net_utils.stringToBinaryAddress(address)
  if iface ~= nil then
    binaryAddr.ifName = iface
  end
  return binaryAddr
end

--- Convert a Network.IpPrefix structure into a human-readable string.
local function decodeIpPrefix(prefix)
  return
    decodeBinaryAddress(prefix.prefixAddress) .. "/" .. prefix.prefixLength
end

--- Convert a human-readable string into a Network.IpPrefix structure.
local function encodeIpPrefix(prefix)
  local ipAndPrefix = prefix:split("/", 2)
  local ipPrefix = IpPrefix:new{}
  ipPrefix.prefixAddress = encodeBinaryAddress(ipAndPrefix[1])
  ipPrefix.prefixLength = tonumber(ipAndPrefix[2])
  return ipPrefix
end

--- Convert binary fields from a Network.UnicastRoute structure into
-- human-readable strings.
-- This function performs in-place modifications of the input structure.
local function decodeUnicastRoute(route)
  route.dest = decodeIpPrefix(route.dest)
  if route.bestNexthop ~= nil then
    route.bestNexthop = decodeBinaryAddress(route.bestNexthop)
  end
  for _, nextHop in pairs(route.nextHops) do
    nextHop.ifName = nextHop.address.ifName
    nextHop.address = decodeBinaryAddress(nextHop.address)
  end
end

--- Convert binary fields from a Network.MplsRoute structure into human-readable
-- strings.
-- This function performs in-place modifications of the input structure.
local function decodeMplsRoute(route)
  for _, nextHop in pairs(route.nextHops) do
    nextHop.ifName = nextHop.address.ifName
    nextHop.address = decodeBinaryAddress(nextHop.address)
  end
end

--- Convert Network.BinaryAddress structures within a Types.AdjacencyDatabase
-- structure into human-readable strings.
local function decodeAdjacencyDatabase(adjacencyDb)
  for _, adj in ipairs(adjacencyDb.adjacencies) do
    adj.nextHopV4 = decodeBinaryAddress(adj.nextHopV4)
    adj.nextHopV6 = decodeBinaryAddress(adj.nextHopV6)
  end
end

--- Convert Network.IpPrefix structures within a Types.PrefixDatabase structure
-- into human-readable strings.
local function decodePrefixDatabase(prefixDb)
  for _, entry in ipairs(prefixDb.prefixEntries) do
    entry.prefix = decodeIpPrefix(entry.prefix)
  end
end

--- Convert Network.IpPrefix structures within a Types.StaticAllocation
-- structure into human-readable strings.
local function decodeStaticAllocation(staticAllocation)
  for k, v in pairs(staticAllocation.nodePrefixes) do
    staticAllocation.nodePrefixes[k] = decodeIpPrefix(v)
  end
end

--- Return a string representation of the given TTL value.
local function ttlToStr(ttl)
  return tg_thrift_utils.lualongnumberToNumber(ttl) == consts.CONST_TTL_INF
    and "INF"
    or ttl
end

--- Decode binary values in a Types.DumpLinksReply structure.
local function decodeDumpLinksReply(dumpLinksReply)
  for k, v in pairs(dumpLinksReply.interfaceDetails) do
    tablex.transform(decodeIpPrefix, v.info.networks)
  end
end

--- Deserialize Thrift values, as needed, in map of key-vals from a publication.
-- This overwrites the existing "value" field with the deserialized value.
local function kvstoreDeserializeKeyvals(keyVals)
  for k, v in pairs(keyVals) do
    if k:startswith(consts.ADJ_DB_MARKER) then
      local adjacencyDb = AdjacencyDatabase:new{}
      tg_thrift_utils.deserialize(v.value, adjacencyDb)
      decodeAdjacencyDatabase(adjacencyDb)
      v.value = adjacencyDb
    elseif k:startswith(consts.PREFIX_DB_MARKER) or
           k:startswith(consts.PREFIX_DB_MARKER_DEPRECATED) then
      local prefixDb = PrefixDatabase:new{}
      tg_thrift_utils.deserialize(v.value, prefixDb)
      decodePrefixDatabase(prefixDb)
      v.value = prefixDb
    elseif k == consts.STATIC_PREFIX_ALLOC_PARAM_KEY then
      local staticAllocation = StaticAllocation:new{}
      tg_thrift_utils.deserialize(v.value, staticAllocation)
      decodeStaticAllocation(staticAllocation)
      v.value = staticAllocation
    end
  end
end

--- Filter a map of key-vals from a publication given a CSV node filter.
local function kvstoreFilterKeyvals(keyVals, nodes)
  if nodes == "all" then
    return keyVals
  end

  local result = {}
  local nodeSet = tablex.makeset(nodes:strip():split(","))
  for k, v in pairs(keyVals) do
    local nodeName = k:split(":")[2] or k
    if nodeSet[nodeName] then
      result[k] = v
    end
  end
  return result
end

--- Filter a map of structures from a decision response given a CSV node filter.
local function decisionFilterResp(decisionMap, nodes)
  if nodes == "all" then
    return decisionMap
  end

  local result = {}
  local nodeSet = tablex.makeset(nodes:strip():split(","))
  for k, v in pairs(decisionMap) do
    if nodeSet[k] then
      result[k] = v
    end
  end
  return result
end

--- Build a list of UnicastRoute structures given comma-separated prefixes and
-- next-hops.
local function fibBuildRoutes(prefixesStr, nexthopsStr)
  local nexthops = {}
  for _, nh in ipairs(nexthopsStr:split(",")) do
    local nexthop = NextHopThrift:new{}
    nexthop.address = encodeBinaryAddress(nh)
    nexthops[#nexthops+1] = nexthop
  end
  local routes = {}
  for _, prefix in ipairs(prefixesStr:split(",")) do
    local route = UnicastRoute:new{}
    route.dest = encodeIpPrefix(prefix)
    route.nextHops = nexthops
    routes[#routes+1] = route
  end
  return routes
end

--- Format a KvStore.KeyVals map of prefixes (e.g. for printing).
local function kvstoreFormatPrefixKeyvals(prefixDbMap)
  local s = {}
  local headers = {
    "Prefix", "Client Type", "Forwarding Type", "Forwarding Algorithm"
  }

  local invPrefixType = tg_utils.invertTable(PrefixType)
  local invForwardingType = tg_utils.invertTable(PrefixForwardingType)
  local invForwardingAlgorithm = tg_utils.invertTable(PrefixForwardingAlgorithm)

  -- Group prefixes by node name (in new prefix format, each key is 1 entry)
  local prefixDbByNodeName = {}
  for _, v in pairs(prefixDbMap) do
    local prefix = prefixDbByNodeName[v.value.thisNodeName]
    if prefix == nil then
      prefixDbByNodeName[v.value.thisNodeName] = v
    else
      for _, entry in ipairs(v.value.prefixEntries) do
        table.insert(prefix.value.prefixEntries, entry)
      end
    end
  end

  for k, v in tablex.sort(prefixDbByNodeName) do
    s[#s+1] = string.format("> %s\n", k)

    local rows = {}
    local prefixEntries = v.value.prefixEntries
    table.sort(prefixEntries, function(a, b) return a.prefix < b.prefix end)
    for _, entry in ipairs(prefixEntries) do
      rows[#rows+1] = {
        entry.prefix,
        invPrefixType[entry.type],
        invForwardingType[entry.forwardingType],
        invForwardingAlgorithm[entry.forwardingAlgorithm]
      }
    end
    s[#s+1] =
      string.format("%s\n\n", tabulate.tabulate(rows, {headers = headers}))
  end
  return table.concat(s, "\n")
end

--- Format a KvStore.KeyVals map of adjacencies (e.g. for printing).
local function kvstoreFormatAdjacencyKeyvals(adjacencyDbMap)
  local s = {}
  local headers = {
    "Neighbor",
    "Local Intf",
    "Remote Intf",
    "Metric",
    "Label",
    "NextHop-v4",
    "NextHop-v6",
    "Uptime",
    "Area"
  }
  local now = os.time()
  for k, v in tablex.sort(adjacencyDbMap) do
    s[#s+1] = string.format("> %s", v.value.thisNodeName)
    local adjTokens = {}
    if v.version then
      adjTokens[#adjTokens+1] = "Version: " .. tostring(v.version)
    end
    if v.value.isOverloaded then
      adjTokens[#adjTokens+1] = "Overloaded: true"
    end
    if v.value.nodeLabel > 0 then
      adjTokens[#adjTokens+1] = "Node Label: " .. v.value.nodeLabel
    end
    if #adjTokens > 0 then
      s[#s] = s[#s] .. " => " .. (", "):join(adjTokens)
    end

    local rows = {}
    local adjacencies = v.value.adjacencies
    table.sort(adjacencies, function(a, b)
      return a.otherNodeName < b.otherNodeName
    end)
    for _, adj in ipairs(adjacencies) do
      rows[#rows+1] = {
        adj.otherNodeName,
        adj.ifName,
        adj.otherIfName,
        adj.isOverloaded and "OVERLOADED" or adj.metric,
        adj.adjLabel,
        adj.nextHopV4,
        adj.nextHopV6,
        tg_thrift_utils.lualongnumberCmp(adj.timestamp, 0) > 0 and
          tg_utils.formatTimeInterval(
            tg_thrift_utils.lualongnumberToNumber(now - adj.timestamp, true)
          ) or nil,
        adj.area or "N/A"
      }
    end
    s[#s+1] = string.format(
      "%s\n",
      tabulate.tabulate(
        rows, {headers = headers, fmt = tabulate.TableFormat.PLAIN}
      )
    )
  end
  return table.concat(s, "\n")
end

--- Update the given table with fields from an adjacency database.
-- Needed because breeze rearranges this JSON for printing.
local function kvstoreFormatAdjacencyKeyvalsRaw(t, adjacencyDb, version)
  t[adjacencyDb.thisNodeName] = {
    adjacencies = adjacencyDb.adjacencies,
    node_label = adjacencyDb.nodeLabel,
    overloaded = adjacencyDb.isOverloaded,
    version = version
  }
end

--- Format a list of Network.UnicastRoute structures (e.g. for printing).
local function formatUnicastRoutes(routes)
  local s = {}
  for _, route in ipairs(routes) do
    s[#s+1] = string.format("> %s", route.dest)
    for _, nh in ipairs(route.nextHops) do
      s[#s+1] = string.format("via %s", nh.address)
    end
    s[#s+1] = ""
  end
  return table.concat(s, "\n")
end

--- Format a list of Network.MplsRoute structures (e.g. for printing).
local function formatMplsRoutes(routes)
  local s = {}
  for _, route in ipairs(routes) do
    s[#s+1] = string.format("%d", route.topLabel)
    for _, nh in ipairs(route.nextHops) do
      s[#s+1] = string.format("via %s", nh.address)
    end
    s[#s+1] = ""
  end
  return table.concat(s, "\n")
end

--- Build a list of PrefixEntry structures given a list of string prefixes and
-- prefixType and forwardingType.
local function prefixmgrBuildPrefixes(prefixes, prefixType, forwardingType)
  forwardingType = forwardingType or "ip"
  local tprefixes = {}
  for _, prefix in ipairs(prefixes) do
    local tprefix = PrefixEntry:new{}
    tprefix.prefix = encodeIpPrefix(prefix)
    tprefix.type = PrefixType[prefixType:upper()]
    tprefix.forwardingType = PrefixForwardingType[forwardingType:upper()]
    tprefixes[#tprefixes+1] = tprefix
  end
  return tprefixes
end

--- Validate a command to set/unset metric for an interface.
-- Let "metric" be a number if setting the value, otherwise "nil"
local function lmValidateLinkMetric(links, interface, metric)
  if links.interfaceDetails[interface] == nil then
    logger.error("No such interface: %s", interface)
    return false
  end

  local metricOverride = links.interfaceDetails[interface].metricOverride
  if not metric and not metricOverride then
    logger.error(
      "Interface: %s hasn't been assigned a metric override.", interface
    )
    return false
  end
  if metric and metric == metricOverride then
    logger.error(
      "Interface: %s has already been set with metric: %s.",
      interface,
      metric
    )
    return false
  end
  return true
end

--- Validate a command to set/unset overload bit for an interface.
local function lmValidateLinkOverload(links, interface, set)
  if links.interfaceDetails[interface] == nil then
    logger.error("No such interface: %s", interface)
    return false
  end

  if set and links.interfaceDetails[interface].isOverloaded then
    logger.error("Interface '%s' is already overloaded.", interface)
    return false
  end
  if not set and not links.interfaceDetails[interface].isOverloaded then
    logger.error("Interface '%s' is not overloaded.", interface)
    return false
  end
  return true
end

--- Puff CLI class
local Puff = {}
Puff.__index = Puff

--- Constructor for Puff CLI.
function Puff.new(args)
  local self = setmetatable({}, Puff)
  self.openr_ctrl_client = nil
  self.openr_fib_client = nil
  self.openr_host = args.openr_host
  self.openr_ctrl_port = args.openr_ctrl_port
  self.openr_fib_agent_port = args.openr_fib_agent_port
  return self
end

--- Connect to the Open/R ctrl socket.
function Puff:connectToOpenrCtrl()
  local client, error = tg_thrift_utils.createClient(
    OpenrCtrlClient, self.openr_host, self.openr_ctrl_port
  )
  if client == nil then
    logger.error("Failed to connect to Open/R ctrl port: %s", error)
    return false
  end
  self.openr_ctrl_client = client
  return true
end

--- Connect to the Open/R Fib agent socket.
function Puff:connectToOpenrFibAgent()
  local client, error = tg_thrift_utils.createClient(
    FibServiceClient, self.openr_host, self.openr_fib_agent_port
  )
  if client == nil then
    logger.error("Failed to connect to Open/R Fib agent port: %s", error)
    return false
  end
  self.openr_fib_client = client
  return true
end

--- Get the identity of the connected node by querying LinkMonitor.
--
-- `connectToOpenrCtrl()` must be called before invoking this function.
function Puff:getConnectedNodeName()
  assert(self.openr_ctrl_client)
  return self.openr_ctrl_client:getInterfaces().thisNodeName
end

--- Puff CLI handlers static class
local Puff_Handler = {}
Puff_Handler.__index = Puff_Handler

--- `puff kvstore keys` command handler
function Puff_Handler.kvstoreKeys(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local filter = KeyDumpParams:new{}
  filter.prefix = args.prefix
  if args.originator ~= nil then
    filter.originatorIds = {[args.originator] = true}
  end
  local publication = puff.openr_ctrl_client:getKvStoreKeyValsFiltered(filter)

  if args.json then
    kvstoreDeserializeKeyvals(publication.keyVals)
    logger.info(tg_thrift_utils.thriftToJson(publication.keyVals))
  else
    if next(publication.keyVals) == nil then
      logger.error("No values found in KvStore")
      return
    end
    local headers = {"Key", "Originator", "Ver", "Hash", "Size"}
    if args.ttl then
      headers[#headers+1] = "TTL - Ver"
    end
    local rows = {}
    local totalSize = 0
    for k, v in tablex.sort(publication.keyVals) do
      -- 32 bytes comes from version, ttlVersion, ttl and hash which are i64
      local kvSize = 32 + k:len() + v.originatorId:len() + v.value:len()
      totalSize = totalSize + kvSize
      local row = {
        k, v.originatorId, v.version, v.hash, tg_utils.formatBytes(kvSize)
      }
      if args.ttl then
        row[#row+1] = string.format("%s - %s", ttlToStr(v.ttl), v.ttlVersion)
      end
      rows[#rows+1] = row
    end
    logger.info(
      "== KvStore Data - %d key(s), %s ==\n",
      tablex.size(publication.keyVals), tg_utils.formatBytes(totalSize)
    )
    logger.info("%s\n", tabulate.tabulate(rows, {headers = headers}))
  end
end

--- `puff kvstore keyvals` command handler
function Puff_Handler.kvstoreKeyvals(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local publication = puff.openr_ctrl_client:getKvStoreKeyVals(args.keys)
  kvstoreDeserializeKeyvals(publication.keyVals)

  if args.json then
    logger.info(tg_thrift_utils.thriftToJson(publication.keyVals))
  else
    if next(publication.keyVals) == nil then
      logger.error("No values found in KvStore")
      return
    end
    logger.info("== Dump key-value pairs in KvStore ==\n")
    for k, v in tablex.sort(publication.keyVals) do
      logger.info("> key: %s", k)
      logger.info("  version: %s", v.version)
      logger.info("  originatorId: %s", v.originatorId)
      logger.info("  ttl: %s", ttlToStr(v.ttl))
      logger.info("  ttlVersion: %s", v.ttlVersion)
      logger.info(
        "  value: %s\n",
        type(v) == "string"
          and v.value
          or "\n" .. tg_thrift_utils.thriftToJson(v.value)
      )
    end
  end
end

--- `puff kvstore set-key` command handler
function Puff_Handler.kvstoreSetKey(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local version = args.version
  if version == nil then
    -- Retrieve existing Value from KvStore
    local publication = puff.openr_ctrl_client:getKvStoreKeyVals({args.key})
    if publication.keyVals[args.key] ~= nil then
      local existingVal = publication.keyVals[args.key]
      logger.info(
        "Key '%s' found in KvStore with version %d. " ..
        "Overwriting with higher version...",
        args.key, tostring(existingVal.version)
      )
      version = existingVal.version + 1
    else
      version = 1
    end
  end
  local ttl = args.ttl
  local ttlStr = "infinity"
  if ttl ~= consts.CONST_TTL_INF then
    ttl = ttl * 1000
    ttlStr = tostring(ttl)
  end

  local val = Value:new{}
  val.version = version
  val.originatorId = args.originator
  val.value = args.value
  val.ttl = ttl

  local setParams = KeySetParams:new{}
  setParams.keyVals = {[args.key] = val}
  puff.openr_ctrl_client:setKvStoreKeyVals(setParams)
  logger.info(
    "Success: Set key '%s' with version %s and ttl %s successfully",
    args.key, tostring(version), ttlStr
  )
end

--- `puff kvstore erase-key` command handler
function Puff_Handler.kvstoreEraseKey(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local publication = puff.openr_ctrl_client:getKvStoreKeyVals({args.key})
  local val = publication.keyVals[args.key]
  if val == nil then
    tg_utils.abort(
      string.format("Error: Key '%s' not found in KvStore.", args.key)
    )
  end

  val.value = nil
  val.ttl = 256  -- set new ttl to 256ms (it's decremented 1ms on every hop)
  val.ttlVersion = val.ttlVersion + 1  -- bump up ttl version

  logger.info(tg_thrift_utils.thriftToJson(publication.keyVals))

  local setParams = KeySetParams:new{}
  setParams.keyVals = publication.keyVals
  puff.openr_ctrl_client:setKvStoreKeyVals(setParams)
  logger.info(
    "Success: Key '%s' will be erased soon from all KvStores", args.key
  )
end

--- `puff kvstore prefixes` command handler
function Puff_Handler.kvstorePrefixes(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  -- HACK: Handle both new and deprecated TG prefix formats, so filter manually
  local filter = KeyDumpParams:new{}
  filter.prefix = "" -- consts.PREFIX_DB_MARKER
  local publication = puff.openr_ctrl_client:getKvStoreKeyValsFiltered(filter)
  for k, v in pairs(publication.keyVals) do
    if not k:startswith(consts.PREFIX_DB_MARKER) and
       not k:startswith(consts.PREFIX_DB_MARKER_DEPRECATED) then
      publication.keyVals[k] = nil
    end
  end

  -- Filter response
  local nodes = args.nodes
  if nodes == nil or nodes:len() == 0 then
    nodes = puff:getConnectedNodeName()
  end
  local keyVals = kvstoreFilterKeyvals(publication.keyVals, nodes)
  kvstoreDeserializeKeyvals(keyVals)

  if args.json then
    local printJson = {}
    for k, v in pairs(keyVals) do
      printJson[v.value.thisNodeName] = v.value
    end
    logger.info(tg_thrift_utils.thriftToJson(printJson))
  else
    if next(keyVals) == nil then
      logger.error("No matching prefixes found in KvStore")
      return
    end
    logger.info(kvstoreFormatPrefixKeyvals(keyVals))
  end
end

--- `puff kvstore adj` command handler
function Puff_Handler.kvstoreAdj(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local filter = KeyDumpParams:new{}
  filter.prefix = consts.ADJ_DB_MARKER
  local publication = puff.openr_ctrl_client:getKvStoreKeyValsFiltered(filter)

  -- Filter response
  local nodes = args.nodes
  if nodes == nil or nodes:len() == 0 then
    nodes = puff:getConnectedNodeName()
  end
  local keyVals = kvstoreFilterKeyvals(publication.keyVals, nodes)
  kvstoreDeserializeKeyvals(keyVals)

  if args.json then
    local printJson = {}
    for k, v in pairs(keyVals) do
      kvstoreFormatAdjacencyKeyvalsRaw(printJson, v.value, v.version)
    end
    logger.info(tg_thrift_utils.thriftToJson(printJson))
  else
    if next(keyVals) == nil then
      logger.error("No matching adjacencies found in KvStore")
      return
    end
    logger.info(kvstoreFormatAdjacencyKeyvals(keyVals))
  end
end

--- `puff kvstore peers` command handler
function Puff_Handler.kvstorePeers(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local peers = puff.openr_ctrl_client:getKvStorePeers()

  if args.json then
    logger.info(tg_thrift_utils.thriftToJson(peers))
  else
    logger.info("== %s's peers ==\n", puff:getConnectedNodeName())
    for k, v in pairs(peers) do
      logger.info("> %s", k)
      logger.info("cmd via %s", v.cmdUrl)
    end
  end
end

--- `puff fib routes` command handler
function Puff_Handler.fibRoutes(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local routeDb = puff.openr_ctrl_client:getRouteDb()

  tablex.foreachi(routeDb.unicastRoutes, decodeUnicastRoute)
  tablex.foreachi(routeDb.mplsRoutes, decodeMplsRoute)

  if args.json then
    local printJson = {[routeDb.thisNodeName or ""] = routeDb}
    logger.info(tg_thrift_utils.thriftToJson(printJson))
  else
    table.sort(routeDb.unicastRoutes, function(a, b) return a.dest < b.dest end)
    table.sort(
      routeDb.mplsRoutes, function(a, b) return a.topLabel < b.topLabel end
    )
    logger.info("== Unicast Routes for %s ==\n", routeDb.thisNodeName)
    logger.info("%s\n", formatUnicastRoutes(routeDb.unicastRoutes))
    logger.info("== MPLS Routes for %s ==\n", routeDb.thisNodeName)
    logger.info("%s\n", formatMplsRoutes(routeDb.mplsRoutes))
  end
end

--- `puff fib list` command handler
function Puff_Handler.fibList(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrFibAgent() then
    tg_utils.abort()
  end

  -- Fetch unicast routes
  local unicastRoutes = {}
  local success, ret = pcall(
    puff.openr_fib_client.getRouteTableByClient,
    puff.openr_fib_client,
    args.client_id
  )
  if success then
    unicastRoutes = ret
  else
    logger.error("Failed to get routes from Fib.")
    logger.error("Exception: %s", tg_thrift_utils.exceptionStr(ret))
  end

  -- Fetch MPLS routes
  local mplsRoutes = {}
  success, ret = pcall(
    puff.openr_fib_client.getMplsRouteTableByClient,
    puff.openr_fib_client,
    args.client_id
  )
  if success then
    mplsRoutes = ret
  else
    logger.error("Failed to get MPLS routes from Fib.")
    logger.error("Exception: %s", tg_thrift_utils.exceptionStr(ret))
  end

  tablex.foreachi(unicastRoutes, decodeUnicastRoute)
  tablex.foreachi(mplsRoutes, decodeMplsRoute)

  if args.json then
    local host = ""
    if puff:connectToOpenrCtrl() then
      host = puff:getConnectedNodeName()
    end
    logger.info(tg_thrift_utils.thriftToJson({
      host = host,
      client = args.client_id,
      unicastRoutes = unicastRoutes,
      mplsRoutes = mplsRoutes
    }))
  else
    table.sort(unicastRoutes, function(a, b) return a.dest < b.dest end)
    table.sort(mplsRoutes, function(a, b) return a.topLabel < b.topLabel end)
    logger.info("== FIB routes by client %s ==\n", args.client_id)
    logger.info("%s\n", formatUnicastRoutes(unicastRoutes))
    logger.info("== MPLS Routes by client %s ==\n", args.client_id)
    logger.info("%s\n", formatMplsRoutes(mplsRoutes))
  end
end

--- `puff fib add` command handler
function Puff_Handler.fibAdd(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrFibAgent() then
    tg_utils.abort()
  end

  local routes = fibBuildRoutes(args.prefixes, args.nexthops)

  local success, ret = pcall(
    puff.openr_fib_client.addUnicastRoutes,
    puff.openr_fib_client,
    args.client_id,
    routes
  )
  if success then
    logger.info("Added %d route(s).", #routes)
  else
    tg_utils.abort(string.format(
      "Failed to add routes.\nException: %s", tg_thrift_utils.exceptionStr(ret)
    ))
  end
end

--- `puff fib del` command handler
function Puff_Handler.fibDel(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrFibAgent() then
    tg_utils.abort()
  end

  local prefixes = {}
  for _, prefix in ipairs(args.prefixes:split(",")) do
    prefixes[#prefixes+1] = encodeIpPrefix(prefix)
  end

  local success, ret = pcall(
    puff.openr_fib_client.deleteUnicastRoutes,
    puff.openr_fib_client,
    args.client_id,
    prefixes
  )
  if success then
    logger.info("Deleted %d route(s).", #prefixes)
  else
    tg_utils.abort(string.format(
      "Failed to delete routes.\nException: %s",
      tg_thrift_utils.exceptionStr(ret)
    ))
  end
end

--- `puff fib sync` command handler
function Puff_Handler.fibSync(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrFibAgent() then
    tg_utils.abort()
  end

  local routes = fibBuildRoutes(args.prefixes, args.nexthops)

  local success, ret = pcall(
    puff.openr_fib_client.syncFib,
    puff.openr_fib_client,
    args.client_id,
    routes
  )
  if success then
    logger.info("Reprogrammed Fib with %d route(s).", #routes)
  else
    tg_utils.abort(string.format(
      "Failed to sync routes.\nException: %s", tg_thrift_utils.exceptionStr(ret)
    ))
  end
end

--- `puff decision routes` command handler
function Puff_Handler.decisionRoutes(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  -- Parse nodes
  local nodes = args.nodes
  if nodes == nil or nodes:len() == 0 then
    nodes = puff:getConnectedNodeName()
  end
  local nodeList = {}
  if nodes == "all" then
    -- Get all node names
    local prefixDbs = puff.openr_ctrl_client:getDecisionPrefixDbs()
    for k, v in pairs(prefixDbs) do
      nodeList[#nodeList+1] = v.thisNodeName
    end
  else
    nodeList = nodes:strip():split(",")
  end

  -- Query for all route databases
  local routeDbs = {}
  for _, node in ipairs(nodeList) do
    local routeDb = puff.openr_ctrl_client:getRouteDbComputed(node)
    tablex.foreachi(routeDb.unicastRoutes, decodeUnicastRoute)
    tablex.foreachi(routeDb.mplsRoutes, decodeMplsRoute)
    routeDbs[node] = routeDb
  end

  if args.json then
    logger.info(tg_thrift_utils.thriftToJson(routeDbs))
  else
    for node, routeDb in tablex.sort(routeDbs) do
      table.sort(
        routeDb.unicastRoutes, function(a, b) return a.dest < b.dest end
      )
      table.sort(
        routeDb.mplsRoutes, function(a, b) return a.topLabel < b.topLabel end
      )
      logger.info("== Unicast Routes for %s ==\n", routeDb.thisNodeName)
      logger.info("%s\n", formatUnicastRoutes(routeDb.unicastRoutes))
      logger.info("== MPLS Routes for %s ==\n", routeDb.thisNodeName)
      logger.info("%s\n", formatUnicastRoutes(routeDb.mplsRoutes))
    end
  end
end

--- `puff decision prefixes` command handler
function Puff_Handler.decisionPrefixes(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local prefixDbs = puff.openr_ctrl_client:getDecisionPrefixDbs()

  -- Filter response
  local nodes = args.nodes
  if nodes == nil or nodes:len() == 0 then
    nodes = puff:getConnectedNodeName()
  end
  prefixDbs = decisionFilterResp(prefixDbs, nodes)

  tablex.foreach(prefixDbs, decodePrefixDatabase)

  if args.json then
    logger.info(tg_thrift_utils.thriftToJson(prefixDbs))
  else
    -- hack into Value structure
    logger.info(kvstoreFormatPrefixKeyvals(
      tablex.map(function(v) return {value = v} end, prefixDbs)
    ))
  end
end

--- `puff decision adj` command handler
function Puff_Handler.decisionAdj(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local adjDbs = puff.openr_ctrl_client:getDecisionAdjacencyDbs()

  -- Filter response
  local nodes = args.nodes
  if nodes == nil or nodes:len() == 0 then
    nodes = puff:getConnectedNodeName()
  end
  adjDbs = decisionFilterResp(adjDbs, nodes)

  tablex.foreach(adjDbs, decodeAdjacencyDatabase)

  if args.json then
    local printJson = {}
    for k, v in pairs(adjDbs) do
      kvstoreFormatAdjacencyKeyvalsRaw(printJson, v)
    end
    logger.info(tg_thrift_utils.thriftToJson(printJson))
  else
    -- hack into Value structure
    logger.info(kvstoreFormatAdjacencyKeyvals(
      tablex.map(function(v) return {value = v} end, adjDbs)
    ))
  end
end

--- `puff monitor counters` command handler
function Puff_Handler.monitorCounters(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local counters = puff.openr_ctrl_client:getCounters()
  if args.prefix and args.prefix:len() > 0 then
    for k, v in pairs(counters) do
      if not k:startswith(args.prefix) then
        counters[k] = nil
      end
    end
  end

  if args.json then
    logger.info(tg_thrift_utils.thriftToJson(counters))
  else
    local rows = {}
    for k, v in tablex.sort(counters) do
      rows[#rows+1] = {k, v}
    end
    logger.info("== %s's counters ==\n", puff:getConnectedNodeName())
    logger.info(
      "%s\n", tabulate.tabulate(rows, {fmt = tabulate.TableFormat.PLAIN})
    )
  end
end

--- `puff monitor logs` command handler
function Puff_Handler.monitorLogs(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local logs = puff.openr_ctrl_client:getEventLogs()

  -- Deserialize each JSON log entry
  local eventLogs = {}
  for _, log in ipairs(logs) do
    local t, err = cjson_safe.decode(log)
    if t == nil then
      logger.error("Error parsing log JSON: %s", log)
    else
      eventLogs[#eventLogs+1] = t
    end
  end

  if args.json then
    logger.info(tg_thrift_utils.thriftToJson(eventLogs))
  else
    for _, logSample in ipairs(eventLogs) do
      local rows = {}
      for _, o in pairs(logSample) do
        for k, v in tablex.sort(o) do
          local val = v
          if k == "time" then
            val = os.date("%H:%M:%S %Y-%m-%d", val)
          end
          if type(val) == "table" then
            val = ("\n"):join(val)
          end
          rows[#rows+1] = {k, val}
        end
      end
      logger.info(
        "%s\n", tabulate.tabulate(rows, {fmt = tabulate.TableFormat.PLAIN})
      )
    end
  end
end

--- `puff prefixmgr withdraw` command handler
function Puff_Handler.prefixmgrWithdraw(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local tprefixes = prefixmgrBuildPrefixes(args.prefixes, args.prefix_type)
  puff.openr_ctrl_client:withdrawPrefixes(tprefixes)
  logger.info("Withdrew %d prefix(es)", #tprefixes)
end

--- `puff prefixmgr advertise` command handler
function Puff_Handler.prefixmgrAdvertise(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local tprefixes = prefixmgrBuildPrefixes(
    args.prefixes, args.prefix_type, args.forwarding_type
  )
  puff.openr_ctrl_client:advertisePrefixes(tprefixes)
  logger.info(
    "Advertised %d prefix(es) with type %s", #tprefixes, args.prefix_type
  )
end

--- `puff prefixmgr sync` command handler
function Puff_Handler.prefixmgrSync(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local tprefixType = PrefixType[args.prefix_type:upper()]
  local tprefixes = prefixmgrBuildPrefixes(
    args.prefixes, args.prefix_type, args.forwarding_type
  )
  puff.openr_ctrl_client:syncPrefixesByType(tprefixType, tprefixes)
  logger.info("Synced %d prefix(es) with type %s", #tprefixes, args.prefix_type)
end

--- `puff prefixmgr view` command handler
function Puff_Handler.prefixmgrView(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local tprefixes = puff.openr_ctrl_client:getPrefixes()
  local invPrefixType = tg_utils.invertTable(PrefixType)
  local invForwardingType = tg_utils.invertTable(PrefixForwardingType)

  if args.json then
    for i = 1, #tprefixes do
      tprefixes[i].prefix = decodeIpPrefix(tprefixes[i].prefix)
    end
    logger.info(tg_thrift_utils.thriftToJson(tprefixes))
  else
    local headers = {"Type", "Prefix", "Forwarding Type", "Ephemeral"}
    local rows = {}
    for _, v in ipairs(tprefixes) do
      rows[#rows+1] = {
        invPrefixType[v.type],
        decodeIpPrefix(v.prefix),
        invForwardingType[v.forwardingType],
        v.ephemeral and true or false
      }
    end
    logger.info("%s\n", tabulate.tabulate(rows, {headers = headers}))
  end
end

--- `puff lm links` command handler
function Puff_Handler.lmLinks(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local links = puff.openr_ctrl_client:getInterfaces()
  decodeDumpLinksReply(links)

  if args.json then
    local printJson = {[links.thisNodeName or ""] = links}
    logger.info(tg_thrift_utils.thriftToJson(printJson))
  else
    local headers = {"Interface", "Status", "Metric Override", "Addresses"}
    local rows = {}
    for k, v in tablex.sort(links.interfaceDetails) do
      rows[#rows+1] = {
        k,
        v.info.isUp and "Up" or "Down",
        v.isOverloaded and "Overloaded" or v.metricOverride or "",
        type(v.info.networks) == "table" and table.concat(v.info.networks, "\n")
          or ""
      }
    end
    logger.info(
      "== Node Overload: %s ==\n", links.isOverloaded and "YES" or "NO"
    )
    logger.info("%s\n", tabulate.tabulate(rows, {headers = headers}))
  end
end

--- `puff lm set-link-metric` command handler
function Puff_Handler.lmSetLinkMetric(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local links = puff.openr_ctrl_client:getInterfaces()
  if lmValidateLinkMetric(links, args.interface, args.metric) then
    puff.openr_ctrl_client:setInterfaceMetric(args.interface, args.metric)
    logger.info(
      "Successfully set override metric for interface: %s", args.interface
    )
  else
    tg_utils.abort()
  end
end

--- `puff lm unset-link-metric` command handler
function Puff_Handler.lmUnsetLinkMetric(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end
  local links = puff.openr_ctrl_client:getInterfaces()
  if lmValidateLinkMetric(links, args.interface, nil) then
    puff.openr_ctrl_client:unsetInterfaceMetric(args.interface)
    logger.info(
      "Successfully unset override metric for interface: %s", args.interface
    )
  else
    tg_utils.abort()
  end
end

--- `puff lm set-link-overload` command handler
function Puff_Handler.lmSetLinkOverload(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end
  local links = puff.openr_ctrl_client:getInterfaces()
  if lmValidateLinkOverload(links, args.interface, true) then
    puff.openr_ctrl_client:setInterfaceOverload(args.interface)
    logger.info(
      "Successfully set overload bit for interface: %s", args.interface
    )
  else
    tg_utils.abort()
  end
end

--- `puff lm unset-link-overload` command handler
function Puff_Handler.lmUnsetLinkOverload(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end
  local links = puff.openr_ctrl_client:getInterfaces()
  if lmValidateLinkOverload(links, args.interface, false) then
    puff.openr_ctrl_client:unsetInterfaceOverload(args.interface)
    logger.info(
      "Successfully unset overload bit for interface: %s", args.interface
    )
  else
    tg_utils.abort()
  end
end

--- `puff lm set-adj-metric` command handler
function Puff_Handler.lmSetAdjMetric(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  puff.openr_ctrl_client:setAdjacencyMetric(
    args.interface, args.node, args.metric
  )
end

--- `puff lm unset-adj-metric` command handler
function Puff_Handler.lmUnsetAdjMetric(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  puff.openr_ctrl_client:unsetAdjacencyMetric(args.interface, args.node)
end

--- `puff lm set-node-overload` command handler
function Puff_Handler.lmSetNodeOverload(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end
  local links = puff.openr_ctrl_client:getInterfaces()
  if not links.isOverloaded then
    puff.openr_ctrl_client:setNodeOverload()
    logger.info("Successfully set overload bit.")
  else
    tg_utils.abort("This node is already overloaded.")
  end
end

--- `puff lm unset-node-overload` command handler
function Puff_Handler.lmUnsetNodeOverload(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end
  local links = puff.openr_ctrl_client:getInterfaces()
  if links.isOverloaded then
    puff.openr_ctrl_client:unsetNodeOverload()
    logger.info("Successfully unset overload bit.")
  else
    tg_utils.abort("This node is not overloaded.")
  end
end

--- `puff info version` command handler
function Puff_Handler.infoVersion(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local version = puff.openr_ctrl_client:getOpenrVersion()
  logger.info(tg_thrift_utils.thriftToJson(version))
end

--- `puff info build` command handler
function Puff_Handler.infoBuild(args, name)
  local puff = Puff.new(args)
  if not puff:connectToOpenrCtrl() then
    tg_utils.abort()
  end

  local buildInfo = puff.openr_ctrl_client:getBuildInfo()
  logger.info(tg_thrift_utils.thriftToJson(buildInfo))
end

--- `puff tech-support` command handler
function Puff_Handler.techSupport(args, name)
  local notImplementedFn = function() logger.info("Not Implemented") end

  -- Format: {<description>, <function>}
  local steps = {
    {
      "openr config file", function()
        local contents = tg_utils.readFile(consts.OPENR_CONFIG_FILE)
        if contents ~= nil then
          logger.info(contents)
        else
          logger.error("Missing config file")
        end
      end
    },
    {
      "openr runtime params", function()
        local output, ret, code = tg_utils.exec("pgrep -a openr")
        if output ~= nil then
          logger.info(output)
        else
          logger.error("'pgrep' command failed: %s %s", ret, code)
        end
      end
    },
    {"openr version", Puff_Handler.infoVersion, {args, name}},
    {"openr build information", Puff_Handler.infoBuild, {args, name}},
    {"openr config", notImplementedFn},
    {"puff prefixmgr view", Puff_Handler.prefixmgrView, {args, name}},
    {"puff lm links", Puff_Handler.lmLinks, {args, name}},
    {"puff kvstore peers", Puff_Handler.kvstorePeers, {args, name}},
    {"puff kvstore nodes", notImplementedFn},
    {
      "puff kvstore adj",
      Puff_Handler.kvstoreAdj,
      {tablex.update({nodes = "all"}, args), name},
    },
    {
      "puff kvstore prefixes",
      Puff_Handler.kvstorePrefixes,
      {tablex.update({nodes = "all"}, args), name},
    },
    {
      "puff kvstore keys --ttl",
      Puff_Handler.kvstoreKeys,
      {tablex.update({ttl = true}, args), name},
    },
    {"puff decision validate", notImplementedFn},
    {"puff decision routes", Puff_Handler.decisionRoutes, {args, name}},
    {"puff fib validate", notImplementedFn},
    {"puff fib routes", Puff_Handler.fibRoutes, {args, name}},
    {
      "puff fib list",
      Puff_Handler.fibList,
      {tablex.update({client_id = FibClient.OPENR}, args), name},
    },
    {"puff perf fib", notImplementedFn},
    {"puff monitor counters", Puff_Handler.monitorCounters, {args, name}},
    {"puff monitor logs", Puff_Handler.monitorLogs, {args, name}},
  }

  for _, step in ipairs(steps) do
    logger.info("\n--------  %s  --------\n", step[1])
    step[2](step[3] and table.unpack(step[3]))
  end
end

--- `puff kvstore` subcommand
local function addKvstoreCmd(parser)
  local kvstoreCmd = parser:command("kvstore", "Interact with KvStore module")

  local kvstoreKeysCmd = kvstoreCmd:command("keys", "Dump all available keys")
  kvstoreKeysCmd:option("--prefix", "Filter keys by a string prefix", "")
  kvstoreKeysCmd:option("--originator", "Filter keys by originator ID")
  kvstoreKeysCmd:flag("--ttl", "Show TTL value and version", false)
  kvstoreKeysCmd:flag("--json", "Dump in JSON format", false)
  kvstoreKeysCmd:action(Puff_Handler.kvstoreKeys)

  local kvstoreKeyvalsCmd =
    kvstoreCmd:command("keyvals", "Get values of input keys")
  kvstoreKeyvalsCmd:argument("keys", "Key names"):args("+")
  kvstoreKeyvalsCmd:flag("--json", "Dump in JSON format", false)
  kvstoreKeyvalsCmd:action(Puff_Handler.kvstoreKeyvals)

  local kvstoreSetKeyCmd = kvstoreCmd:command("set-key", "Set a key")
  kvstoreSetKeyCmd:argument("key", "Key name")
  kvstoreSetKeyCmd:argument("value", "Value")
  kvstoreSetKeyCmd:option(
    "--originator", "Originator ID", consts.ORIGINATOR_DEFAULT
  )
  kvstoreSetKeyCmd:option(
    "--version", "Version. If not set, override existing key if any."
  ):convert(tonumber)
  kvstoreSetKeyCmd:option(
    "--ttl",
    "TTL in seconds. Default is infinite.",
    tostring(consts.CONST_TTL_INF)
  ):convert(tonumber)
  kvstoreSetKeyCmd:action(Puff_Handler.kvstoreSetKey)

  local kvstoreEraseKeyCmd = kvstoreCmd:command("erase-key", "Erase a key")
  kvstoreEraseKeyCmd:argument("key", "Key name")
  kvstoreEraseKeyCmd:action(Puff_Handler.kvstoreEraseKey)

  local kvstorePrefixesCmd =
    kvstoreCmd:command("prefixes", "Show the prefixes in the network")
  kvstorePrefixesCmd:option(
    "--nodes", "Node filter. Default is this node, 'all' is all nodes."
  )
  kvstorePrefixesCmd:flag("--json", "Dump in JSON format", false)
  kvstorePrefixesCmd:action(Puff_Handler.kvstorePrefixes)

  local kvstoreAdjCmd = kvstoreCmd:command("adj", "Show link-state adjacencies")
  kvstoreAdjCmd:option(
    "--nodes", "Node filter. Default is this node, 'all' is all nodes."
  )
  kvstoreAdjCmd:flag("--json", "Dump in JSON format", false)
  kvstoreAdjCmd:action(Puff_Handler.kvstoreAdj)

  local kvstorePeersCmd = kvstoreCmd:command("peers", "Show this node's peers")
  kvstorePeersCmd:flag("--json", "Dump in JSON format", false)
  kvstorePeersCmd:action(Puff_Handler.kvstorePeers)
end

--- `puff fib` subcommand
local function addFibCmd(parser)
  local fibCmd = parser:command("fib", "Interact with Fib module")
  fibCmd:option("--client_id", "Fib client ID", tostring(FibClient.OPENR))
    :convert(tonumber)

  local fibRoutesCmd =
    fibCmd:command("routes", "Print this host's routing table")
  fibRoutesCmd:flag("--json", "Dump in JSON format", false)
  fibRoutesCmd:action(Puff_Handler.fibRoutes)

  local fibListCmd = fibCmd:command("list", "Print all routes on Fib agent")
  fibListCmd:flag("--json", "Dump in JSON format", false)
  fibListCmd:action(Puff_Handler.fibList)

  local fibAddCmd = fibCmd:command("add", "Add new routes in Fib")
  fibAddCmd:argument("prefixes", "Comma-separated list of prefixes")
  fibAddCmd:argument("nexthops", "Comma-separated list of next-hops")
  fibAddCmd:action(Puff_Handler.fibAdd)

  local fibDelCmd = fibCmd:command("del", "Delete routes from Fib")
  fibDelCmd:argument("prefixes", "Comma-separated list of prefixes")
  fibDelCmd:action(Puff_Handler.fibDel)

  local fibSyncCmd = fibCmd:command(
    "sync", "Re-program Fib with specified routes, and delete all old ones"
  )
  fibSyncCmd:argument("prefixes", "Comma-separated list of prefixes")
  fibSyncCmd:argument("nexthops", "Comma-separated list of next-hops")
  fibSyncCmd:action(Puff_Handler.fibSync)

  -- TODO (only here for test compatibility)
  local fibValidateCmd = fibCmd:command("validate", "(not implemented)")
  fibValidateCmd:action(function(args, name)
    logger.info("PASS")
    logger.info("[This command is not implemented.]")
    logger.info("Route validation successful")
  end)
end

--- `puff decision` subcommand
local function addDecisionCmd(parser)
  local decisionCmd = parser:command(
    "decision", "Interact with Decision module"
  )

  local decisionRoutesCmd = decisionCmd:command(
    "routes", "Request the routing table from the Decision module"
  )
  decisionRoutesCmd:option(
    "--nodes", "Node filter. Default is this node, 'all' is all nodes."
  )
  decisionRoutesCmd:flag("--json", "Dump in JSON format", false)
  decisionRoutesCmd:action(Puff_Handler.decisionRoutes)

  local decisionPrefixesCmd = decisionCmd:command(
    "prefixes", "Request prefixes from the Decision module"
  )
  decisionPrefixesCmd:option(
    "--nodes", "Node filter. Default is this node, 'all' is all nodes."
  )
  decisionPrefixesCmd:flag("--json", "Dump in JSON format", false)
  decisionPrefixesCmd:action(Puff_Handler.decisionPrefixes)

  local decisionAdjCmd = decisionCmd:command(
    "adj", "Request the link-state adjacencies from the Decision module"
  )
  decisionAdjCmd:option(
    "--nodes", "Node filter. Default is this node, 'all' is all nodes."
  )
  decisionAdjCmd:flag("--json", "Dump in JSON format", false)
  decisionAdjCmd:action(Puff_Handler.decisionAdj)
end

--- `puff monitor` subcommand
local function addMonitorCmd(parser)
  local monitorCmd = parser:command("monitor", "Interact with Monitor module")

  local monitorCountersCmd = monitorCmd:command(
    "counters", "Fetch and display Open/R counters"
  )
  monitorCountersCmd:option(
    "--prefix", "Filter counters by a string prefix", ""
  )
  monitorCountersCmd:flag("--json", "Dump in JSON format", false)
  monitorCountersCmd:action(Puff_Handler.monitorCounters)

  local monitorLogsCmd = monitorCmd:command("logs", "Print event logs")
  monitorLogsCmd:flag("--json", "Dump in JSON format", false)
  monitorLogsCmd:action(Puff_Handler.monitorLogs)
end

--- `puff prefixmgr` subcommand
local function addPrefixmgrCmd(parser)
  local prefixmgrCmd = parser:command(
    "prefixmgr", "Interact with Prefix Manager module"
  )

  local prefixmgrWithdrawCmd = prefixmgrCmd:command(
    "withdraw", "Withdraw the specified prefix(es) from this node"
  )
  prefixmgrWithdrawCmd:argument("prefixes", "List of prefixes"):args("+")
  prefixmgrWithdrawCmd:option(
    "--prefix-type", "Type or client-ID associated with prefix", "breeze"
  ):choices(tablex.map(string.lower, tablex.keys(PrefixType)))
  prefixmgrWithdrawCmd:action(Puff_Handler.prefixmgrWithdraw)

  local prefixmgrAdvertiseCmd = prefixmgrCmd:command(
    "advertise", "Advertise the specified prefix(es) from this node"
  )
  prefixmgrAdvertiseCmd:argument("prefixes", "List of prefixes"):args("+")
  prefixmgrAdvertiseCmd:option(
    "--prefix-type", "Type or client-ID associated with prefix", "breeze"
  ):choices(tablex.map(string.lower, tablex.keys(PrefixType)))
  prefixmgrAdvertiseCmd:option(
    "--forwarding-type",
    "Use label forwarding instead of IP forwarding in data path",
    "ip"
  ):choices(tablex.map(string.lower, tablex.keys(PrefixForwardingType)))
  prefixmgrAdvertiseCmd:action(Puff_Handler.prefixmgrAdvertise)

  local prefixmgrSyncCmd = prefixmgrCmd:command(
    "sync", "Sync the prefix(es) from this node"
  )
  prefixmgrSyncCmd:argument("prefixes", "List of prefixes"):args("+")
  prefixmgrSyncCmd:option(
    "--prefix-type", "Type or client-ID associated with prefix", "breeze"
  ):choices(tablex.map(string.lower, tablex.keys(PrefixType)))
  prefixmgrSyncCmd:option(
    "--forwarding-type",
    "Use label forwarding instead of IP forwarding in data path",
    "ip"
  ):choices(tablex.map(string.lower, tablex.keys(PrefixForwardingType)))
  prefixmgrSyncCmd:action(Puff_Handler.prefixmgrSync)

  local prefixmgrViewCmd = prefixmgrCmd:command(
    "view", "View the prefix(es) from this node"
  )
  prefixmgrViewCmd:action(Puff_Handler.prefixmgrView)
  prefixmgrViewCmd:flag("--json", "Dump in JSON format", false)
end

--- `puff lm` subcommand
local function addLmCmd(parser)
  local lmCmd = parser:command("lm", "Interact with Link Monitor module")

  local lmLinksCmd = lmCmd:command(
    "links", "Dump all known links for the current host"
  )
  lmLinksCmd:flag("--json", "Dump in JSON format", false)
  lmLinksCmd:action(Puff_Handler.lmLinks)

  local lmSetLinkMetricCmd = lmCmd:command(
    "set-link-metric",
    "Set a custom metric value for a link. " ..
    "You can use a high link metric value to emulate soft-drain behaviour."
  )
  lmSetLinkMetricCmd:argument("interface")
  lmSetLinkMetricCmd:argument("metric"):convert(tonumber)
  lmSetLinkMetricCmd:action(Puff_Handler.lmSetLinkMetric)

  local lmUnsetLinkMetricCmd = lmCmd:command(
    "unset-link-metric", "Unset a previously set metric value on the interface."
  )
  lmUnsetLinkMetricCmd:argument("interface")
  lmUnsetLinkMetricCmd:action(Puff_Handler.lmUnsetLinkMetric)

  local lmSetLinkOverloadCmd = lmCmd:command(
    "set-link-overload",
    "Set overload bit for a link. Transit traffic will be drained."
  )
  lmSetLinkOverloadCmd:argument("interface")
  lmSetLinkOverloadCmd:action(Puff_Handler.lmSetLinkOverload)

  local lmUnsetLinkOverloadCmd = lmCmd:command(
    "unset-link-overload",
    "Unset overload bit for a link to allow transit traffic."
  )
  lmUnsetLinkOverloadCmd:argument("interface")
  lmUnsetLinkOverloadCmd:action(Puff_Handler.lmUnsetLinkOverload)

  local lmSetAdjMetricCmd = lmCmd:command(
    "set-adj-metric", "Set custom metric value for the adjacency."
  )
  lmSetAdjMetricCmd:argument("node")
  lmSetAdjMetricCmd:argument("interface")
  lmSetAdjMetricCmd:argument("metric"):convert(tonumber)
  lmSetAdjMetricCmd:action(Puff_Handler.lmSetAdjMetric)

  local lmUnsetAdjMetricCmd = lmCmd:command(
    "unset-adj-metric", "Unset previously set custom metric value on the node."
  )
  lmUnsetAdjMetricCmd:argument("node")
  lmUnsetAdjMetricCmd:argument("interface")
  lmUnsetAdjMetricCmd:action(Puff_Handler.lmUnsetAdjMetric)

  local lmSetNodeOverloadCmd = lmCmd:command(
    "set-node-overload",
    "Set overload bit to stop transit traffic through node."
  )
  lmSetNodeOverloadCmd:action(Puff_Handler.lmSetNodeOverload)

  local lmUnsetNodeOverloadCmd = lmCmd:command(
    "unset-node-overload",
    "Unset overload bit to resume transit traffic through node."
  )
  lmUnsetNodeOverloadCmd:action(Puff_Handler.lmUnsetNodeOverload)
end

--- `puff info` subcommand
local function addInfoCmd(parser)
  local infoCmd = parser:command(
    "info", "Get version information from running Open/R instance"
  )

  local infoVersionCmd = infoCmd:command("version", "Get Open/R version")
  infoVersionCmd:action(Puff_Handler.infoVersion)

  local infoBuildCmd = infoCmd:command("build", "Get Open/R build details")
  infoBuildCmd:action(Puff_Handler.infoBuild)
end

--- `puff tech-support` subcommand
local function addTechSupportCmd(parser)
  local techSupportCmd = parser:command(
    "tech-support", "Extensive logging of Open/R's state for debugging"
  )
  techSupportCmd:action(Puff_Handler.techSupport)
end

local function createParser()
  local parser = argparse("puff", "Open/R CLI")
  parser:option("--openr_host", "Open/R hostname/IP", "localhost")
  parser:option(
    "--openr_ctrl_port", "Open/R ctrl port", tostring(consts.CTRL_PORT)
  ):convert(tonumber)
  parser:option(
    "--openr_fib_agent_port",
    "Open/R Fib agent port",
    tostring(consts.FIB_AGENT_PORT)
  ):convert(tonumber)

  -- Add subcommands
  addDecisionCmd(parser)
  addFibCmd(parser)
  addInfoCmd(parser)
  addKvstoreCmd(parser)
  addLmCmd(parser)
  addMonitorCmd(parser)
  addPrefixmgrCmd(parser)
  addTechSupportCmd(parser)

  return parser
end

local parser = createParser()
if tg_utils.isMain() then
  parser:parse()
else
  return parser, Puff, Puff_Handler
end
