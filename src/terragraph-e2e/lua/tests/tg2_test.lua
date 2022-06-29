#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local tg2 = assert(loadfile("/usr/sbin/tg2"))

require "terragraph_thrift.Controller_ttypes"
require "terragraph_thrift.DriverMessage_ttypes"
require "terragraph_thrift.Event_ttypes"
require "terragraph_thrift.FwOptParams_ttypes"
require "terragraph_thrift.PassThru_ttypes"
local lu = require "luaunit"
local tg_utils = require "tg.utils"
local tablex = require "pl.tablex"
local pretty = require "pl.pretty"
require("pl.stringx").import()

TestMain = {}
TestTopology = setmetatable({}, {__index = TestMain})
TestController = setmetatable({}, {__index = TestMain})
TestEvent = setmetatable({}, {__index = TestMain})
TestMinion = setmetatable({}, {__index = TestMain})
TestFw = setmetatable({}, {__index = TestMain})
TestStats = setmetatable({}, {__index = TestMain})

-- Wrapper around deepcompare() that prints both table values upon
-- mismatch (used to debug assertion failures).
local function deepcompare(a, b)
  local ret = tablex.deepcompare(a, b)
  if not ret then
    pretty.dump({actual = a, expected = b})
  end
  return ret
end

function TestMain:setUp()
  self.parser, self.TG, self.TG_Handler = tg2()

  -- Mock network functions
  -- Store invoked functions and arguments in "invokedFns"
  self.invokedFns = {}
  self.TG.serializeDriverMsg = function(data, radioMac)
    -- Thrift serialization yields non-deterministic order, so don't
    return {data, radioMac}
  end
  self.TG.connectToCtrl = function(_self, recvTimeout, sendTimeout)
    self.invokedFns.connectToCtrl = {}
    return true
  end
  self.TG.connectToMinion = function(_self, recvTimeout, sendTimeout)
    self.invokedFns.connectToMinion = {}
    return true
  end
  self.TG.connectToDriverIf = function(_self, recvTimeout, sendTimeout)
    self.invokedFns.connectToDriverIf = {}
    return true
  end
  self.TG.connectToStatsAgent = function(_self, recvTimeout, sendTimeout)
    self.invokedFns.connectToStatsAgent = {}
    return true
  end
  self.TG.connectToPub = function(_self, host, port, recvTimeout, sendTimeout)
    self.invokedFns.connectToPub = {host = host, port = port}
    -- Create a fake "subscriber" that errors on "recv" so that we break out of
    -- the message loop.
    local socket = {}
    socket.recv = function(__self, flags)
      return nil, "unit test"
    end
    socket.recvx = function(__self, flags)
      return nil, "unit test"
    end
    socket.poll = function(__self, timeout)
      return true
    end
    socket.close = function(__self) end
    return socket, nil
  end
  self.TG.sendToCtrl = function(_self, msgType, msgData, receiverApp, minion)
    self.invokedFns.sendToCtrl = {
      msgType = msgType,
      msgData = msgData,
      receiverApp = receiverApp,
      minion = minion
    }
  end
  self.TG.sendToMinion = function(_self, msgType, msgData, receiverApp)
    self.invokedFns.sendToMinion = {
      msgType = msgType, msgData = msgData, receiverApp = receiverApp
    }
  end
  self.TG.sendToDriverIf = function(_self, msgType, msgData, radioMac)
    self.invokedFns.sendToDriverIf = {
      msgType = msgType, msgData = msgData, radioMac = radioMac
    }
  end
  self.TG.sendToStatsAgent = function(_self, msgType, msgData)
    self.invokedFns.sendToStatsAgent = {
      msgType = msgType, msgData = msgData
    }
  end
  self.TG.recvFromCtrl = function(_self, msgType, msgData, senderApp)
    self.invokedFns.recvFromCtrl = {
      msgType = msgType, msgData = msgData, senderApp = senderApp
    }
    return true
  end
  self.TG.recvFromMinion = function(_self, msgType, msgData, senderApp)
    self.invokedFns.recvFromMinion = {
      msgType = msgType, msgData = msgData, senderApp = senderApp
    }
    return true
  end
  self.TG.doMinionSubLoop =
    function(_self, subscriber, msgHandler, maxPollTime) return true end
  self.TG.doMinionStatusReportLoop =
    function(_self, msgHandler, maxPollTime) return true end
end

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertStrContains(self.parser:get_usage(), "Usage:")
end

--- Test "tg2 topology ls" command.
function TestTopology:test_topologyLs()
  lu.assertTrue(self.parser:pparse({"topology", "ls"}))

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.GET_TOPOLOGY,
      msgData = GetTopology:new{},
      receiverApp = "ctrl-app-TOPOLOGY_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.TOPOLOGY,
      msgData = Topology:new{},
      senderApp = "ctrl-app-TOPOLOGY_APP"
    }
  ))
end

--- Test "tg2 topology site add" command.
function TestTopology:test_topologySiteAdd()
  local name = "test site"
  lu.assertTrue(self.parser:pparse({"topology", "site", "add", name}))

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  local req = AddSite:new{}
  req.site = Site:new{}
  req.site.name = name
  req.site.location = Location:new{}
  req.site.location.latitude = 0
  req.site.location.longitude = 0
  req.site.location.altitude = 0
  req.site.location.accuracy = 40000000
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.ADD_SITE,
      msgData = req,
      receiverApp = "ctrl-app-TOPOLOGY_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.E2E_ACK,
      msgData = E2EAck:new{},
      senderApp = "ctrl-app-TOPOLOGY_APP"
    }
  ))
end

--- Test "tg2 topology site del" command.
function TestTopology:test_topologySiteDel()
  local name = "test site"
  lu.assertTrue(self.parser:pparse({"topology", "site", "del", name}))

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  local req = DelSite:new{}
  req.siteName = name
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.DEL_SITE,
      msgData = req,
      receiverApp = "ctrl-app-TOPOLOGY_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.E2E_ACK,
      msgData = E2EAck:new{},
      senderApp = "ctrl-app-TOPOLOGY_APP"
    }
  ))
end

--- Test "tg2 topology node add" command.
function TestTopology:test_topologyNodeAdd()
  local name = "test node"
  local siteName = "test site"
  lu.assertTrue(
    self.parser:pparse({"topology", "node", "add", name, "-s", siteName})
  )

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  local req = AddNode:new{}
  req.node = Node:new{}
  req.node.name = name
  req.node.node_type = NodeType.DN
  req.node.mac_addr = ""
  req.node.pop_node = false
  req.node.wlan_mac_addrs = {}
  req.node.site_name = siteName
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.ADD_NODE,
      msgData = req,
      receiverApp = "ctrl-app-TOPOLOGY_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.E2E_ACK,
      msgData = E2EAck:new{},
      senderApp = "ctrl-app-TOPOLOGY_APP"
    }
  ))
end

