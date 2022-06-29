#!/usr/bin/env lua

-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

local puff = assert(loadfile("/usr/sbin/puff"))

require "openr.OpenrCtrl_OpenrCtrl"
require "openr.Platform_FibService"
require "openr.KvStore_ttypes"
require "openr.Network_ttypes"
require "openr.Types_ttypes"
local lu = require "luaunit"
local tablex = require "pl.tablex"
require("pl.stringx").import()

TestMain = {}
TestDecision = setmetatable({}, {__index = TestMain})
TestFib = setmetatable({}, {__index = TestMain})
TestInfo = setmetatable({}, {__index = TestMain})
TestKvStore = setmetatable({}, {__index = TestMain})
TestLm = setmetatable({}, {__index = TestMain})
TestMonitor = setmetatable({}, {__index = TestMain})
TestPrefixmgr = setmetatable({}, {__index = TestMain})

function TestMain:setUp()
  self.parser, self.Puff, self.Puff_Handler = puff()
  self.thisNodeName = "node-11.22.33.44.55.66"

  -- Mock network functions
  -- Store invoked functions in "invokedFns"
  -- Store mock Thrift handlers in "ctrlHandlers" and "fibHandlers"
  self.invokedFns = {}
  self.ctrlHandlers = {
    getInterfaces = function()
      local reply = DumpLinksReply:new{}
      reply.thisNodeName = self.thisNodeName
      reply.isOverloaded = false
      reply.interfaceDetails = {}
      return reply
    end
  }
  self.fibHandlers = {}
  self.Puff.connectToOpenrCtrl = function(_self)
    _self.openr_ctrl_client = setmetatable({}, {
      __index = function(t, k)
        self.invokedFns[k] = true
        lu.assertNotNil(OpenrCtrlClient[k], string.format(
          "OpenrCtrlClient:%s() not found - did the API change?", k
        ))
        return self.ctrlHandlers[k]
      end
    })
    return true
  end
  self.Puff.connectToOpenrFibAgent = function(_self)
    _self.openr_fib_client = setmetatable({}, {
      __index = function(t, k)
        self.invokedFns[k] = true
        lu.assertNotNil(FibServiceClient[k], string.format(
          "FibServiceClient:%s() not found - did the API change?", k
        ))
        return self.fibHandlers[k]
      end
    })
    return true
  end
end

--- Basic functionality checks.
function TestMain:testBasicFunctionality()
  lu.assertStrContains(self.parser:get_usage(), "Usage:")
end

--- Return a KvStore.Value struct with the given parameters.
function TestKvStore._getKvStoreValue(args)
  local value = Value:new{}
  if not args.NIL_VALUE then
    value.value = args.value or "test_value"
  end
  value.version = args.version or 1
  value.originatorId = args.originatorId or "puff"
  value.ttl = args.ttl or (-2 ^ 31)
  value.ttlVersion = args.ttlVersion or nil
  return value
end

--- Test "puff kvstore keys" command.
function TestKvStore:test_kvstoreKeys()
  self.ctrlHandlers.getKvStoreKeyValsFiltered = function(_self, filter)
    -- Validate args
    local expectedFilter = KeyDumpParams:new{}
    expectedFilter.prefix = "test"
    expectedFilter.originatorIds = {unit_test = true}
    lu.assertTrue(tablex.deepcompare(filter, expectedFilter))

    -- Mock response
    local publication = Publication:new{}
    publication.keyVals = {testKey = self._getKvStoreValue({})}
    return publication
  end

  lu.assertTrue(self.parser:pparse({
    "kvstore", "keys", "--prefix", "test", "--originator", "unit_test"
  }))
  lu.assertNotNil(self.invokedFns.getKvStoreKeyValsFiltered)
end

