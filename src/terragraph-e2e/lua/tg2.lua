#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Terragraph command-line interface.
-- @script tg2

require "terragraph_thrift.BWAllocation_ttypes"
require "terragraph_thrift.Controller_ttypes"
require "terragraph_thrift.DriverMessage_ttypes"
require "terragraph_thrift.Event_ttypes"
require "terragraph_thrift.FwOptParams_ttypes"
require "terragraph_thrift.NodeConfig_ttypes"
require "terragraph_thrift.PassThru_ttypes"
require "terragraph_thrift.Topology_ttypes"
require "fbzmq.Monitor_ttypes"
local tg_techsupport = require "tg.techsupport"
local tg_utils = require "tg.utils"
local tg_config_utils = require "tg.config_utils"
local tg_net_utils = require "tg.net_utils"
local tg_platform_utils = require "tg.platform_utils"
local tg_thrift_utils = require "tg.thrift_utils"
local tabulate = require "tg.tabulate"
local logger = require "tg.logger"
local argparse = require "argparse"
local signal = require "posix.signal"
local posix_time = require "posix.time"
local zmq = require "lzmq"
local tablex = require "pl.tablex"
local types = require "pl.types"
require("pl.stringx").import()

math.randomseed(os.time())

local consts = {
  -- Controller
  CONFIG_APP_CTRL_ID = "ctrl-app-CONFIG_APP",
  STATUS_APP_CTRL_ID = "ctrl-app-STATUS_APP",
  TOPOLOGY_APP_CTRL_ID = "ctrl-app-TOPOLOGY_APP",
  TOPOLOGY_BUILDER_APP_CTRL_ID = "ctrl-app-TOPOLOGYBUILDER_APP",

  -- Minion
  CONFIG_APP_MINION_ID = "minion-app-CONFIG_APP",
  DRIVER_APP_MINION_ID = "minion-app-DRIVER_APP",
  IGNITION_APP_MINION_ID = "minion-app-IGNITION_APP",
  STATUS_APP_MINION_ID = "minion-app-STATUS_APP",

  -- CLI-specific
  CLI_APP_ID = "TG2-CLI",
  KVSTORE_E2E_CTRL_URL = "e2e-ctrl-url",
  MINION_APPS_SOCK_FORWARD_PREFIX = ":FWD:",
  NODE_CONFIG = "/data/cfg/node_config.json",
  ZMQ_SUB_POLL_TIMEOUT_MS = 1000,

  -- MAC addresses
  EMPTY_MAC_ADDRESS = "00:00:00:00:00:00",
  BROADCAST_MAC_ADDRESS = "ff:ff:ff:ff:ff:ff",

  -- Runtime firmware parameters
  RUNTIME_FW_NODE_PARAMS = {
    "forceGpsDisable",
    "topoScanEnable",
    "crsScale",
    "maxAgcUseSameForAllSta",
    "polarity",
    "mtpoEnabled",
    "htsfMsgInterval",
  },
  RUNTIME_FW_LINK_PARAMS = {
    "txPower",
    "txBeamIndex",
    "rxBeamIndex",
    "maxTxPower",
    "minTxPower",
    "maxAgcRfGainHiLo",
    "maxAgcTrackingEnabled",
    "maxAgcTrackingMargindB",
    "linkAgc",
    "mcs",
    "measSlotEnable",
    "measSlotOffset",
    "laMaxMcs",
    "laMinMcs",
    "tpcEnable",
    "txGolayIdx",
    "rxGolayIdx",
    "restrictToSfParity",
    "linkImpairmentDetectionEnable",
    "latpcLinkImpairConfig",
    "latpc100PercentPERDrop",
    "tpcPBEnable",
  },
}

--- argparse validator (via "convert" function) used to set logger verbosity.
local function argparse_setLogLevel(level)
  logger.level = level
  return level
end

--- argparse validator (via "convert" function) for MAC addresses.
local function argparse_validateMac(arg)
  if tg_net_utils.isMacAddr(arg) then
    return arg
  else
    return nil,
      arg ..
      ": MAC addresses must be in two-digit, lowercase, colon-separated format"
  end
end

--- argparse validator (via "convert" function) for converting 1/0/true/false to
-- bool type.
local function argparse_toBool(arg)
  -- string type
  if string.lower(arg) == "true" then
    return true
  elseif string.lower(arg) == "false" then
    return false
  end

  -- int type
  local n = tonumber(arg)
  if n == nil then
    return nil
  end
  if n ~= 0 and n ~= 1 then
    return nil, string.format("Invalid value %d (must be 0 or 1)", n)
  end
  return n == 1 and true or false
end

--- Add "scan" command options (shared between "minion" and "fw" subcommands).
local function argparse_addScanCommandOptions(cmd)
  cmd:option("--radio_mac", "Radio MAC address"):convert(argparse_validateMac)
  cmd:option("--token", "Token to match request to response (default=random)")
    :convert(tonumber)
  cmd:option("-t --scan_type", "Scan type")
    :choices(tablex.map(string.lower, tablex.keys(ScanType)))
    :count(1)
  cmd:option("-m --scan_mode", "Scan mode", "fine")
    :choices(tablex.map(string.lower, tablex.keys(ScanMode)))
  cmd:option("--bwgd_idx", "BWGD index of scan start", "0"):convert(tonumber)
  cmd:mutex(
    cmd:flag("--tx", "Device is transmitter in scan", false),
    cmd:flag("--rx", "Device is receiver in scan", false)
  )
  cmd:flag(
    "--bf_scan_invert_polarity",
    "Invert polarity if both nodes have the same polarity",
    false
  )
  cmd:option(
    "-b --beams",
    "Specific beam indices to use (if set). This takes a pair: " ..
    "<start_idx> <end_idx> (inclusive). For relative PBF, 'end_idx' is used " ..
    "as the one-sided codebook range to use relative to the current beam, " ..
    "e.g. specifying `-b 0 5` means sweeping the current codebook beam +/- 5."
  ):args(2)
  cmd:flag(
    "--no_apply",
    "Do not apply new beam after scan procedure if new beams are selected",
    false
  )
  cmd:option("--sub_type", "Scan subtype", "no_cal")
    :choices(tablex.map(string.lower, tablex.keys(ScanSubType)))
  cmd:option("--bwgd_len", "BWGD duration for which scan is active", "64")
    :convert(tonumber)
  cmd:option(
    "--tx_power_index", "Transmit power index used during scanning", "16"
  ):convert(tonumber)
  cmd:option("-p --peer", "Peer MAC address for the scan")
    :convert(argparse_validateMac)
  cmd:option("--null_angle", "Null angle for CBF"):convert(tonumber)
  cmd:option("--cbf_beam_idx", "Beam index for CBF"):convert(tonumber)
  cmd:flag("--aggressor", "Aggressor flag for CBF")
end

--- Build `ScanReq` from options provided in `argparse_addScanCommandOptions()`.
--
-- Returns `ScanReq` upon success, otherwise a tuple [nil, errorStr] upon error.
local function argparse_buildScanRequest(args)
  local scanType = ScanType[args.scan_type:upper()]
  local scanMode = ScanMode[args.scan_mode:upper()]

  -- Basic validation
  if scanMode == ScanMode.SELECTIVE and not args.beams then
    return nil, "Selective scan mode requires '--beams' (beam indices)."
  end
  if args.tx_power_index < 0 or args.tx_power_index > 31 then
    return nil, "'--tx_power_index' must be in range [0, 31]."
  end

  -- Construct request
  local req = ScanReq:new{}
  req.radioMac = args.radio_mac
  req.token = args.token or math.random(2^31)
  req.scanType = scanType
  if scanType ~= ScanType.TOPO then
    req.scanMode = scanMode
    req.apply = not args.no_apply
    -- For selective scan
    req.subType = ScanSubType[args.sub_type:upper()]
    req.bwgdLen = args.bwgd_len
  end
  req.startBwgdIdx = args.bwgd_idx
  req.bfScanInvertPolarity = args.bf_scan_invert_polarity
  if req.scanType == ScanType.TOPO then
    req.rxNodeMac = consts.BROADCAST_MAC_ADDRESS
  elseif args.tx then
    req.rxNodeMac = args.peer
  elseif args.rx then
    req.txNodeMac = args.peer
  else
    return nil, "Please enter '--tx' (transmitter) or '--rx' (receiver)."
  end
  if args.beams then
    req.beams = BeamIndices:new{}
    req.beams.low = args.beams[1]
    req.beams.high = args.beams[2]
  end
  req.txPwrIndex = args.tx_power_index
  -- For CBF scans
  req.nullAngle = args.null_angle
  req.cbfBeamIdx = args.cbf_beam_idx
  req.isAggressor = args.aggressor

  return req, nil
end

--- Convert a link quality metric (LQM) to signal-to-noise ratio (SNR), in dB.
local function lqmToSnr(lqm)
  return (lqm - 256) / 8
end

--- Convert a beam index to beam angle, in degrees.
local function beamIndexToAngle(beamIdx)
  return (beamIdx * 1.5) - 45
end

--- Given an initiator-to-responder LQM matrix, return three values:
-- best SNR, Tx beam angle, Rx beam angle
--
-- This searches for the best beam with the smallest combined angle.
local function findBestBeam(itorLqmMat)
  local bestLqm = 0
  local bestTxBeamAngle, bestRxBeamAngle
  local bestCombinedAngle = 0
  for txBeamIdx, rxLqmMat in pairs(itorLqmMat) do
    local txBeamAngle = beamIndexToAngle(txBeamIdx)
    for rxBeamIdx, lqm in pairs(rxLqmMat) do
      local rxBeamAngle = beamIndexToAngle(rxBeamIdx)
      local combinedAngle = math.abs(txBeamAngle) + math.abs(rxBeamAngle)
      if lqm > bestLqm or
         (lqm == bestLqm and combinedAngle < bestCombinedAngle) then
        bestLqm = lqm
        bestTxBeamAngle = txBeamAngle
        bestRxBeamAngle = rxBeamAngle
        bestCombinedAngle = combinedAngle
      end
    end
  end
  local bestSnr = bestLqm and lqmToSnr(bestLqm) or nil
  return bestSnr, bestTxBeamAngle, bestRxBeamAngle
end

--- Print results from a topology scan in table format.
--
-- - `txNode` is the initiator MAC or name
-- - `topoResponderInfo` is the list of responders (`TopoResponderInfo` structs)
-- - `respToSite` is a map of responder MAC to nearest site name (optional)
local function printTopoScanResults(
  txNode, topoResponderInfo, respToSite
)
  -- Process results from each responder
  local results = {}
  for _, v in ipairs(topoResponderInfo or {}) do
    local bestSnr, bestTxBeamAngle, bestRxBeamAngle = findBestBeam(v.itorLqmMat)
    local location = v.pos and tg_utils.formatPositionString(
      v.pos.latitude, v.pos.longitude
    ) or nil
    results[#results+1] = {
      addr = v.addr,
      bestSnr = bestSnr,
      bestTxBeamAngle = bestTxBeamAngle,
      bestRxBeamAngle = bestRxBeamAngle,
      location = location,
      adjs = tablex.keys(v.adjs or {}),
      site = respToSite and respToSite[v.addr] or nil,
    }
  end

  -- Sort by SNR
  table.sort(results, function(a, b) return a.bestSnr > b.bestSnr end)

  -- Group by node using adjacency information
  local resultsByNode = {}  -- each entry is an array of results
  local macToResult = {}
  for _, result in ipairs(results) do
    local nodeIdx
    for __, adj in ipairs(result.adjs) do
      nodeIdx = macToResult[adj]
      if nodeIdx ~= nil then
        break  -- found existing node entry
      end
    end
    if nodeIdx == nil then
      -- New node
      nodeIdx = #resultsByNode + 1
      resultsByNode[nodeIdx] = {}
    end
    table.insert(resultsByNode[nodeIdx], result)
    macToResult[result.addr] = nodeIdx
  end

  -- Print in table format
  local headers = {
    "Node",
    "MAC Addrs",
    "SNR (dB)",
    "Tx Angle",
    "Rx Angle",
    "Location",
    "Adjacencies",
    respToSite and "Site" or nil,
  }
  local rows = {}
  for i, objs in ipairs(resultsByNode) do
    -- Per-responder fields
    local addr = tablex.map(function(o) return o.addr or "" end, objs)
    local bestSnr =
      tablex.map(function(o) return o.bestSnr or "" end, objs)
    local bestTxBeamAngle =
      tablex.map(function(o) return o.bestTxBeamAngle or "" end, objs)
    local bestRxBeamAngle =
      tablex.map(function(o) return o.bestRxBeamAngle or "" end, objs)
    local location =
      tablex.map(function(o) return o.location or "" end, objs)
    local site = respToSite
      and tablex.map(function(o) return o.site or "" end, objs)
      or nil

    -- Look for any other adjacencies not captured by node grouping
    local adjs = {}
    for _, obj in ipairs(objs) do
      for __, adj in ipairs(obj.adjs) do
        if macToResult[adj] == nil then
          adjs[adj] = true
        end
      end
    end

    rows[#rows+1] = {
      i,
      ("\n"):join(addr),
      ("\n"):join(bestSnr),
      ("\n"):join(bestTxBeamAngle),
      ("\n"):join(bestRxBeamAngle),
      ("\n"):join(location),
      ("\n"):join(tablex.keys(adjs)),
      respToSite and ("\n"):join(site) or nil,
    }
  end
  logger.info(
    "\n== Topology Scan from %s - %d responder(s), %d node(s) ==\n",
    txNode,
    #results,
    #resultsByNode
  )
  logger.info(
    "%s\n",
    tabulate.tabulate(rows, {headers = headers, skip_lines = true})
  )
end

--- Print the given stat counter (unless filtered out based on `entityFilter`).
local function printCounter(counterName, counter, entityFilter)
  -- For stats originating from firmware, names are formatted:
  --   [key_name]\0[entity]
  local entity = ""
  local nullCharIdx = counterName:find("\0")
  if nullCharIdx ~= nil then
    entity = counterName:sub(nullCharIdx + 1)
    counterName = counterName:sub(1, nullCharIdx)
  end

  -- Filtered out?
  if entityFilter ~= nil and entityFilter ~= entity then
    return
  end

  -- Print stats
  if entityFilter ~= nil or entity:len() == 0 then
    logger.info(
      "%s, %s, %s",
      counter.timestamp,
      counterName,
      tg_utils.stringifyNumber(counter.value)
    )
  else
    logger.info(
      "%s, %s, %s, %s",
      counter.timestamp,
      counterName,
      tg_utils.stringifyNumber(counter.value),
      entity
    )
  end
end

