-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- ZMQ Monitor Client class.
-- @classmod ZmqMonitorClient

require "fbzmq.Monitor_ttypes"
local tg_utils = require "tg.utils"
local tg_net_utils = require "tg.net_utils"
local tg_thrift_utils = require "tg.thrift_utils"
local logger = require "tg.logger"
local tablex = require "pl.tablex"
local zmq = require "lzmq"

local ZmqMonitorClient = {}
ZmqMonitorClient.__index = ZmqMonitorClient

--- Connect to a ZMQ socket of the given type and returns (socket, error).
function ZmqMonitorClient:connectToSocket(type, opts)
  if tg_net_utils.isIPv6(self.host) then
    self.host = string.format("[%s]", self.host)
  end
  local url = string.format("tcp://%s:%s", self.host, self.port)
  logger.debug("Connecting to %s...", url)
  local sktOpts = {
    connect = url,
    linger = 250,
    rcvtimeo = self.recvTimeout,
    sndtimeo = self.sendTimeout,
    ipv6 = 1,
    identity = self.zmqId,
  }
  tg_utils.tableMerge(sktOpts, opts)
  return self.zmqCtxt:socket(type, sktOpts)
end

--- Connect to a ZMQ ROUTER socket and returns (socket, error).
function ZmqMonitorClient:connectToRouter()
  return self:connectToSocket(zmq.DEALER, {})
end

--- Initialize a ZMQ Monitor Client on a given host and port.
--
-- - host (string): host to connect
-- - port (int): port to connect
-- - zmqId (string): ZMQ ID of application
-- - recvTimeout(int): Socket receive timeout
-- - sendTimeout(int): Socket send timeout
function ZmqMonitorClient.new(host, port, zmqId, recvTimeout, sendTimeout)
  local self = setmetatable({}, ZmqMonitorClient)
  self.host = host
  self.port = port
  self.zmqId = zmqId
  self.recvTimeout = recvTimeout or 20000
  self.sendTimeout = sendTimeout or 20000
  self.zmqCtxt = zmq.context()

  -- Connect to ZMQ ROUTER port for stats
  local monitorCmdSock, err = self:connectToRouter()
  if err then
    logger.error("Failed to connect to stats ROUTER port: %s", err)
    return nil
  end
  self.monitorCmdSock = monitorCmdSock
  return self
end

--- Print the given stat counter (unless filtered out based on `entityFilter`).
function ZmqMonitorClient:printCounter(counterName, counter, entityFilter)
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

--- Set ZMQ Counters and push to a ZMQ Monitor Client.
--
-- - counters (table): map of counter names to Counters
function ZmqMonitorClient:setCounters(counters)
  -- Send request
  local req = MonitorRequest:new{}
  req.cmd = MonitorCommand.SET_COUNTER_VALUES
  req.counterSetParams = CounterSetParams:new{}
  req.counterSetParams.counters = counters
  self.monitorCmdSock:send(tg_thrift_utils.serialize(req))
end

--- Get counters as a map of counter names to ZMQ Counters.
--
-- - counterNames (array): list of counter names
function ZmqMonitorClient:getCounters(counterNames)
  -- Send request
  local req = MonitorRequest:new{}
  req.cmd = MonitorCommand.GET_COUNTER_VALUES
  req.counterGetParams = CounterGetParams:new{}
  req.counterGetParams.counterNames = counterNames
  self.monitorCmdSock:send(tg_thrift_utils.serialize(req))

  -- Receive and deserialize response
  local serMsg, more = self.monitorCmdSock:recv()
  if serMsg == nil then
    logger.error("Failed to receive stats response")
    return nil
  end
  local response = CounterValuesResponse:new{}
  tg_thrift_utils.deserialize(serMsg, response)
  return tablex.sort(response.counters)
end

--- Dump all ZMQ Counters
function ZmqMonitorClient:dumpCounters(filter)
  -- Send request
  local req = MonitorRequest:new{}
  req.cmd = MonitorCommand.DUMP_ALL_COUNTER_DATA
  self.monitorCmdSock:send(tg_thrift_utils.serialize(req))

  -- Receive and deserialize response
  local serMsg, more = self.monitorCmdSock:recv()
  if serMsg == nil then
    logger.error("Failed to receive stats response")
    return
  end
  local response = CounterValuesResponse:new{}
  tg_thrift_utils.deserialize(serMsg, response)

  -- Print counters
  for counterName, counter in tablex.sort(response.counters) do
    self:printCounter(counterName, counter, filter)
  end
end

-- Create a Gauge from value
function ZmqMonitorClient:createGauge(value)
  local counter = Counter:new{}
  counter.value = value
  counter.valueType = CounterValueType.GAUGE
  -- Get unixtime in milliseconds
  counter.timestamp = os.time()
  return counter
end

-- Create a Counter from value
function ZmqMonitorClient:createCounter(value)
  local counter = Counter:new{}
  counter.value = value
  counter.valueType = CounterValueType.COUNTER
  -- Get unixtime in milliseconds
  counter.timestamp = os.time()
  return counter
end

--- Clean up.
function ZmqMonitorClient:close()
  if self.monitorCmdSock ~= nil then
    self.monitorCmdSock:close()
    self.monitorCmdSock = nil
  end
  if self.zmqCtxt ~= nil then
    self.zmqCtxt:term()
    self.zmqCtxt = nil
  end
end

return ZmqMonitorClient
