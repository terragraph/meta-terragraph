-- Copyright (c) Meta Platforms, Inc. and affiliates.
--
-- This source code is licensed under the MIT license found in the
-- LICENSE file in the root directory of this source tree.

--- Frr-related utilities.
-- @module tg.frr_utils

local logger = require "tg.logger"
local tg_utils = require "tg.utils"
local tg_net_utils = require "tg.net_utils"
local tablex = require "pl.tablex"
local Template = (require "pl.text").Template
require("pl.stringx").import()

local frr_utils = {}

-- Template strings
local CONF_FILE_FORMAT = Template([[
# THIS FILE IS AUTO-GENERATED, DO NOT EDIT
router bgp ${asn}
 bgp router-id ${router_id}
 no bgp default ipv4-unicast
 bgp deterministic-med
 bgp bestpath as-path multipath-relax
 bgp log-neighbor-changes
${no_prefix_check}
${neighbors_info}

 timers bgp ${keepalive} ${holdtime}

 address-family ipv6 unicast
  maximum-paths 2
  maximum-paths ibgp 2
${network_prefixes}

${active_neighbors}

${af_neighbors_info}
 exit-address-family

${tg_out_prefixes_list}
${tg_in_prefixes_list}
ipv6 prefix-list TG-PREFIXES-IN seq 50 permit any

${cpe_out_prefixes_list}
${cpe_in_prefixes_list}

route-map ALLOW-TG-PREFIXES-OUT permit 10
 match ipv6 address prefix-list CPE-PREFIXES-OUT
 set ipv6 next-hop global ${advertise_route_next_hop}

route-map ALLOW-TG-PREFIXES-OUT permit 20
 match ipv6 address prefix-list TG-PREFIXES-OUT
 set ipv6 next-hop global ${advertise_route_next_hop}

route-map BLOCK-TG-PREFIXES-IN permit 10
 match ipv6 address prefix-list CPE-PREFIXES-IN
 set ipv6 next-hop prefer-global

route-map BLOCK-TG-PREFIXES-IN permit 20
 match ipv6 address prefix-list TG-PREFIXES-IN
 set ipv6 next-hop prefer-global

log stdout
]])
local NEIGHBOR_INFO_FORMAT = Template(
  " neighbor ${neighbor_ip} remote-as ${remote_asn}"
)
local NEIGHBOR_MD5_FORMAT  = Template (
   " neighbor ${neighbor_ip} password ${md5Password}"
)
local AF_NEIGHBOR_INFO_FORMAT = Template([[
  neighbor ${neighbor_ip} soft-reconfiguration inbound
  neighbor ${neighbor_ip} maximum-prefix ${maximum_prefix}
  neighbor ${neighbor_ip} route-map BLOCK-TG-PREFIXES-IN in
  neighbor ${neighbor_ip} route-map ALLOW-TG-PREFIXES-OUT out
]])
local ACTIVE_NEIGHBOR_FORMAT = Template("  neighbor ${neighbor_ip} activate")
local NETWORK_FORMAT = Template("  network ${network_prefix}")
local TG_PREFIX_IN_LIST_FORMAT = Template(([[
ipv6 prefix-list TG-PREFIXES-IN seq ${prefix_count} deny ${network_prefix}
]]):strip())
local TG_PREFIX_OUT_LIST_FORMAT = Template(([[
ipv6 prefix-list TG-PREFIXES-OUT seq ${prefix_count} permit ${network_prefix}
]]):strip())
local CPE_PREFIX_IN_LIST_FORMAT = Template(([[
ipv6 prefix-list CPE-PREFIXES-IN seq ${prefix_count} deny ${network_prefix}
]]):strip())
local CPE_PREFIX_OUT_LIST_FORMAT = Template(([[
ipv6 prefix-list CPE-PREFIXES-OUT seq ${prefix_count} permit ${network_prefix}
]]):strip())
local NO_PREFIX_CHECK_STR = " no bgp network import-check\n"