--- Test "tg2 topology node del" command.
function TestTopology:test_topologyNodeDel()
  local name = "test node"
  lu.assertTrue(self.parser:pparse({"topology", "node", "del", name}))

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  local req = DelNode:new{}
  req.nodeName = name
  req.force = false
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.DEL_NODE,
      msgData = req,
      receiverApp = "ctrl-app-TOPOLOGY_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.E2E_ACK,
      msgData = E2EAck:new{},
      senderApp = "ctrl-app-TOPOLOGY_APP"
    }
  ))
end

--- Test "tg2 topology link add" command.
function TestTopology:test_topologyLinkAdd()
  local aNode = "node1"
  local zNode = "node2"
  lu.assertTrue(
    self.parser:pparse({"topology", "link", "add", "-a", aNode, "-z", zNode})
  )

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  local req = AddLink:new{}
  req.link = Link:new{}
  req.link.a_node_name = aNode
  req.link.a_node_mac = ""
  req.link.z_node_name = zNode
  req.link.z_node_mac = ""
  req.link.link_type = LinkType.WIRELESS
  req.link.is_backup_cn_link = false
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.ADD_LINK,
      msgData = req,
      receiverApp = "ctrl-app-TOPOLOGY_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.E2E_ACK,
      msgData = E2EAck:new{},
      senderApp = "ctrl-app-TOPOLOGY_APP"
    }
  ))
end

--- Test "tg2 topology link del" command.
function TestTopology:test_topologyLinkDel()
  local aNode = "node1"
  local zNode = "node2"
  lu.assertTrue(
    self.parser:pparse({"topology", "link", "del", "-a", aNode, "-z", zNode})
  )

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  local req = DelLink:new{}
  req.aNodeName = aNode
  req.zNodeName = zNode
  req.force = false
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.DEL_LINK,
      msgData = req,
      receiverApp = "ctrl-app-TOPOLOGY_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.E2E_ACK,
      msgData = E2EAck:new{},
      senderApp = "ctrl-app-TOPOLOGY_APP"
    }
  ))
end

--- Test "tg2 controller status" command.
function TestController:test_controllerStatus()
  lu.assertTrue(self.parser:pparse({"controller", "status"}))

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.GET_STATUS_DUMP,
      msgData = GetStatusDump:new{},
      receiverApp = "ctrl-app-STATUS_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.STATUS_DUMP,
      msgData = StatusDump:new{},
      senderApp = "ctrl-app-STATUS_APP"
    }
  ))
end

--- Test "tg2 controller topo_scan" command.
function TestController:test_controllerTopoScan()
  local txNode = "11:22:33:44:55:66"
  lu.assertTrue(
    self.parser:pparse({"controller", "topo_scan", txNode, "--json"})
  )

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  local req = StartTopologyScan:new{}
  req.txNode = txNode
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.START_TOPOLOGY_SCAN,
      msgData = req,
      receiverApp = "ctrl-app-TOPOLOGYBUILDER_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.START_TOPOLOGY_SCAN_RESP,
      msgData = StartTopologyScanResp:new{},
      senderApp = "ctrl-app-TOPOLOGYBUILDER_APP"
    }
  ))
end

--- Test "tg2 controller network_topo_scan start" command.
function TestController:test_controllerNetworkTopoScanStart()
  local siteLinks = {{"site a", "site b"}, {"yyyyy", "zzzzz"}}
  lu.assertTrue(self.parser:pparse({
    "controller", "network_topo_scan", "start",
    "-l", siteLinks[1][1], siteLinks[1][2],
    "-l", siteLinks[2][1], siteLinks[2][2]
  }))

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  local req = StartNetworkTopologyScan:new{}
  req.siteLinks = {SiteLink:new{}, SiteLink:new{}}
  req.siteLinks[1].aSite = siteLinks[1][1]
  req.siteLinks[1].zSite = siteLinks[1][2]
  req.siteLinks[2].aSite = siteLinks[2][1]
  req.siteLinks[2].zSite = siteLinks[2][2]
  req.macAddrs = {}
  req.cnSites = {}
  req.yStreetSites = {}
  req.beamAnglePenalty = 0.1
  req.distanceThreshold = 50
  req.snrThreshold = 6.1
  req.scansPerNode = 1
  req.mergeAdjMacs = true
  req.storeResults = false
  req.dryRun = false
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.START_NETWORK_TOPOLOGY_SCAN,
      msgData = req,
      receiverApp = "ctrl-app-TOPOLOGYBUILDER_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.E2E_ACK,
      msgData = E2EAck:new{},
      senderApp = "ctrl-app-TOPOLOGYBUILDER_APP"
    }
  ))
end

--- Test "tg2 controller network_topo_scan stop" command.
function TestController:test_controllerNetworkTopoScanStop()
  lu.assertTrue(self.parser:pparse({"controller", "network_topo_scan", "stop"}))

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.STOP_NETWORK_TOPOLOGY_SCAN,
      msgData = StopNetworkTopologyScan:new{},
      receiverApp = "ctrl-app-TOPOLOGYBUILDER_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.E2E_ACK,
      msgData = E2EAck:new{},
      senderApp = "ctrl-app-TOPOLOGYBUILDER_APP"
    }
  ))
end

--- Test "tg2 controller network_topo_scan status" command.
function TestController:test_controllerNetworkTopoScanStatus()
  lu.assertTrue(
    self.parser:pparse({"controller", "network_topo_scan", "status"})
  )

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.GET_NETWORK_TOPOLOGY_SCAN_STATUS,
      msgData = GetNetworkTopologyScanStatus:new{},
      receiverApp = "ctrl-app-TOPOLOGYBUILDER_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.NETWORK_TOPOLOGY_SCAN_STATUS,
      msgData = NetworkTopologyScanStatus:new{},
      senderApp = "ctrl-app-TOPOLOGYBUILDER_APP"
    }
  ))
end

--- Test "tg2 controller optimize polarity" command.
function TestController:test_controllerOptimizePolarity()
  lu.assertTrue(self.parser:pparse({"controller", "optimize", "polarity"}))

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  local req = TriggerPolarityOptimization:new{}
  req.clearUserPolarityConfig = false
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.TRIGGER_POLARITY_OPTIMIZATION,
      msgData = req,
      receiverApp = "ctrl-app-CONFIG_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.E2E_ACK,
      msgData = E2EAck:new{},
      senderApp = "ctrl-app-CONFIG_APP"
    }
  ))
end

--- Test "tg2 controller optimize golay" command.
function TestController:test_controllerOptimizeGolay()
  lu.assertTrue(self.parser:pparse({"controller", "optimize", "golay"}))

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  local req = TriggerGolayOptimization:new{}
  req.clearUserConfig = false
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.TRIGGER_GOLAY_OPTIMIZATION,
      msgData = req,
      receiverApp = "ctrl-app-CONFIG_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.E2E_ACK,
      msgData = E2EAck:new{},
      senderApp = "ctrl-app-CONFIG_APP"
    }
  ))
end

