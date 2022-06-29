#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- L2 Tunnel monitor script.
-- @script tunnel_monitor
require "fbzmq.Monitor_ttypes"
local argparse = require "argparse"
local tg_utils = require "tg.utils"
local tg_net_utils = require "tg.net_utils"
local tg_platform_utils = require "tg.platform_utils"
local logger = require "tg.logger"
local unistd = require "posix.unistd"
local pretty = require "pl.pretty"
local tablex = require "pl.tablex"
local ZmqMonitorClient = require "tg.ZmqMonitorClient"
require("pl.stringx").import()

local CONFIG_DIR = "/data/cfg"
local CONFIG_FILE = CONFIG_DIR .. "/node_config.json"
local TUNNEL_MONITOR_INTERVAL_S = 5
local DATE_FORMAT = "!%Y-%m-%dT%TZ"

--- Required keys for tunnels
local REQUIRED_KEYS = {"enabled", "dstIp", "localInterface", "tunnelType"}

local TUNNEL_STATS_LABEL = "tunnels"

local consts = {
  -- POP interface in VPP which usually is the interface
  -- for default route to GW.
  VPP_POP_INTF = "loop1",
  -- Internal Bridge ID in POP node which consists of loop1,
  -- tap interfaces and tunnel interface.
  VPP_POP_BRIDGE = 1,
}

--- Tunnel Monitor class
local TunnelMonitor = {}
TunnelMonitor.__index = TunnelMonitor

--- Constructor for TunnelMonitor.
function TunnelMonitor.new(monitorInterval, zmqHost, zmqPort, nodeConfig)
  local self = setmetatable({}, TunnelMonitor)
  self.monitorInterval = monitorInterval
  self.enabledTunnels = {}
  self.loopbackAddress = nil
  self.currentTunnelEndpoints = {}
  -- TODO (retry connection on failure)
  self.monitorClient = ZmqMonitorClient.new(
    zmqHost, zmqPort, "TUNNEL_MONITOR")
  self.fibRoutes = {}
  self.openrRoutes = {}
  local popIface = tg_utils.get(nodeConfig, "popParams", "POP_IFACE") or ""
  local popAddr = tg_utils.get(nodeConfig, "popParams", "POP_ADDR") or ""
  self.isPopNode = popIface ~= "" and popAddr ~= ""
  return self
end

--- Queries VPP for SRv6 policies of a given node.
--
-- Returns a table mapping of destination (decap) SID to source (encap) SID and
-- interface for each tunnel.
function TunnelMonitor:getSrv6Sids()
  local tunnelSids = {}
  local cmd = '/usr/bin/vppctl show sr policies | grep "].-"'
  local output, ret, code = tg_utils.exec(cmd, true)
  if output == nil then
    return tunnelSids
  end
  local lines = output:splitlines()
  -- The VPP command output contains source and destination SIDs
  -- for each tunnel every 2 lines. Output is something like below.
  -- [0].-   BSID: 2620:10d:c089:3388::1101
  --        [0].- < 2620:10d:c089:3389::2101 > weight: 1

  for i = 1, #lines, 2 do
    local srcSid = lines[i]:strip():split("BSID: ")[2]
    -- Get dst SID from next line
    if i < #lines then
      local dstSid = lines[i+1]:lstrip():split("< ")[2]
        :split(" >")[1]
        :split(",")[1]
      tunnelSids[dstSid] = {
        srcEndpoint = srcSid,
        intf = ""
      }
    else
      break
    end
  end
  logger.debug("Current VPP SIDs %s\n", pretty.write(tunnelSids))
  return tunnelSids
end

-- Get current tunnel endpoints from VPP
function TunnelMonitor:updateCurrentTunnelEndpoints()
  -- Update current SRv6 SIDs from VPP
  local tunnelEndpoints = self:getSrv6Sids()
  -- Get current VxLAN tunnels from VPP.
  tg_utils.tableMergeAppend(
    tunnelEndpoints, self:getVxLanTunnels())
  self.currentTunnelEndpoints = tunnelEndpoints
end