--- Test "puff kvstore keyvals" command.
function TestKvStore:test_kvstoreKeyvals()
  self.ctrlHandlers.getKvStoreKeyVals = function(_self, filter)
    -- Validate args
    lu.assertItemsEquals(filter, {"test", "another"})

    -- Mock response
    local publication = Publication:new{}
    publication.keyVals = {testKey = self._getKvStoreValue({})}
    return publication
  end

  -- Not enough args
  lu.assertFalse(self.parser:pparse({"kvstore", "keyvals"}))

  lu.assertTrue(self.parser:pparse({"kvstore", "keyvals", "test", "another"}))
  lu.assertNotNil(self.invokedFns.getKvStoreKeyVals)
end

--- Test "puff kvstore set-key" command.
function TestKvStore:test_kvstoreSetKey()
  -- Not enough args
  lu.assertFalse(self.parser:pparse({"kvstore", "set-key"}))
  lu.assertFalse(self.parser:pparse({"kvstore", "set-key", "test"}))

  -- Explicitly pass new version
  self.ctrlHandlers.setKvStoreKeyVals = function(_self, setParams)
    local expectedParams = KeySetParams:new{}
    expectedParams.keyVals = {
      test = self._getKvStoreValue({value = "my_value", version = 1})
    }
    lu.assertTrue(tablex.deepcompare(setParams, expectedParams))
  end
  lu.assertTrue(self.parser:pparse({
    "kvstore", "set-key", "test", "my_value", "--version", "1"
  }))
  lu.assertNil(self.invokedFns.getKvStoreKeyVals)
  lu.assertNotNil(self.invokedFns.setKvStoreKeyVals)
  self.invokedFns = {}

  -- Query old version before setting new version
  self.ctrlHandlers.getKvStoreKeyVals = function(_self, filter)
    lu.assertItemsEquals(filter, {"test"})
    local publication = Publication:new{}
    publication.keyVals = {test = self._getKvStoreValue({version = 9})}
    return publication
  end
  self.ctrlHandlers.setKvStoreKeyVals = function(_self, setParams)
    local expectedParams = KeySetParams:new{}
    expectedParams.keyVals = {
      test = self._getKvStoreValue({value = "my_value", version = 10})
    }
    lu.assertTrue(tablex.deepcompare(setParams, expectedParams))
  end
  lu.assertTrue(self.parser:pparse({"kvstore", "set-key", "test", "my_value"}))
  lu.assertNotNil(self.invokedFns.getKvStoreKeyVals)
  lu.assertNotNil(self.invokedFns.setKvStoreKeyVals)
end

--- Test "puff kvstore erase-key" command.
function TestKvStore:test_kvstoreEraseKey()
  -- Not enough args
  lu.assertFalse(self.parser:pparse({"kvstore", "erase-key"}))

  -- Key doesn't exist
  self.ctrlHandlers.getKvStoreKeyVals = function(_self, filter)
    lu.assertItemsEquals(filter, {"test"})
    local publication = Publication:new{}
    publication.keyVals = {}
    return publication
  end
  lu.assertError(function()
    self.parser:pparse({"kvstore", "erase-key", "test"})
  end)
  lu.assertNotNil(self.invokedFns.getKvStoreKeyVals)
  lu.assertNil(self.invokedFns.setKvStoreKeyVals)
  self.invokedFns = {}

  -- Query old version before erasing with new version
  self.ctrlHandlers.getKvStoreKeyVals = function(_self, filter)
    lu.assertItemsEquals(filter, {"test"})
    local publication = Publication:new{}
    publication.keyVals = {test = self._getKvStoreValue({ttlVersion = 123})}
    return publication
  end
  self.ctrlHandlers.setKvStoreKeyVals = function(_self, setParams)
    local expectedParams = KeySetParams:new{}
    expectedParams.keyVals = {
      test = self._getKvStoreValue({
        NIL_VALUE = true, ttl = 256, ttlVersion = 124
      })
    }
    lu.assertTrue(tablex.deepcompare(setParams, expectedParams))
  end
  lu.assertTrue(self.parser:pparse({"kvstore", "erase-key", "test"}))
  lu.assertNotNil(self.invokedFns.getKvStoreKeyVals)
  lu.assertNotNil(self.invokedFns.setKvStoreKeyVals)
end