--- Test "tg2 controller optimize control_superframe" command.
function TestController:test_controllerOptimizeControlSuperframe()
  lu.assertTrue(
    self.parser:pparse({"controller", "optimize", "control_superframe"})
  )

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  local req = TriggerControlSuperframeOptimization:new{}
  req.clearUserConfig = false
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.TRIGGER_CONTROL_SUPERFRAME_OPTIMIZATION,
      msgData = req,
      receiverApp = "ctrl-app-CONFIG_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.E2E_ACK,
      msgData = E2EAck:new{},
      senderApp = "ctrl-app-CONFIG_APP"
    }
  ))
end

--- Test "tg2 controller optimize channel" command.
function TestController:test_controllerOptimizeChannel()
  lu.assertTrue(self.parser:pparse({"controller", "optimize", "channel"}))

  lu.assertNotNil(self.invokedFns.connectToCtrl)
  local req = TriggerChannelOptimization:new{}
  req.clearUserChannelConfig = false
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToCtrl,
    {
      msgType = MessageType.TRIGGER_CHANNEL_OPTIMIZATION,
      msgData = req,
      receiverApp = "ctrl-app-CONFIG_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromCtrl,
    {
      msgType = MessageType.E2E_ACK,
      msgData = E2EAck:new{},
      senderApp = "ctrl-app-CONFIG_APP"
    }
  ))
end

--- Test "tg2 event" command.
function TestEvent:test_event()
  lu.assertTrue(self.parser:pparse({
    "event",
    "-c", "IGNITION",
    "-i", "DRIVER_LINK_STATUS",
    "-l", "INFO",
    "-r", "test link event"
  }))

  lu.assertNotNil(self.invokedFns.connectToStatsAgent)
  lu.assertNotNil(self.invokedFns.sendToStatsAgent)
  lu.assertIsNumber(self.invokedFns.sendToStatsAgent.msgData.timestamp)

  local event = Event:new{}
  event.category = EventCategory.IGNITION
  event.eventId = EventId.DRIVER_LINK_STATUS
  event.level = EventLevel.INFO
  event.reason = "test link event"
  event.source = "CLI"
  event.timestamp = self.invokedFns.sendToStatsAgent.msgData.timestamp

  lu.assertTrue(deepcompare(
    self.invokedFns.sendToStatsAgent,
    {msgType = MessageType.EVENT, msgData = event}
  ))
end

--- Test "tg2 minion sub" command.
function TestMinion:test_minionSub()
  lu.assertTrue(self.parser:pparse({"minion", "sub"}))

  lu.assertNotNil(self.invokedFns.connectToPub)
end

--- Test "tg2 minion assoc" command.
function TestMinion:test_minionAssoc()
  -- Mock response
  self.TG.minionWaitForLinkStatus = function(
    subscriber, responderMac, maxPollTime
  )
    return LinkStatusType.LINK_UP, nil
  end

  local initiatorMac = "11:22:33:44:55:66"
  local responderMac = "77:88:99:aa:bb:cc"
  lu.assertTrue(self.parser:pparse({
    "minion", "assoc", "-i", initiatorMac, "-m", responderMac, "-n", "dn"
  }))

  lu.assertNotNil(self.invokedFns.connectToMinion)

  local req = SetLinkStatus:new{}
  req.linkStatusType = LinkStatusType.LINK_UP
  req.responderMac = responderMac
  req.initiatorMac = initiatorMac
  req.responderNodeType = NodeType.DN
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.SET_LINK_STATUS,
      msgData = req,
      receiverApp = "minion-app-IGNITION_APP"
    }
  ))
end

--- Test "tg2 minion dissoc" command.
function TestMinion:test_minionDissoc()
  -- Mock response
  self.TG.minionWaitForLinkStatus = function(
    _self, subscriber, responderMac, maxPollTime
  )
    return LinkStatusType.LINK_DOWN, DriverLinkStatusType.LINK_SHUTDOWN_RECVD
  end

  local initiatorMac = "11:22:33:44:55:66"
  local responderMac = "77:88:99:aa:bb:cc"
  lu.assertTrue(self.parser:pparse({
    "minion", "dissoc", "-i", initiatorMac, "-m", responderMac
  }))

  lu.assertNotNil(self.invokedFns.connectToMinion)

  local req = SetLinkStatus:new{}
  req.linkStatusType = LinkStatusType.LINK_DOWN
  req.responderMac = responderMac
  req.initiatorMac = initiatorMac
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.SET_LINK_STATUS,
      msgData = req,
      receiverApp = "minion-app-IGNITION_APP"
    }
  ))
end

--- Test "tg2 minion gps_enable" command.
function TestMinion:test_minionGpsEnable()
  lu.assertTrue(self.parser:pparse({"minion", "gps_enable"}))

  lu.assertNotNil(self.invokedFns.connectToMinion)

  local req = NodeParams:new{}
  req.type = NodeParamsType.GPS
  req.enableGps = true
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.SET_NODE_PARAMS,
      msgData = req,
      receiverApp = "minion-app-STATUS_APP"
    }
  ))
end

--- Test "tg2 minion set_params" command.
function TestMinion:test_minionSetParams()
  local radioMac = "11:22:33:44:55:66"
  lu.assertTrue(self.parser:pparse({
    "minion", "set_params", "-c", "2", "-p", "odd", "-m", radioMac
  }))

  lu.assertNotNil(self.invokedFns.connectToMinion)

  local req = NodeParams:new{}
  req.type = NodeParamsType.MANUAL
  req.channel = 2
  req.polarity = PolarityType.ODD
  req.radioMac = radioMac
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.SET_NODE_PARAMS,
      msgData = req,
      receiverApp = "minion-app-STATUS_APP"
    }
  ))
end

--- Test "tg2 minion fw_set_log_config" command.
function TestMinion:test_minionFwSetLogConfig()
  -- Specific modules, all MACs
  lu.assertTrue(self.parser:pparse({
    "minion", "fw_set_log_config", "-l", "info", "-m", "bf", "-m", "tsf"
  }))

  lu.assertNotNil(self.invokedFns.connectToMinion)

  local req = SetLogConfig:new{}
  req.configs = {
    [LogModule.BF] = LogLevel.INFO,
    [LogModule.TSF] = LogLevel.INFO
  }
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.FW_SET_LOG_CONFIG,
      msgData = req,
      receiverApp = "minion-app-CONFIG_APP"
    }
  ))

  self.invokedFns = {}

  -- All modules, specific MAC
  local radioMac = "11:22:33:44:55:66"
  lu.assertTrue(self.parser:pparse({
    "minion", "fw_set_log_config", "-l", "debug", "--radio_mac", radioMac
  }))

  lu.assertNotNil(self.invokedFns.connectToMinion)

  req = SetLogConfig:new{}
  req.configs = {}
  for k, v in pairs(LogModule) do
    req.configs[v] = LogLevel.DEBUG
  end
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.FW_SET_LOG_CONFIG,
      msgData = self.TG.serializeDriverMsg(req, radioMac),
      receiverApp = "minion-app-DRIVER_APP"
    }
  ))