--- Get IP of Tunnel endpoints which might be different
-- from node's global IPs (for SRv6).
function TunnelMonitor:getConfigTunnelEndpoints(tunnel, dstIp)
  local tunnelSrc, tunnelDst = nil

  if self.loopbackAddress == nil then
    return
  end

  local dstNodeIp = dstIp or tunnel.dstIp
  if tunnel.tunnelType == "VXLAN" then
    -- "::2" is the VPP loopback IP and the VxLAN tunnel endpoint.
    tunnelSrc = self.loopbackAddress:split("::")[1] .. "::" .. "2"
    tunnelDst = dstNodeIp:split("::")[1] .. "::" .. "2"
  elseif tunnel.tunnelType == "SRV6" then
    -- SRv6 tunnel endpoints (Segment ID) are derived from VLAN ID.
    local srcEncapHextet = 1001 + tonumber(tunnel.tunnelParams.vlanId)
    local dstDecapHextet = 2001 + tonumber(tunnel.tunnelParams.vlanId)
    tunnelDst = dstNodeIp:split("::")[1] .. "::" .. tostring(dstDecapHextet)
    tunnelSrc = self.loopbackAddress:split("::")[1]
      .. "::" .. tostring(srcEncapHextet)
  end
  return tunnelSrc, tunnelDst
end

--- Queries VPP for VxLAN tunnels.
--
-- Returns a table mapping of destination IP to source IP and interface for each
-- tunnel.
function TunnelMonitor:getVxLanTunnels()
  local vxLanTunnels = {}
  local cmd = '/usr/bin/vppctl show vxlan tunnel | grep "instance"'
  local output, ret, code = tg_utils.exec(cmd, true)

  if output == nil then
    return vxLanTunnels
  end
  local lines = output:splitlines()
  -- vppctl show vxlan tunnel
  -- [0] instance 0 src 2620:10d:c089:3388::2 dst 2620:10d:c089:3389::2 ...
  for _, line in ipairs(lines) do
    -- HACK - Sometimes an extra "vpp#" gets added to vppctl output.
    -- TODO Delete this workaround once vppctl output is fixed reliably.
    local splitOutput = line:split()
    local startIdx = tablex.find(splitOutput, "instance")

    local srcEndpoint = splitOutput[startIdx + 3]
    local dstEndpoint = splitOutput[startIdx + 5]
    local vxLanIntfName = "vxlan_tunnel" .. splitOutput[startIdx + 1]
    vxLanTunnels[dstEndpoint] = {
      srcEndpoint = srcEndpoint,
      intf = vxLanIntfName
    }
  end
  logger.debug("Current VxLAN tunnels are %s\n", pretty.write(vxLanTunnels))
  return vxLanTunnels
end

--- Check that the tunnels are properly configured and matches with
-- the current runtime tunnels in VPP.
function TunnelMonitor:checkCurrentTunnelConfig(tunnel)
  -- Dst IP might be empty sometimes
  if tunnel.dstIp == "" then
    return false
  end

  local tunnelSrc, tunnelDst = self:getConfigTunnelEndpoints(tunnel, nil)
  -- Check 1: Compare current loopback address and tunnel destination SID
  if self.currentTunnelEndpoints[tunnelDst] == nil then
    logger.error(
      "%s tunnel: Destination IP %s does not exist in runtime VPP tunnels %s",
      tunnel.tunnelType, tunnelDst, pretty.write(self.currentTunnelEndpoints))
    return false
  end

  -- Check 2: Compare source endpoints between config and VPP
  if tunnelSrc ~= self.currentTunnelEndpoints[tunnelDst].srcEndpoint then
    logger.error(
      "%s tunnel: Source IP %s doesn't match VPP endpoint %s.",
      tunnel.tunnelType, tunnelSrc,
      self.currentTunnelEndpoints[tunnelDst].srcEndpoint)
    return false
  end
  return true
end

--- Validate that tunnel config has required fields for tunnels.
function TunnelMonitor:validateTunnelConfig(tunnel)
  for _, v in ipairs(REQUIRED_KEYS) do
    if tunnel[v] == nil then
      logger.info("Required key %s not found in tunnel config", v)
      return false
    end
  end
  return true
end