--- Test "puff kvstore prefixes" command.
function TestKvStore:test_kvstorePrefixes()
  self.ctrlHandlers.getKvStoreKeyValsFiltered = function(_self, filter)
    -- Mock response
    local publication = Publication:new{}
    publication.keyVals = {}
    return publication
  end

  lu.assertTrue(self.parser:pparse({"kvstore", "prefixes"}))
  lu.assertNotNil(self.invokedFns.getKvStoreKeyValsFiltered)
  lu.assertNotNil(self.invokedFns.getInterfaces)
  self.invokedFns = {}

  lu.assertTrue(self.parser:pparse({"kvstore", "prefixes", "--nodes", "all"}))
  lu.assertNotNil(self.invokedFns.getKvStoreKeyValsFiltered)
  lu.assertNil(self.invokedFns.getInterfaces)
end

--- Test "puff kvstore adj" command.
function TestKvStore:test_kvstoreAdj()
  self.ctrlHandlers.getKvStoreKeyValsFiltered = function(_self, filter)
    -- Validate args
    local expectedFilter = KeyDumpParams:new{}
    expectedFilter.prefix = "adj:"
    lu.assertTrue(tablex.deepcompare(filter, expectedFilter))

    -- Mock response
    local publication = Publication:new{}
    publication.keyVals = {}
    return publication
  end

  lu.assertTrue(self.parser:pparse({"kvstore", "adj"}))
  lu.assertNotNil(self.invokedFns.getKvStoreKeyValsFiltered)
  lu.assertNotNil(self.invokedFns.getInterfaces)
  self.invokedFns = {}

  lu.assertTrue(self.parser:pparse({"kvstore", "adj", "--nodes", "all"}))
  lu.assertNotNil(self.invokedFns.getKvStoreKeyValsFiltered)
  lu.assertNil(self.invokedFns.getInterfaces)
end

--- Test "puff kvstore peers" command.
function TestKvStore:test_kvstorePeers()
  self.ctrlHandlers.getKvStorePeers = function(_self)
    -- Mock response
    local peerSpec = PeerSpec:new{}
    peerSpec.cmdUrl = "tcp://[fe80::1234:5678:90ab:cdef%terra0]:60002"
    return {["node-01.23.45.67.89.0a"] = peerSpec}
  end

  lu.assertTrue(self.parser:pparse({"kvstore", "peers"}))
  lu.assertNotNil(self.invokedFns.getKvStorePeers)
end

--- Return a Network.UnicastRoute struct.
function TestFib._getUnicastRoute(args)
  local nexthop = NextHopThrift:new{}
  nexthop.address = BinaryAddress:new{}
  nexthop.address.addr = args.nexthopAddr
  nexthop.address.ifName = args.nexthopIface
  local route = UnicastRoute:new{}
  route.dest = IpPrefix:new{}
  route.dest.prefixAddress = BinaryAddress:new{}
  route.dest.prefixAddress.addr = args.prefixAddr
  route.dest.prefixLength = args.prefixLen
  route.nextHops = {nexthop}
  return route
end

--- Test "puff fib routes" command.
function TestFib:test_fibRoutes()
  self.ctrlHandlers.getRouteDb = function(_self, filter)
    -- Mock response
    local routeDb = RouteDatabase:new{}
    routeDb.thisNodeName = self.thisNodeName
    routeDb.unicastRoutes = {}
    routeDb.mplsRoutes = {}
    return routeDb
  end

  lu.assertTrue(self.parser:pparse({"fib", "routes"}))
  lu.assertNotNil(self.invokedFns.getRouteDb)
end

--- Test "puff fib list" command.
function TestFib:test_fibList()
  self.fibHandlers.getRouteTableByClient = function(_self, clientId)
    lu.assertEquals(clientId, FibClient.OPENR)
    return {}
  end
  self.fibHandlers.getMplsRouteTableByClient = function(_self, clientId)
    lu.assertEquals(clientId, FibClient.OPENR)
    return {}
  end

  lu.assertTrue(self.parser:pparse({"fib", "list"}))
  lu.assertNotNil(self.invokedFns.getRouteTableByClient)
  lu.assertNotNil(self.invokedFns.getMplsRouteTableByClient)