end

--- Test "tg2 minion fw_stats_config" command.
function TestMinion:test_minionFwStatsConfig()
  local radioMac = "11:22:33:44:55:66"
  lu.assertTrue(self.parser:pparse({
    "minion",
    "fw_stats_config",
    "-m", radioMac,
    "-y", "TGF_STATS_GPS",
    "-y", "TGF_STATS_BF",
    "-n", "TGF_STATS_TSF"
  }))

  lu.assertNotNil(self.invokedFns.connectToMinion)

  local req = StatsConfigure:new{}
  req.configs = {
    TGF_STATS_GPS = true,
    TGF_STATS_BF = true,
    TGF_STATS_TSF = false
  }
  req.onDuration = 1
  req.period = 1

  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.FW_STATS_CONFIGURE_REQ,
      msgData = self.TG.serializeDriverMsg(req, radioMac),
      receiverApp = "minion-app-DRIVER_APP"
    }
  ))
end

--- Test "tg2 minion fw_set_params" command.
function TestMinion:test_minionFwSetParams()
  -- No parameters
  lu.assertFalse(self.parser:pparse({"minion", "fw_set_params"}))
  -- Invalid parameter name
  lu.assertError(function()
    self.parser:pparse({"minion", "fw_set_params", "asdf", "1"})
  end)
  -- Invalid parameter value (not a number)
  lu.assertError(function()
    self.parser:pparse({"minion", "fw_set_params", "topoScanEnable", "asdf"})
  end)
  -- Odd number of parameters
  lu.assertError(function()
    self.parser:pparse({
      "minion", "fw_set_params", "topoScanEnable", "1", "forceGpsDisable"
    })
  end)
  -- Link parameter without --responder_mac
  lu.assertError(function()
    self.parser:pparse({"minion", "fw_set_params", "mcs", "5"})
  end)

  local radioMac = "11:22:33:44:55:66"
  local responderMac = "77:88:99:aa:bb:cc"
  lu.assertTrue(self.parser:pparse({
    "minion", "fw_set_params",
    "-m", radioMac,
    "--responder_mac", responderMac,
    "mcs", "12"
  }))
  lu.assertNotNil(self.invokedFns.connectToMinion)

  local msg = PassThruMsg:new{}
  msg.msgType = PtMsgTypes.SB_SET_FW_PARAMS
  msg.dest = PtMsgDest.SB
  msg.setfwParamsReq = SetFwParamsMsg:new{}
  msg.setfwParamsReq.addr = responderMac
  msg.setfwParamsReq.optionalParams = FwOptParams:new{}
  msg.setfwParamsReq.optionalParams.mcs = 12
  local req = FwConfigParams:new{}
  req.passThruMsgs = {msg}

  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.FW_CONFIG_REQ,
      msgData = self.TG.serializeDriverMsg(req, radioMac),
      receiverApp = "minion-app-DRIVER_APP"
    }
  ))
end

--- Test "tg2 minion fw_get_params" command.
function TestMinion:test_minionFwGetParams()
  lu.assertFalse(self.parser:pparse({"minion", "fw_get_params"}))
  lu.assertError(function()
    self.parser:pparse({"minion", "fw_get_params", "-t", "link"})
  end)

  local radioMac = "11:22:33:44:55:66"
  lu.assertTrue(self.parser:pparse({
    "minion", "fw_get_params", "-t", "node", "-m", radioMac
  }))
  lu.assertNotNil(self.invokedFns.connectToMinion)

  local msg = PassThruMsg:new{}
  msg.msgType = PtMsgTypes.SB_GET_FW_PARAMS
  msg.dest = PtMsgDest.SB
  msg.getFwParamsReq = GetFwParamsReq:new{}
  msg.getFwParamsReq.requestedParamsType = FwParamsType.FW_PARAMS_NODE_FW_CFG
  local req = FwConfigParams:new{}
  req.passThruMsgs = {msg}

  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.FW_CONFIG_REQ,
      msgData = self.TG.serializeDriverMsg(req, radioMac),
      receiverApp = "minion-app-DRIVER_APP"
    }
  ))
end

--- Test "tg2 minion links" command.
function TestMinion:test_minionLinks()
  lu.assertTrue(self.parser:pparse({"minion", "links"}))

  lu.assertNotNil(self.invokedFns.connectToMinion)
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.GET_LINK_STATUS_DUMP,
      msgData = GetLinkStatusDump:new{},
      receiverApp = "minion-app-IGNITION_APP"
    }
  ))
  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromMinion,
    {
      msgType = MessageType.LINK_STATUS_DUMP,
      msgData = LinkStatusDump:new{},
      senderApp = "minion-app-IGNITION_APP"
    }
  ))
end

--- Test "tg2 minion status" command.
function TestMinion:test_minionStatus()
  lu.assertTrue(self.parser:pparse({"minion", "status"}))

  lu.assertNotNil(self.invokedFns.connectToMinion)
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.GET_STATUS_REPORT,
      msgData = GetStatusReport:new{},
      receiverApp = "minion-app-STATUS_APP"
    }
  ))

  lu.assertTrue(deepcompare(
    self.invokedFns.recvFromMinion,
    {
      msgType = MessageType.STATUS_REPORT,
      msgData = StatusReport:new{},
      senderApp = "minion-app-STATUS_APP"
    }
  ))
end

--- Test "tg2 minion get_gps_pos" command.
function TestMinion:test_minionGetGpsPos()
  lu.assertTrue(self.parser:pparse({"minion", "get_gps_pos"}))

  lu.assertNotNil(self.invokedFns.connectToMinion)

  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.GPS_GET_POS_REQ,
      msgData = Empty:new{},
      receiverApp = "minion-app-STATUS_APP"
    }
  ))
end

--- Test "tg2 minion set_gps_pos" command.
function TestMinion:test_minionSetGpsPos()
  local latitude = 37.4847215
  local longitude = -122.1472362
  local altitude = 17.92
  local accuracy = 50
  lu.assertTrue(self.parser:pparse({
    "minion",
    "set_gps_pos",
    "--latitude=" .. latitude,
    "--longitude=" .. longitude,
    "--altitude=" .. altitude,
    "--accuracy=" .. accuracy
  }))

  lu.assertNotNil(self.invokedFns.connectToMinion)

  local location = Location:new{}
  location.latitude = latitude
  location.longitude = longitude
  location.altitude = altitude
  location.accuracy = accuracy
  local req = NodeParams:new{}
  req.type = NodeParamsType.GPS
  req.location = location
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.SET_NODE_PARAMS,
      msgData = req,
      receiverApp = "minion-app-STATUS_APP"
    }
  ))
end