--- Validate that backup tunnel config aligns with primary tunnel.
function TunnelMonitor:validateBackupTunnelConfig(name, backupTunnel)
  if self.enabledTunnels[name] == nil then
    return false
  end

  local primaryTunnel = self.enabledTunnels[name]
  if primaryTunnel["localInterface"] ~= backupTunnel["localInterface"] then
    logger.error(
      "Backup tunnel 'localInterface %s' mismatch",
      backupTunnel["localInterface"]
    )
    return false
  end
  if primaryTunnel["tunnelType"] ~= backupTunnel["tunnelType"] then
    logger.error(
      "Backup tunnel 'tunnelType %s' mismatch", backupTunnel["tunnelType"]
    )
    return false
  end

  return true
end

--- Validates tunnel config in node config and add/delete
-- tunnels to be monitored.
function TunnelMonitor:trackTunnelsFromNodeConfig(config)
  local tunnelCfg = config.tunnelConfig
  logger.info(
    "Checking tunnel config\n%s",
    pretty.write(tunnelCfg)
  )

  local backupTunnels = {}
  -- Currently tunnel_monitor needs to be restarted
  -- everytime there is a tunnel config change (config_tunnel.sh).
  for name, tunnel in pairs(tunnelCfg) do
    if self:validateTunnelConfig(tunnel) == true and
      tunnel.enabled == true then
      if tunnel.tunnelParams.primaryTunnelName ~= nil then
        backupTunnels[name] = tunnel
      else
        -- Add enabled tunnels for healthcheck
        tunnel["configured_"] = false
        tunnel["status_"] = "down"
        tunnel["name_"] = name
        self.enabledTunnels[name] = tunnel
      end
    else
      self.enabledTunnels[name] = nil
    end
  end

  -- Add enabled backup tunnels for healthcheck
  for _, tunnel in pairs(backupTunnels) do
    local name = tunnel.tunnelParams.primaryTunnelName
    if self:validateBackupTunnelConfig(name, tunnel) then
      self.enabledTunnels[name]["dstIpBackup"] = tunnel.dstIp
    end
  end
end