end

--- Test "puff fib add" command.
function TestFib:test_fibAdd()
  self.fibHandlers.addUnicastRoutes = function(_self, clientId, routes)
    lu.assertEquals(clientId, FibClient.OPENR)

    local route = self._getUnicastRoute({
      nexthopAddr = " \1\0\0\0\0\0\0\0\0\0\0\0\0\0\1", nexthopIface = "terra6",
      prefixAddr = " \1\0\0\0\0\0\0\0\0\0\0\0\0\0\0", prefixLen = 64
    })
    local expectedRoutes = {route}
    lu.assertTrue(tablex.deepcompare(routes, expectedRoutes))
  end

  -- Not enough args
  lu.assertFalse(self.parser:pparse({"fib", "add"}))
  lu.assertFalse(self.parser:pparse({"fib", "add", "2001::/64"}))

  lu.assertTrue(self.parser:pparse({
    "fib", "add", "2001::/64", "2001::1@terra6"
  }))
  lu.assertNotNil(self.invokedFns.addUnicastRoutes)
end

--- Test "puff fib del" command.
function TestFib:test_fibDel()
  self.fibHandlers.deleteUnicastRoutes = function(_self, clientId, prefixes)
    lu.assertEquals(clientId, FibClient.OPENR)

    local prefix = IpPrefix:new{}
    prefix.prefixAddress = BinaryAddress:new{}
    prefix.prefixAddress.addr = " \1\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    prefix.prefixLength = 64
    local expectedPrefixes = {prefix}
    lu.assertTrue(tablex.deepcompare(prefixes, expectedPrefixes))
  end

  -- Not enough args
  lu.assertFalse(self.parser:pparse({"fib", "del"}))

  lu.assertTrue(self.parser:pparse({"fib", "del", "2001::/64"}))
  lu.assertNotNil(self.invokedFns.deleteUnicastRoutes)
end

--- Test "puff fib sync" command.
function TestFib:test_fibSync()
  self.fibHandlers.syncFib = function(_self, clientId, routes)
    lu.assertEquals(clientId, FibClient.OPENR)

    local route = self._getUnicastRoute({
      nexthopAddr = " \1\0\0\0\0\0\0\0\0\0\0\0\0\0\1", nexthopIface = "terra6",
      prefixAddr = " \1\0\0\0\0\0\0\0\0\0\0\0\0\0\0", prefixLen = 64
    })
    local expectedRoutes = {route}
    lu.assertTrue(tablex.deepcompare(routes, expectedRoutes))
  end

  -- Not enough args
  lu.assertFalse(self.parser:pparse({"fib", "sync"}))
  lu.assertFalse(self.parser:pparse({"fib", "sync", "2001::/64"}))

  lu.assertTrue(self.parser:pparse({
    "fib", "sync", "2001::/64", "2001::1@terra6"
  }))
  lu.assertNotNil(self.invokedFns.syncFib)
end

--- Test "puff decision routes" command.
function TestDecision:test_decisionRoutes()
  self.ctrlHandlers.getRouteDbComputed = function(_self, node)
    lu.assertEquals(node, self.thisNodeName)

    -- Mock response
    local routeDb = RouteDatabase:new{}
    routeDb.thisNodeName = self.thisNodeName
    routeDb.unicastRoutes = {}
    routeDb.mplsRoutes = {}
    return routeDb
  end
  self.ctrlHandlers.getDecisionPrefixDbs = function(_self)
    -- Mock response
    local prefixDb = PrefixDatabase:new{}
    prefixDb.thisNodeName = self.thisNodeName
    prefixDb.prefixEntries = {}
    return {[self.thisNodeName] = prefixDb}
  end

  lu.assertTrue(self.parser:pparse({"decision", "routes"}))
  lu.assertNotNil(self.invokedFns.getInterfaces)
  lu.assertNil(self.invokedFns.getDecisionPrefixDbs)
  lu.assertNotNil(self.invokedFns.getRouteDbComputed)
  self.invokedFns = {}

  lu.assertTrue(self.parser:pparse({"decision", "routes", "--nodes", "all"}))
  lu.assertNil(self.invokedFns.getInterfaces)
  lu.assertNotNil(self.invokedFns.getDecisionPrefixDbs)
  lu.assertNotNil(self.invokedFns.getRouteDbComputed)