--- Validate a list of arguments representing pairs of runtime firmware
-- parameters.
--
-- Returns a tuple: [success:bool, errorMsg:str|nil]
local function validateRuntimeFwParams(params, responderMac)
  if (#params % 2) ~= 0 then
    return false, "Please enter a list of pairs: <paramName> <paramValue> ..."
  end
  for idx, x in ipairs(params) do
    if idx % 2 == 1 then
      -- param name
      if tablex.find(consts.RUNTIME_FW_LINK_PARAMS, x) ~= nil then
        if responderMac == nil then
          return false,
            "'--responder_mac' is required for link parameter '" .. x .. "'."
        end
      elseif tablex.find(consts.RUNTIME_FW_NODE_PARAMS, x) == nil then
        return false, "Unsupported parameter value '" .. x .. "'."
      end
    else
      -- param value
      if tonumber(x) == nil then
        return false, "Parameter value '" .. x .. "' is not a number."
      end
    end
  end
  return true, nil
end

--- TG CLI class
local TG = {}
TG.__index = TG

--- Constructor for TG CLI.
function TG.new(args)
  local self = setmetatable({}, TG)
  self.ctx = zmq.context()
  self.zmq_id = consts.CLI_APP_ID .. "-" .. math.random(1, 1000000)
  -- ZMQ sockets
  self.controller_sock = nil
  self.minion_sock = nil
  self.driver_if_sock = nil
  self.agent_sock = nil
  -- Connection options
  self.fetch_controller_host = args.fetch_controller_host
  self.controller_host = args.controller_host
  self.controller_port = args.controller_port
  self.minion_host = args.minion_host
  self.minion_port = args.minion_port
  self.minion_pub_port = args.minion_pub_port
  self.driver_if_host = args.driver_if_host
  self.driver_if_port = args.driver_if_port
  self.agent_host = args.agent_host
  self.agent_port = args.agent_port
  -- ZMQ socket options
  self.router_recv_timeout = args.router_recv_timeout
  self.router_send_timeout = args.router_send_timeout
  self.pair_recv_timeout = args.pair_recv_timeout
  self.pair_send_timeout = args.pair_send_timeout
  self.pub_recv_timeout = args.pub_recv_timeout
  self.pub_send_timeout = args.pub_send_timeout
  return self
end

--- Clean up.
function TG:close()
  if self.controller_sock ~= nil then
    self.controller_sock:close()
    self.controller_sock = nil
  end
  if self.minion_sock ~= nil then
    self.minion_sock:close()
    self.minion_sock = nil
  end
  if self.driver_if_sock ~= nil then
    self.driver_if_sock:close()
    self.driver_if_sock = nil
  end
  if self.agent_sock ~= nil then
    self.agent_sock:close()
    self.agent_sock = nil
  end
  if self.ctx ~= nil then
    self.ctx:term()
    self.ctx = nil
  end
end

--- Serialize to `thrift::DriverMessage`.
function TG.serializeDriverMsg(data, radioMac)
  local drMsg = DriverMessage:new{}
  drMsg.radioMac = radioMac
  drMsg.value = tg_thrift_utils.serialize(data)
  return drMsg
end

--- Deserialize from `thrift::DriverMessage`, and return
-- `DriverMessage.radioMac`.
function TG.deserializeDriverMsg(data, msgData)
  local drMsg = DriverMessage:new{}
  tg_thrift_utils.deserialize(data, drMsg)
  tg_thrift_utils.deserialize(drMsg.value, msgData)
  return drMsg.radioMac
end

--- Connect to a ZMQ socket of the given type and returns (socket, error).
function TG:connectToSocket(type, opts, host, port, recvTimeout, sendTimeout)
  if tg_net_utils.isIPv6(host) then
    host = string.format("[%s]", host)
  end
  local url = string.format("tcp://%s:%s", host, port)
  logger.debug("Connecting to %s...", url)
  local sktOpts = {
    connect = url,
    linger = 250,
    rcvtimeo = recvTimeout,
    sndtimeo = sendTimeout,
    ipv6 = 1,
    identity = self.zmq_id,
  }
  tg_utils.tableMerge(sktOpts, opts)
  return self.ctx:socket(type, sktOpts)
end

--- Connect to a ZMQ ROUTER socket and returns (socket, error).
function TG:connectToRouter(host, port, recvTimeout, sendTimeout)
  return self:connectToSocket(
    zmq.DEALER,
    {},
    host,
    port,
    recvTimeout or self.router_recv_timeout,
    sendTimeout or self.router_send_timeout
  )
end

--- Connect to a ZMQ PAIR socket and returns (socket, error).
function TG:connectToPair(host, port, recvTimeout, sendTimeout)
  return self:connectToSocket(
    zmq.PAIR,
    {},
    host,
    port,
    recvTimeout or self.pair_recv_timeout,
    sendTimeout or self.pair_send_timeout
  )
end

--- Connect to a ZMQ PUB socket and returns (socket, error).
function TG:connectToPub(host, port, recvTimeout, sendTimeout)
  local subscriber, err = self:connectToSocket(
    zmq.SUB,
    {subscribe = ""},
    host,
    port,
    recvTimeout or self.pub_recv_timeout,
    sendTimeout or self.pub_send_timeout
  )
  -- ZMQ's "slow joiner syndrome" means we need to sleep 200ms to ensure we
  -- aren't losing messages (documented in chapter 5 of the ZMQ guide).
  posix_time.nanosleep({tv_sec = 0, tv_nsec = 200000000})
  return subscriber, err
end

--- Connect to the E2E minion's ZMQ PUB socket and returns (socket, error).
function TG:connectToMinionPub(recvTimeout, sendTimeout)
  return self:connectToPub(
    self.minion_host, self.minion_pub_port, recvTimeout, sendTimeout
  )
end

--- Fetch E2E controller's hostname/IP from Open/R's KvStore.
function TG:autoFetchControllerHost()
  local url = tg_platform_utils.getOpenrKey(consts.KVSTORE_E2E_CTRL_URL)
  if url == nil then
    return nil
  end

  local ZMQ_PROTOCOL = "tcp://"
  if url:find(ZMQ_PROTOCOL) ~= 1 then
    return nil
  end
  local portIdx = url:find(":%d+$")
  if not portIdx then
    return nil
  end
  return url:sub(ZMQ_PROTOCOL:len() + 1, portIdx - 1)
end

--- Connect to the E2E controller's router socket.
function TG:connectToCtrl(recvTimeout, sendTimeout)
  local controllerHost = self.fetch_controller_host
    and self:autoFetchControllerHost()
    or self.controller_host
  local sock, err = self:connectToRouter(
    controllerHost, self.controller_port, recvTimeout, sendTimeout
  )
  if err then
    logger.error("Failed to connect to controller: %s", err)
    return false
  else
    self.controller_sock = sock
    return true
  end
end

--- Connect to the E2E minion's router socket.
function TG:connectToMinion(recvTimeout, sendTimeout)
  -- Minion forwards responses when target ZMQ ID has this prefix
  self.zmq_id = consts.MINION_APPS_SOCK_FORWARD_PREFIX .. self.zmq_id

  local sock, err = self:connectToRouter(
    self.minion_host, self.minion_port, recvTimeout, sendTimeout
  )
  if err then
    logger.error("Failed to connect to minion: %s", err)
    return false
  else
    self.minion_sock = sock
    return true
  end
end

--- Connect to the E2E driver-if's pair socket.
function TG:connectToDriverIf(recvTimeout, sendTimeout)
  local sock, err = self:connectToPair(
    self.driver_if_host, self.driver_if_port, recvTimeout, sendTimeout
  )
  if err then
    logger.error("Failed to connect to driver-if: %s", err)
    return false
  else
    self.driver_if_sock = sock
    return true
  end
end

--- Connect to the stats agent's router socket.
function TG:connectToStatsAgent(recvTimeout, sendTimeout)
  local sock, err = self:connectToRouter(
    self.agent_host, self.agent_port, recvTimeout, sendTimeout
  )
  if err then
    logger.error("Failed to connect to stats agent: %s", err)
    return false
  else
    self.agent_sock = sock
    return true
  end
end

--- Send a message to the E2E controller.
function TG:sendToCtrl(msgType, msgData, receiverApp, minion)
  minion = minion or ""

  local data = tg_thrift_utils.serialize(Message:new{
    mType = msgType,
    value = tg_thrift_utils.serialize(msgData),
  })

  self.controller_sock:send_more(minion)
  self.controller_sock:send_more(receiverApp)
  self.controller_sock:send_more(self.zmq_id)
  self.controller_sock:send(data)
end

--- Send a message to the E2E minion.
function TG:sendToMinion(msgType, msgData, receiverApp)
  local data = tg_thrift_utils.serialize(Message:new{
    mType = msgType,
    value = tg_thrift_utils.serialize(msgData),
  })

  -- Passing dummy "minion" message part (just needs to be non-empty)
  self.minion_sock:send_more("dummy")
  self.minion_sock:send_more(receiverApp)
  self.minion_sock:send_more(self.zmq_id)
  self.minion_sock:send(data)
end

--- Send a message to the E2E driver-if.
function TG:sendToDriverIf(msgType, msgData, radioMac)
  local drMsg = TG.serializeDriverMsg(msgData, radioMac)
  local data = tg_thrift_utils.serialize(Message:new{
    mType = msgType,
    value = tg_thrift_utils.serialize(drMsg),
  })

  self.driver_if_sock:send(data)
end

--- Send a message to the stats agent.
function TG:sendToStatsAgent(msgType, msgData)
  local data = tg_thrift_utils.serialize(Message:new{
    mType = msgType,
    value = tg_thrift_utils.serialize(msgData),
  })

  self.agent_sock:send(data)
end

--- Receive a message from the E2E controller.
function TG:recvFromCtrl(msgType, msgData, senderApp)
  -- Receive message
  local minion, app, serMsg = self.controller_sock:recvx()
  if minion == nil then
    logger.error(app) -- app => error
    return false
  end
  if app ~= senderApp then
    logger.error("Unexpected sender app %s, expected %s", app, senderApp)
    return false
  end

  -- Deserialize message
  local deserMsg = Message:new{}
  tg_thrift_utils.deserialize(serMsg, deserMsg)
  if deserMsg.mType ~= msgType then
    local invMessageType = tg_utils.invertTable(MessageType)
    logger.error(
      "Unexpected message type %s (#%d), expected %s (#%d)",
      invMessageType[deserMsg.mType] or "unknown",
      deserMsg.mType,
      invMessageType[msgType],
      msgType
    )

    -- Controller may return E2EAck on error, so print it here if applicable
    if deserMsg.mType == MessageType.E2E_ACK then
      local ack = E2EAck:new{}
      tg_thrift_utils.deserialize(deserMsg.value, ack)
      if not ack.success and ack.message ~= "" then
        logger.error("Error: %s", ack.message)
      end
    end

    return false
  end
  tg_thrift_utils.deserialize(deserMsg.value, msgData)
  return true
end

--- Receive a message from the E2E minion.
function TG:recvFromMinion(msgType, msgData, senderApp)
  -- Receive message
  local app, serMsg = self.minion_sock:recvx()
  if app == nil then
    logger.error(serMsg) -- serMsg => error
    return false
  end
  if app ~= senderApp then
    logger.error(
      "Unexpected sender app %s, expected %s",
      app,
      senderApp
    )
    return false
  end

  -- Deserialize message
  local deserMsg = Message:new{}
  tg_thrift_utils.deserialize(serMsg, deserMsg)
  if deserMsg.mType ~= msgType then
    local invMessageType = tg_utils.invertTable(MessageType)
    logger.error(
      "Unexpected message type %s (#%d), expected %s (#%d)",
      invMessageType[deserMsg.mType] or "unknown",
      deserMsg.mType,
      invMessageType[msgType],
      msgType
    )
    return false
  end
  tg_thrift_utils.deserialize(deserMsg.value, msgData)
  return true
end

--- Receive a message from the E2E driver-if.
--
-- * If `radioMac` is provided, then `DriverMessage.radioMac` must match.
-- * If `filterLinkMacAddr` is provided, then `msgData.macAddr` must match.
function TG:recvFromDriverIf(msgType, msgData, radioMac, filterLinkMacAddr)
  local invMessageType = tg_utils.invertTable(MessageType)
  local function recvImpl()
    -- Receive message
    local serMsg, err = self.driver_if_sock:recv()
    if serMsg == nil then
      logger.error(err)
      return false
    end

    -- Deserialize message
    local deserMsg = Message:new{}
    tg_thrift_utils.deserialize(serMsg, deserMsg)
    local rcvdRadioMac = TG.deserializeDriverMsg(deserMsg.value, msgData)
    if radioMac ~= nil and rcvdRadioMac ~= radioMac then
      logger.debug(
        "Ignoring %s (#%d) from unexpected radio MAC %s (expected %s)",
        invMessageType[deserMsg.mType] or "unknown",
        deserMsg.mType,
        rcvdRadioMac,
        radioMac
      )
      return false
    end
    if deserMsg.mType ~= msgType then
      logger.debug(
        "Ignoring unexpected message type %s (#%d), expected %s (#%d)",
        invMessageType[deserMsg.mType] or "unknown",
        deserMsg.mType,
        invMessageType[msgType],
        msgType
      )
      return false
    end
    if filterMacAddr ~= nil and deserMsg.macAddr ~= filterLinkMacAddr then
      logger.debug(
        "Ignoring message for unexpected link MAC address %s (expected %s)",
        deserMsg.macAddr,
        filterLinkMacAddr
      )
      return false
    end
    return true
  end

  -- Continue receiving messages until the expected message is received,
  -- or we hit our specified timeout period.
  local maxRecvTime = self.pair_recv_timeout / 1000
  local startTime = posix_time.clock_gettime(posix_time.CLOCK_MONOTONIC)
  while true do
    local now = posix_time.clock_gettime(posix_time.CLOCK_MONOTONIC)
    if tg_utils.timespecSub(now, startTime).tv_sec >= maxRecvTime then
      break
    end
    if recvImpl() then
      return true
    end
  end
  logger.error("Timed out!")
  return false
end

--- Receive a FwAck message from the E2E driver-if and handle the response.
function TG:recvFwAckFromDriverIf(radioMac)
  local fwAck = FwAck:new{}
  if self:recvFromDriverIf(MessageType.FW_ACK, fwAck, radioMac) then
    if fwAck.success then
      logger.info("Firmware command succeeded.")
    else
      tg_utils.abort("Firmware command failed.")
    end
  else
    tg_utils.abort("Failed to receive data from driver-if")
  end
end

--- Start a subscriber loop, repeatedly reading messages from the minion's ZMQ
-- PUB socket.
--
-- `msgHandler` is a callback function that is given two arguments:
-- the app name (string) and deserialized Thrift "Message" struct.
--
-- The loop ends when `msgHandler` returns true, or after `maxPollTime` seconds.
-- This function returns true in the former case, or false otherwise.
function TG:doMinionSubLoop(subscriber, msgHandler, maxPollTime)
  local startTime = posix_time.clock_gettime(posix_time.CLOCK_MONOTONIC)
  while true do
    local now = posix_time.clock_gettime(posix_time.CLOCK_MONOTONIC)
    if maxPollTime ~= nil and
       tg_utils.timespecSub(now, startTime).tv_sec >= maxPollTime then
      break
    end

    -- Receive message
    if subscriber:poll(consts.ZMQ_SUB_POLL_TIMEOUT_MS) then
      local app, serMsg = subscriber:recvx()
      if app == nil then
        logger.error(serMsg) -- serMsg => error
        break
      else
        -- Deserialize message
        local deserMsg = Message:new{}
        tg_thrift_utils.deserialize(serMsg, deserMsg)

        -- Invoke given message handler
        if msgHandler(app, deserMsg) then
          return true
        end
      end
    end
  end
  return false
end

--- Wait for a link status event for the given responder, returning a pair
-- (LinkStatusType, LinkDownCause) or nil upon timeout.
function TG:minionWaitForLinkStatus(subscriber, responderMac, maxPollTime)
  local linkStatusType, linkDownCause
  self:doMinionSubLoop(subscriber, function(app, deserMsg)
    -- We listen for two messages:
    --   DR_LINK_STATUS - sent from firmware (always accurate), but ignore
    --                    LINK_PAUSE as that is a transient state
    --   LINK_STATUS - sent from e2e_minion, but only process LINK_UP which may
    --                 be generated when e.g. assoc is sent for an existing link
    if deserMsg.mType == MessageType.DR_LINK_STATUS then
      local msgData = DriverLinkStatus:new{}
      TG.deserializeDriverMsg(deserMsg.value, msgData)
      if msgData.macAddr ~= responderMac then
        logger.debug(
          "Ignoring DR_LINK_STATUS for responder " .. msgData.macAddr
        )
        return false
      end
      if msgData.drLinkStatusType == DriverLinkStatusType.LINK_PAUSE then
        logger.debug("Ignoring LINK_PAUSE in DR_LINK_STATUS")
        return false
      end
      linkStatusType = msgData.drLinkStatusType == DriverLinkStatusType.LINK_UP
        and LinkStatusType.LINK_UP
        or LinkStatusType.LINK_DOWN
      linkDownCause = msgData.linkDownCause
      return true -- stop looping
    elseif deserMsg.mType == MessageType.LINK_STATUS then
      local msgData = LinkStatus:new{}
      tg_thrift_utils.deserialize(deserMsg.value, msgData)
      if msgData.responderMac ~= responderMac then
        logger.debug(
          "Ignoring LINK_STATUS for responder " .. msgData.responderMac
        )
        return false
      end
      if msgData.linkStatusType == LinkStatusType.LINK_DOWN then
        logger.debug("Ignoring LINK_DOWN in LINK_STATUS")
        return false
      end
      linkStatusType = msgData.linkStatusType
      return true -- stop looping
    end
    return false -- keep looping
  end, maxPollTime)
  return linkStatusType, linkDownCause
end

--- Repeatedly poll for minion status reports.
--
-- `msgHandler` is a callback function that is given one argument: the status
-- report structure.
--
-- The loop ends when `msgHandler` returns true, or after `maxPollTime` seconds.
-- This function returns true in the former case, or false otherwise.
function TG:doMinionStatusReportLoop(msgHandler, maxPollTime)
  local startTime = posix_time.clock_gettime(posix_time.CLOCK_MONOTONIC)
  while true do
    local now = posix_time.clock_gettime(posix_time.CLOCK_MONOTONIC)
    if maxPollTime ~= nil and
       tg_utils.timespecSub(now, startTime).tv_sec >= maxPollTime then
      break
    end

    -- Request status report
    local req = GetStatusReport:new{}
    self:sendToMinion(
      MessageType.GET_STATUS_REPORT, req, consts.STATUS_APP_MINION_ID
    )

    -- Wait for status report reply
    local report = StatusReport:new{}
    if self:recvFromMinion(
      MessageType.STATUS_REPORT, report, consts.STATUS_APP_MINION_ID
    ) then
      -- Invoke given message handler
      if msgHandler(report) then
        return true
      end
      -- Wait 200ms before next query
      posix_time.nanosleep({tv_sec = 0, tv_nsec = 200000000})
    else
      tg_utils.abort("Failed to receive data from minion")
    end
  end
  return false
end

--- Wait for a RadioStatus field to be set for a specific radio (or any radio
-- if nil), returning true if found.
function TG:minionWaitForRadioStatus(radioMac, field, maxPollTime)
  return self:doMinionStatusReportLoop(function(report)
    if tablex.size(report.radioStatus or {}) < 1 then
      return false
    end
    if radioMac ~= nil then
      -- Check specific radio
      local radioStatus = report.radioStatus[radioMac]
      return radioStatus ~= nil and radioStatus[field]
    else
      -- Check any radio
      return #tablex.filter(
        tablex.values(report.radioStatus), function(v) return v[field] end
      ) > 0
    end
  end, maxPollTime)
end