--- Test "tg2 minion topo_scan" command.
function TestMinion:test_minionTopoScan()
  local token = 123
  lu.assertTrue(
    self.parser:pparse({"minion", "topo_scan", "--token", tostring(token)})
  )

  lu.assertNotNil(self.invokedFns.connectToMinion)

  local req = ScanReq:new{}
  req.token = token
  req.scanType = ScanType.TOPO
  req.startBwgdIdx = 0
  req.rxNodeMac = "ff:ff:ff:ff:ff:ff"
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.SCAN_REQ,
      msgData = req,
      receiverApp = "minion-app-STATUS_APP"
    }
  ))
end

--- Test "tg2 minion topo_scan_loop" command.
function TestMinion:test_minionTopoScanLoop()
  local radioMac = "11:22:33:44:55:66"
  lu.assertTrue(self.parser:pparse({
    "minion", "topo_scan_loop", "-m", radioMac, "-t", "10"
  }))

  lu.assertNotNil(self.invokedFns.connectToMinion)

  local req = StartContinuousTopoScan:new{}
  req.radioMac = radioMac
  req.durationSec = 10
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.START_CONTINUOUS_TOPO_SCAN,
      msgData = req,
      receiverApp = "minion-app-IGNITION_APP"
    }
  ))
end

--- Test "tg2 minion fw_debug" command.
function TestMinion:test_minionFwDebug()
  local radioMac = "11:22:33:44:55:66"
  local command = "asdf"
  local value = 123
  lu.assertTrue(self.parser:pparse({
    "minion", "fw_debug", "-m", radioMac, "-c", command, "-v", tostring(value)
  }))

  lu.assertNotNil(self.invokedFns.connectToMinion)

  local req = Debug:new{}
  req.cmdStr = command
  req.value = value
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.FW_DEBUG_REQ,
      msgData = self.TG.serializeDriverMsg(req, radioMac),
      receiverApp = "minion-app-DRIVER_APP"
    }
  ))
end

--- Test "tg2 minion bf_resp_scan" command.
function TestMinion:test_minionBfRespScan()
  local radioMac = "11:22:33:44:55:66"
  lu.assertTrue(self.parser:pparse({
    "minion", "bf_resp_scan", "-m", radioMac, "enable"
  }))

  lu.assertNotNil(self.invokedFns.connectToMinion)

  local req = BfRespScanConfig:new{}
  req.cfg = true
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.FW_BF_RESP_SCAN,
      msgData = self.TG.serializeDriverMsg(req, radioMac),
      receiverApp = "minion-app-DRIVER_APP"
    }
  ))
end

--- Test "tg2 minion scan" command.
function TestMinion:test_minionScan()
  local radioMac = "11:22:33:44:55:66"
  local token = 123

  -- scan type: TOPO
  lu.assertTrue(self.parser:pparse({
    "minion", "scan",
    "--radio_mac", radioMac,
    "--token", tostring(token),
    "-t", "topo"
  }))
  lu.assertNotNil(self.invokedFns.connectToMinion)
  local req = ScanReq:new{}
  req.radioMac = radioMac
  req.bfScanInvertPolarity = false
  req.rxNodeMac = "ff:ff:ff:ff:ff:ff"
  req.scanType = ScanType.TOPO
  req.startBwgdIdx = 0
  req.token = token
  req.txPwrIndex = 16
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.SCAN_REQ,
      msgData = req,
      receiverApp = "minion-app-STATUS_APP"
    }
  ))
  self.invokedFns.connectToMinion = nil

  -- scan type: PBF
  local peerMac = "aa:bb:cc:dd:ee:ff"
  local bwgdIdx = 50432100000
  lu.assertTrue(self.parser:pparse({
    "minion", "scan",
    "--radio_mac", radioMac,
    "--token", tostring(token),
    "-t", "pbf",
    "-m", "fine",
    "--tx",
    "-p", peerMac,
    "--bwgd_idx", tostring(bwgdIdx),
    "--no-apply"
  }))
  lu.assertNotNil(self.invokedFns.connectToMinion)
  req = ScanReq:new{}
  req.radioMac = radioMac
  req.apply = false
  req.bfScanInvertPolarity = false
  req.bwgdLen = 64
  req.rxNodeMac = peerMac
  req.scanMode = ScanMode.FINE
  req.scanType = ScanType.PBF
  req.startBwgdIdx = bwgdIdx
  req.subType = ScanSubType.NO_CAL
  req.token = token
  req.txPwrIndex = 16
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.SCAN_REQ,
      msgData = req,
      receiverApp = "minion-app-STATUS_APP"
    }
  ))
end

--- Test "tg2 minion set_node_config" command.
function TestMinion:test_minionSetNodeConfig()
  -- Write temporary config file
  local configStr = '{"asdf": "jkl"}'
  local tmpFile = os.tmpname()
  tg_utils.writeFile(tmpFile, configStr)

  -- Invoke command and delete temporary file
  local success, ret = pcall(
    self.parser.pparse,
    self.parser,
    {"minion", "set_node_config", "--node_config", tmpFile}
  )
  os.remove(tmpFile)
  lu.assertTrue(success)
  lu.assertTrue(ret)

  lu.assertNotNil(self.invokedFns.connectToMinion)

  local req = SetMinionConfigReq:new{}
  req.config = configStr
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToMinion,
    {
      msgType = MessageType.SET_MINION_CONFIG_REQ,
      msgData = req,
      receiverApp = "minion-app-CONFIG_APP"
    }
  ))
end

-- Mock successful FwAck in recvFromDriverIf().
function TestFw:_recvFwAckFromDriverIf(
  msgType, msgData, radioMac, filterLinkMacAddr
)
  msgData.success = true
  return true
end

--- Test "tg2 fw node_init" command.
function TestFw:test_fwNodeInit()
  -- Mock response
  self.TG.recvFromDriverIf = function(
    _self, msgType, msgData, radioMac, filterLinkMacAddr
  )
    msgData.success = true
    msgData.macAddr = "00:00:de:ad:be:ef"
    msgData.vendor = "pork"
    return true
  end

  -- Write temporary config file
  local configStr = '{"radioParamsBase": {"fwParams": {}}}'
  local tmpFile = os.tmpname()
  tg_utils.writeFile(tmpFile, configStr)

  -- Invoke command and delete temporary file
  local success, ret = pcall(
    self.parser.pparse, self.parser, {"fw", "node_init", "-f", tmpFile}
  )
  os.remove(tmpFile)
  lu.assertTrue(success)
  lu.assertTrue(ret)

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local req = DriverNodeInitReq:new{}
  req.optParams = FwOptParams:new{}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.NODE_INIT, msgData = req}
  ))
end

--- Test "tg2 fw channel_config" command.
function TestFw:test_fwChannelConfig()
  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  local channel = 2
  lu.assertTrue(
    self.parser:pparse({"fw", "channel_config", "-c", tostring(channel)})
  )

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local channelCfgMsg = PassThruMsg:new{}
  channelCfgMsg.msgType = PtMsgTypes.SB_CHANNEL_CONFIG
  channelCfgMsg.dest = PtMsgDest.SB
  channelCfgMsg.channelCfg = ChannelConfig:new{}
  channelCfgMsg.channelCfg.channel = channel
  local req = FwSetNodeParams:new{}
  req.passThruMsgs = {channelCfgMsg}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_SET_NODE_PARAMS, msgData = req}
  ))