--- Get all keys from node config and process routes data.
--
-- - `nodeConfig` - the node config object
-- - `routerMac` - the router MAC address used to compute the router ID
-- - `openrNetworkPrefix` - optional network prefix to include in TG prefixes
--
-- Returns 3 tables:
--
-- - `staticBgpParams` - map of BGP parameters to values (ex. "asn", "routerId")
-- - `neighbors` - map of arbitrary keys to parameters (map) for all neighbors
-- - `tgPrefixes` - sorted list of node prefixes to announce to BGP peers
--
-- Upon failure in any field, returns nil.
function frr_utils.getNodeBgpInfo(nodeConfig, routerMac, openrNetworkPrefix)
  -- Read and validate keys from node config
  if type(nodeConfig) ~= "table" then
    logger.error("The nodeConfig table is invalid")
    return nil
  end
  local bgpParams = nodeConfig.bgpParams
  if bgpParams == nil or type(bgpParams) ~= "table" then
    logger.error("Missing 'bgpParams' structure in node config")
    return nil
  end
  local popParams = nodeConfig.popParams
  if popParams == nil or type(popParams) ~= "table" then
    logger.error("Missing 'popParams' structure in node config")
    return nil
  end
  local dpdkVppEnabled = tg_utils.get(
    nodeConfig, "envParams", "DPDK_ENABLED"
  ) or "1"

  -- Read and validate BGP params
  local staticBgpParams = frr_utils.readBgpParams(
    bgpParams,
    popParams,
    dpdkVppEnabled
  )
  if staticBgpParams == nil then
    logger.error("Error reading bgp parameters from node config")
    return nil
  end

  -- Compute router ID
  local routerId = frr_utils.computeRouterId(
    staticBgpParams.localAsn,
    routerMac
  )
  if routerId == nil then
    logger.error("Unable to compute BGP router-id")
    return nil
  end
  staticBgpParams["routerId"] = routerId

  -- Read and validate neighbors
  local neighbors = frr_utils.readNeighbors(bgpParams)
  if neighbors == nil then
    logger.error("Error reading neighbors structure from node config")
    return nil
  end

  -- Read and validate TG prefixes
  local tgPrefixes = frr_utils.readTgPrefixesFromNodeConfig(
    bgpParams,
    openrNetworkPrefix
  )
  if tgPrefixes == nil then
    logger.error("Error reading TG prefixes from node config")
    return nil
  end

  return staticBgpParams, neighbors, tgPrefixes
end

--- Read and validate keys from node config.
function frr_utils.readBgpParams(bgpParams, popParams, dpdkVppEnabled)
  if type(bgpParams) ~= "table" or type(popParams) ~= "table" then
    logger.error("Invalid bgpParams or popParams struct")
    return nil
  end

  local localAsn = bgpParams.localAsn
  if (
    localAsn == nil
  ) then
    logger.error("Missing or invalid 'bgpParams.localAsn' in node config")
    return nil
  end

  local md5Password = bgpParams.md5Password or ""

  local keepalive = bgpParams.keepalive or 30
  if (
    keepalive == nil
    or type(keepalive) ~= "number"
    or keepalive < 0
  ) then
    logger.error("Invalid 'bgpParams.keepalive' in node config")
    return nil
  end

  local nextHop = popParams["VPP_ADDR"]
  if not dpdkVppEnabled then
    nextHop = popParams["POP_ADDR"]
  end
  if nextHop == "" then
    logger.error("Missing 'POP_ADDR' or 'VPP_ADDR' in node configuration")
    return nil
  end

  -- Group static bgp params in a table
  return {
    localAsn = localAsn,
    keepalive = keepalive,
    nextHop = nextHop,
    md5Password = md5Password,
  }
end

--- Read and validate neighbors.
function frr_utils.readNeighbors(bgpParams)
  if type(bgpParams) ~= "table" then
    logger.error("Invalid bgpParams struct")
    return nil
  end

  for k, v in tablex.sort(bgpParams.neighbors or {}) do
    if v.asn == nil or v.asn <= 0 then
      logger.error("Invalid ASN '%s' for neighbor %s", v.asn, k)
      return nil
    end
    if not tg_net_utils.isIPv6(v.ipv6) then
      logger.error("Invalid IPv6 address '%s' for neighbor %s", v.ipv6, k)
      return nil
    end
  end
  return bgpParams.neighbors
end

--- Read and validate TG prefixes.
function frr_utils.readTgPrefixesFromNodeConfig(bgpParams, openrNetworkPrefix)
  if type(bgpParams) ~= "table" then
    logger.error("Invalid bgpParams struct")
    return nil
  end

  local bgpPrefixMap = {}  -- used as a set
  local prefixKeys = {
    "cpeNetworkPrefix", "delegatedNetworkPrefixes", "specificNetworkPrefixes"
  }
  for _, key in ipairs(prefixKeys) do
    if bgpParams[key] ~= nil then
      for __, prefix in ipairs(bgpParams[key]:split(",")) do
        -- Validate prefixes
        if not tg_net_utils.isIPv6Network(prefix) then
          logger.error("Invalid prefix found: %s", prefix)
          return nil
        else
          bgpPrefixMap[prefix] = true
        end
      end
    end
  end
  -- Hack in Open/R KvStore prefix if present
  if openrNetworkPrefix ~= nil then
    bgpPrefixMap[openrNetworkPrefix] = true
  end
  local bgpTgPrefixes = tablex.keys(bgpPrefixMap)
  table.sort(bgpTgPrefixes)
  return bgpTgPrefixes
end