--- Parse ping results and return if ping healthcheck was success/failure.
function TunnelMonitor:parsePingResults(output)
  if output == nil then
    return false
  end
  local lines = output:splitlines()
  -- Get ping stats from the ping summary.
  local pingStats = lines[#lines - 1]:split(" ")
  -- 4th column is number of received pings
  return tonumber(pingStats[4]) ~= 0
end

--- Ping destination node with a local ping as a simple healthcheck.
--
-- Returns true if pingCheck succeeds.
function TunnelMonitor:pingCheck(ip)
  local PING_CMD = "/bin/ping6 -c 2 -W 0.5 -i 0.5 %s"
  local cmd = string.format(PING_CMD, ip)
  local output, ret, code = tg_utils.exec(cmd, true)
  return self:parsePingResults(output)
end

--- Update loopback address from node.
function TunnelMonitor:updateLoopbackAddress()
  self.loopbackAddress = tg_platform_utils.getLoopbackAddress()
  if self.loopbackAddress == nil then
    logger.error("No global loopback address assigned. Tunnels are down...")
    return false
  else
    logger.debug("Current loopback IP %s", self.loopbackAddress)
    return true
  end
end

--- Query latest routes from VPP FIB.
function TunnelMonitor:getFibRoutes(tunnel)
  local cmd = "vppctl show ip6 fib"
  local output, ret, code = tg_utils.exec(cmd, true)

  if output == nil then
    logger.error("'%s' failed", cmd)
    self.fibRoutes = {}
  else
    self.fibRoutes = output:splitlines()
  end
end

--- Query latest routes from Open/R.
function TunnelMonitor:getOpenrRoutes(tunnel)
  local cmd = "breeze decision routes"
  local output, ret, code = tg_utils.exec(cmd, true)
  if output == nil then
    logger.error("'%s' failed", cmd)
    self.openrRoutes = {}
  else
    self.openrRoutes = output:splitlines()
  end
end

--- Get latest routes only if previously cleared.
function TunnelMonitor:updateRoutes(tunnel)
  if not next(self.openrRoutes) then
    self:getOpenrRoutes()
  end
  if not next(self.fibRoutes) then
    self:getFibRoutes()
  end
end

--- Clear stored routes.
function TunnelMonitor:clearRoutes(tunnel)
  self.fibRoutes = {}
  self.openrRoutes = {}
end

--- Inspect tunnel route is valid in Open/R and FIB.
--
-- Returns false if a valid route does not exist for the tunnel destination.
function TunnelMonitor:inspectRoutes(tunnel)
  -- Route checking only applies for POP Nodes.
  if not self.isPopNode then
    return true
  end

  -- Update routes if not already present. All routes will be queried once
  -- from VPP and Open/R, only if a tunnel's healthcheck fails or succeeds
  -- the first time.
  self:updateRoutes()

  -- Check if Open/R has a route for the destination node.
  if tablex.find(self.openrRoutes,
    string.format("> %s/128", tunnel.dstIp)) then
    logger.info("Route for destination node %s exists in Open/R", tunnel.dstIp)
  else
    logger.error("No route for destination node %s in Open/R", tunnel.dstIp)
  end

  -- Check VPP FIB if the tunnel destination route points to default route or
  -- interface.
  local tunnelSrc, tunnelDst = self:getConfigTunnelEndpoints(tunnel, nil)

  local idx = tablex.find(self.fibRoutes, tunnelDst .. "/128")
  if idx == nil then
    logger.error("No specific route for tunnel destination IP %s in FIB",
      tunnelDst)
    return true
  else
    -- Assume that there is at least one route within the next 4 lines
    -- of output and if spurious, would be the only route
    for i = idx + 1, idx + 5, 1 do
      if i < #self.fibRoutes then
        -- loop1 is the VPP default route interface on POP. If the tunnel
        -- destination is routed via the default route to loop1, which
        -- egresses out of TG network, this might cause a routing loop.
        if self.fibRoutes[i]:find("ipv6 via") and
          self.fibRoutes[i]:find(consts.VPP_POP_INTF) then
          local nextHop = self.fibRoutes[i]:split("ipv6 via ")[2]
          if nextHop ~= "" then
            nextHop = nextHop:split()[1]
          end
          nextHop = nextHop .. "/64"
          if tg_net_utils.isIPv6(nextHop) and
            not tg_net_utils.isAddrLinkLocal(nextHop) then
            logger.error(
              "Invalid route for tunnel destination %s via default gateway %s",
              tunnelDst, nextHop
            )
            return false
          end
        end
      end
    end
  end
  -- No problems found with Open/R and FIB routes
  return true
end

--- Bridge/unbridge VxLAN tunnel in VPP.
function TunnelMonitor:vxlanBridge(tunnelIntf, enable)
  -- Check if the tunnel interface is part of the bridge
  local cmd =
    string.format("vppctl show bridge %s detail", consts.VPP_POP_BRIDGE)
  local output, ret, code = tg_utils.exec(cmd, true)
  if output == nil then
    logger.error("'%s' failed", cmd)
    return false
  else
    local intfFound = output:find(tunnelIntf)
    if (intfFound and enable) or not (intfFound or enable) then
      logger.debug(
        "VxLAN tunnel interface %s does not need to be toggled", tunnelIntf
      )
      return true
    end
  end

  cmd = "vppctl set interface "
  if enable then
    cmd = cmd .. string.format("l2 bridge %s %s",
      tunnelIntf, consts.VPP_POP_BRIDGE)
  else
    cmd = cmd .. string.format("l3 %s", tunnelIntf)
  end

  output, ret, code = tg_utils.exec(cmd, true)
  if output == nil then
    logger.error("'%s' failed", cmd)
    return false
  end

  logger.debug(
    "Interface %s is %s with bridge-%s in VPP",
    tunnelIntf, enable and "bridged" or "unbridged", consts.VPP_POP_BRIDGE
  )
  return true
end

--- Xconnect/disconnect VxLAN tunnel in VPP.
function TunnelMonitor:vxlanXconnect(fromIntf, toIntf, enable)
  -- Check if two interfaces are xconnected
  local cmd = string.format("vppctl show mode %s", fromIntf)
  local output, ret, code = tg_utils.exec(cmd, true)
  if output == nil then
    logger.error("'%s' failed", cmd)
    return false
  else
    local intfFound = output:find(toIntf)
    if (intfFound and enable) or not (intfFound or enable) then
      logger.debug("Interface %s does not need to be toggled", fromIntf)
      return true
    end
  end

  cmd = "vppctl set interface "
  if enable then
    cmd = cmd .. string.format("l2 xconnect %s %s", fromIntf, toIntf)
  else
    cmd = cmd .. string.format("l3 %s", fromIntf)
  end

  output, ret, code = tg_utils.exec(cmd, true)
  if output == nil then
    logger.error("'%s' failed", cmd)
    return false
  end

  logger.info(
    "Interface %s is %s with %s in VPP",
    fromIntf, enable and "xconnected" or "disconnected", toIntf
  )
  return true
end

--- Toggle tunnel state in VPP.
function TunnelMonitor:toggleVPPTunnelState(tunnel, dstIp, enable)
  -- Only VxLAN tunnels need to be disabled/enabled runtime
  -- at this point.
  if tunnel.tunnelType ~= "VXLAN" then
    return false
  end

  local tunnelSrc, tunnelDst = self:getConfigTunnelEndpoints(tunnel, dstIp)
  local vxLanTunnel = self.currentTunnelEndpoints[tunnelDst]
  if vxLanTunnel == nil then
    return false
  end

  local success
  if self.isPopNode then
    -- Check if the tunnel interface is added into bridge
    success = self:vxlanBridge(vxLanTunnel.intf, enable)
  else
    -- Check if the tunnel interface is xconnected with CPE interface
    success =
      self:vxlanXconnect(vxLanTunnel.intf, tunnel.localInterface, enable)
      and self:vxlanXconnect(tunnel.localInterface, vxLanTunnel.intf, enable)
  end
  return success
end

--- Update tunnel status based on route checks and ping results.
function TunnelMonitor:updateTunnelStatus(tunnel, loopback)
  -- Update tunnel status based on route checks.
  if not loopback or not self:inspectRoutes(tunnel) then
    local dstIp =
      tunnel.status_ == "backup" and tunnel.dstIpBackup or tunnel.dstIp
    if tunnel.status_ ~= "down" then
      if self:toggleVPPTunnelState(tunnel, dstIp, false) then
        tunnel.status_ = "down"
      end
    end

    if tunnel.status_ == "down" then
      logger.info(
        "%s: %s Tunnel to %s is down ...",
        loopback and "Ping/Route failure" or "No Loopback IP",
        tunnel.tunnelType, dstIp
      )
    else
      logger.error(
        "%s: Failed to turn down %s Tunnel to %s ...",
        loopback and "Ping/Route failure" or "No Loopback IP",
        tunnel.tunnelType, dstIp
      )
    end
    return
  end

  -- Update tunnel status based on ping results.
  local status = nil
  local dstIp
  if self:pingCheck(tunnel.dstIp) then
    dstIp = tunnel.dstIp
    if tunnel.status_ ~= "up" then
      status =
        self:toggleVPPTunnelState(tunnel, dstIp, true) and "up" or "fail"
    end
  elseif tunnel.dstIpBackup ~= nil and self:pingCheck(tunnel.dstIpBackup) then
    dstIp = tunnel.dstIpBackup
    if tunnel.status_ ~= "backup" then
      status =
        self:toggleVPPTunnelState(tunnel, dstIp, true) and "backup" or "fail"
    end
  else
    dstIp = tunnel.status_ == "backup" and tunnel.dstIpBackup or tunnel.dstIp
    if tunnel.status_ ~= "down" then
      status =
        self:toggleVPPTunnelState(tunnel, dstIp, false) and "down" or "fail"
    end
  end

  if status == nil then
    logger.info(
      "%s %s tunnel to %s is %s ...", tunnel.tunnelType,
      tunnel.status_ == "backup" and "backup" or "primary", dstIp,
      tunnel.status_ == "down" and "offline" or "online"
    )
  elseif status == "fail" then
    logger.error(
      "Failed to bring %s %s tunnel to %s %s ...",
      tunnel.tunnelType, tunnel.status_ == "backup" and "backup" or "primary",
      dstIp, tunnel.status_ == "down" and "online" or "offline"
    )
  else
    local isBackup =
      status == "down" and tunnel.status_ == "backup" or status == "backup"
    tunnel.status_ = status
    logger.info(
      "Bring %s %s tunnel to %s %s ...",
      tunnel.tunnelType, isBackup and "backup" or "primary", dstIp,
      tunnel.status_ == "down" and "offline" or "online"
    )
  end
end

--- Run a periodic healthcheck for each tunnel endpoint.
function TunnelMonitor:healthCheck()
  logger.info(
    "Running health check on following tunnels\n%s",
    pretty.write(self.enabledTunnels)
  )

  while true do
    local tunnelStatusCounterMap = {}

    -- Get latest linux loopback address
    local loopbackPresent = self:updateLoopbackAddress()
    -- Update current tunnel endpoints from VPP
    self:updateCurrentTunnelEndpoints()
    for _, tunnel in pairs(self.enabledTunnels) do
      if loopbackPresent then
        -- Check config matches current runtime policies
        tunnel.configured_ = self:checkCurrentTunnelConfig(tunnel)
        if tunnel.configured_ then
          -- Change tunnel status based on local ping results and
          -- optional route checks.
          self:updateTunnelStatus(tunnel, loopbackPresent)
        end
      else
        -- If loopback address is not present, tunnels on this
        -- node should be down.
        self:updateTunnelStatus(tunnel, loopbackPresent)
      end

      -- Add tunnel status to counter map
      if self.monitorClient ~= nil then
        -- Set the tunnel status to 1 if "up" or "backup", 0 otherwise
        local status = tunnel.status_ == ("up" or "backup") and 1 or 0
        local counter = self.monitorClient:createGauge(status)
        tunnelStatusCounterMap[TUNNEL_STATS_LABEL .. "."
                              .. tunnel.name_ .. ".status"] = counter
      end
    end

    -- Publish tunnel statuses to stats_agent
    if self.monitorClient ~= nil then
      self.monitorClient:setCounters(tunnelStatusCounterMap)
      -- Publish # tunnels to stats
      local counter = self.monitorClient:createGauge(
        tablex.size(self.enabledTunnels))
      local counterMap = {}
      counterMap[TUNNEL_STATS_LABEL .. ".num-tunnels"] = counter
      self.monitorClient:setCounters(counterMap)
    end

    -- Clear routes after every tick.
    self:clearRoutes()
    unistd.sleep(self.monitorInterval)
  end
end

local function main()
  local parser = argparse(
    "tunnel_monitor", "Run tunnel monitoring."
  )
  parser:option(
    "-i --node_config_file", "Path to the node configuration file", CONFIG_FILE
  )
  parser:option(
    "-t --monitor_interval", "Tunnel monitor interval (seconds)",
    TUNNEL_MONITOR_INTERVAL_S
  )
  parser:option(
    "--zmq_host", "Host address of ZMQ monitor", "localhost"
  )
  parser:option(
    "--zmq_port", "Port of ZMQ monitor", 17009
  )

  logger.enableDateTimeLogging(DATE_FORMAT)
  local args = parser:parse()
  -- Read node config file
  local nodeConfig = tg_utils.readJsonFile(args.node_config_file)
  if nodeConfig == nil then
    logger.error("Cannot read from %s", args.node_config_file)
    os.exit(1)
  end

  if nodeConfig.tunnelConfig == nil then
    logger.error("No tunnel config found")
    os.exit(1)
  end
  logger.info("Starting Tunnel Monitor")

  local tunnelMonitor = TunnelMonitor.new(args.monitor_interval,
    args.zmq_host, args.zmq_port, nodeConfig)
  tunnelMonitor:trackTunnelsFromNodeConfig(nodeConfig)
  tunnelMonitor:healthCheck()
  tunnelMonitor.monitorClient:close()
end

if tg_utils.isMain() then
  main()
else
  return TunnelMonitor
end