end

--- Test "puff decision prefixes" command.
function TestDecision:test_decisionPrefixes()
  self.ctrlHandlers.getDecisionPrefixDbs = function(_self)
    -- Mock response
    local prefixDb = PrefixDatabase:new{}
    prefixDb.thisNodeName = self.thisNodeName
    prefixDb.prefixEntries = {}
    return {[self.thisNodeName] = prefixDb}
  end

  lu.assertTrue(self.parser:pparse({"decision", "prefixes"}))
  lu.assertNotNil(self.invokedFns.getInterfaces)
  lu.assertNotNil(self.invokedFns.getDecisionPrefixDbs)
  self.invokedFns = {}

  lu.assertTrue(self.parser:pparse({"decision", "prefixes", "--nodes", "all"}))
  lu.assertNil(self.invokedFns.getInterfaces)
  lu.assertNotNil(self.invokedFns.getDecisionPrefixDbs)
end

--- Test "puff decision adj" command.
function TestDecision:test_decisionAdj()
  self.ctrlHandlers.getDecisionAdjacencyDbs = function(_self)
    -- Mock response
    local adjDb = AdjacencyDatabase:new{}
    adjDb.thisNodeName = self.thisNodeName
    adjDb.isOverloaded = false
    adjDb.adjacencies = {}
    adjDb.nodeLabel = 0
    return {[self.thisNodeName] = adjDb}
  end

  lu.assertTrue(self.parser:pparse({"decision", "adj"}))
  lu.assertNotNil(self.invokedFns.getInterfaces)
  lu.assertNotNil(self.invokedFns.getDecisionAdjacencyDbs)
  self.invokedFns = {}

  lu.assertTrue(self.parser:pparse({"decision", "adj", "--nodes", "all"}))
  lu.assertNil(self.invokedFns.getInterfaces)
  lu.assertNotNil(self.invokedFns.getDecisionAdjacencyDbs)
end

--- Test "puff monitor counters" command.
function TestMonitor:test_monitorCounters()
  self.ctrlHandlers.getCounters = function(_self)
    -- Mock response
    return {
      ["spark.hello.bytes_sent.sum"] = 474869,
      ["spark.hello.bytes_sent.sum.3600"] = 0,
      ["spark.hello.bytes_sent.sum.60"] = 0,
      ["spark.hello.bytes_sent.sum.600"] = 0,
      ["spark.hello.packets_sent.sum"] = 2973,
      ["spark.hello.packets_sent.sum.3600"] = 0,
      ["spark.hello.packets_sent.sum.60"] = 0,
      ["spark.hello.packets_sent.sum.600"] = 0,
    }
  end

  lu.assertTrue(self.parser:pparse({"monitor", "counters"}))
  lu.assertNotNil(self.invokedFns.getCounters)
end

--- Test "puff monitor logs" command.
function TestMonitor:test_monitorLogs()
  self.ctrlHandlers.getEventLogs = function(_self)
    -- Mock response
    return {
      '{"int":{"backoff_ms":0,"time":1648656611},"normal":{' ..
        '"domain":"terragraph","event":"IFACE_UP","interface":' ..
        '"lo","node_name":"node-aa.bb.cc.dd.ee.ff"}}',
      '{"int":{"backoff_ms":0,"time":1648686053},"normal":{' ..
        '"domain":"terragraph","event":"IFACE_UP","interface":' ..
        '"terra16","node_name":"node-aa.bb.cc.dd.ee.ff"}}'
    }
  end

  lu.assertTrue(self.parser:pparse({"monitor", "logs"}))
  lu.assertNotNil(self.invokedFns.getEventLogs)
end