end

--- Test "tg2 fw gps_enable" command.
function TestFw:test_fwGpsEnable()
  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  lu.assertTrue(self.parser:pparse({"fw", "gps_enable"}))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.GPS_ENABLE_REQ, msgData = Empty:new{}}
  ))
end

--- Test "tg2 fw get_gps_pos" command.
function TestFw:test_fwGetGpsPos()
  -- Mock response
  self.TG.recvFromDriverIf = function(
    _self, msgType, msgData, radioMac, filterLinkMacAddr
  )
    msgData.latitude = 12.34
    msgData.longitude = 56.78
    msgData.altitude = 9.10
    msgData.accuracy = 12345.6789
    return true
  end

  lu.assertTrue(self.parser:pparse({"fw", "get_gps_pos"}))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.GPS_GET_POS_REQ, msgData = Empty:new{}}
  ))
end

--- Test "tg2 fw set_gps_pos" command.
function TestFw:test_fwSetGpsPos()
  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  lu.assertTrue(self.parser:pparse({
    "fw",
    "set_gps_pos",
    "--lat=12.34",
    "--lon=56.78",
    "--alt=9.10",
    "--acc=12345.6789"
  }))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local req = FwSetNodeParams:new{}
  req.location = Location:new{}
  req.location.latitude = 12.34
  req.location.longitude = 56.78
  req.location.altitude = 9.10
  req.location.accuracy = 12345.6789
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_SET_NODE_PARAMS, msgData = req}
  ))
end

--- Test "tg2 fw gps_send_time" command.
function TestFw:test_fwGpsSendTime()
  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  lu.assertTrue(self.parser:pparse({"fw", "gps_send_time", "123456789"}))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local req = GpsTimeValue:new{}
  req.unixTimeSecs = 123456789
  req.unixTimeNsecs = 0
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.GPS_SEND_TIME, msgData = req}
  ))
end

--- Test "tg2 fw assoc" command.
function TestFw:test_fwAssoc()
  local responderMac = "aa:bb:cc:dd:ee:ff"

  -- Mock response
  self.TG.recvFromDriverIf = function(
    _self, msgType, msgData, radioMac, filterLinkMacAddr
  )
    if msgType == MessageType.DR_DEV_ALLOC_RES then
      msgData.success = true
      msgData.ifname = "terra0"
    elseif msgType == MessageType.DR_LINK_STATUS then
      msgData.valid = true
      msgData.drLinkStatusType = DriverLinkStatusType.LINK_UP
      return true
    else
      return false
    end
    return true
  end

  -- Write temporary config file
  local configStr = '{"linkParamsBase": {"fwParams": {}}}'
  local tmpFile = os.tmpname()
  tg_utils.writeFile(tmpFile, configStr)

  -- Invoke command and delete temporary file
  local success, ret = pcall(
    self.parser.pparse,
    self.parser,
    {"fw", "assoc", "-m", responderMac, "-f", tmpFile}
  )
  os.remove(tmpFile)
  lu.assertTrue(success)
  lu.assertTrue(ret)

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local req = DriverSetLinkStatus:new{}
  req.isAssoc = true
  req.responderMac = responderMac
  req.optParams = FwOptParams:new{}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.DR_SET_LINK_STATUS, msgData = req}
  ))
end

--- Test "tg2 fw dissoc" command.
function TestFw:test_fwDissoc()
  local responderMac = "aa:bb:cc:dd:ee:ff"

  -- Mock response
  self.TG.recvFromDriverIf = function(
    _self, msgType, msgData, radioMac, filterLinkMacAddr
  )
    msgData.valid = true
    msgData.drLinkStatusType = DriverLinkStatusType.LINK_DOWN
    return true
  end

  lu.assertTrue(self.parser:pparse({"fw", "dissoc", "-m", responderMac}))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local req = DriverSetLinkStatus:new{}
  req.isAssoc = false
  req.responderMac = responderMac
  req.optParams = FwOptParams:new{}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.DR_SET_LINK_STATUS, msgData = req}
  ))
end

--- Test "tg2 fw fw_set_log_config" command.
function TestFw:test_fwFwSetLogConfig()
  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  -- Specific modules, all MACs
  lu.assertTrue(self.parser:pparse({
    "fw", "fw_set_log_config", "-l", "info", "-m", "bf", "-m", "tsf"
  }))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local req = SetLogConfig:new{}
  req.configs = {
    [LogModule.BF] = LogLevel.INFO,
    [LogModule.TSF] = LogLevel.INFO
  }
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_SET_LOG_CONFIG, msgData = req}
  ))

  self.invokedFns = {}

  -- All modules, specific MAC
  local radioMac = "11:22:33:44:55:66"
  lu.assertTrue(self.parser:pparse({
    "fw", "fw_set_log_config", "-l", "debug", "--radio_mac", radioMac
  }))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  req = SetLogConfig:new{}
  req.configs = {}
  for k, v in pairs(LogModule) do
    req.configs[v] = LogLevel.DEBUG
  end
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {
      msgType = MessageType.FW_SET_LOG_CONFIG,
      msgData = req,
      radioMac = radioMac
    }
  ))
end

--- Test "tg2 fw fw_stats_config" command.
function TestFw:test_fwFwStatsConfig()
  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  -- Write temporary config file
  local configStr =
    '{"configs": {"TGF_STATS_BF": true}, "onDuration": 1, "period": 1}'
  local tmpFile = os.tmpname()
  tg_utils.writeFile(tmpFile, configStr)

  -- Invoke command and delete temporary file
  local success, ret = pcall(
    self.parser.pparse, self.parser, {"fw", "fw_stats_config", "-f", tmpFile}
  )
  os.remove(tmpFile)
  lu.assertTrue(success)
  lu.assertTrue(ret)

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local req = StatsConfigure:new{}
  req.configs = {TGF_STATS_BF = true}
  req.onDuration = 1
  req.period = 1
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_STATS_CONFIGURE_REQ, msgData = req}
  ))
end

--- Test "tg2 fw debug" command.
function TestFw:test_fwDebug()
  local command = "asdf"
  local value = 123

  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  lu.assertTrue(self.parser:pparse({
    "fw", "debug", "-c", command, "-v", tostring(value)
  }))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local req = Debug:new{}
  req.cmdStr = command
  req.value = value
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_DEBUG_REQ, msgData = req}
  ))
end

--- Test "tg2 fw polarity_config" command.
function TestFw:test_fwPolarityConfig()
  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  lu.assertTrue(self.parser:pparse({"fw", "polarity_config", "-p", "odd"}))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local polarityCfgMsg = PassThruMsg:new{}
  polarityCfgMsg.msgType = PtMsgTypes.SB_POLARITY
  polarityCfgMsg.dest = PtMsgDest.SB
  polarityCfgMsg.polarityCfg = PolarityConfig:new{}
  polarityCfgMsg.polarityCfg.polarity = PolarityType.ODD
  local req = FwSetNodeParams:new{}
  req.passThruMsgs = {polarityCfgMsg}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_SET_NODE_PARAMS, msgData = req}
  ))