--- Fetch all radio MACs from minion and return them as a list.
--
-- If `maybeRadioMac` is non-empty, this will NOT fetch anything and only return
-- the argument in a single-item list.
function TG:getMinionRadioMacs(maybeRadioMac)
  -- Don't fetch anything?
  if maybeRadioMac and #maybeRadioMac > 0 then
    return {maybeRadioMac}
  end

  -- Poll status report once
  local macs = {}
  self:doMinionStatusReportLoop(function(report)
    for radioMac, radioStatus in pairs(report.radioStatus or {}) do
      if radioStatus.initialized then
        macs[#macs+1] = radioMac
      end
    end
    return true
  end)
  return macs
end

--- TG CLI handlers static class
local TG_Handler = {}
TG_Handler.__index = TG_Handler

--- `tg2 topology ls` command handler
function TG_Handler.topologyLs(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send topology request
  tg:sendToCtrl(
    MessageType.GET_TOPOLOGY, GetTopology:new{}, consts.TOPOLOGY_APP_CTRL_ID
  )
  local topology = Topology:new{}
  if tg:recvFromCtrl(
    MessageType.TOPOLOGY, topology, consts.TOPOLOGY_APP_CTRL_ID
  ) then
    if args.json then
      logger.info(tg_thrift_utils.thriftToJson(topology))
    else
      local getNodesTable = function()
        local invNodeType = tg_utils.invertTable(NodeType)
        local invNodeStatusType = tg_utils.invertTable(NodeStatusType)
        local headers = {
          "Node Name",
          "Node MAC",
          "Radio MACs",
          "POP Node",
          "Type",
          "Status",
          "Site",
        }
        local rows = {}
        for k, v in pairs(topology.nodes or {}) do
          rows[#rows+1] = {
            v.name,
            v.mac_addr,
            ("\n"):join(v.wlan_mac_addrs),
            v.pop_node and "Yes" or "No",
            invNodeType[v.node_type] or "?",
            invNodeStatusType[v.status] or "UNKNOWN",
            v.site_name,
          }
        end
        table.sort(rows, function(a, b) return a[1] < b[1] end)  -- by name
        return rows, headers
      end
      local getLinksTable = function()
        local invLinkType = tg_utils.invertTable(LinkType)
        local headers = {
          "Link Name",
          "A-Node",
          "Z-Node",
          "Status",
          "Type",
          "Ignition Attempts",
        }
        local rows = {}
        for k, v in pairs(topology.links or {}) do
          rows[#rows+1] = {
            v.name,
            v.a_node_name .. "\n" .. v.a_node_mac,
            v.z_node_name .. "\n" .. v.z_node_mac,
            v.is_alive and "Up" or "Down",
            invLinkType[v.link_type] or "UNKNOWN",
            v.linkup_attempts,
          }
        end
        table.sort(rows, function(a, b) return a[1] < b[1] end)  -- by name
        return rows, headers
      end
      local getSitesTable = function()
        local headers = {
          "Site Name", "Latitude", "Longitude", "Altitude", "Accuracy"
        }
        local rows = {}
        for k, v in pairs(topology.sites or {}) do
          rows[#rows+1] = {
            v.name,
            v.location.latitude,
            v.location.longitude,
            v.location.altitude,
            v.location.accuracy,
          }
        end
        table.sort(rows, function(a, b) return a[1] < b[1] end)  -- by name
        return rows, headers
      end
      local nodeRows, nodeHeaders = getNodesTable()
      local linkRows, linkHeaders = getLinksTable()
      local siteRows, siteHeaders = getSitesTable()
      logger.info(
        "\n%s\n\n%s\n\n%s\n",
        tabulate.tabulate(nodeRows, {headers = nodeHeaders}),
        tabulate.tabulate(linkRows, {headers = linkHeaders}),
        tabulate.tabulate(siteRows, {headers = siteHeaders})
      )
    end
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 topology site add` command handler
function TG_Handler.topologySiteAdd(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send request
  local req = AddSite:new{}
  req.site = Site:new{}
  req.site.name = args.name
  req.site.location = Location:new{}
  req.site.location.latitude = args.lat
  req.site.location.longitude = args.lon
  req.site.location.altitude = args.alt
  req.site.location.accuracy = args.acc
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToCtrl(MessageType.ADD_SITE, req, consts.TOPOLOGY_APP_CTRL_ID)

  -- Receive ACK
  local ack = E2EAck:new{}
  if tg:recvFromCtrl(MessageType.E2E_ACK, ack, consts.TOPOLOGY_APP_CTRL_ID) then
    logger.info("%s: %s", ack.success and "SUCCESS" or "FAIL", ack.message)
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 topology site del` command handler
function TG_Handler.topologySiteDel(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send request
  local req = DelSite:new{}
  req.siteName = args.name
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToCtrl(MessageType.DEL_SITE, req, consts.TOPOLOGY_APP_CTRL_ID)

  -- Receive ACK
  local ack = E2EAck:new{}
  if tg:recvFromCtrl(MessageType.E2E_ACK, ack, consts.TOPOLOGY_APP_CTRL_ID) then
    logger.info("%s: %s", ack.success and "SUCCESS" or "FAIL", ack.message)
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 topology node add` command handler
function TG_Handler.topologyNodeAdd(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send request
  local req = AddNode:new{}
  req.node = Node:new{}
  req.node.name = args.name
  req.node.node_type = NodeType[args.node_type:upper()]
  req.node.mac_addr = args.mac_addr or ""
  req.node.pop_node = args.pop_node
  req.node.wlan_mac_addrs = args.wlan_mac_addrs or {}
  req.node.site_name = args.site_name
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToCtrl(MessageType.ADD_NODE, req, consts.TOPOLOGY_APP_CTRL_ID)

  -- Receive ACK
  local ack = E2EAck:new{}
  if tg:recvFromCtrl(MessageType.E2E_ACK, ack, consts.TOPOLOGY_APP_CTRL_ID) then
    logger.info("%s: %s", ack.success and "SUCCESS" or "FAIL", ack.message)
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 topology node del` command handler
function TG_Handler.topologyNodeDel(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send request
  local req = DelNode:new{}
  req.nodeName = args.name
  req.force = args.force
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToCtrl(MessageType.DEL_NODE, req, consts.TOPOLOGY_APP_CTRL_ID)

  -- Receive ACK
  local ack = E2EAck:new{}
  if tg:recvFromCtrl(MessageType.E2E_ACK, ack, consts.TOPOLOGY_APP_CTRL_ID) then
    logger.info("%s: %s", ack.success and "SUCCESS" or "FAIL", ack.message)
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 topology link add` command handler
function TG_Handler.topologyLinkAdd(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send request
  local req = AddLink:new{}
  req.link = Link:new{}
  req.link.a_node_name = args.a_node_name
  req.link.a_node_mac = args.a_node_mac or ""
  req.link.z_node_name = args.z_node_name
  req.link.z_node_mac = args.z_node_mac or ""
  req.link.link_type = LinkType[args.link_type:upper()]
  req.link.is_backup_cn_link = args.is_backup_cn_link
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToCtrl(MessageType.ADD_LINK, req, consts.TOPOLOGY_APP_CTRL_ID)

  -- Receive ACK
  local ack = E2EAck:new{}
  if tg:recvFromCtrl(MessageType.E2E_ACK, ack, consts.TOPOLOGY_APP_CTRL_ID) then
    logger.info("%s: %s", ack.success and "SUCCESS" or "FAIL", ack.message)
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 topology link del` command handler
function TG_Handler.topologyLinkDel(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send request
  local req = DelLink:new{}
  req.aNodeName = args.a_node_name
  req.zNodeName = args.z_node_name
  req.force = args.force
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToCtrl(MessageType.DEL_LINK, req, consts.TOPOLOGY_APP_CTRL_ID)

  -- Receive ACK
  local ack = E2EAck:new{}
  if tg:recvFromCtrl(MessageType.E2E_ACK, ack, consts.TOPOLOGY_APP_CTRL_ID) then
    logger.info("%s: %s", ack.success and "SUCCESS" or "FAIL", ack.message)
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 controller status` command handler
function TG_Handler.controllerStatus(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send status request
  tg:sendToCtrl(
    MessageType.GET_STATUS_DUMP, GetStatusDump:new{}, consts.STATUS_APP_CTRL_ID
  )
  local statusDump = StatusDump:new{}
  if tg:recvFromCtrl(
    MessageType.STATUS_DUMP, statusDump, consts.STATUS_APP_CTRL_ID
  ) then
    if args.json then
      logger.info("%s", tg_thrift_utils.thriftToJson(statusDump))
    else
      local DATE_FORMAT = "!%Y-%m-%dT%TZ"
      logger.info(
        "\nStatus reports for %d node(s) generated at %s.",
        tablex.size(statusDump.statusReports or {}),
        os.date(
          DATE_FORMAT,
          tg_thrift_utils.lualongnumberToNumber(statusDump.timeStamp, true)
        )
      )
      local invNodeStatusType = tg_utils.invertTable(NodeStatusType)
      local headers = {
        "Node MAC",
        "IPv6 Address",
        "Status",
        "Radios",
        "Last Seen",
        "TG Version",
        "FW Version",
      }
      local rows = {}
      for k, v in pairs(statusDump.statusReports or {}) do
        local radiosUp = 0
        for radioMac, radioStatus in pairs(v.radioStatus) do
          if radioStatus.initialized then
            radiosUp = radiosUp + 1
          end
        end
        local release, major, minor =
          tg_config_utils.parseReleaseVersion(v.version)
        rows[#rows+1] = {
          k,
          v.ipv6Address,
          invNodeStatusType[v.status] or "UNKNOWN",
          string.format("%d/%d", radiosUp, tablex.size(v.radioStatus)),
          os.date(
            DATE_FORMAT,
            tg_thrift_utils.lualongnumberToNumber(v.timeStamp, true)
          ),
          release,
          v.firmwareVersion,
        }
      end
      table.sort(rows, function(a, b) return a[1] < b[1] end)  -- by name
      logger.info("\n%s\n", tabulate.tabulate(rows, {headers = headers}))
    end
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 controller topo_scan` command handler
function TG_Handler.controllerTopoScan(args, name)
  local tg = TG.new(args)
  -- Set longer response timeout (need to wait for topo scan to finish)
  if not tg:connectToCtrl(math.max(args.router_recv_timeout, 20000)) then
    tg_utils.abort()
  end

  -- Send request
  local req = StartTopologyScan:new{}
  req.txNode = args.txNode
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToCtrl(
    MessageType.START_TOPOLOGY_SCAN,
    req,
    consts.TOPOLOGY_BUILDER_APP_CTRL_ID
  )
  local resp = StartTopologyScanResp:new{}
  if tg:recvFromCtrl(
    MessageType.START_TOPOLOGY_SCAN_RESP,
    resp,
    consts.TOPOLOGY_BUILDER_APP_CTRL_ID
  ) then
    if args.json then
      logger.info(tg_thrift_utils.thriftToJson(resp))
    else
      local respToSite = {}
      for _, info in ipairs(resp.responders) do
        respToSite[info.responderInfo.addr] = info.nearestSite
      end
      printTopoScanResults(
        resp.txNode,
        tablex.map(function(o) return o.responderInfo end, resp.responders),
        respToSite
      )
    end
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 controller network_topo_scan start` command handler
function TG_Handler.controllerNetworkTopoScanStart(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send request
  local req = StartNetworkTopologyScan:new{}
  req.siteLinks = {}
  for _, l in ipairs(args.site_link) do
    local siteLink = SiteLink:new{}
    siteLink.aSite = l[1]
    siteLink.zSite = l[2]
    req.siteLinks[#req.siteLinks+1] = siteLink
  end
  req.macAddrs = tg_thrift_utils.listToThriftSet(args.mac_addrs or {})
  req.cnSites = tg_thrift_utils.listToThriftSet(args.cn_sites or {})
  req.yStreetSites = tg_thrift_utils.listToThriftSet(args.y_street_sites or {})
  req.beamAnglePenalty = args.beam_angle_penalty
  req.distanceThreshold = args.distance_threshold
  req.snrThreshold = args.snr_threshold
  req.scansPerNode = args.scans_per_node
  req.mergeAdjMacs = not args.no_merge_adj_macs
  req.storeResults = args.store_results
  req.dryRun = args.dry_run
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToCtrl(
    MessageType.START_NETWORK_TOPOLOGY_SCAN,
    req,
    consts.TOPOLOGY_BUILDER_APP_CTRL_ID
  )

  -- Receive ACK
  local ack = E2EAck:new{}
  if tg:recvFromCtrl(
    MessageType.E2E_ACK, ack, consts.TOPOLOGY_BUILDER_APP_CTRL_ID
  ) then
    logger.info("%s: %s", ack.success and "SUCCESS" or "FAIL", ack.message)
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 controller network_topo_scan stop` command handler
function TG_Handler.controllerNetworkTopoScanStop(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send request
  tg:sendToCtrl(
    MessageType.STOP_NETWORK_TOPOLOGY_SCAN,
    StopNetworkTopologyScan:new{},
    consts.TOPOLOGY_BUILDER_APP_CTRL_ID
  )

  -- Receive ACK
  local ack = E2EAck:new{}
  if tg:recvFromCtrl(
    MessageType.E2E_ACK, ack, consts.TOPOLOGY_BUILDER_APP_CTRL_ID
  ) then
    logger.info("%s: %s", ack.success and "SUCCESS" or "FAIL", ack.message)
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 controller network_topo_scan status` command handler
function TG_Handler.controllerNetworkTopoScanStatus(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send request
  tg:sendToCtrl(
    MessageType.GET_NETWORK_TOPOLOGY_SCAN_STATUS,
    GetNetworkTopologyScanStatus:new{},
    consts.TOPOLOGY_BUILDER_APP_CTRL_ID
  )

  -- Receive response
  local resp = NetworkTopologyScanStatus:new{}
  if tg:recvFromCtrl(
    MessageType.NETWORK_TOPOLOGY_SCAN_STATUS,
    resp,
    consts.TOPOLOGY_BUILDER_APP_CTRL_ID
  ) then
    if args.brief then
      for txNode, responders in pairs(resp.responses or {}) do
        for addr, info in pairs(responders) do
          info.responderInfo.itorLqmMat = nil
          info.responderInfo.rtoiLqmMat = nil
        end
      end
    end
    logger.info(tg_thrift_utils.thriftToJson(resp))
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 controller optimize polarity` command handler
function TG_Handler.controllerOptimizePolarity(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send request
  local req = TriggerPolarityOptimization:new{}
  req.clearUserPolarityConfig = args.clear_user_config
  tg:sendToCtrl(
    MessageType.TRIGGER_POLARITY_OPTIMIZATION, req, consts.CONFIG_APP_CTRL_ID
  )

  -- Receive response
  local ack = E2EAck:new{}
  if tg:recvFromCtrl(
    MessageType.E2E_ACK, ack, consts.CONFIG_APP_CTRL_ID
  ) then
    logger.info("%s: %s", ack.success and "SUCCESS" or "FAIL", ack.message)
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 controller optimize golay` command handler
function TG_Handler.controllerOptimizeGolay(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send request
  local req = TriggerGolayOptimization:new{}
  req.clearUserConfig = args.clear_user_config
  tg:sendToCtrl(
    MessageType.TRIGGER_GOLAY_OPTIMIZATION, req, consts.CONFIG_APP_CTRL_ID
  )

  -- Receive response
  local ack = E2EAck:new{}
  if tg:recvFromCtrl(
    MessageType.E2E_ACK, ack, consts.CONFIG_APP_CTRL_ID
  ) then
    logger.info("%s: %s", ack.success and "SUCCESS" or "FAIL", ack.message)
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 controller optimize control_superframe` command handler
function TG_Handler.controllerOptimizeControlSuperframe(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send request
  local req = TriggerControlSuperframeOptimization:new{}
  req.clearUserConfig = args.clear_user_config
  tg:sendToCtrl(
    MessageType.TRIGGER_CONTROL_SUPERFRAME_OPTIMIZATION,
    req,
    consts.CONFIG_APP_CTRL_ID
  )

  -- Receive response
  local ack = E2EAck:new{}
  if tg:recvFromCtrl(
    MessageType.E2E_ACK, ack, consts.CONFIG_APP_CTRL_ID
  ) then
    logger.info("%s: %s", ack.success and "SUCCESS" or "FAIL", ack.message)
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 controller optimize channel` command handler
function TG_Handler.controllerOptimizeChannel(args, name)
  local tg = TG.new(args)
  if not tg:connectToCtrl() then
    tg_utils.abort()
  end

  -- Send request
  local req = TriggerChannelOptimization:new{}
  req.clearUserChannelConfig = args.clear_user_config
  tg:sendToCtrl(
    MessageType.TRIGGER_CHANNEL_OPTIMIZATION, req, consts.CONFIG_APP_CTRL_ID
  )

  -- Receive response
  local ack = E2EAck:new{}
  if tg:recvFromCtrl(
    MessageType.E2E_ACK, ack, consts.CONFIG_APP_CTRL_ID
  ) then
    logger.info("%s: %s", ack.success and "SUCCESS" or "FAIL", ack.message)
  else
    tg_utils.abort("Failed to receive data from controller")
  end

  tg:close()
end

--- `tg2 fw node_init` command handler
function TG_Handler.fwNodeInit(args, name)
  -- Read node config
  local configStr = tg_utils.readFile(args.node_config)
  if configStr == nil then
    tg_utils.abort("Failed to read node config file")
  end
  local nodeConfig = NodeConfig:new{}
  tg_thrift_utils.thriftFromJson(configStr, nodeConfig)
  if tg_utils.get(nodeConfig, "radioParamsBase", "fwParams") == nil then
    tg_utils.abort("'radioParamsBase.fwParams' is missing from node config!")
  end

  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local req = DriverNodeInitReq:new{}
  req.optParams = nodeConfig.radioParamsBase.fwParams
  tg:sendToDriverIf(MessageType.NODE_INIT, req, args.radio_mac)

  -- Receive response
  local resp = DriverNodeInitNotif:new{}
  if tg:recvFromDriverIf(
    MessageType.NODE_INIT_NOTIFY, resp, args.radio_mac
  ) then
    if args.json then
      logger.info(tg_thrift_utils.thriftToJson(resp))
    else
      if resp.success then
        logger.info(
          "Node init succeeded with MAC=<%s>, vendor=<%s>.",
          resp.macAddr,
          resp.vendor
        )
      else
        tg_utils.abort("Node init failed.")
      end
    end
  else
    tg_utils.abort("Failed to receive data from driver-if")
  end

  tg:close()
end

--- `tg2 fw channel_config` command handler
function TG_Handler.fwChannelConfig(args, name)
  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local channelCfgMsg = PassThruMsg:new{}
  channelCfgMsg.msgType = PtMsgTypes.SB_CHANNEL_CONFIG
  channelCfgMsg.dest = PtMsgDest.SB
  channelCfgMsg.channelCfg = ChannelConfig:new{}
  channelCfgMsg.channelCfg.channel = args.channel
  local req = FwSetNodeParams:new{}
  req.passThruMsgs = {channelCfgMsg}
  tg:sendToDriverIf(MessageType.FW_SET_NODE_PARAMS, req, args.radio_mac)

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw gps_enable` command handler
function TG_Handler.fwGpsEnable(args, name)
  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  tg:sendToDriverIf(MessageType.GPS_ENABLE_REQ, Empty:new{}, args.radio_mac)

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw get_gps_pos` command handler
function TG_Handler.fwGetGpsPos(args, name)
  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  tg:sendToDriverIf(MessageType.GPS_GET_POS_REQ, Empty:new{}, args.radio_mac)

  -- Receive response
  local loc = Location:new{}
  if tg:recvFromDriverIf(
    MessageType.GPS_GET_POS_RESP, loc, args.radio_mac
  ) then
    if args.json then
      logger.info(tg_thrift_utils.thriftToJson(loc))
    else
      logger.info(
        "latitude=%f, longitude=%f, altitude=%f, accuracy=%f",
        loc.latitude, loc.longitude, loc.altitude, loc.accuracy
      )
    end
  else
    tg_utils.abort("Failed to receive data from driver-if")
  end

  tg:close()
end

--- `tg2 fw set_gps_pos` command handler
function TG_Handler.fwSetGpsPos(args, name)
  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local req = FwSetNodeParams:new{}
  req.location = Location:new{}
  req.location.latitude = args.lat
  req.location.longitude = args.lon
  req.location.altitude = args.alt
  req.location.accuracy = args.acc
  tg:sendToDriverIf(MessageType.FW_SET_NODE_PARAMS, req, args.radio_mac)

  -- Receive ACK
  if args.no_ack then
    logger.info("Request sent")
  else
    tg:recvFwAckFromDriverIf(args.radio_mac)
  end

  tg:close()
end

--- `tg2 fw gps_send_time` command handler
function TG_Handler.fwGpsSendTime(args, name)
  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local req = GpsTimeValue:new{}
  req.unixTimeSecs = args.time_sec
  req.unixTimeNsecs = 0
  tg:sendToDriverIf(MessageType.GPS_SEND_TIME, req, args.radio_mac)

  -- Receive ACK
  if args.no_ack then
    logger.info("Request sent")
  else
    tg:recvFwAckFromDriverIf(args.radio_mac)
  end

  tg:close()
end

--- `tg2 fw assoc` command handler
function TG_Handler.fwAssoc(args, name)
  -- Read node config
  local configStr = tg_utils.readFile(args.node_config)
  if configStr == nil then
    tg_utils.abort("Failed to read node config file")
  end
  local nodeConfig = NodeConfig:new{}
  tg_thrift_utils.thriftFromJson(configStr, nodeConfig)
  if tg_utils.get(nodeConfig, "linkParamsBase", "fwParams") == nil then
    tg_utils.abort("'linkParamsBase.fwParams' is missing from node config!")
  end

  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- 1. Send DR_DEV_ALLOC_REQ
  local drDevAllocReq = DriverDevAllocReq:new{}
  drDevAllocReq.macAddr = args.responder_mac
  tg:sendToDriverIf(MessageType.DR_DEV_ALLOC_REQ, drDevAllocReq, args.radio_mac)

  local drDevAllocResp = DriverDevAllocRes:new{}
  if tg:recvFromDriverIf(
    MessageType.DR_DEV_ALLOC_RES,
    drDevAllocResp,
    args.radio_mac,
    args.responder_mac
  ) then
    if drDevAllocResp.success then
      logger.info("Reserved interface: %s", drDevAllocResp.ifname)
    else
      tg_utils.abort("Unable to reserve terra interface")
    end
  else
    tg_utils.abort("Failed to receive data from driver-if")
  end

  -- 2. Send DR_SET_LINK_STATUS
  local req = DriverSetLinkStatus:new{}
  req.isAssoc = true
  req.responderMac = args.responder_mac
  req.optParams = nodeConfig.linkParamsBase.fwParams
  tg:sendToDriverIf(MessageType.DR_SET_LINK_STATUS, req, args.radio_mac)

  -- Receive response
  -- We could receive LINK_PAUSE before LINK_DOWN, so receive up to 2 messages
  local success = false
  local linkDownCause = nil
  for i = 1, 2 do
    local status = DriverLinkStatus:new{}
    if tg:recvFromDriverIf(
      MessageType.DR_LINK_STATUS, status, args.radio_mac, args.responder_mac
    ) then
      if status.valid then
        if status.drLinkStatusType == DriverLinkStatusType.LINK_UP then
          success = true
          break
        elseif status.drLinkStatusType == DriverLinkStatusType.LINK_DOWN then
          linkDownCause = status.linkDownCause
          break
        end
      end
    else
      tg_utils.abort("Failed to receive data from driver-if")
    end
  end
  if success then
    logger.info("Assoc succeeded.")
  else
    if linkDownCause ~= nil then
      local invLinkDownCause = tg_utils.invertTable(LinkDownCause)
      local reason =
        invLinkDownCause[linkDownCause] or ("#" .. tostring(linkDownCause))
      tg_utils.abort(string.format("Assoc failed: %s", reason))
    else
      tg_utils.abort("Assoc failed.")
    end
  end

  tg:close()
end

--- `tg2 fw dissoc` command handler
function TG_Handler.fwDissoc(args, name)
  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local req = DriverSetLinkStatus:new{}
  req.isAssoc = false
  req.responderMac = args.responder_mac
  req.optParams = FwOptParams:new{}
  tg:sendToDriverIf(MessageType.DR_SET_LINK_STATUS, req, args.radio_mac)

  -- Receive response
  -- We could receive LINK_PAUSE before LINK_DOWN, so receive up to 2 messages
  local invDriverLinkStatusType = tg_utils.invertTable(DriverLinkStatusType)
  local success = false
  for i = 1, 2 do
    local status = DriverLinkStatus:new{}
    if tg:recvFromDriverIf(
      MessageType.DR_LINK_STATUS, status, args.radio_mac, args.responder_mac
    ) then
      logger.debug(
        "Received %s", invDriverLinkStatusType[status.drLinkStatusType]
      )
      if status.valid and
         status.drLinkStatusType == DriverLinkStatusType.LINK_DOWN then
        success = true
        break
      end
    else
      tg_utils.abort("Failed to receive data from driver-if")
    end
  end
  if success then
    logger.info("Dissoc succeeded.")
  else
    tg_utils.abort("Dissoc failed.")
  end

  tg:close()
end

--- `tg2 fw fw_set_log_config` command handler
function TG_Handler.fwFwSetLogConfig(args, name)
  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  local modules = args.module
  if modules == nil or #modules == 0 then
    modules = tablex.map(string.lower, tablex.keys(LogModule))
  end
  local level = LogLevel[args.level:upper()]

  -- Send request
  local req = SetLogConfig:new{}
  req.configs = {}
  for _, m in ipairs(modules) do
    req.configs[LogModule[m:upper()]] = level
  end
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToDriverIf(MessageType.FW_SET_LOG_CONFIG, req, args.radio_mac)

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw fw_stats_config` command handler
function TG_Handler.fwFwStatsConfig(args, name)
  -- Read stats config
  local configStr = tg_utils.readFile(args.config_file)
  if configStr == nil then
    tg_utils.abort("Failed to read stats config file")
  end
  local statsConfigure = StatsConfigure:new{}
  tg_thrift_utils.thriftFromJson(configStr, statsConfigure)

  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  tg:sendToDriverIf(
    MessageType.FW_STATS_CONFIGURE_REQ, statsConfigure, args.radio_mac
  )

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw debug` command handler
function TG_Handler.fwDebug(args, name)
  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local req = Debug:new{}
  req.cmdStr = args.command
  req.value = args.value
  tg:sendToDriverIf(MessageType.FW_DEBUG_REQ, req, args.radio_mac)

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw polarity_config` command handler
function TG_Handler.fwPolarityConfig(args, name)
  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local polarityCfgMsg = PassThruMsg:new{}
  polarityCfgMsg.msgType = PtMsgTypes.SB_POLARITY
  polarityCfgMsg.dest = PtMsgDest.SB
  polarityCfgMsg.polarityCfg = PolarityConfig:new{}
  polarityCfgMsg.polarityCfg.polarity = PolarityType[args.polarity:upper()]
  local req = FwSetNodeParams:new{}
  req.passThruMsgs = {polarityCfgMsg}
  tg:sendToDriverIf(MessageType.FW_SET_NODE_PARAMS, req, args.radio_mac)

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw golay_config` command handler
function TG_Handler.fwGolayConfig(args, name)
  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local golayCfgMsg = PassThruMsg:new{}
  golayCfgMsg.msgType = PtMsgTypes.SB_GOLAY_INDX
  golayCfgMsg.dest = PtMsgDest.SB
  golayCfgMsg.golayCfg = GolayConfig:new{}
  golayCfgMsg.golayCfg.txGolayIdx = args.tx_index
  golayCfgMsg.golayCfg.rxGolayIdx = args.rx_index
  local req = FwSetNodeParams:new{}
  req.passThruMsgs = {golayCfgMsg}
  tg:sendToDriverIf(MessageType.FW_SET_NODE_PARAMS, req, args.radio_mac)

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw bf_slot_exclusion_req` command handler
function TG_Handler.fwBfSlotExclusionReq(args, name)
  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local bfSlotExclusionReqMsg = PassThruMsg:new{}
  bfSlotExclusionReqMsg.msgType = PtMsgTypes.SB_BF_SLOT_EXCLUSION_REQ
  bfSlotExclusionReqMsg.dest = PtMsgDest.SB
  bfSlotExclusionReqMsg.bfSlotExclusionReq = BfSlotExclusionReq:new{}
  bfSlotExclusionReqMsg.bfSlotExclusionReq.startBwgdIdx = args.bwgd_idx
  local req = FwSetNodeParams:new{}
  req.passThruMsgs = {bfSlotExclusionReqMsg}
  tg:sendToDriverIf(MessageType.FW_SET_NODE_PARAMS, req, args.radio_mac)

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw bf_resp_scan_config` command handler
function TG_Handler.fwBfRespScanConfig(args, name)
  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local req = BfRespScanConfig:new{}
  req.cfg = args.cfg
  tg:sendToDriverIf(MessageType.FW_BF_RESP_SCAN, req, args.radio_mac)

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw phyla_config` command handler
function TG_Handler.fwPhyLAConfig(args, name)
  -- Read phyla config
  local configStr = tg_utils.readFile(args.config_file)
  if configStr == nil then
    tg_utils.abort("Failed to read phyla config file")
  end
  local phyLAParams = PhyLAParams:new{}
  tg_thrift_utils.thriftFromJson(configStr, phyLAParams)

  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local phyLAConfigMsg = PassThruMsg:new{}
  phyLAConfigMsg.msgType = PtMsgTypes.SB_PHY_LA_CONFIG
  phyLAConfigMsg.dest = PtMsgDest.SB
  phyLAConfigMsg.phyLAConfig = PhyLAConfig:new{}
  phyLAConfigMsg.phyLAConfig.addr = args.responder_mac
  phyLAConfigMsg.phyLAConfig.laParams = phyLAParams.laParams
  phyLAConfigMsg.phyLAConfig.laNodeParams = phyLAParams.laNodeParams
  local req = FwConfigParams:new{}
  req.passThruMsgs = {phyLAConfigMsg}
  tg:sendToDriverIf(MessageType.FW_CONFIG_REQ, req, args.radio_mac)

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw phyagc_config` command handler
function TG_Handler.fwPhyAgcConfig(args, name)
  -- Read phyagc config
  local configStr = tg_utils.readFile(args.config_file)
  if configStr == nil then
    tg_utils.abort("Failed to read phyagc config file")
  end
  local phyAgcParams = PhyAgcParams:new{}
  tg_thrift_utils.thriftFromJson(configStr, phyAgcParams)

  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local phyAgcConfigMsg = PassThruMsg:new{}
  phyAgcConfigMsg.msgType = PtMsgTypes.SB_PHY_AGC_CONFIG
  phyAgcConfigMsg.dest = PtMsgDest.SB
  phyAgcConfigMsg.phyAgcConfig = PhyAgcConfig:new{}
  phyAgcConfigMsg.phyAgcConfig.addr = args.responder_mac
  phyAgcConfigMsg.phyAgcConfig.agcNodeParams = phyAgcParams.agcNodeParams
  phyAgcConfigMsg.phyAgcConfig.agcLinkParams = phyAgcParams.agcLinkParams
  local req = FwConfigParams:new{}
  req.passThruMsgs = {phyAgcConfigMsg}
  tg:sendToDriverIf(MessageType.FW_CONFIG_REQ, req, args.radio_mac)

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw phytpc_config` command handler
function TG_Handler.fwPhyTpcConfig(args, name)
  -- Read phytpc config
  local configStr = tg_utils.readFile(args.config_file)
  if configStr == nil then
    tg_utils.abort("Failed to read phytpc config file")
  end
  local phyTpcParams = PhyTpcParams:new{}
  tg_thrift_utils.thriftFromJson(configStr, phyTpcParams)

  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local phyTpcConfigReq = PhyTpcConfig:new{}
  phyTpcConfigReq.addr = args.responder_mac
  phyTpcConfigReq.tpcNodeParams = phyTpcParams.tpcNodeParams
  phyTpcConfigReq.tpcLinkParams = phyTpcParams.tpcLinkParams
  tg:sendToDriverIf(
    MessageType.PHY_TPC_CONFIG_REQ,
    phyTpcConfigReq,
    args.radio_mac
  )

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw phy_tpc_adj_tbl_config` command handler
function TG_Handler.fwPhyTpcAdjTblConfig(args, name)
  -- Read TPC adjustment table
  local configStr = tg_utils.readFile(args.config_file)
  if configStr == nil then
    tg_utils.abort("Failed to read TPC adjustment table config file")
  end
  local phyTpcAdjTblCfg = PhyTpcAdjTblCfg:new{}
  tg_thrift_utils.thriftFromJson(configStr, phyTpcAdjTblCfg)

  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  tg:sendToDriverIf(
    MessageType.PHY_TPC_ADJ_TBL_CFG_REQ,
    phyTpcAdjTblCfg,
    args.radio_mac
  )

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw phy_ant_wgt_code_book_config` command handler
function TG_Handler.fwPhyAntWgtCodeBookConfig(args, name)
  -- Read antenna codebook table
  local configStr = tg_utils.readFile(args.config_file)
  if configStr == nil then
    tg_utils.abort("Failed to read antenna codebook table config file")
  end
  local phyAntWgtCodeBookConfig = PhyAntWgtCodeBookConfig:new{}
  tg_thrift_utils.thriftFromJson(configStr, phyAntWgtCodeBookConfig)

  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  tg:sendToDriverIf(
    MessageType.FW_SET_CODEBOOK,
    phyAntWgtCodeBookConfig,
    args.radio_mac
  )

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw phy_golay_sequence_config` command handler
function TG_Handler.fwPhyGolaySequenceConfig(args, name)
  -- Read golay sequence file
  local configStr = tg_utils.readFile(args.config_file)
  if configStr == nil then
    tg_utils.abort("Failed to read Golay sequence config file")
  end
  local phyGolaySequenceConfig = PhyGolaySequenceConfig:new{}
  tg_thrift_utils.thriftFromJson(configStr, phyGolaySequenceConfig)

  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local req = PhyGolaySequenceConfigReq:new{}
  req.transmitSequence = phyGolaySequenceConfig.transmitSequence
  req.receiveSequence = phyGolaySequenceConfig.receiveSequence
  tg:sendToDriverIf(
    MessageType.PHY_GOLAY_SEQUENCE_CONFIG_REQ, req, args.radio_mac
  )

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw airtime_alloc` command handler
function TG_Handler.fwAirtimeAlloc(args, name)
  -- Read airtime allocation file
  local configStr = tg_utils.readFile(args.airtime_alloc_file)
  if configStr == nil then
    tg_utils.abort("Failed to read airtime allocation file")
  end
  local nodeAirtime = NodeAirtime:new{}
  tg_thrift_utils.thriftFromJson(configStr, nodeAirtime)

  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local airtimeCfgMsg = PassThruMsg:new{}
  airtimeCfgMsg.msgType = PtMsgTypes.SB_AIRTIMEALLOC
  airtimeCfgMsg.dest = PtMsgDest.SB
  airtimeCfgMsg.airtimeAllocMap = nodeAirtime
  local req = FwSetNodeParams:new{}
  req.passThruMsgs = {airtimeCfgMsg}
  tg:sendToDriverIf(MessageType.FW_SET_NODE_PARAMS, req, args.radio_mac)

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw fw_get_params` command handler
function TG_Handler.fwFwGetParams(args, name)
  if args.type == "link" and args.responder_mac == nil then
    tg_utils.abort("'--responder_mac' is required for reading link parameters.")
  end

  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local msg = PassThruMsg:new{}
  msg.msgType = PtMsgTypes.SB_GET_FW_PARAMS
  msg.dest = PtMsgDest.SB
  msg.getFwParamsReq = GetFwParamsReq:new{}
  if args.type == "link" then
    msg.getFwParamsReq.addr = args.responder_mac
    msg.getFwParamsReq.requestedParamsType = FwParamsType.FW_PARAMS_LINK_FW_CFG
  else
    msg.getFwParamsReq.requestedParamsType = FwParamsType.FW_PARAMS_NODE_FW_CFG
  end
  local req = FwConfigParams:new{}
  req.passThruMsgs = {msg}
  tg:sendToDriverIf(MessageType.FW_CONFIG_REQ, req, args.radio_mac)

  -- Receive response
  local resp = GetFwParamsResp:new{}
  if tg:recvFromDriverIf(MessageType.FW_CONFIG_RESP, resp, args.radio_mac) then
    if args.json then
      logger.info(tg_thrift_utils.thriftToJson(resp))
    else
      logger.info("Received parameters of type: %d", resp.fwParamsType)
      logger.info("Current BWGD = %s", resp.bwgdIdx)
      if resp.fwParamsType == FwParamsType.FW_PARAMS_INVALID then
        logger.error("ERROR: Unable to get requested params.")
      else
        logger.info("\n--- start of list ---\n")
        for k, v in tablex.sort(resp.optParams or {}) do
          if v ~= nil and type(v) ~= "table" then
            logger.info("  %s = %s\n", k, v)
          end
        end
        logger.info("\n---  end of list  ---\n")
      end
    end
  else
    tg_utils.abort("Failed to receive data from driver-if")
  end

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw fw_set_params` command handler
function TG_Handler.fwFwSetParams(args, name)
  -- Validate inputs
  local valid, errMsg = validateRuntimeFwParams(args.params, args.responder_mac)
  if not valid then
    tg_utils.abort(errMsg)
  end

  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local fwOptParams = FwOptParams:new{}
  for i = 1, #args.params, 2 do
    fwOptParams[args.params[i]] = tonumber(args.params[i + 1])
  end
  local msg = PassThruMsg:new{}
  msg.msgType = PtMsgTypes.SB_SET_FW_PARAMS
  msg.dest = PtMsgDest.SB
  msg.setfwParamsReq = SetFwParamsMsg:new{}
  msg.setfwParamsReq.addr = args.responder_mac or consts.EMPTY_MAC_ADDRESS
  msg.setfwParamsReq.optionalParams = fwOptParams
  msg.setfwParamsReq.bwgdIdx = args.bwgd_idx
  local req = FwConfigParams:new{}
  req.passThruMsgs = {msg}
  tg:sendToDriverIf(MessageType.FW_CONFIG_REQ, req, args.radio_mac)

  -- Receive response
  local resp = SetFwParamsResp:new{}
  if tg:recvFromDriverIf(MessageType.FW_CONFIG_RESP, resp, args.radio_mac) then
    if args.json then
      logger.info(tg_thrift_utils.thriftToJson(resp))
    else
      if resp.status then
        logger.info("Setting parameters succeeded.")
      else
        logger.error("ERROR: Setting one or more requested parameters failed.")
      end
    end
  else
    tg_utils.abort("Failed to receive data from driver-if")
  end

  -- Receive ACK
  tg:recvFwAckFromDriverIf(args.radio_mac)

  tg:close()
end

--- `tg2 fw scan` command handler
function TG_Handler.fwScan(args, name)
  local tg = TG.new(args)
  if not tg:connectToDriverIf() then
    tg_utils.abort()
  end

  -- Send request
  local req, errMsg = argparse_buildScanRequest(args)
  if req == nil then
    tg_utils.abort(errMsg)
  end
  logger.info("ScanReq:\n%s\n", tg_thrift_utils.thriftToJson(req))
  tg:sendToDriverIf(MessageType.SCAN_REQ, req, args.radio_mac)

  -- Receive response
  -- NOTE: We may receive SCAN_RESP and FW_ACK in any order, e.g. if the scan
  -- immediately fails. For implementation simplicity, we ignore the FW_ACK.
  local resp = ScanResp:new{}
  if tg:recvFromDriverIf(MessageType.SCAN_RESP, resp, args.radio_mac) then
    logger.info("ScanResp:\n%s\n", tg_thrift_utils.thriftToJson(resp))
  else
    tg_utils.abort("Failed to receive data from driver-if")
  end

  tg:close()
end

--- `tg2 event` command handler
function TG_Handler.event(args, name)
  local tg = TG.new(args)
  if not tg:connectToStatsAgent() then
    tg_utils.abort()
  end

  -- Send event request
  local event = Event:new{}
  event.source = args.source
  event.category = EventCategory[args.category:upper()]
  event.eventId = EventId[args.id:upper()]
  event.level = EventLevel[args.level:upper()]
  event.reason = args.reason
  event.details = args.details
  event.timestamp = os.time()
  event.entity = args.entity
  event.nodeId = args.node_id
  event.nodeName = args.node_name
  event.topologyName = args.topology
  tg:sendToStatsAgent(MessageType.EVENT, event)
  logger.info("Request sent")

  tg:close()
end

--- `tg2 minion sub` command handler
function TG_Handler.minionSub(args, name)
  local tg = TG.new(args)

  -- Create SUB socket
  local subscriber, err = tg:connectToMinionPub()
  if err then
    tg_utils.abort(
      string.format("Failed to connect to minion PUB port: %s", err)
    )
  end

  local invMessageType = tg_utils.invertTable(MessageType)
  local filter
  if args.filter and #args.filter > 0 then
    filter = tablex.makeset(args.filter)
  end
  tg:doMinionSubLoop(subscriber, function(app, deserMsg)
    local mTypeStr = invMessageType[deserMsg.mType]
    if filter and (mTypeStr == nil or filter[mTypeStr] == nil) then
      return false -- keep looping
    end
    logger.info(
      "Received %s (#%d) from '%s'", mTypeStr or "unknown", deserMsg.mType, app
    )

    -- Try to print to deserialized type
    local msgData = nil
    local isDriverMsg = false
    if deserMsg.mType == MessageType.NODE_INIT_NOTIFY then
      msgData = DriverNodeInitNotif:new{}
      isDriverMsg = true
    elseif deserMsg.mType == MessageType.DR_LINK_STATUS then
      msgData = DriverLinkStatus:new{}
      isDriverMsg = true
    elseif deserMsg.mType == MessageType.DR_DEV_ALLOC_RES then
      msgData = DriverDevAllocRes:new{}
      isDriverMsg = true
    elseif deserMsg.mType == MessageType.DR_WSEC_STATUS then
      msgData = DriverWsecStatus:new{}
      isDriverMsg = true
    elseif deserMsg.mType == MessageType.DR_WSEC_LINKUP_STATUS then
      msgData = DriverWsecLinkupStatus:new{}
      isDriverMsg = true
    elseif deserMsg.mType == MessageType.DR_DEV_UPDOWN_STATUS then
      msgData = DriverDevUpDownStatus:new{}
      isDriverMsg = true
    elseif deserMsg.mType == MessageType.LINK_STATUS then
      msgData = LinkStatus:new{}
    elseif deserMsg.mType == MessageType.LINK_AUTHORIZED then
      msgData = LinkAuthorized:new{}
    elseif deserMsg.mType == MessageType.STATUS_REPORT then
      msgData = StatusReport:new{}
    elseif deserMsg.mType == MessageType.GPS_GET_POS_RESP then
      msgData = Location:new{}
      isDriverMsg = true
    elseif deserMsg.mType == MessageType.SCAN_RESP then
      msgData = ScanResp:new{}
      isDriverMsg = true
    elseif deserMsg.mType == MessageType.FW_CONFIG_RESP then
      msgData = GetFwParamsResp:new{}
      isDriverMsg = true
    end
    if msgData ~= nil then
      if isDriverMsg then
        TG.deserializeDriverMsg(deserMsg.value, msgData)
      else
        tg_thrift_utils.deserialize(deserMsg.value, msgData)
      end
    else
      msgData = deserMsg
    end
    logger.info(tg_thrift_utils.thriftToJson(msgData))

    return false -- keep looping
  end, args.poll_time)

  subscriber:close()
  tg:close()
end

--- `tg2 minion assoc` command handler
function TG_Handler.minionAssoc(args, name)
  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Create SUB socket (for receiving response)
  local subscriber, err = tg:connectToMinionPub()
  if err then
    tg_utils.abort(
      string.format("Failed to connect to minion PUB port: %s", err)
    )
  end

  -- Send assoc request
  local req = SetLinkStatus:new{}
  req.linkStatusType = LinkStatusType.LINK_UP
  req.responderMac = args.responder_mac
  req.initiatorMac = args.initiator_mac
  if args.responder_node_type ~= nil then
    req.responderNodeType = NodeType[args.responder_node_type:upper()]
  end
  if args.tx_golay ~= nil or args.rx_golay ~= nil then
    req.golayIdx = GolayIdx:new{}
    if args.tx_golay ~= nil then
      req.golayIdx.txGolayIdx = args.tx_golay
    end
    if args.rx_golay ~= nil then
      req.golayIdx.rxGolayIdx = args.rx_golay
    end
  end
  if args.control_superframe ~= nil then
    req.controlSuperframe = args.control_superframe
  end
  if args.responder_polarity ~= nil then
    req.responderNodePolarity = PolarityType[args.responder_polarity:upper()]
  end
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToMinion(
    MessageType.SET_LINK_STATUS, req, consts.IGNITION_APP_MINION_ID
  )
  logger.debug("Request sent, waiting for response...")

  -- Listen for response event
  local linkStatusType, linkDownCause =
    tg:minionWaitForLinkStatus(subscriber, args.responder_mac, args.poll_time)
  if linkStatusType == nil then
    tg_utils.abort("Timed out.")
  elseif linkStatusType == LinkStatusType.LINK_UP then
    logger.info("Assoc succeeded.")
  else
    local invLinkDownCause = tg_utils.invertTable(LinkDownCause)
    local reason =
      invLinkDownCause[linkDownCause] or ("#" .. tostring(linkDownCause))
    tg_utils.abort("Assoc failed: " .. reason)
  end

  subscriber:close()
  tg:close()
end

--- `tg2 minion dissoc` command handler
function TG_Handler.minionDissoc(args, name)
  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Create SUB socket (for receiving response)
  local subscriber, err = tg:connectToMinionPub()
  if err then
    tg_utils.abort(
      string.format("Failed to connect to minion PUB port: %s", err)
    )
  end

  -- Send dissoc request
  local req = SetLinkStatus:new{}
  req.linkStatusType = LinkStatusType.LINK_DOWN
  req.responderMac = args.responder_mac
  req.initiatorMac = args.initiator_mac
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToMinion(
    MessageType.SET_LINK_STATUS, req, consts.IGNITION_APP_MINION_ID
  )
  logger.debug("Request sent, waiting for response...")

  -- Listen for response event
  local linkStatusType, linkDownCause =
    tg:minionWaitForLinkStatus(subscriber, args.responder_mac, args.poll_time)
  if linkStatusType == nil then
    tg_utils.abort("Timed out.")
  elseif linkStatusType == LinkStatusType.LINK_DOWN then
    logger.info("Dissoc succeeded.")
  else
    tg_utils.abort("Dissoc failed.")
  end

  tg:close()
end

--- `tg2 minion gps_enable` command handler
function TG_Handler.minionGpsEnable(args, name)
  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Send NodeParams request
  local req = NodeParams:new{}
  req.type = NodeParamsType.GPS
  req.enableGps = true
  if args.radio_mac ~= nil then
    req.radioMac = args.radio_mac
  end
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToMinion(
    MessageType.SET_NODE_PARAMS, req, consts.STATUS_APP_MINION_ID
  )
  logger.debug("Request sent, waiting for acknowledgement...")

  -- Wait for ack
  local success =
    tg:minionWaitForRadioStatus(args.radio_mac, "gpsSync", args.poll_time)
  if not success then
    tg_utils.abort("Timed out.")
  end
  logger.info("GPS is in sync.")

  tg:close()
end

--- `tg2 minion set_params` command handler
function TG_Handler.minionSetParams(args, name)
  if args.channel == nil and
     args.polarity == nil and
     args.airtime_alloc_file == nil then
    tg_utils.abort(
      "Provide a channel (-c), polarity (-p), or airtime allocation file (-a)."
    )
  end

  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Send NodeParams request
  local req = NodeParams:new{}
  req.type = NodeParamsType.MANUAL
  if args.channel ~= nil then
    req.channel = args.channel
  end
  if args.polarity ~= nil then
    req.polarity = PolarityType[args.polarity:upper()]
  end
  if args.airtime_alloc_file ~= nil then
    local airtimeAllocJson = tg_utils.readFile(args.airtime_alloc_file)
    if airtimeAllocJson == nil then
      tg_utils.abort()
    end
    local nodeAirtime = NodeAirtime:new{}
    tg_thrift_utils.thriftFromJson(airtimeAllocJson, nodeAirtime)
    req.airtimeAllocMap = nodeAirtime
  end
  if args.radio_mac ~= nil then
    req.radioMac = args.radio_mac
  end
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToMinion(
    MessageType.SET_NODE_PARAMS, req, consts.STATUS_APP_MINION_ID
  )
  logger.debug("Request sent, waiting for acknowledgement...")

  -- Wait for ack
  local success = tg:minionWaitForRadioStatus(
    args.radio_mac, "nodeParamsSet", args.poll_time
  )
  if not success then
    tg_utils.abort("Timed out.")
  end
  logger.info("Node params set successfully.")

  tg:close()
end

--- `tg2 minion fw_set_log_config` command handler
function TG_Handler.minionFwSetLogConfig(args, name)
  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  local modules = args.module
  if modules == nil or #modules == 0 then
    modules = tablex.map(string.lower, tablex.keys(LogModule))
  end
  local level = LogLevel[args.level:upper()]

  -- Send SetLogConfig request
  local req = SetLogConfig:new{}
  req.configs = {}
  for _, m in ipairs(modules) do
    req.configs[LogModule[m:upper()]] = level
  end
  logger.debug(tg_thrift_utils.thriftToJson(req))
  if args.radio_mac ~= nil then
    -- Send to specific baseband directly
    tg:sendToMinion(
      MessageType.FW_SET_LOG_CONFIG,
      TG.serializeDriverMsg(req, args.radio_mac),
      consts.DRIVER_APP_MINION_ID
    )
  else
    -- Send to all basebands via ConfigApp API
    tg:sendToMinion(
      MessageType.FW_SET_LOG_CONFIG, req, consts.CONFIG_APP_MINION_ID
    )
  end
  logger.info("Request sent")

  tg:close()
end

--- `tg2 minion fw_stats_config` command handler
function TG_Handler.minionFwStatsConfig(args, name)
  local enableStats = args.enable or {}
  local disableStats = args.disable or {}
  if #enableStats == 0 and #disableStats == 0 then
    tg_utils.abort(
      "Provide a list of stats to enable ('-y TGF_STATS_BF') " ..
      "or disable ('-n TGF_STATS_BF')."
    )
  end

  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Send StatsConfigure request
  local req = StatsConfigure:new{}
  req.configs = {}
  for _, key in ipairs(enableStats) do
    req.configs[key] = true
  end
  for _, key in ipairs(disableStats) do
    req.configs[key] = false
  end
  req.onDuration = 1
  req.period = 1
  logger.debug(tg_thrift_utils.thriftToJson(req))
  for _, radioMac in ipairs(tg:getMinionRadioMacs(args.radio_mac)) do
    tg:sendToMinion(
      MessageType.FW_STATS_CONFIGURE_REQ,
      TG.serializeDriverMsg(req, radioMac),
      consts.DRIVER_APP_MINION_ID
    )
    logger.info("Request sent to MAC '%s'", radioMac or "")
  end

  tg:close()
end

--- `tg2 minion fw_set_params` command handler
function TG_Handler.minionFwSetParams(args, name)
  -- Validate inputs
  local valid, errMsg = validateRuntimeFwParams(args.params, args.responder_mac)
  if not valid then
    tg_utils.abort(errMsg)
  end

  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Create SUB socket (for receiving response)
  local subscriber, err = tg:connectToMinionPub()
  if err then
    tg_utils.abort(
      string.format("Failed to connect to minion PUB port: %s", err)
    )
  end

  -- Send FwConfigParams request
  local fwOptParams = FwOptParams:new{}
  for i = 1, #args.params, 2 do
    fwOptParams[args.params[i]] = tonumber(args.params[i + 1])
  end
  local msg = PassThruMsg:new{}
  msg.msgType = PtMsgTypes.SB_SET_FW_PARAMS
  msg.dest = PtMsgDest.SB
  msg.setfwParamsReq = SetFwParamsMsg:new{}
  msg.setfwParamsReq.addr = args.responder_mac or consts.EMPTY_MAC_ADDRESS
  msg.setfwParamsReq.optionalParams = fwOptParams
  msg.setfwParamsReq.bwgdIdx = args.bwgd_idx
  local req = FwConfigParams:new{}
  req.passThruMsgs = {msg}
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToMinion(
    MessageType.FW_CONFIG_REQ,
    TG.serializeDriverMsg(req, args.radio_mac),
    consts.DRIVER_APP_MINION_ID
  )
  logger.debug(
    "Request sent to MAC '%s', waiting for response...", args.radio_mac or ""
  )

  -- Listen for response event
  local status = true -- default true for unit tests
  local success = tg:doMinionSubLoop(subscriber, function(app, deserMsg)
    if deserMsg.mType == MessageType.FW_CONFIG_RESP then
      local msgData = SetFwParamsResp:new{}
      TG.deserializeDriverMsg(deserMsg.value, msgData)
      status = msgData.status
      return true -- stop looping
    end
    return false -- keep looping
  end, args.poll_time)
  if not success then
    tg_utils.abort("Timed out.")
  end
  if status then
    logger.info("Setting parameters succeeded.")
  else
    tg_utils.abort("Setting one or more requested parameters failed.")
  end

  subscriber:close()
  tg:close()
end

--- `tg2 minion fw_get_params` command handler
function TG_Handler.minionFwGetParams(args, name)
  if args.type == "link" and args.responder_mac == nil then
    tg_utils.abort("'--responder_mac' is required for reading link parameters.")
  end

  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Create SUB socket (for receiving response)
  local subscriber, err = tg:connectToMinionPub()
  if err then
    tg_utils.abort(
      string.format("Failed to connect to minion PUB port: %s", err)
    )
  end

  -- Send FwConfigParams request
  local msg = PassThruMsg:new{}
  msg.msgType = PtMsgTypes.SB_GET_FW_PARAMS
  msg.dest = PtMsgDest.SB
  msg.getFwParamsReq = GetFwParamsReq:new{}
  if args.type == "link" then
    msg.getFwParamsReq.addr = args.responder_mac
    msg.getFwParamsReq.requestedParamsType = FwParamsType.FW_PARAMS_LINK_FW_CFG
  else
    msg.getFwParamsReq.requestedParamsType = FwParamsType.FW_PARAMS_NODE_FW_CFG
  end
  local req = FwConfigParams:new{}
  req.passThruMsgs = {msg}
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToMinion(
    MessageType.FW_CONFIG_REQ,
    TG.serializeDriverMsg(req, args.radio_mac),
    consts.DRIVER_APP_MINION_ID
  )
  logger.debug(
    "Request sent to MAC '%s', waiting for response...", args.radio_mac or ""
  )

  -- Listen for response event
  local success = tg:doMinionSubLoop(subscriber, function(app, deserMsg)
    if deserMsg.mType == MessageType.FW_CONFIG_RESP then
      local msgData = GetFwParamsResp:new{}
      TG.deserializeDriverMsg(deserMsg.value, msgData)
      logger.info(tg_thrift_utils.thriftToJson(msgData))
      return true -- stop looping
    end
    return false -- keep looping
  end, args.poll_time)
  if not success then
    tg_utils.abort("Timed out.")
  end

  subscriber:close()
  tg:close()
end

--- `tg2 minion links` command handler
function TG_Handler.minionLinks(args, name)
  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Request link status dump
  local req = GetLinkStatusDump:new{}
  tg:sendToMinion(
    MessageType.GET_LINK_STATUS_DUMP, req, consts.IGNITION_APP_MINION_ID
  )

  -- Wait for reply
  local dump = LinkStatusDump:new{}
  if tg:recvFromMinion(
    MessageType.LINK_STATUS_DUMP, dump, consts.IGNITION_APP_MINION_ID
  ) then
    if args.json then
      logger.info(tg_thrift_utils.thriftToJson(dump))
    else
      local headers = {"Local MAC", "Local Intf", "Remote MAC", "Status"}
      local rows = {}
      for k, v in pairs(dump.linkStatusDump or {}) do
        rows[#rows+1] = {
          v.radioMac,
          v.ifname,
          v.responderMac,
          v.linkStatusType == LinkStatusType.LINK_UP and "UP" or "DOWN"
        }
      end
      table.sort(rows, function(a, b)
        if a[1] == b[1] then
          return a[2] < b[2] -- ifname
        end
        return a[1] < b[1] -- radioMac
      end)
      logger.info("\n%s", tabulate.tabulate(rows, {headers = headers}))
    end
  else
    tg_utils.abort("Failed to receive data from minion")
  end

  tg:close()
end

--- `tg2 minion status` command handler
function TG_Handler.minionStatus(args, name)
  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Request status report
  local req = GetStatusReport:new{}
  tg:sendToMinion(
    MessageType.GET_STATUS_REPORT, req, consts.STATUS_APP_MINION_ID
  )

  -- Wait for status report reply
  local report = StatusReport:new{}
  if tg:recvFromMinion(
    MessageType.STATUS_REPORT, report, consts.STATUS_APP_MINION_ID
  ) then
    if args.json then
      logger.info(tg_thrift_utils.thriftToJson(report))
    else
      local headers = {"Radio MAC", "Status", "GPS Sync"}
      local rows = {}
      for k, v in tablex.sort(report.radioStatus or {}) do
        local rStatus
        if v.nodeParamsSet == true then
          rStatus = "configured"
        elseif v.initialized == true then
          rStatus = "initialized"
        else
          rStatus = "N/A (crashed?)"
        end
        rows[#rows+1] = {
          k,
          rStatus,
          v.gpsSync
        }
      end
      logger.info("\n%s", tabulate.tabulate(rows, {headers = headers}))
    end
  else
    tg_utils.abort("Failed to receive data from minion")
  end

  tg:close()
end

--- `tg2 minion get_gps_pos` command handler
function TG_Handler.minionGetGpsPos(args, name)
  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Create SUB socket (for receiving response)
  local subscriber, err = tg:connectToMinionPub()
  if err then
    tg_utils.abort(
      string.format("Failed to connect to minion PUB port: %s", err)
    )
  end

  -- Send request
  local req = Empty:new{}
  tg:sendToMinion(
    MessageType.GPS_GET_POS_REQ, req, consts.STATUS_APP_MINION_ID
  )
  logger.debug("Request sent, waiting for response...")

  -- Listen for location event
  local success = tg:doMinionSubLoop(subscriber, function(app, deserMsg)
    if deserMsg.mType == MessageType.GPS_GET_POS_RESP then
      local msgData = Location:new{}
      TG.deserializeDriverMsg(deserMsg.value, msgData)
      logger.info(tg_thrift_utils.thriftToJson(msgData))
      return true -- stop looping
    end
    return false -- keep looping
  end, args.poll_time)
  if not success then
    tg_utils.abort("Timed out.")
  end

  subscriber:close()
  tg:close()
end

--- `tg2 minion set_gps_pos` command handler
function TG_Handler.minionSetGpsPos(args, name)
  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Send NodeParams request
  local location = Location:new{}
  location.latitude = args.latitude
  location.longitude = args.longitude
  location.altitude = args.altitude
  location.accuracy = args.accuracy
  local req = NodeParams:new{}
  req.type = NodeParamsType.GPS
  req.location = location
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToMinion(
    MessageType.SET_NODE_PARAMS, req, consts.STATUS_APP_MINION_ID
  )
  logger.info("Request sent")

  tg:close()
end

--- `tg2 minion topo_scan` command handler
function TG_Handler.minionTopoScan(args, name)
  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Create SUB socket (for receiving response)
  local subscriber, err = tg:connectToMinionPub()
  if err then
    tg_utils.abort(
      string.format("Failed to connect to minion PUB port: %s", err)
    )
  end

  -- Send scan request
  local req = ScanReq:new{}
  req.radioMac = args.radio_mac
  req.token = args.token or math.random(2^31)
  req.scanType = ScanType.TOPO
  req.startBwgdIdx = 0 -- now
  req.rxNodeMac = consts.BROADCAST_MAC_ADDRESS
  tg:sendToMinion(
    MessageType.SCAN_REQ, req, consts.STATUS_APP_MINION_ID
  )
  logger.debug(
    "Request sent to MAC '%s', waiting for scan response...",
    args.radio_mac or ""
  )

  -- Listen for scan response event
  local success = tg:doMinionSubLoop(subscriber, function(app, deserMsg)
    if deserMsg.mType == MessageType.SCAN_RESP then
      local msgData = ScanResp:new{}
      TG.deserializeDriverMsg(deserMsg.value, msgData)
      if msgData.token == req.token then
        if msgData.status ~= ScanFwStatus.COMPLETE then
          -- Failed, print reason
          local invScanFwStatus = tg_utils.invertTable(ScanFwStatus)
          local errMsg = string.format(
            "Scan failed: %s (code %d)",
            invScanFwStatus[msgData.status] or "?",
            msgData.status
          )
          if msgData.status == ScanFwStatus.UNEXPECTED_ERROR then
            errMsg = errMsg ..
              "\n\nCheck that the channel is configured and GPS is enabled."
          end
          tg_utils.abort(errMsg)
        elseif args.json then
          logger.info(tg_thrift_utils.thriftToJson(msgData))
        else
          local topoResps = {}
          for k, v in tablex.sort(msgData.topoResps or {}) do
            topoResps[#topoResps+1] = v
          end
          printTopoScanResults(msgData.radioMac, topoResps)
        end
        return true -- stop looping
      end
    end
    return false -- keep looping
  end, args.poll_time)
  if not success then
    tg_utils.abort("Timed out.")
  end

  subscriber:close()
  tg:close()
end

--- `tg2 minion topo_scan_loop` command handler
function TG_Handler.minionTopoScanLoop(args, name)
  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Send request
  local req = StartContinuousTopoScan:new{}
  req.radioMac = args.radio_mac
  req.durationSec = args.time
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToMinion(
    MessageType.START_CONTINUOUS_TOPO_SCAN, req, consts.IGNITION_APP_MINION_ID
  )
  logger.info("Request sent to MAC '%s'", args.radio_mac or "")

  tg:close()
end

--- `tg2 minion scan` command handler
function TG_Handler.minionScan(args, name)
  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Send request
  local req, errMsg = argparse_buildScanRequest(args)
  if req == nil then
    tg_utils.abort(errMsg)
  end
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToMinion(
    MessageType.SCAN_REQ, req, consts.STATUS_APP_MINION_ID
  )
  logger.info("Request sent to MAC '%s'", args.radio_mac or "")

  tg:close()
end

--- `tg2 minion fw_debug` command handler
function TG_Handler.minionFwDebug(args, name)
  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Send request
  local req = Debug:new{}
  req.cmdStr = args.command
  req.value = args.value
  logger.debug(tg_thrift_utils.thriftToJson(req))
  for _, radioMac in ipairs(tg:getMinionRadioMacs(args.radio_mac)) do
    tg:sendToMinion(
      MessageType.FW_DEBUG_REQ,
      TG.serializeDriverMsg(req, radioMac),
      consts.DRIVER_APP_MINION_ID
    )
    logger.info("Request sent to MAC '%s'", radioMac or "")
  end

  tg:close()
end

--- `tg2 minion bf_resp_scan` command handler
function TG_Handler.minionBfRespScan(args, name)
  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Send request
  local req = BfRespScanConfig:new{}
  req.cfg = args.flag
  logger.debug(tg_thrift_utils.thriftToJson(req))
  for _, radioMac in ipairs(tg:getMinionRadioMacs(args.radio_mac)) do
    tg:sendToMinion(
      MessageType.FW_BF_RESP_SCAN,
      TG.serializeDriverMsg(req, radioMac),
      consts.DRIVER_APP_MINION_ID
    )
    logger.info("Request sent to MAC '%s'", radioMac or "")
  end

  tg:close()
end

--- `tg2 minion set_node_config` command handler
function TG_Handler.minionSetNodeConfig(args, name)
  local configStr = tg_utils.readFile(args.node_config)
  if configStr == nil then
    tg_utils.abort("Failed to read node config file")
  end

  local tg = TG.new(args)
  if not tg:connectToMinion() then
    tg_utils.abort()
  end

  -- Send request
  local req = SetMinionConfigReq:new{}
  req.config = configStr
  logger.debug(tg_thrift_utils.thriftToJson(req))
  tg:sendToMinion(
    MessageType.SET_MINION_CONFIG_REQ, req, consts.CONFIG_APP_MINION_ID
  )
  logger.info("Request sent")

  tg:close()
end

--- `tg2 stats` command handler
function TG_Handler.stats(args, name)
  local tg = TG.new(args)

  -- Handle "dump" command
  if args.dump then
    -- Connect to ZMQ ROUTER port
    local router, err =
      tg:connectToRouter(args.stats_host, args.stats_source.router)
    if err then
      tg_utils.abort(
        string.format("Failed to connect to stats ROUTER port: %s", err)
      )
    end

    -- Send request
    local req = MonitorRequest:new{}
    req.cmd = MonitorCommand.DUMP_ALL_COUNTER_DATA
    router:send(tg_thrift_utils.serialize(req))

    -- Receive and deserialize response
    local serMsg, more = router:recv()
    if serMsg == nil then
      tg_utils.abort("Failed to receive stats response")
    end
    local response = CounterValuesResponse:new{}
    tg_thrift_utils.deserialize(serMsg, response)

    -- Print counters
    for counterName, counter in tablex.sort(response.counters) do
      printCounter(counterName, counter, args.radio_mac)
    end

    router:close()
    tg:close()
    return
  end

  -- Create SUB socket
  local subscriber, err =
    tg:connectToPub(args.stats_host, args.stats_source.pub)
  if err then
    tg_utils.abort(
      string.format("Failed to connect to stats PUB port: %s", err)
    )
  end

  -- Loop forever...
  local startTime = os.time()
  while true do
    local now = os.time()
    if args.poll_time ~= nil and (now - startTime) >= args.poll_time then
      break
    end

    -- Receive message
    if subscriber:poll(consts.ZMQ_SUB_POLL_TIMEOUT_MS) then
      local serMsg, more = subscriber:recv()
      if serMsg == nil then
        logger.error(more) -- more => error
        break
      else
        -- Deserialize message
        local response = MonitorPub:new{}
        tg_thrift_utils.deserialize(serMsg, response)

        -- Print counters
        if response.pubType == PubType.COUNTER_PUB then
          for counterName, counter in pairs(response.counterPub.counters) do
            printCounter(counterName, counter, args.radio_mac)
          end
        end
      end
    end
  end

  subscriber:close()
  tg:close()
end

--- `tg2 tech-support` command handler
function TG_Handler.techsupport(args, name)
  --- TODO: Check node config if VPP is enabled and error if not
  --- TODO(wishlist): Write a Linux Forwarding check
  if not tg_techsupport.supported(args.node_config) then
    tg_utils.abort(string.format(
      "%s config is not supported for tech-support. EXITING.",
      args.node_config
    ))
  end

  local components = {args.components}
  if string.match(args.components, ",") then
    components = args.components:split(",")
  end

  local do_all = false
  if tablex.find(components, "all") then
    do_all = true
  end

  if not args.noversion then
    tg_techsupport.printVersion()
  end

  logger.info(
    "== Running tech-support for components: '%s' ==",
    table.concat(components, ", ")
  )

  local techsupportChecks = {
    {name="e2e_minion", check=tg_techsupport.check_e2e_minion},
    {name="vpp", check=tg_techsupport.check_vpp},
    {
      name="linux_default_route",
      check=tg_techsupport.check_linux_default_route
    },
    {name="openr", check=tg_techsupport.check_openr},
    {name="fib_vpp", check=tg_techsupport.check_fib_vpp},
  }
  local componentCount = 0
  local failedCount = 0
  for _, component in ipairs(techsupportChecks) do
    if do_all or tablex.find(components, component.name) ~= nil then
      componentCount = componentCount + 1
      if not component.check() then
        failedCount = failedCount + 1
      end
    end
  end

  if failedCount > 0 then
    tg_utils.abort(string.format(
      "\n[FAIL] %d/%d check(s) failed!", failedCount, componentCount
    ))
  elseif componentCount > 0 then
    logger.info(
      "\n[PASS] Node looks healthy! Completed %d check(s).",
      componentCount
    )
  else
    logger.info("\nNo checks were run.")
  end
end

--- `tg2 version` command handler
function TG_Handler.version(args, name)
  -- Passing true here gets uboot version - takes ~2 seconds
  tg_techsupport.printVersion(true)
end

--- `tg2 whoami` command handler
function TG_Handler.whoami(args, name)
  local config = tg_utils.readJsonFile(args.node_config)
  if config == nil then
    tg_utils.abort("Failed to read node config file")
  end

  local node_name = tg_utils.get(
    config, "topologyInfo", "nodeName"
  ) or "unknown"
  logger.info(node_name)
end

--- `tg2 topology` subcommand
local function addTopologyCmd(parser)
  local topologyCmd = parser:command(
    "topology", "View or edit the network topology (via E2E controller)"
  )

  -- "topology ls"
  local topologyLsCmd = topologyCmd:command("ls", "View current topology")
  topologyLsCmd:flag("--json", "Dump in JSON format", false)
  topologyLsCmd:action(TG_Handler.topologyLs)

  -- "topology site"
  local topologySiteCmd = topologyCmd:command(
    "site", "Add/remove sites in the topology"
  )
  -- "topology site add"
  local topologySiteAddCmd = topologySiteCmd:command("add", "Add a site")
  topologySiteAddCmd:argument("name", "Site name")
  topologySiteAddCmd:option("--lat", "Latitude (range: [-90, 90])", "0")
    :convert(tonumber)
  topologySiteAddCmd:option("--lon", "Longitude (range: [-180, 180])", "0")
    :convert(tonumber)
  topologySiteAddCmd:option("--alt", "Altitude (in meters)", "0")
    :convert(tonumber)
  topologySiteAddCmd:option("--acc", "Accuracy (in meters)", "40000000")
    :convert(tonumber)
  topologySiteAddCmd:action(TG_Handler.topologySiteAdd)
  -- "topology site del"
  local topologySiteDelCmd = topologySiteCmd:command("del", "Delete a site")
  topologySiteDelCmd:argument("name", "Site name")
  topologySiteDelCmd:action(TG_Handler.topologySiteDel)

  -- "topology node"
  local topologyNodeCmd = topologyCmd:command(
    "node", "Add/remove nodes in the topology"
  )
  -- "topology node add"
  local topologyNodeAddCmd = topologyNodeCmd:command("add", "Add a node")
  topologyNodeAddCmd:argument("name", "Node name")
  topologyNodeAddCmd:option("-s --site_name", "Site name"):count(1)
  topologyNodeAddCmd:option("-m --mac_addr", "Node ID (MAC address)")
    :convert(argparse_validateMac)
  topologyNodeAddCmd:option(
    "--wlan_mac_addrs", "MAC addresses of WLAN interfaces"
  ):args("*"):convert(argparse_validateMac)
  topologyNodeAddCmd:option("-t --node_type", "Node type", "dn")
    :choices(tablex.map(string.lower, tablex.keys(NodeType)))
  topologyNodeAddCmd:flag("--pop_node", "Whether this is a POP node", false)
  topologyNodeAddCmd:action(TG_Handler.topologyNodeAdd)
  -- "topology node del"
  local topologyNodeDelCmd = topologyNodeCmd:command("del", "Delete a node")
  topologyNodeDelCmd:argument("name", "Node name")
  topologyNodeDelCmd:flag(
    "-f --force", "Force deletion (disregarding liveness)", false
  )
  topologyNodeDelCmd:action(TG_Handler.topologyNodeDel)

  -- "topology link"
  local topologyLinkCmd = topologyCmd:command(
    "link", "Add/remove links in the topology"
  )
  -- "topology link add"
  local topologyLinkAddCmd = topologyLinkCmd:command("add", "Add a link")
  topologyLinkAddCmd:option(
    "-a --a_node_name", "A-node name (lexicographically smaller)"
  ):count(1)
  topologyLinkAddCmd:option(
    "-z --z_node_name", "Z-node name (lexicographically larger)"
  ):count(1)
  topologyLinkAddCmd:option("--a_node_mac", "A-node WLAN MAC address")
    :convert(argparse_validateMac)
  topologyLinkAddCmd:option("--z_node_mac", "Z-node WLAN MAC address")
    :convert(argparse_validateMac)
  topologyLinkAddCmd:option("-t --link_type", "Link type", "wireless")
    :choices(tablex.map(string.lower, tablex.keys(LinkType)))
  topologyLinkAddCmd:flag(
    "--is_backup_cn_link", "Whether this is a backup DN-to-CN link", false
  )
  topologyLinkAddCmd:action(TG_Handler.topologyLinkAdd)
  -- "topology link del"
  local topologyLinkDelCmd = topologyLinkCmd:command("del", "Delete a link")
  topologyLinkDelCmd:option(
    "-a --a_node_name", "A-node name (lexicographically smaller)"
  ):count(1)
  topologyLinkDelCmd:option(
    "-z --z_node_name", "Z-node name (lexicographically larger)"
  ):count(1)
  topologyLinkDelCmd:flag(
    "-f --force", "Force deletion (disregarding liveness)", false
  )
  topologyLinkDelCmd:action(TG_Handler.topologyLinkDel)
end

--- `tg2 controller` subcommand
local function addControllerCmd(parser)
  local controllerCmd = parser:command(
    "controller", "Interface with E2E controller"
  )

  -- "controller status"
  local controllerStatusCmd = controllerCmd:command(
    "status", "View latest node status reports"
  )
  controllerStatusCmd:flag("--json", "Dump in JSON format", false)
  controllerStatusCmd:action(TG_Handler.controllerStatus)

  -- "controller topo_scan"
  local controllerTopoScanCmd = controllerCmd:command(
    "topo_scan", "Run a topology scan on a given node/radio"
  )
  controllerTopoScanCmd:argument("txNode", "The radio MAC or node name")
  controllerTopoScanCmd:flag("--json", "Dump in JSON format", false)
  controllerTopoScanCmd:action(TG_Handler.controllerTopoScan)

  -- "controller network_topo_scan"
  local controllerNetworkTopoScanCmd = controllerCmd:command(
    "network_topo_scan", "Run a network-wide topology scan"
  )
  -- "controller network_topo_scan start"
  local controllerNetworkTopoScanStartCmd = controllerNetworkTopoScanCmd
    :command("start", "Start a scan")
  controllerNetworkTopoScanStartCmd:option(
    "-l --site_link", "All links between sites that should be formed"
  ):args(2):argname({"<site 1>", "<site 2>"}):count("+")
  controllerNetworkTopoScanStartCmd:option(
    "--mac_addrs", "The node MAC addresses to accept (or any MAC if empty)"
  ):args("*"):convert(argparse_validateMac)
  controllerNetworkTopoScanStartCmd:option(
    "--cn_sites", "The sites comprised of CN nodes"
  ):args("*")
  controllerNetworkTopoScanStartCmd:option(
    "--y_street_sites", "The sites to allow creating Y-street topologies on"
  ):args("*")
  controllerNetworkTopoScanStartCmd:option(
    "--beam_angle_penalty",
    "The penalty for high tx/rx beam angles when selecting the 'best' " ..
    "quality link to form, except on P2MP sites: " ..
    "link quality := SNR - (penalty * combined beam angle)",
    "0.1"
  ):convert(tonumber)
  controllerNetworkTopoScanStartCmd:option(
    "--distance_threshold",
    "The maximum distance, in meters, to allow between a responder's " ..
    "reported position and the nearest site",
    "50"
  ):convert(tonumber)
  controllerNetworkTopoScanStartCmd:option(
    "--snr_threshold",
    "The minimum signal-to-noise ratio (SNR), in dB, to allow on new links " ..
    "(default of 6.1dB is needed to support MCS2 at a PER of 1e-3)",
    "6.1"
  ):convert(tonumber)
  controllerNetworkTopoScanStartCmd:option(
    "--scans_per_node",
    "The number of scans that each node will initiate (regardless of success)",
    "1"
  ):convert(tonumber)
  controllerNetworkTopoScanStartCmd:flag(
    "--no_merge_adj_macs",
    "Treat all 'adjs' as separate nodes instead of radios on a single node",
    false
  )
  controllerNetworkTopoScanStartCmd:flag(
    "-s --store_results", "Store all scan results on the controller", false
  )
  controllerNetworkTopoScanStartCmd:flag(
    "--dry_run", "Do not add any elements to the topology", false
  )
  controllerNetworkTopoScanStartCmd
    :action(TG_Handler.controllerNetworkTopoScanStart)
  -- "controller network_topo_scan stop"
  local controllerNetworkTopoScanStopCmd = controllerNetworkTopoScanCmd
    :command("stop", "Stop a scan")
  controllerNetworkTopoScanStopCmd
    :action(TG_Handler.controllerNetworkTopoScanStop)
  -- "controller network_topo_scan status"
  local controllerNetworkTopoScanStatusCmd = controllerNetworkTopoScanCmd
    :command("status", "Show current scan status")
  controllerNetworkTopoScanStatusCmd
    :flag("--brief", "Omit verbose fields", false)
  controllerNetworkTopoScanStatusCmd
    :action(TG_Handler.controllerNetworkTopoScanStatus)

  -- "controller optimize"
  local controllerOptimizeCmd = controllerCmd:command(
    "optimize", "Run network optimization algorithms"
  )
  controllerOptimizeCmd:flag(
    "-c --clear_user_config", "Clear user-configured values", false
  )
  -- "controller optimize polarity"
  local controllerOptimizePolarityCmd = controllerOptimizeCmd
    :command("polarity", "Optimize polarity")
  controllerOptimizePolarityCmd:action(TG_Handler.controllerOptimizePolarity)
  -- "controller optimize golay"
  local controllerOptimizeGolayCmd = controllerOptimizeCmd
    :command("golay", "Optimize Golay code")
  controllerOptimizeGolayCmd:action(TG_Handler.controllerOptimizeGolay)
  -- "controller optimize control_superframe"
  local controllerOptimizeControlSuperframeCmd = controllerOptimizeCmd
    :command("control_superframe", "Optimize control superframe")
  controllerOptimizeControlSuperframeCmd
    :action(TG_Handler.controllerOptimizeControlSuperframe)
  -- "controller optimize channel"
  local controllerOptimizeChannelCmd = controllerOptimizeCmd
    :command("channel", "Optimize channel")
  controllerOptimizeChannelCmd:action(TG_Handler.controllerOptimizeChannel)
end

--- `tg2 fw` subcommand
local function addFwCmd(parser)
  local fwCmd = parser:command(
    "fw", "Interface with firmware via driver-if in daemon mode"
  )
  fwCmd:option("--radio_mac", "Radio MAC address"):convert(argparse_validateMac)

  -- "fw node_init"
  local fwNodeInitCmd = fwCmd:command(
    "node_init", "Initialize firmware with parameters from node config"
  )
  fwNodeInitCmd:option(
    "-f --node_config", "Node config path", consts.NODE_CONFIG
  )
  fwNodeInitCmd:flag("--json", "Dump in JSON format", false)
  fwNodeInitCmd:action(TG_Handler.fwNodeInit)

  -- "fw channel_config"
  local fwChannelConfigCmd = fwCmd:command(
    "channel_config", "Send channel configuration to firmware"
  )
  fwChannelConfigCmd:option("-c --channel", "Radio channel", "2")
    :convert(tonumber)
  fwChannelConfigCmd:action(TG_Handler.fwChannelConfig)

  -- "fw gps_enable"
  local fwGpsEnableCmd = fwCmd:command("gps_enable", "Enable GPS sync mode")
  fwGpsEnableCmd:action(TG_Handler.fwGpsEnable)

  -- "fw get_gps_pos"
  local fwGetGpsPosCmd = fwCmd:command("get_gps_pos", "Get GPS position")
  fwGetGpsPosCmd:flag("--json", "Dump in JSON format", false)
  fwGetGpsPosCmd:action(TG_Handler.fwGetGpsPos)

  -- "fw set_gps_pos"
  local fwSetGpsPosCmd = fwCmd:command(
    "set_gps_pos", "Set GPS position to enable GPS single-satellite mode"
  )
  fwSetGpsPosCmd:option("--lat", "Latitude (range: [-90, 90])", "0")
    :convert(tonumber)
  fwSetGpsPosCmd:option("--lon", "Longitude (range: [-180, 180])", "0")
    :convert(tonumber)
  fwSetGpsPosCmd:option("--alt", "Altitude (in meters)", "0")
    :convert(tonumber)
  fwSetGpsPosCmd:option("--acc", "Accuracy (in meters)", "40000000")
    :convert(tonumber)
  fwSetGpsPosCmd:flag("--no_ack", "Do not wait for acknowledgement", false)
  fwSetGpsPosCmd:action(TG_Handler.fwSetGpsPos)

  -- "fw gps_send_time"
  local fwGpsSendTimeCmd = fwCmd:command(
    "gps_send_time", "Send a GPS time to firmware"
  )
  fwGpsSendTimeCmd:argument("time_sec", "GPS time (in seconds)")
    :convert(tonumber)
  fwGpsSendTimeCmd:flag("--no_ack", "Do not wait for acknowledgement", false)
  fwGpsSendTimeCmd:action(TG_Handler.fwGpsSendTime)

  -- "fw assoc"
  local fwAssocCmd = fwCmd:command(
    "assoc", "Associate a link with parameters from node config"
  )
  fwAssocCmd:option("-f --node_config", "Node config path", consts.NODE_CONFIG)
  fwAssocCmd:option("-m --responder_mac", "Responder MAC address")
    :count(1):convert(argparse_validateMac)
  fwAssocCmd:action(TG_Handler.fwAssoc)

  -- "fw dissoc"
  local fwDissocCmd = fwCmd:command("dissoc", "Dissociate a link")
  fwDissocCmd:option("-m --responder_mac", "Responder MAC address")
    :count(1):convert(argparse_validateMac)
  fwDissocCmd:action(TG_Handler.fwDissoc)

  -- "fw fw_set_log_config"
  local fwFwSetLogConfigCmd = fwCmd:command(
    "fw_set_log_config",
    "Set firmware verbosity logging level for specified modules"
  )
  fwFwSetLogConfigCmd:option(
    "-m --module",
    "Module name (supports multiple modules, empty = all modules)"
  ):count("*"):choices(tablex.map(string.lower, tablex.keys(LogModule)))
  fwFwSetLogConfigCmd:option(
    "-l --level",
    "Logging level"
  ):count(1):choices(tablex.map(string.lower, tablex.keys(LogLevel)))
  fwFwSetLogConfigCmd:action(TG_Handler.fwFwSetLogConfig)

  -- fw fw_stats_config
  local fwFwStatsConfigCmd = fwCmd:command(
    "fw_stats_config", "Set firmware stats config"
  )
  fwFwStatsConfigCmd:option(
    "-f --config_file",
    "Firmware Stats Config (StatsConfigure in PassThru.thrift)"
  ):count(1)
  fwFwStatsConfigCmd:action(TG_Handler.fwFwStatsConfig)

  -- "fw debug"
  local fwDebugCmd = fwCmd:command(
    "debug",
    "Send a debug command to firmware"
  )
  fwDebugCmd:option("-c --command", "Debug command"):count(1)
  fwDebugCmd:option("-v --value", "Command value")
    :count(1):convert(tonumber)
  fwDebugCmd:action(TG_Handler.fwDebug)

  -- "fw polarity_config"
  local fwPolarityConfigCmd = fwCmd:command(
    "polarity_config",
    "Send polarity configuration to firmware, to be used in subsequent " ..
    "assoc commands"
  )
  fwPolarityConfigCmd:option("-p --polarity", "Node polarity")
    :count(1):choices(tablex.map(string.lower, tablex.keys(PolarityType)))
  fwPolarityConfigCmd:action(TG_Handler.fwPolarityConfig)

  -- "fw golay_config"
  local fwGolayConfigCmd = fwCmd:command(
    "golay_config",
    "Send Golay code configuration to firmware, to be used on the " ..
    "initiator node only in subsequent assoc commands"
  )
  fwGolayConfigCmd:option(
    "-t --tx_index",
    "TX Golay code index (0 to 7)"
  ):count(1):convert(tonumber)
  fwGolayConfigCmd:option(
    "-r --rx_index",
    "RX Golay code index (0 to 7)"
  ):count(1):convert(tonumber)
  fwGolayConfigCmd:action(TG_Handler.fwGolayConfig)

  -- "fw bf_slot_exclusion_req"
  local fwBfSlotExclusionReqCmd = fwCmd:command(
    "bf_slot_exclusion_req",
    "Configure BF slot exclusion to ensure that scans and beamforming do " ..
    "not collide"
  )
  fwBfSlotExclusionReqCmd:option(
    "-b --bwgd_idx",
    "BWGD index of slot exclusion start"
  ):count(1):convert(tonumber)
  fwBfSlotExclusionReqCmd:action(TG_Handler.fwBfSlotExclusionReq)

  -- "fw bf_resp_scan_config"
  local fwBfRespScanConfigCmd = fwCmd:command(
    "bf_resp_scan_config",
    "Enable/disable BF responder scan mode"
  )
  fwBfRespScanConfigCmd:option(
    "-c --cfg",
    "Enable/disable BF responder scan mode (1=enable, 0=disable)"
  ):count(1):convert(argparse_toBool)
  fwBfRespScanConfigCmd:action(TG_Handler.fwBfRespScanConfig)

  -- "fw phyla_config"
  local fwPhyLAConfigCmd = fwCmd:command(
    "phyla_config",
    "Configure PHY Link Adaptation at run time"
  )
  fwPhyLAConfigCmd:option(
    "-f --config_file",
    "PHY LA Config file (PhyLAParams in PassThru.thrift)"
  ):count(1)
  fwPhyLAConfigCmd:option("-m --responder_mac", "Responder MAC address")
    :count(1):convert(argparse_validateMac)
  fwPhyLAConfigCmd:action(TG_Handler.fwPhyLAConfig)

  -- fw phyagc_config
  local fwPhyAgcConfigCmd = fwCmd:command(
    "phyagc_config",
    "Configure PHY Max Automatic Gain Control tracking"
  )
  fwPhyAgcConfigCmd:option(
    "-f --config_file",
    "PHY AGC Config file PHY AGC Config file (PhyAgcParams in " ..
    "PassThru.thrift). The node parameters (agcNodeParams) apply to " ..
    "all links on the node."
  ):count(1)
  fwPhyAgcConfigCmd:option("-m --responder_mac", "Responder MAC address")
    :count(1):convert(argparse_validateMac)
  fwPhyAgcConfigCmd:action(TG_Handler.fwPhyAgcConfig)

  -- fw phytpc_config
  local fwPhyTpcConfigCmd = fwCmd:command(
    "phytpc_config",
    "Configure PHY Transmit Power Control"
  )
  fwPhyTpcConfigCmd:option(
    "-f --config_file",
    "PHY TPC Config file (PhyTpcConfig in PassThru.thrift)"
  ):count(1)
  fwPhyTpcConfigCmd:option("-m --responder_mac", "Responder MAC address")
    :count(1):convert(argparse_validateMac)
  fwPhyTpcConfigCmd:action(TG_Handler.fwPhyTpcConfig)

  -- fw phy_tpc_adj_tbl_config
  local fwPhyTpcAdjTblConfigCmd = fwCmd:command(
    "phy_tpc_adj_tbl_config",
    "Configure PHY TxPower Adjustment Table"
  )
  fwPhyTpcAdjTblConfigCmd:option(
    "-f --config_file",
    "PHY TxPower Adjustment Table Config (PhyTpcAdjTblCfg in PassThru.thrift)"
  ):count(1)
  fwPhyTpcAdjTblConfigCmd:action(TG_Handler.fwPhyTpcAdjTblConfig)

  -- fw phy_ant_wgt_code_book_config
  local fwPhyAntWgtCodeBookConfigCmd = fwCmd:command(
    "phy_ant_wgt_code_book_config",
    "Configure PHY Antenna Codebook"
  )
  fwPhyAntWgtCodeBookConfigCmd:option(
    "-f --config_file",
    "PHY Antenna Weight Table Config (PhyAntWgtCodeBookConfig in " ..
    "PassThru.thrift)"
  ):count(1)
  fwPhyAntWgtCodeBookConfigCmd:action(TG_Handler.fwPhyAntWgtCodeBookConfig)

  -- fw phy_golay_sequence_config
  local fwPhyGolaySequenceConfigCmd = fwCmd:command(
    "phy_golay_sequence_config",
    "Configure PHY Golay Sequences"
  )
  fwPhyGolaySequenceConfigCmd:option(
    "-f --config_file",
    "PHY Golay Sequences Config (PhyGolaySequenceConfig in PassThru.thrift)"
  ):count(1)
  fwPhyGolaySequenceConfigCmd:action(TG_Handler.fwPhyGolaySequenceConfig)

  -- fw airtime_alloc
  local fwAirtimeAllocCmd = fwCmd:command(
    "airtime_alloc", "Perform dynamic airtime allocation"
  )
  fwAirtimeAllocCmd:option(
    "-f --airtime_alloc_file",
    "Dynamic Airtime Allocation Config (NodeAirtime in BWAllocation.thrift)"
  ):count(1)
  fwAirtimeAllocCmd:action(TG_Handler.fwAirtimeAlloc)

  -- fw fw_get_params
  local fwFwGetParamsCmd = fwCmd:command(
    "fw_get_params", "Get firmware node/link runtime parameters"
  )
  fwFwGetParamsCmd:option(
    "-m --responder_mac", "Responder MAC address (for link parameters)"
  ):convert(argparse_validateMac)
  fwFwGetParamsCmd:flag("--json", "Dump in JSON format", false)
  fwFwGetParamsCmd:argument("type", "Parameter type")
    :choices({"node", "link"}):argname("[node|link]")
  fwFwGetParamsCmd:action(TG_Handler.fwFwGetParams)

  -- fw fw_set_params
  local fwFwSetParamsCmd = fwCmd:command(
    "fw_set_params",
    "Set firmware node/link runtime parameters",
    "Runtime node parameters:\n" ..
    "    " .. (", "):join(consts.RUNTIME_FW_NODE_PARAMS) ..
    "\n\nRuntime link parameters:\n" ..
    "    " .. (", "):join(consts.RUNTIME_FW_LINK_PARAMS)
  )
  fwFwSetParamsCmd:option(
    "-m --responder_mac", "Responder MAC address (for link parameters)"
  ):convert(argparse_validateMac)
  fwFwSetParamsCmd:option(
    "-b --bwgd_idx",
    "BWGD index of execution start " ..
    "(if not given, command will be executed immediately)"
  ):convert(tonumber)
  fwFwSetParamsCmd:flag("--json", "Dump in JSON format", false)
  fwFwSetParamsCmd:argument("params", "Parameter name-value pairs")
    :args("+"):argname("<paramName> <paramValue>")
  fwFwSetParamsCmd:action(TG_Handler.fwFwSetParams)

  -- fw scan
  local fwScanCmd = fwCmd:command("scan", "Start a scan")
  argparse_addScanCommandOptions(fwScanCmd)
  fwScanCmd:action(TG_Handler.fwScan)

  -- Group related commands (in "--help" text)
  fwCmd:group(
    "Action commands",
    fwAssocCmd,
    fwDissocCmd,
    fwScanCmd,
    fwDebugCmd,
    fwBfRespScanConfigCmd,
    fwBfSlotExclusionReqCmd
  )
  fwCmd:group(
    "Configuration commands",
    fwNodeInitCmd,
    fwFwSetLogConfigCmd,
    fwFwStatsConfigCmd,
    fwFwGetParamsCmd,
    fwFwSetParamsCmd,
    fwChannelConfigCmd,
    fwPolarityConfigCmd,
    fwGolayConfigCmd,
    fwAirtimeAllocCmd,
    fwPhyLAConfigCmd,
    fwPhyAgcConfigCmd,
    fwPhyTpcConfigCmd,
    fwPhyTpcAdjTblConfigCmd,
    fwPhyAntWgtCodeBookConfigCmd,
    fwPhyGolaySequenceConfigCmd
  )
  fwCmd:group(
    "GPS commands",
    fwGpsEnableCmd,
    fwGetGpsPosCmd,
    fwSetGpsPosCmd,
    fwGpsSendTimeCmd
  )
end

--- `tg2 event` subcommand
local function addEventCmd(parser)
  local eventCmd = parser:command("event", "Send an event to stats agent")
  eventCmd:option("-c --category", "Event category"):count(1)
    :choices(tablex.keys(EventCategory))
  eventCmd:option("-i --id", "Event ID"):count(1):choices(tablex.keys(EventId))
  eventCmd:option("-l --level", "Event severity level", "INFO")
    :choices(tablex.keys(EventLevel))
  eventCmd:option("-r --reason", "Plain-text event description"):count(1)
  eventCmd:option("-d --details", "JSON containing additional details")
  eventCmd:option(
    "-s --source", "Event source (ex. process or file name)", "CLI"
  )
  eventCmd:option("-e --entity", "Event entity (optional)")
  eventCmd:option("-n --node_id", "Node ID (MAC address, optional)")
  eventCmd:option("--node_name", "Node name")
  eventCmd:option("-t --topology", "Topology name")
  eventCmd:action(TG_Handler.event)
end

--- `tg2 minion` subcommand
local function addMinionCmd(parser)
  local minionCmd = parser:command("minion", "Interface with E2E minion")

  -- "minion sub"
  local minionSubCmd = minionCmd:command(
    "sub", "Subscribe to broadcast messages"
  )
  minionSubCmd:option(
    "-t --poll_time", "Exit after the given number of seconds"
  ):convert(tonumber)
  minionSubCmd:option(
    "-f --filter", "Only show the given list of message types (MessageType)"
  ):count("*")
  minionSubCmd:action(TG_Handler.minionSub)

  -- "minion assoc"
  local minionAssocCmd = minionCmd:command("assoc", "Associate a link")
  minionAssocCmd:group(
    "Required named arguments",
    minionAssocCmd:option("-m --responder_mac", "Responder MAC address")
      :count(1):convert(argparse_validateMac),
    minionAssocCmd:option("-i --initiator_mac", "Initiator MAC address")
      :count(1):convert(argparse_validateMac)
  )
  minionAssocCmd:group(
    "Optional arguments",
    minionAssocCmd:option("-n --responder_node_type", "Responder node type")
      :choices(tablex.map(string.lower, tablex.keys(NodeType))),
    minionAssocCmd:option("--tx_golay", "Responder TX golay")
      :convert(tonumber),
    minionAssocCmd:option("--rx_golay", "Responder RX golay")
      :convert(tonumber),
    minionAssocCmd:option(
      "-s --control_superframe", "Control superframe for the link"
    ):convert(tonumber),
    minionAssocCmd:option("-p --responder_polarity", "Responder polarity")
      :choices(tablex.map(string.lower, tablex.keys(PolarityType))),
    minionAssocCmd:option(
      "-t --poll_time", "Maximum number of seconds to wait for response", "20"
    ):convert(tonumber)
  )
  minionAssocCmd:action(TG_Handler.minionAssoc)

  -- "minion dissoc"
  local minionDissocCmd = minionCmd:command("dissoc", "Dissociate a link")
  minionDissocCmd:option("-m --responder_mac", "Responder MAC address")
    :count(1):convert(argparse_validateMac)
  minionDissocCmd:option("-i --initiator_mac", "Initiator MAC address")
    :count(1):convert(argparse_validateMac)
  minionDissocCmd:option(
    "-t --poll_time", "Maximum number of seconds to wait for response", "5"
  ):convert(tonumber)
  minionDissocCmd:action(TG_Handler.minionDissoc)

  -- "minion gps_enable"
  local minionGpsEnableCmd = minionCmd:command(
    "gps_enable", "Enable GPS sync mode"
  )
  minionGpsEnableCmd:option(
    "-m --radio_mac",
    "Radio MAC address (if empty, send for all MACs)"
  ):convert(argparse_validateMac)
  minionGpsEnableCmd:option(
    "-t --poll_time", "Maximum number of seconds to wait for response", "5"
  ):convert(tonumber)
  minionGpsEnableCmd:action(TG_Handler.minionGpsEnable)

  -- "minion set_params"
  local minionSetParamsCmd = minionCmd:command(
    "set_params", "Set node parameters"
  )
  minionSetParamsCmd:option(
    "-m --radio_mac",
    "Radio MAC address (if empty, send for all MACs)"
  ):convert(argparse_validateMac)
  minionSetParamsCmd:option("-c --channel", "Radio channel"):convert(tonumber)
  minionSetParamsCmd:option("-p --polarity", "Radio polarity")
    :choices(tablex.map(string.lower, tablex.keys(PolarityType)))
  minionSetParamsCmd:option(
    "-a --airtime_alloc_file",
    "File containing dynamic airtime allocation parameters (NodeAirtime)"
  )
  minionSetParamsCmd:option(
    "-t --poll_time", "Maximum number of seconds to wait for response", "5"
  ):convert(tonumber)
  minionSetParamsCmd:action(TG_Handler.minionSetParams)

  -- "minion fw_set_log_config"
  local minionFwSetLogConfigCmd = minionCmd:command(
    "fw_set_log_config",
    "Set firmware verbosity logging level for specified modules"
  )
  minionFwSetLogConfigCmd:option(
    "--radio_mac",
    "Radio MAC address (if empty, send for all MACs)"
  ):convert(argparse_validateMac)
  minionFwSetLogConfigCmd:option(
    "-m --module",
    "Module name (supports multiple modules, empty = all modules)"
  ):count("*"):choices(tablex.map(string.lower, tablex.keys(LogModule)))
  minionFwSetLogConfigCmd:option("-l --level", "Logging level")
    :count(1):choices(tablex.map(string.lower, tablex.keys(LogLevel)))
  minionFwSetLogConfigCmd:action(TG_Handler.minionFwSetLogConfig)

  -- "minion fw_stats_config"
  local minionFwStatsConfigCmd = minionCmd:command(
    "fw_stats_config", "Set firmware stats config"
  )
  minionFwStatsConfigCmd:option(
    "-m --radio_mac", "Radio MAC address (if empty, send for all MACs)"
  ):convert(argparse_validateMac)
  minionFwStatsConfigCmd:option(
    "-y --enable", "Stats type to enable (eTgfStatsType)"
  ):count("*")
  minionFwStatsConfigCmd:option(
    "-n --disable", "Stats type to disable (eTgfStatsType)"
  ):count("*")
  minionFwStatsConfigCmd:action(TG_Handler.minionFwStatsConfig)

  -- "minion fw_get_params"
  local minionFwGetParamsCmd = minionCmd:command(
    "fw_get_params", "Get firmware node/link runtime parameters"
  )
  minionFwGetParamsCmd:option("-m --radio_mac", "Radio MAC address")
    :convert(argparse_validateMac)
  minionFwGetParamsCmd:option(
    "-r --responder_mac", "Responder MAC address (for link parameters)"
  ):convert(argparse_validateMac)
  minionFwGetParamsCmd:option("-t --type", "Parameter type"):count(1)
    :choices({"node", "link"})
  minionFwGetParamsCmd:option(
    "--poll_time", "Maximum number of seconds to wait for response", "5"
  ):convert(tonumber)
  minionFwGetParamsCmd:action(TG_Handler.minionFwGetParams)

  -- "minion fw_set_params"
  local minionFwSetParamsCmd = minionCmd:command(
    "fw_set_params",
    "Set firmware node/link runtime parameters",
    "Runtime node parameters:\n" ..
    "    " .. (", "):join(consts.RUNTIME_FW_NODE_PARAMS) ..
    "\n\nRuntime link parameters:\n" ..
    "    " .. (", "):join(consts.RUNTIME_FW_LINK_PARAMS)
  )
  minionFwSetParamsCmd:option("-m --radio_mac", "Radio MAC address")
    :convert(argparse_validateMac)
  minionFwSetParamsCmd:option(
    "-r --responder_mac", "Responder MAC address (for link parameters)"
  ):convert(argparse_validateMac)
  minionFwSetParamsCmd:option(
    "-b --bwgd_idx",
    "BWGD index of execution start " ..
    "(if not given, command will be executed immediately)"
  ):convert(tonumber)
  minionFwSetParamsCmd:option(
    "-t --poll_time", "Maximum number of seconds to wait for response", "5"
  ):convert(tonumber)
  minionFwSetParamsCmd:argument("params", "Parameter name-value pairs")
    :args("+"):argname("<paramName> <paramValue>")
  minionFwSetParamsCmd:action(TG_Handler.minionFwSetParams)

  -- "minion links"
  local minionLinksCmd = minionCmd:command("links", "Dump link status")
  minionLinksCmd:flag("--json", "Dump in JSON format", false)
  minionLinksCmd:action(TG_Handler.minionLinks)

  -- "minion status"
  local minionStatusCmd = minionCmd:command("status", "Get minion status")
  minionStatusCmd:flag("--json", "Dump in JSON format", false)
  minionStatusCmd:action(TG_Handler.minionStatus)

  -- "minion get_gps_pos"
  local minionGetGpsPosCmd = minionCmd:command(
    "get_gps_pos", "Get GPS position"
  )
  minionGetGpsPosCmd:option(
    "-t --poll_time", "Maximum number of seconds to wait for response", "5"
  ):convert(tonumber)
  minionGetGpsPosCmd:action(TG_Handler.minionGetGpsPos)

  -- "minion set_gps_pos"
  local minionSetGpsPosCmd = minionCmd:command(
    "set_gps_pos", "Set GPS position to enable GPS single-satellite mode"
  )
  minionSetGpsPosCmd:option("--latitude", "Latitude"):count(1):convert(tonumber)
  minionSetGpsPosCmd:option("--longitude", "Longitude"):count(1)
    :convert(tonumber)
  minionSetGpsPosCmd:option("--altitude", "Altitude (in meters)"):count(1)
    :convert(tonumber)
  minionSetGpsPosCmd:option("--accuracy", "Location accuracy (in meters)", "50")
    :convert(tonumber)
  minionSetGpsPosCmd:action(TG_Handler.minionSetGpsPos)

  -- "minion topo_scan"
  local minionTopoScanCmd = minionCmd:command(
    "topo_scan", "Run a topology scan"
  )
  minionTopoScanCmd:option("-m --radio_mac", "Radio MAC address")
    :convert(argparse_validateMac)
  minionTopoScanCmd:option(
    "-t --poll_time", "Maximum number of seconds to wait for response", "15"
  ):convert(tonumber)
  minionTopoScanCmd:option(
    "--token", "Token to associate response with request (default=random)"
  ):convert(tonumber)
  minionTopoScanCmd:flag("--json", "Dump in JSON format", false)
  minionTopoScanCmd:action(TG_Handler.minionTopoScan)

  -- "minion topo_scan_loop"
  local minionTopoScanLoopCmd = minionCmd:command(
    "topo_scan_loop", "Run/stop a continuous topology scan loop"
  )
  minionTopoScanLoopCmd:option("-m --radio_mac", "Radio MAC address")
    :convert(argparse_validateMac)
  minionTopoScanLoopCmd:option(
    "-t --time", "Loop duration (in seconds), or zero to stop any running scans"
  ):count(1):convert(tonumber)
  minionTopoScanLoopCmd:action(TG_Handler.minionTopoScanLoop)

  -- "minion scan"
  local minionScanCmd = minionCmd:command("scan", "Start a scan")
  argparse_addScanCommandOptions(minionScanCmd)
  minionScanCmd:action(TG_Handler.minionScan)

  -- "minion fw_debug"
  local minionFwDebugCmd = minionCmd:command(
    "fw_debug", "Send a debug command to firmware"
  )
  minionFwDebugCmd:option(
    "-m --radio_mac", "Radio MAC address (if empty, send for all MACs)"
  ):convert(argparse_validateMac)
  minionFwDebugCmd:option("-c --command", "Debug command"):count(1)
  minionFwDebugCmd:option("-v --value", "Command value"):count(1)
    :convert(tonumber)
  minionFwDebugCmd:action(TG_Handler.minionFwDebug)

  -- "minion bf_resp_scan"
  local minionBfRespScanCmd = minionCmd:command(
    "bf_resp_scan", "Enable/disable BF responder scan mode"
  )
  local minionBfRespScanCmdFlag = {enable = true, disable = false}
  minionBfRespScanCmd:argument("flag", "Enable or disable this setting")
    :choices(tablex.keys(minionBfRespScanCmdFlag))
    :convert(function(arg) return minionBfRespScanCmdFlag[arg] end)
  minionBfRespScanCmd:option(
    "-m --radio_mac", "Radio MAC address (if empty, send for all MACs)"
  ):convert(argparse_validateMac)
  minionBfRespScanCmd:action(TG_Handler.minionBfRespScan)

  -- "minion set_node_config"
  local minionSetNodeConfigCmd = minionCmd:command(
    "set_node_config", "Set node config"
  )
  minionSetNodeConfigCmd:option(
    "--node_config", "Node config path", consts.NODE_CONFIG
  )
  minionSetNodeConfigCmd:action(TG_Handler.minionSetNodeConfig)

  -- Group related commands (in "--help" text)
  minionCmd:group(
    "Info commands", minionStatusCmd, minionLinksCmd, minionSubCmd
  )
  minionCmd:group(
    "Action commands",
    minionAssocCmd,
    minionDissocCmd,
    minionTopoScanCmd,
    minionTopoScanLoopCmd,
    minionScanCmd,
    minionFwDebugCmd,
    minionBfRespScanCmd
  )
  minionCmd:group(
    "Configuration commands",
    minionSetParamsCmd,
    minionFwSetParamsCmd,
    minionFwGetParamsCmd,
    minionFwSetLogConfigCmd,
    minionFwStatsConfigCmd,
    minionSetNodeConfigCmd
  )
  minionCmd:group(
    "GPS commands", minionGpsEnableCmd, minionGetGpsPosCmd, minionSetGpsPosCmd
  )
end

--- `tg2 stats` subcommand
local function addStatsCmd(parser)
  -- from node config (statsAgentParams.sources)
  local STATS_SOURCES = {
    controller = {pub = 28989, router = 27007},
    ["driver-if"] = {pub = 18990, router = 17008},
    minion = {pub = 18989, router = 17007},
    system = {pub = 18991, router = 17009},
  }

  local statsCmd = parser:command("stats", "Dump local stats")
  statsCmd:option("--stats_host", "Source hostname/IP", "localhost")
  statsCmd:option(
    "-m --radio_mac",
    "Filter stats by source MAC address (if applicable)"
  ):convert(argparse_validateMac)
  statsCmd:option(
    "-t --poll_time", "Exit after the given number of seconds"
  ):convert(tonumber)
  statsCmd:argument(
    "stats_source",
    string.format(
      "The stats source (port number, or one of: [%s])",
      (", "):join(tablex.keys(STATS_SOURCES))
    )
  ):convert(function(arg)
    local i = tonumber(arg)
    if i ~= nil and types.is_integer(i) and (i > 0 and i <= 65535) then
      return {pub = i, router = i}
    elseif STATS_SOURCES[arg] ~= nil then
      return STATS_SOURCES[arg]
    else
      return nil
    end
  end)
  statsCmd:flag("--dump", "Dump all counter data once", false)
  statsCmd:action(TG_Handler.stats)
end

--- `tg2 tech-support` subcommand
local function addTechSupportCmd(parser)
  local techCmd = parser:command(
    "tech-support", "Run health checks on local Terragraph services"
  )
  -- TODO: Add command to list supported components
  techCmd:option(
    "-c --components", "CSV of Terragraph component(s) to inspect", "all"
  )
  techCmd:flag("-V --noversion", "Skip printing version before health checks")
  techCmd:option(
    "--node_config", "Node config path", consts.NODE_CONFIG
  )
  techCmd:action(TG_Handler.techsupport)
end

--- `tg2 version` subcommand
local function addVersionCmd(parser)
  local versionCmd = parser:command(
    "version", "Show version of Terragraph components"
  )
  versionCmd:action(TG_Handler.version)
end

--- `tg2 whoami` subcommand
local function addWhoAmICmd(parser)
  local whoAmICmd = parser:command("whoami", "Show local Terragraph node name")
  whoAmICmd:option(
    "--node_config", "Node config path", consts.NODE_CONFIG
  )
  whoAmICmd:action(TG_Handler.whoami)
end

local function createParser()
  local parser = argparse("tg2", "Terragraph CLI")
  parser:help_max_width(80)
  parser:option("--log_level", "Log level for the CLI", "info")
    :choices(logger.LEVELS)
    :convert(argparse_setLogLevel)
  parser:group(
    "Connection options",
    parser:flag(
      "-E --fetch_controller_host",
      "Read E2E controller hostname/IP from Open/R KvStore",
      false
    ),
    parser:option(
      "--controller_host", "E2E controller hostname/IP", "localhost"
    ),
    parser:option("--controller_port", "E2E controller port", "17077")
      :convert(tonumber),
    parser:option("--minion_host", "E2E minion hostname/IP", "localhost"),
    parser:option("--minion_port", "E2E minion port", "17177")
      :convert(tonumber),
    parser:option("--minion_pub_port", "E2E minion PUB port", "17277")
      :convert(tonumber),
    parser:option("--driver_if_host", "E2E driver-if hostname/IP", "localhost"),
    parser:option("--driver_if_port", "E2E driver-if port", "17989")
      :convert(tonumber),
    parser:option("--agent_host", "Stats agent hostname/IP", "localhost"),
    parser:option("--agent_port", "Stats agent port", "4231")
      :convert(tonumber)
  )
  parser:group(
    "ZMQ socket options",
    parser:option(
      "--router_recv_timeout", "ZMQ ROUTER socket recv timeout (ms)", "2000"
    ):convert(tonumber),
    parser:option(
      "--router_send_timeout", "ZMQ ROUTER socket send timeout (ms)", "4000"
    ):convert(tonumber),
    parser:option(
      "--pair_recv_timeout", "ZMQ PAIR socket recv timeout (ms)", "30000"
    ):convert(tonumber),
    parser:option(
      "--pair_send_timeout", "ZMQ PAIR socket send timeout (ms)", "4000"
    ):convert(tonumber),
    parser:option(
      "--pub_recv_timeout", "ZMQ PUB socket recv timeout (ms)", "200000"
    ):convert(tonumber),
    parser:option(
      "--pub_send_timeout", "ZMQ PUB socket send timeout (ms)", "4000"
    ):convert(tonumber)
  )

  -- Add subcommands
  addTopologyCmd(parser)
  addControllerCmd(parser)
  addMinionCmd(parser)
  addFwCmd(parser)
  addEventCmd(parser)
  addStatsCmd(parser)
  addTechSupportCmd(parser)
  addVersionCmd(parser)
  addWhoAmICmd(parser)

  return parser
end

local parser = createParser()
if tg_utils.isMain() then
  signal.signal(signal.SIGINT, function(sig) os.exit() end)
  parser:parse()
else
  return parser, TG, TG_Handler
end