--- Return a Types.PrefixEntry struct.
function TestPrefixmgr._getPrefixEntry(args)
  local entry = PrefixEntry:new{}
  entry.prefix = IpPrefix:new{}
  entry.prefix.prefixAddress = BinaryAddress:new{}
  entry.prefix.prefixAddress.addr = args.prefixAddr
  entry.prefix.prefixLength = args.prefixLen
  entry.type = args.prefixType
  entry.forwardingType = args.forwardingType
  return entry
end

--- Test "puff prefixmgr withdraw" command.
function TestPrefixmgr:test_prefixmgrWithdraw()
  self.ctrlHandlers.withdrawPrefixes = function(_self, prefixes)
    -- Validate args
    local prefix = self._getPrefixEntry({
      prefixAddr = " \1\0\0\0\0\0\0\0\0\0\0\0\0\0\0", prefixLen = 64,
      prefixType = PrefixType.BREEZE, forwardingType = PrefixForwardingType.IP
    })
    local expectedPrefixes = {prefix}
    lu.assertTrue(tablex.deepcompare(prefixes, expectedPrefixes))
  end

  -- Not enough args
  lu.assertFalse(self.parser:pparse({"prefixmgr", "withdraw"}))

  lu.assertTrue(self.parser:pparse({"prefixmgr", "withdraw", "2001::/64"}))
  lu.assertNotNil(self.invokedFns.withdrawPrefixes)
end

--- Test "puff prefixmgr advertise" command.
function TestPrefixmgr:test_prefixmgrAdvertise()
  self.ctrlHandlers.advertisePrefixes = function(_self, prefixes)
    -- Validate args
    local prefix = self._getPrefixEntry({
      prefixAddr = " \1\0\0\0\0\0\0\0\0\0\0\0\0\0\0", prefixLen = 64,
      prefixType = PrefixType.BREEZE, forwardingType = PrefixForwardingType.IP
    })
    local expectedPrefixes = {prefix}
    lu.assertTrue(tablex.deepcompare(prefixes, expectedPrefixes))
  end

  -- Not enough args
  lu.assertFalse(self.parser:pparse({"prefixmgr", "advertise"}))

  lu.assertTrue(self.parser:pparse({"prefixmgr", "advertise", "2001::/64"}))
  lu.assertNotNil(self.invokedFns.advertisePrefixes)
end

--- Test "puff prefixmgr sync" command.
function TestPrefixmgr:test_prefixmgrSync()
  self.ctrlHandlers.syncPrefixesByType = function(_self, prefixType, prefixes)
    -- Validate args
    local prefix = self._getPrefixEntry({
      prefixAddr = " \1\0\0\0\0\0\0\0\0\0\0\0\0\0\0", prefixLen = 64,
      prefixType = PrefixType.BREEZE, forwardingType = PrefixForwardingType.IP
    })
    local expectedPrefixes = {prefix}
    lu.assertTrue(tablex.deepcompare(prefixes, expectedPrefixes))
    lu.assertEquals(prefixType, PrefixType.BREEZE)
  end

  -- Not enough args
  lu.assertFalse(self.parser:pparse({"prefixmgr", "sync"}))

  lu.assertTrue(self.parser:pparse({"prefixmgr", "sync", "2001::/64"}))
  lu.assertNotNil(self.invokedFns.syncPrefixesByType)
end

--- Test "puff prefixmgr view" command.
function TestPrefixmgr:test_prefixmgrView()
  self.ctrlHandlers.getPrefixes = function(_self)
    -- Mock response
    local prefix = self._getPrefixEntry({
      prefixAddr = " \1\0\0\0\0\0\0\0\0\0\0\0\0\0\0", prefixLen = 64,
      prefixType = PrefixType.BREEZE, forwardingType = PrefixForwardingType.IP
    })
    return {prefix}
  end

  lu.assertTrue(self.parser:pparse({"prefixmgr", "view"}))
  lu.assertNotNil(self.invokedFns.getPrefixes)
end

--- Test "puff lm links" command.
function TestLm:test_lmLinks()
  lu.assertTrue(self.parser:pparse({"lm", "links"}))
  lu.assertNotNil(self.invokedFns.getInterfaces)