end

--- Test "tg2 fw golay_config" command.
function TestFw:test_fwGolayConfig()
  local txGolay = 2
  local rxGolay = 2

  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  lu.assertTrue(self.parser:pparse({
    "fw", "golay_config", "-t", tostring(txGolay), "-r", tostring(rxGolay)
  }))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local golayCfgMsg = PassThruMsg:new{}
  golayCfgMsg.msgType = PtMsgTypes.SB_GOLAY_INDX
  golayCfgMsg.dest = PtMsgDest.SB
  golayCfgMsg.golayCfg = GolayConfig:new{}
  golayCfgMsg.golayCfg.txGolayIdx = txGolay
  golayCfgMsg.golayCfg.rxGolayIdx = rxGolay
  local req = FwSetNodeParams:new{}
  req.passThruMsgs = {golayCfgMsg}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_SET_NODE_PARAMS, msgData = req}
  ))
end

--- Test "tg2 fw bf_slot_exclusion_req" command.
function TestFw:test_fwBfSlotExclusionReq()
  local bwgdIdx = 50432100000

  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  lu.assertTrue(self.parser:pparse({
    "fw", "bf_slot_exclusion_req", "-b", tostring(bwgdIdx)
  }))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local bfSlotExclusionReqMsg = PassThruMsg:new{}
  bfSlotExclusionReqMsg.msgType = PtMsgTypes.SB_BF_SLOT_EXCLUSION_REQ
  bfSlotExclusionReqMsg.dest = PtMsgDest.SB
  bfSlotExclusionReqMsg.bfSlotExclusionReq = BfSlotExclusionReq:new{}
  bfSlotExclusionReqMsg.bfSlotExclusionReq.startBwgdIdx = bwgdIdx
  local req = FwSetNodeParams:new{}
  req.passThruMsgs = {bfSlotExclusionReqMsg}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_SET_NODE_PARAMS, msgData = req}
  ))
end

--- Test "tg2 fw bf_resp_scan_config" command.
function TestFw:test_fwBfRespScanConfig()
  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  lu.assertTrue(self.parser:pparse({
    "fw", "bf_resp_scan_config", "-c", "1"
  }))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local req = BfRespScanConfig:new{}
  req.cfg = true
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_BF_RESP_SCAN, msgData = req}
  ))
end

--- Test "tg2 fw phyla_config" command.
function TestFw:test_fwPhyLAConfig()
  local responderMac = "aa:bb:cc:dd:ee:ff"

  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  -- Write temporary config file
  local configStr = '{"laNodeParams": {}, "laParams": {}}'
  local tmpFile = os.tmpname()
  tg_utils.writeFile(tmpFile, configStr)

  -- Invoke command and delete temporary file
  local success, ret = pcall(
    self.parser.pparse,
    self.parser,
    {"fw", "phyla_config", "-m", responderMac, "-f", tmpFile}
  )
  os.remove(tmpFile)
  lu.assertTrue(success)
  lu.assertTrue(ret)

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local phyLAConfigMsg = PassThruMsg:new{}
  phyLAConfigMsg.msgType = PtMsgTypes.SB_PHY_LA_CONFIG
  phyLAConfigMsg.dest = PtMsgDest.SB
  phyLAConfigMsg.phyLAConfig = PhyLAConfig:new{}
  phyLAConfigMsg.phyLAConfig.addr = responderMac
  phyLAConfigMsg.phyLAConfig.laParams = PhyLALinkParams:new{}
  phyLAConfigMsg.phyLAConfig.laNodeParams = PhyLANodeParams:new{}
  local req = FwConfigParams:new{}
  req.passThruMsgs = {phyLAConfigMsg}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_CONFIG_REQ, msgData = req}
  ))
end

--- Test "tg2 fw phyagc_config" command.
function TestFw:test_fwPhyAgcConfig()
  local responderMac = "aa:bb:cc:dd:ee:ff"

  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  -- Write temporary config file
  local configStr = '{"agcNodeParams": {}, "agcLinkParams": {}}'
  local tmpFile = os.tmpname()
  tg_utils.writeFile(tmpFile, configStr)

  -- Invoke command and delete temporary file
  local success, ret = pcall(
    self.parser.pparse,
    self.parser,
    {"fw", "phyagc_config", "-m", responderMac, "-f", tmpFile}
  )
  os.remove(tmpFile)
  lu.assertTrue(success)
  lu.assertTrue(ret)

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local phyAgcConfigMsg = PassThruMsg:new{}
  phyAgcConfigMsg.msgType = PtMsgTypes.SB_PHY_AGC_CONFIG
  phyAgcConfigMsg.dest = PtMsgDest.SB
  phyAgcConfigMsg.phyAgcConfig = PhyAgcConfig:new{}
  phyAgcConfigMsg.phyAgcConfig.addr = responderMac
  phyAgcConfigMsg.phyAgcConfig.agcNodeParams = PhyAgcNodeParams:new{}
  phyAgcConfigMsg.phyAgcConfig.agcLinkParams = PhyAgcLinkParams:new{}
  local req = FwConfigParams:new{}
  req.passThruMsgs = {phyAgcConfigMsg}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_CONFIG_REQ, msgData = req}
  ))
end

--- Test "tg2 fw phytpc_config" command.
function TestFw:test_fwPhyTpcConfig()
  local responderMac = "aa:bb:cc:dd:ee:ff"

  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  -- Write temporary config file
  local configStr = '{"tpcNodeParams": {}, "tpcLinkParams": {}}'
  local tmpFile = os.tmpname()
  tg_utils.writeFile(tmpFile, configStr)

  -- Invoke command and delete temporary file
  local success, ret = pcall(
    self.parser.pparse,
    self.parser,
    {"fw", "phytpc_config", "-m", responderMac, "-f", tmpFile}
  )
  os.remove(tmpFile)
  lu.assertTrue(success)
  lu.assertTrue(ret)

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local req = PhyTpcConfig:new{}
  req.addr = responderMac
  req.tpcNodeParams = PhyTpcNodeParams:new{}
  req.tpcLinkParams = PhyTpcLinkParams:new{}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.PHY_TPC_CONFIG_REQ, msgData = req}
  ))
end

--- Test "tg2 fw phy_tpc_adj_tbl_config" command.
function TestFw:test_fwPhyTpcAdjTblConfig()
  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  -- Write temporary config file
  local configStr = '{}'
  local tmpFile = os.tmpname()
  tg_utils.writeFile(tmpFile, configStr)

  -- Invoke command and delete temporary file
  local success, ret = pcall(
    self.parser.pparse,
    self.parser,
    {"fw", "phy_tpc_adj_tbl_config", "-f", tmpFile}
  )
  os.remove(tmpFile)
  lu.assertTrue(success)
  lu.assertTrue(ret)

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local req = PhyTpcAdjTblCfg:new{}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.PHY_TPC_ADJ_TBL_CFG_REQ, msgData = req}
  ))