--- Creates and returns FRR configuration using the provided fields.
--
-- - `staticBgpParams` - map of BGP parameters to values (ex. "asn", "routerId")
-- - `neighbors` - map of arbitrary keys to parameters (map) for all neighbors
-- - `tgPrefixes` - ordered list of node prefixes to announce to BGP peers (may
--                  contain empty strings to preserve ordering)
-- - `cpePrefixes` - ordered list of CPE prefixes to announce to BGP peers (may
--                   contain empty strings to preserve ordering)
function frr_utils.fillConfigTemplate(
  staticBgpParams, neighbors, tgPrefixes, cpePrefixes
)
  -- Fill in TG prefixes
  local networkPrefixes, tgInPrefixes, tgOutPrefixes = {}, {}, {}
  for idx, prefix in ipairs(tgPrefixes or {}) do
    if prefix ~= "" then
      local templateData = {network_prefix = prefix, prefix_count = idx}
      networkPrefixes[#networkPrefixes + 1] =
        NETWORK_FORMAT:substitute(templateData)
      tgInPrefixes[#tgInPrefixes + 1] =
        TG_PREFIX_IN_LIST_FORMAT:substitute(templateData)
      tgOutPrefixes[#tgOutPrefixes + 1] =
        TG_PREFIX_OUT_LIST_FORMAT:substitute(templateData)
    end
  end

  -- Fill in CPE prefixes
  local cpeInPrefixes, cpeOutPrefixes = {}, {}
  for idx, prefix in ipairs(cpePrefixes or {}) do
    if prefix ~= "" then
      local templateData = {network_prefix = prefix, prefix_count = idx}
      networkPrefixes[#networkPrefixes + 1] =
        NETWORK_FORMAT:substitute(templateData)
      cpeInPrefixes[#cpeInPrefixes + 1] =
        CPE_PREFIX_IN_LIST_FORMAT:substitute(templateData)
      cpeOutPrefixes[#cpeOutPrefixes + 1] =
        CPE_PREFIX_OUT_LIST_FORMAT:substitute(templateData)
    end
  end

  -- Fill in neighbors
  local neighborsInfo, afNeighborsInfo, activeNeighbors = {}, {}, {}
  for k, v in tablex.sort(neighbors or {}) do
    local neighborMd5Password

    if staticBgpParams.md5Password == "" then
      neighborMd5Password = ""
    else
      neighborMd5Password = "\n"..NEIGHBOR_MD5_FORMAT:substitute{
        neighbor_ip = v.ipv6,
        md5Password = staticBgpParams.md5Password
      }
    end

    local neighborGeneralConfig = NEIGHBOR_INFO_FORMAT:substitute{
      neighbor_ip = v.ipv6,
      remote_asn = v.asn,
    }
    neighborsInfo[#neighborsInfo + 1] =
      neighborGeneralConfig..neighborMd5Password

    afNeighborsInfo[#afNeighborsInfo + 1] = AF_NEIGHBOR_INFO_FORMAT:substitute{
      neighbor_ip = v.ipv6,
      maximum_prefix = v.maximumPrefixes or 128,
    }
    activeNeighbors[#activeNeighbors + 1] =
      ACTIVE_NEIGHBOR_FORMAT:substitute{neighbor_ip = v.ipv6}

  end

  return CONF_FILE_FORMAT:substitute{
    active_neighbors = ("\n"):join(activeNeighbors),
    asn = staticBgpParams.localAsn,
    holdtime = 3 * staticBgpParams.keepalive,
    keepalive = staticBgpParams.keepalive,
    neighbors_info = ("\n"):join(neighborsInfo),
    af_neighbors_info = ("\n"):join(afNeighborsInfo),
    network_prefixes = ("\n"):join(networkPrefixes),
    -- TODO: Without import check routes are not advertised in FRR BGPD.
    no_prefix_check = NO_PREFIX_CHECK_STR,
    router_id = staticBgpParams.routerId,
    advertise_route_next_hop = staticBgpParams.nextHop,
    tg_in_prefixes_list = ("\n"):join(tgInPrefixes),
    tg_out_prefixes_list = ("\n"):join(tgOutPrefixes),
    cpe_in_prefixes_list = ("\n"):join(cpeInPrefixes),
    cpe_out_prefixes_list = ("\n"):join(cpeOutPrefixes),
  }
end

--- Compute BGP4 "router-ID" according to the following IEEE draft:
-- https://tools.ietf.org/id/draft-dupont-durand-idr-ipv6-bgp-routerid-01.txt
--
-- Note that we force the first bit to 0 (diverging from the specification).
--
-- Returns nil upon any error.
function frr_utils.computeRouterId(asn, macAddr)
  if asn == nil or type(asn) ~= "number" or macAddr == nil then
    return nil
  end

  local mac = tonumber(macAddr:gsub(":", ""), 16)
  if mac == nil then
    return nil
  end

  local id = {}
  id[1] = bit32.band(
    bit32.bor(bit32.rshift(bit32.band(asn, 0xf000), 12), 0x45), 0x7fff
  )
  id[2] = bit32.rshift(bit32.band(asn, 0xff0), 4)
  id[3] = bit32.bor(bit32.band(asn, 0xf), bit32.lshift(bit32.band(mac, 0xf), 4))
  id[4] = bit32.rshift(bit32.band(mac, 0xff0), 4)
  return ("."):join(id)
end

return frr_utils