end

--- Test "puff lm set-adj-metric" command.
function TestLm:test_setAdjMetric()
  self.ctrlHandlers.setAdjacencyMetric = function(
      _self, interface, node, metric
    )
      lu.assertEquals(interface, "terra0")
      lu.assertEquals(metric, 123)
      lu.assertEquals(node, "node")
  end
  lu.assertTrue(
    self.parser:pparse({"lm", "set-adj-metric", "node", "terra0", "123"})
  )
  lu.assertNotNil(self.invokedFns.setAdjacencyMetric)
  self.invokedFns = {}

  -- Not enough args
  lu.assertFalse(self.parser:pparse({"lm","set-adj-metric", "node"}))
end

--- Test "puff lm set-link-metric" command.
function TestLm:test_setLinkMetric()
  self.ctrlHandlers.getInterfaces = function(_self)
    -- Mock response
    return {
      interfaceDetails = {
        terra0 = {
          metricOverride = 1
        }
      }
    }
  end
  self.ctrlHandlers.setInterfaceMetric = function(_self, interface, metric)
    lu.assertEquals(interface, "terra0")
    lu.assertEquals(metric, 123)
  end
  lu.assertTrue(self.parser:pparse({"lm", "set-link-metric", "terra0", "123"}))
  lu.assertNotNil(self.invokedFns.getInterfaces)
  lu.assertNotNil(self.invokedFns.setInterfaceMetric)
  self.invokedFns = {}

  -- Not enough args
  lu.assertFalse(self.parser:pparse({"lm", "set-link-metric", "terra0"}))

  -- Interface does not exist
  lu.assertError(function()
    self.parser:pparse({"lm", "set-link-metric", "terra1", "123"})
  end)
end

--- Test "puff lm set-link-overload" command.
function TestLm:test_setLinkOverload()
  self.ctrlHandlers.getInterfaces = function(_self)
    -- Mock response
    return {
      interfaceDetails = {
        terra0 = {
          isOverloaded = false
        },
        terra1 = {
          isOverloaded = true
        }
      }
    }
  end
  self.ctrlHandlers.setInterfaceOverload = function(_self, interface)
    lu.assertEquals(interface, "terra0")
  end
  lu.assertTrue(self.parser:pparse({"lm", "set-link-overload", "terra0"}))
  lu.assertNotNil(self.invokedFns.getInterfaces)
  lu.assertNotNil(self.invokedFns.setInterfaceOverload)
  self.invokedFns = {}

  -- Not enough args
  lu.assertFalse(self.parser:pparse({"lm", "set-link-overload"}))

  -- Interface already overloaded
  lu.assertError(function()
    self.parser:pparse({"lm", "set-link-overload", "terra1"})
  end)

  -- Interface does not exist
  lu.assertError(function()
    self.parser:pparse({"lm", "set-link-overload", "terra2"})
  end)
end

--- Test "puff lm set-node-overload" command.
function TestLm:test_setNodeOverload()
  self.ctrlHandlers.getInterfaces = function(_self)
    -- Mock response
    return {
      isOverloaded = false
    }
  end
  self.ctrlHandlers.setNodeOverload = function(_self)
    return {}
  end
  lu.assertTrue(self.parser:pparse({"lm", "set-node-overload"}))
  lu.assertNotNil(self.invokedFns.getInterfaces)
  lu.assertNotNil(self.invokedFns.setNodeOverload)
  self.invokedFns = {}

  -- Node already overloaded
  self.ctrlHandlers.getInterfaces = function(_self)
    -- Mock response
    return {
      isOverloaded = true
    }
  end
  lu.assertError(function()
    self.parser:pparse({"lm", "set-node-overload"})
  end)
end

--- Test "puff lm unset-adj-metric" command.
function TestLm:test_unsetAdjMetric()
  self.ctrlHandlers.unsetAdjacencyMetric = function(_self, interface, node)
    lu.assertEquals(interface, "terra0")
    lu.assertEquals(node, "node")
  end
  lu.assertTrue(
    self.parser:pparse({"lm", "unset-adj-metric", "node", "terra0"})
  )
  lu.assertNotNil(self.invokedFns.unsetAdjacencyMetric)
  self.invokedFns = {}

  -- Not enough args
  lu.assertFalse(self.parser:pparse({"lm", "unset-adj-metric", "terra0"}))