end

--- Test "tg2 fw phy_ant_wgt_code_book_config" command.
function TestFw:test_fwPhyAntWgtCodeBookConfig()
  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  -- Write temporary config file
  local configStr = '{}'
  local tmpFile = os.tmpname()
  tg_utils.writeFile(tmpFile, configStr)

  -- Invoke command and delete temporary file
  local success, ret = pcall(
    self.parser.pparse,
    self.parser,
    {"fw", "phy_ant_wgt_code_book_config", "-f", tmpFile}
  )
  os.remove(tmpFile)
  lu.assertTrue(success)
  lu.assertTrue(ret)

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local req = PhyAntWgtCodeBookConfig:new{}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_SET_CODEBOOK, msgData = req}
  ))
end

--- Test "tg2 fw phy_golay_sequence_config" command.
function TestFw:test_fwPhyGolaySequenceConfig()
  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  -- Write temporary config file
  local configStr = '{}'
  local tmpFile = os.tmpname()
  tg_utils.writeFile(tmpFile, configStr)

  -- Invoke command and delete temporary file
  local success, ret = pcall(
    self.parser.pparse,
    self.parser,
    {"fw", "phy_golay_sequence_config", "-f", tmpFile}
  )
  os.remove(tmpFile)
  lu.assertTrue(success)
  lu.assertTrue(ret)

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local req = PhyGolaySequenceConfigReq:new{}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.PHY_GOLAY_SEQUENCE_CONFIG_REQ, msgData = req}
  ))
end

--- Test "tg2 fw airtime_alloc" command.
function TestFw:test_fwAirtimeAlloc()
  -- Mock response
  self.TG.recvFromDriverIf = self._recvFwAckFromDriverIf

  -- Write temporary config file
  local configStr = '{}'
  local tmpFile = os.tmpname()
  tg_utils.writeFile(tmpFile, configStr)

  -- Invoke command and delete temporary file
  local success, ret = pcall(
    self.parser.pparse,
    self.parser,
    {"fw", "airtime_alloc", "-f", tmpFile}
  )
  os.remove(tmpFile)
  lu.assertTrue(success)
  lu.assertTrue(ret)

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local airtimeCfgMsg = PassThruMsg:new{}
  airtimeCfgMsg.msgType = PtMsgTypes.SB_AIRTIMEALLOC
  airtimeCfgMsg.dest = PtMsgDest.SB
  airtimeCfgMsg.airtimeAllocMap = NodeAirtime:new{}
  local req = FwSetNodeParams:new{}
  req.passThruMsgs = {airtimeCfgMsg}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_SET_NODE_PARAMS, msgData = req}
  ))
end

--- Test "tg2 fw fw_get_params" command.
function TestFw:test_fwFwGetParams()
  -- Mock response
  -- We send two responses: FW_CONFIG_RESP followed by FW_ACK
  local respIdx = 1
  self.TG.recvFromDriverIf = function(
    _self, msgType, msgData, radioMac, filterLinkMacAddr
  )
    if respIdx == 1 then
      msgData.fwParamsType = FwParamsType.FW_PARAMS_NODE_FW_CFG
      msgData.bwgdIdx = 50432100000
      msgData.optParams = FwOptParams:new{}
    else
      msgData.success = true
    end
    respIdx = respIdx + 1
    return true
  end

  lu.assertTrue(self.parser:pparse({
    "fw", "fw_get_params", "node"
  }))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local msg = PassThruMsg:new{}
  msg.msgType = PtMsgTypes.SB_GET_FW_PARAMS
  msg.dest = PtMsgDest.SB
  msg.getFwParamsReq = GetFwParamsReq:new{}
  msg.getFwParamsReq.requestedParamsType = FwParamsType.FW_PARAMS_NODE_FW_CFG
  local req = FwConfigParams:new{}
  req.passThruMsgs = {msg}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_CONFIG_REQ, msgData = req}
  ))
end

--- Test "tg2 fw fw_set_params" command.
function TestFw:test_fwFwSetParams()
  local bwgdIdx = 50432100000

  -- Mock response
  -- We send two responses: FW_CONFIG_RESP followed by FW_ACK
  local respIdx = 1
  self.TG.recvFromDriverIf = function(
    _self, msgType, msgData, radioMac, filterLinkMacAddr
  )
    if respIdx == 1 then
      msgData.status = true
    else
      msgData.success = true
    end
    respIdx = respIdx + 1
    return true
  end

  lu.assertTrue(self.parser:pparse({
    "fw", "fw_set_params", "-b", tostring(bwgdIdx), "forceGpsDisable", "0"
  }))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local fwOptParams = FwOptParams:new{}
  fwOptParams.forceGpsDisable = 0
  local msg = PassThruMsg:new{}
  msg.msgType = PtMsgTypes.SB_SET_FW_PARAMS
  msg.dest = PtMsgDest.SB
  msg.setfwParamsReq = SetFwParamsMsg:new{}
  msg.setfwParamsReq.addr = "00:00:00:00:00:00"
  msg.setfwParamsReq.optionalParams = fwOptParams
  msg.setfwParamsReq.bwgdIdx = bwgdIdx
  local req = FwConfigParams:new{}
  req.passThruMsgs = {msg}
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.FW_CONFIG_REQ, msgData = req}
  ))
end

--- Test "tg2 fw scan" command.
function TestFw:test_fwScan()
  local token = 123

  -- Mock response
  self.TG.recvFromDriverIf = function(
    _self, msgType, msgData, radioMac, filterLinkMacAddr
  )
    msgData.token = token
    msgData.status = ScanFwStatus.COMPLETE
    return true
  end

  lu.assertTrue(self.parser:pparse({
    "fw", "scan", "--token", tostring(token), "-t", "topo"
  }))

  lu.assertNotNil(self.invokedFns.connectToDriverIf)

  local req = ScanReq:new{}
  req.bfScanInvertPolarity = false
  req.rxNodeMac = "ff:ff:ff:ff:ff:ff"
  req.scanType = ScanType.TOPO
  req.startBwgdIdx = 0
  req.token = token
  req.txPwrIndex = 16
  lu.assertTrue(deepcompare(
    self.invokedFns.sendToDriverIf,
    {msgType = MessageType.SCAN_REQ, msgData = req}
  ))
end

--- Test "tg2 stats" command.
function TestStats:test_stats()
  lu.assertTrue(self.parser:pparse({"stats", "driver-if"}))
  lu.assertNotNil(self.invokedFns.connectToPub)
  self.invokedFns.connectToPub = nil
  lu.assertTrue(self.parser:pparse({"stats", "12345"}))
  lu.assertNotNil(self.invokedFns.connectToPub)
end

os.exit(lu.LuaUnit.run("-v"))
