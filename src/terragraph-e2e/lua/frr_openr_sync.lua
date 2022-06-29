#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Route sync module for syncing BGP routes from FRR-bgpd to FIB and Open/R.
-- @script frr_openr_sync

require "openr.OpenrCtrl_OpenrCtrl"
require "openr.Platform_FibService"
require "openr.Platform_ttypes"
require "openr.Network_ttypes"
require "openr.Types_ttypes"
local tg_utils = require "tg.utils"
local tg_net_utils = require "tg.net_utils"
local tg_thrift_utils = require "tg.thrift_utils"
local logger = require "tg.logger"
local argparse = require "argparse"
local sys_socket = require "posix.sys.socket"
local unistd = require "posix.unistd"
local tg_rtnl = require "tg.rtnl"
local tg_zebra_utils = require "tg.zebra_utils"
local pretty = require "pl.pretty"
require("pl.stringx").import()

local consts = {
  -- VPP mode (TODO: support kernel mode too)
  POP_IFACE = "tap1",

  -- FIB constants
  FIB_HOST = "localhost",
  FIB_PORT = 60100,
  FIB_TIMEOUT = 10000,
  FIB_CLIENTID = 64,

  -- Zebra/FPM constants
  ZEBRA_PROTOCOL_ID = 11,
  FPM_DEFAULT_IP = "127.0.0.1",
  FPM_DEFAULT_PORT = 2620,
  FPM_MAX_MSG_LEN = 4096,
}

--- Route Sync class
local FRRRouteSync = {}
FRRRouteSync.__index = FRRRouteSync

--- Constructor for FRRRouteSync.
function FRRRouteSync.new()
  local self = setmetatable({}, FRRRouteSync)
  self.fpm_socket = nil
  self.current_routes = {}
  self.routes_openr_advertised = false
  return self
end

--- Make sure current default routes are in a clean state.
function FRRRouteSync:performDefaultRouteCheck()
  local SHOW_DEFAULT_ROUTES_CMD = "/sbin/ip -6 route show default"
  local DELETE_DEFAULT_ROUTE_CMD = "/sbin/ip -6 route del ::/0"
  local DISABLE_ACCEPT_RA_CMD = "echo 0 > /proc/sys/net/ipv6/conf/%s/accept_ra"

  -- Get current default routes
  local output, ret, code = tg_utils.exec(SHOW_DEFAULT_ROUTES_CMD)
  if output == nil then
    logger.error("Error running command: %s", SHOW_DEFAULT_ROUTES_CMD)
    return
  end

  -- Check if a Zebra default route is already present
  local lines = output:splitlines()
  for _, line in ipairs(lines) do
    if output:find("default.*zebra.*") then
      logger.info("Found a Zebra default route: %s", line)
      return
    end
  end

  -- Disable RA on tap1 and vnet0 to enable BGP routes to be
  -- pushed by Zebra.
  local cmd = string.format(DISABLE_ACCEPT_RA_CMD, consts.POP_IFACE)
  output, ret, code = tg_utils.exec(cmd)
  if output == nil then
    logger.error("Error running command: %s", cmd)
  end
  cmd = string.format(DISABLE_ACCEPT_RA_CMD, "vnet0")
  output, ret, code = tg_utils.exec(cmd)
  if output == nil then
    logger.error("Error running command: %s", cmd)
  end

  -- Delete all non-Zebra default routes in kernel RIB (even for VPP).
  -- If the kernel RIB has >= 2 default routes, then FRR/Zebra does
  -- not send default routes learned from BGP as it syncs with kernel.
  local count = 0
  for i = 1, #lines do
    output, ret, code = tg_utils.exec(DELETE_DEFAULT_ROUTE_CMD)
    if output == nil then
      logger.error("Error running command: %s", DELETE_DEFAULT_ROUTE_CMD)
    else
      count = count + 1
    end
  end
  if count > 0 then
    logger.info("Deleted %d default route(s)", count)
  end
end

--- Connect to the VPP Fib agent socket.
--
-- Returns the client upon success, or nil upon failure.
function FRRRouteSync:connectToFibAgent()
  local client, error = tg_thrift_utils.createClient(
    FibServiceClient, consts.FIB_HOST, consts.FIB_PORT, consts.FIB_TIMEOUT
  )
  if client == nil then
    logger.error("Failed to connect to Platform Fib agent port: %s", error)
    return nil
  else
    logger.info(
      "FIB: Connected to Platform Fib agent port %d.", consts.FIB_PORT
    )
    return client
  end
end