end

--- Test "puff lm unset-link-metric" command.
function TestLm:test_unsetLinkMetric()
  self.ctrlHandlers.getInterfaces = function(_self)
    -- Mock response
    return {
      interfaceDetails = {
        terra0 = {
          metricOverride = 123
        },
        terra1 = {
          metricOverride = nil
        }
      }
    }
  end
  self.ctrlHandlers.unsetInterfaceMetric = function(_self, interface)
    lu.assertEquals(interface, "terra0")
  end
  lu.assertTrue(self.parser:pparse({"lm", "unset-link-metric", "terra0"}))
  lu.assertNotNil(self.invokedFns.getInterfaces)
  lu.assertNotNil(self.invokedFns.unsetInterfaceMetric)
  self.invokedFns = {}

  -- Cannot unset a metric for an interface that has no metric set
  self.ctrlHandlers.unsetInterfaceMetric = function(_self, interface)
    lu.assertEquals(interface, "terra1")
  end
  lu.assertError(function()
    self.parser:pparse({"lm", "unset-link-metric", "terra1"})
  end)
end

--- Test "puff lm unset-link-overload" command.
function TestLm:test_unsetLinkOverload()
  self.ctrlHandlers.getInterfaces = function(_self)
    -- Mock response
    return {
      interfaceDetails = {
        terra0 = {
          isOverloaded = false
        },
        terra1 = {
          isOverloaded = true
        }
      }
    }
  end
  self.ctrlHandlers.unsetInterfaceOverload = function(_self, interface)
    lu.assertEquals(interface, "terra1")
  end
  lu.assertTrue(self.parser:pparse({"lm", "unset-link-overload", "terra1"}))
  lu.assertNotNil(self.invokedFns.getInterfaces)
  lu.assertNotNil(self.invokedFns.unsetInterfaceOverload)
  self.invokedFns = {}

  -- Not enough args
  lu.assertFalse(self.parser:pparse({"lm", "unset-link-overload"}))

  -- Interface not overloaded
  lu.assertError(function()
    self.parser:pparse({"lm", "unset-link-overload", "terra0"})
  end)

  -- Interface does not exist
  lu.assertError(function()
    self.parser:pparse({"lm", "unset-link-overload", "terra2"})
  end)
end

--- Test "puff lm unset-node-overload" command.
function TestLm:test_unsetNodeOverload()
  self.ctrlHandlers.getInterfaces = function(_self)
    -- Mock response
    return {
      isOverloaded = true
    }
  end
  self.ctrlHandlers.unsetNodeOverload = function(_self)
    return {}
  end
  lu.assertTrue(self.parser:pparse({"lm", "unset-node-overload"}))
  lu.assertNotNil(self.invokedFns.getInterfaces)
  lu.assertNotNil(self.invokedFns.unsetNodeOverload)
  self.invokedFns = {}

  -- Node not overloaded
  self.ctrlHandlers.getInterfaces = function(_self)
    -- Mock response
    return {
      isOverloaded = false
    }
  end
  lu.assertError(function()
    self.parser:pparse({"lm", "unset-node-overload"})
  end)
end

--- Test "puff info version" command.
function TestInfo:test_infoVersion()
  self.ctrlHandlers.getOpenrVersion = function(_self)
    return {}
  end
  lu.assertTrue(self.parser:pparse({"info", "version"}))
  lu.assertNotNil(self.invokedFns.getOpenrVersion)
end

--- Test "puff info build" command.
function TestInfo:test_infoBuild()
  self.ctrlHandlers.getBuildInfo = function(_self)
    return {}
  end
  lu.assertTrue(self.parser:pparse({"info", "build"}))
  lu.assertNotNil(self.invokedFns.getBuildInfo)
end

os.exit(lu.LuaUnit.run("-v"))