--- Add routes to FIB, and return true upon success.
function FRRRouteSync:addRoutesToFIB(client, routes)
  local success, ret = pcall(
    client.addUnicastRoutes, client, consts.FIB_CLIENTID, routes
  )
  if success then
    logger.info("FibClient: Added %s route(s).", #routes)
    return true
  else
    logger.error(
      "Failed to add routes.\nException: %s", tg_thrift_utils.exceptionStr(ret)
    )
    return false
  end
end

--- Delete the given routes from FIB, and return true upon success.
function FRRRouteSync:delRoutesFromFIB(client, routes)
  local success, ret = pcall(
    client.deleteUnicastRoutes, client, consts.FIB_CLIENTID, routes
  )
  if success then
    logger.info("FibClient: Deleted %s route(s).", #routes)
    return true
  else
    logger.error(
      "Failed to delete routes.\nException: %s",
      tg_thrift_utils.exceptionStr(ret)
    )
    return false
  end
end

--- Create route in Thrift format for FIB consumption.
function FRRRouteSync:createUnicastRoute(route)
  local nexthop = NextHopThrift:new{}
  nexthop.address = BinaryAddress:new{}
  nexthop.address.addr = tg_net_utils.stringToBinaryAddress(route.nextHop)
  nexthop.address.ifName = route.ifName

  local fibRoute = UnicastRoute:new{}
  fibRoute.dest = IpPrefix:new{}
  fibRoute.dest.prefixAddress = BinaryAddress:new{}
  fibRoute.dest.prefixAddress.addr =
    tg_net_utils.stringToBinaryAddress(route.dest)
  fibRoute.dest.prefixLength = route.prefixLen
  fibRoute.nextHops = {nexthop}

  return fibRoute
end

--- Sync routes with FIB (add and delete).
function FRRRouteSync:syncRoutesFIB()
  -- Create routes in FIB/Thrift format
  local fibAddRoutes, fibDelRoutes = {}, {}
  for _, route in pairs(self.current_routes) do
    if not route.fib_sync then
      if route.add then
        table.insert(fibAddRoutes, self:createUnicastRoute(route))
      else
        table.insert(fibDelRoutes, self:createUnicastRoute(route))
      end
    end
  end
  if #fibAddRoutes == 0 and #fibDelRoutes == 0 then
    return
  end

  -- Connect to FIB Agent
  local fibClient = self:connectToFibAgent()
  if fibClient == nil then
    return
  end

  -- Add/delete routes
  local addSuccess =
    (#fibAddRoutes >= 1 and self:addRoutesToFIB(fibClient, fibAddRoutes))
  local delSuccess =
    (#fibDelRoutes >= 1 and self:delRoutesFromFIB(fibClient, fibDelRoutes))
  fibClient:close()

  -- Mark these routes as FIB synced. FRR will push new routes on change.
  for _, route in pairs(self.current_routes) do
    if (route.add and addSuccess) or (not route.add and delSuccess) then
      route.fib_sync = true
      logger.info(
        "FIB: %s route for prefix %s with next-hop %s.",
        route.add and "Added" or "Deleted",
        route.dest,
        route.nextHop
      )
    end
  end
end

--- Sync routes with Open/R (advertise and withdraw).
function FRRRouteSync:syncRoutesOpenr()
  for _, route in pairs(self.current_routes) do
    if route.fib_sync and not route.openr_sync then
      local action, actionStr
      if route.add then
        action = "advertise"
        actionStr = "Advertising"
      else
        action = "withdraw"
        actionStr = "Withdrawing"
      end

      -- Run breeze command (TODO: use Thrift client directly?)
      logger.info("Open/R: %s prefix %s via breeze...", actionStr, route.dest)
      local cmd = string.format("breeze prefixmgr %s %s", action, route.dest)
      local output, ret, code = tg_utils.exec(cmd)
      if output == nil then
        logger.error("Command failed (returned %d): %s", code, cmd)
      end

      route.openr_sync = true
    end
  end
end

--- Clean up routes from tracking if synced to FIB and Open/R.
function FRRRouteSync:cleanupRoutes()
  for key, route in pairs(self.current_routes) do
    -- Remove route tracking only if route was deleted and synced with
    -- FIB and Open/R. Syncing will be tried again in the next loop.
    if not route.add and route.fib_sync and route.openr_sync then
      self.current_routes.key = nil
    end
  end
end

--- Add a route for tracking (if not already present).
function FRRRouteSync:addToTrackedRoutes(route)
  -- Make a unique key for each route (for easy tracking):
  --   <dest>@<nextHop>
  local routeKey = route.dest .. "@" .. route.nextHop
  local existingRoute = self.current_routes[routeKey]
  if existingRoute ~= nil then
    -- Route already present and added/deleted by bgpd.
    -- Need to sync FIB and Open/R.
    if route.add ~= existingRoute.add then
      existingRoute.add = route.add
      existingRoute.openr_sync = route.openr_sync
      existingRoute.fib_sync = route.fib_sync
      logger.info(
        "BGP route changed attributes and will be %s:\n%s",
        route.add and "added" or "deleted",
        pretty.write(route)
      )
    end
  else
    -- New route to be tracked
    self.current_routes[routeKey] = route
    logger.info(
      "New BGP route to be %s:\n%s",
      route.add and "added" or "deleted",
      pretty.write(route)
    )
  end
end

--- Return true if we should handle the given netlink message.
function FRRRouteSync:validateNetlinkMessage(nlMsg)
  -- Check if event was a ROUTE_EVENT
  if nlMsg.header.type ~= tg_rtnl.RtmType.RTM_NEWROUTE and
     nlMsg.header.type ~= tg_rtnl.RtmType.RTM_DELROUTE then
    logger.debug("Skipping due to message not being a route event")
    return false
  end

  -- Check if route is a default route
  if nlMsg.attrs.RTA_DST ~= "::" then
    logger.debug("Skipping non-default routes %s", nlMsg.attrs.RTA_DST)
    return false
  end

  -- Check message protocol
  if nlMsg.protocol ~= consts.ZEBRA_PROTOCOL_ID then
    logger.debug("Skipping non-Zebra routes %d", nlMsg.protocol)
    return false
  end

  -- Check for next-hop attributes
  if nlMsg.attrs.RTA_DST == nil or
     nlMsg.attrs.RTA_GATEWAY == nil or
     nlMsg.attrs.RTA_OIF == nil then
    logger.debug("Skipping routes with no valid nexthop attrs")
    return false
  end

  return true
end

--- Handle a netlink message and extract route attributes.
function FRRRouteSync:handleNetlinkMessage(nlMsg)
  if not self:validateNetlinkMessage(nlMsg) then
    return
  end

  -- Add to current routes being tracked
  local curRoute = {
    dest = nlMsg.attrs.RTA_DST,
    -- TODO: Get interface name from netlink interface index via "ip link"
    ifName = consts.POP_IFACE,
    nextHop = nlMsg.attrs.RTA_GATEWAY,
    prefixLen = 0,
    add = (nlMsg.header.type == tg_rtnl.RtmType.RTM_NEWROUTE),
    fib_sync = false,
    openr_sync = false
  }
  self:addToTrackedRoutes(curRoute)
end

--- Process FPM (and Netlink) messages from Zebra.
function FRRRouteSync:processFPMMessages()
  while true do
    logger.info("Awaiting connection...")
    local cfd, addr = sys_socket.accept(self.fpm_socket)

    logger.info("Connected, receiving data...")
    local data = sys_socket.recv(cfd, consts.FPM_MAX_MSG_LEN)
    if data and #data > 0 then
      -- Parse FPM header and Netlink messages from received data
      local messages = tg_zebra_utils.parse(data)
      logger.info("Received %d message(s) from Zebra.", #messages)

      -- Process all messages
      for _, msg in ipairs(messages) do
        if msg.fpm.type == 1 then
          self:handleNetlinkMessage(msg.rtnl)
        end
      end

      -- Sync routes with FIB, Open/R
      self:syncRoutesFIB()
      self:syncRoutesOpenr()
      self:cleanupRoutes()
    else
      logger.error("FPM message read error: %s", data)
    end

    unistd.close(cfd)
  end
end

--- Connect to the given FPM (Forwarding Plane Manager) socket, and return true
-- upon success.
function FRRRouteSync:startFPM(ip, port)
  -- Create socket
  local fd, err = sys_socket.socket(
    sys_socket.AF_INET, sys_socket.SOCK_STREAM, 0
  )
  if fd == nil then
    logger.error("Cannot create FPM socket, err: %s", err)
    return false
  end
  sys_socket.setsockopt(fd, sys_socket.SOL_SOCKET, sys_socket.SO_REUSEADDR, 1)

  -- Bind to Zebra IP/port and listen for FPM messages
  while true do
    local ret, errmsg = sys_socket.bind(
      fd, {family = sys_socket.AF_INET, addr = ip, port = port}
    )
    if ret ~= 0 then
      logger.info("Unable to bind to %s:%d (returned %s)", ip, port, ret)
      unistd.sleep(5)
    else
      break
    end
  end
  logger.info("Successfully bound to %s:%d", ip, port)

  -- Make sure current default routes are in a clean state
  self:performDefaultRouteCheck()

  -- Number of queued connections. Only expect 1 connection from Zebra.
  sys_socket.listen(fd, 1)

  self.fpm_socket = fd
  return true
end

local function main()
  local parser = argparse(
    "frr_openr_sync",
    "Route sync module for syncing BGP routes from FRR-bgpd to FIB and Open/R."
  )
  parser:option("--fpm_ip", "Zebra/FPM IPv4 address", consts.FPM_DEFAULT_IP)
  parser:option(
    "--fpm_port", "Zebra/FPM port", tostring(consts.FPM_DEFAULT_PORT)
  ):convert(tonumber)
  local args = parser:parse()

  -- Connect to FPM socket and process incoming messages
  local routeSync = FRRRouteSync.new()
  if routeSync:startFPM(args.fpm_ip, args.fpm_port) then
    routeSync:processFPMMessages()
  else
    os.exit(1)
  end
end

if tg_utils.isMain() then
  main()
else
  return FRRRouteSync
end
