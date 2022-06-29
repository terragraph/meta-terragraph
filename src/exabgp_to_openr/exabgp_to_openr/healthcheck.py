#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import asyncio
import logging
from concurrent.futures import ThreadPoolExecutor
from ipaddress import ip_address, ip_network
from typing import Any, Dict, List

from aioexabgp.announcer.healthcheck import HealthChecker, IPNetwork
from netifaces import AF_INET6, ifaddresses
from openr.cli.utils import utils
from openr.KvStore import ttypes as kv_store_types
from openr.Network import ttypes as network_types
from openr.OpenrCtrl import OpenrCtrl
from openr.Types import ttypes as openr_types
from openr.utils import ipnetwork
from openr.utils.consts import Consts
from thrift.protocol import TBinaryProtocol
from thrift.Thrift import TApplicationException
from thrift.transport import TSocket, TTransport


LOG = logging.getLogger(__name__)


class OpenRChecker(HealthChecker):
    """Hit OpenR and get it's RIB to see if an Subnet of an
    advertise route exists"""

    def __init__(
        self,
        prefix: str,
        timeout: float = 5,
        threads: int = 2,
        local_interface: str = "lo",
        zone_nodes: List[str] = None,
        auto_prefix_adv: bool = True,
    ) -> None:
        self.executor = ThreadPoolExecutor(max_workers=threads)
        self.loop = asyncio.get_event_loop()
        self.prefix = ip_network(prefix)
        self.openr_prefix = ipnetwork.ip_str_to_prefix(prefix)
        self.timeout = timeout
        self.local_interface = local_interface
        self.zone_nodes = zone_nodes
        self.auto_prefix_adv = auto_prefix_adv

    def check_local_interface(self) -> bool:
        """Check if lo has a Global Prefix within our summary route
        - OpenR Rib does not know about our own Prefix / IP"""
        for ifaddr in ifaddresses(self.local_interface)[AF_INET6]:
            # Strict False is important as we'll have host bits set
            ipnet = ip_network(
                f"{ifaddr['addr']}/{ifaddr['netmask'].split('/', 1)[1]}", strict=False
            )
            if not ipnet.is_global:
                continue
            if ipnet.network_address in self.prefix:
                LOG.debug(
                    f"Found {ipnet.network_address.compressed} is in {self.prefix}"
                )
                return True
        return False

    async def check(self) -> bool:
        """Lets check local lo interface for a subnet of our summary and only fall back to
        OpenR if it's not found locally"""
        if self.check_local_interface():
            return True

        fib_database = await self.loop.run_in_executor(self.executor, get_openr_fib_db)
        if not fib_database or not fib_database.unicastRoutes:
            LOG.error("[OpenRChecker] No FIB database unicastRoutes")
            return False

        LOG.info(
            f"[OpenRChecker] OpenR returned {len(fib_database.unicastRoutes)} route(s)"
        )
        for fib_route in fib_database.unicastRoutes:
            py_prefix_addr = ip_address(fib_route.dest.prefixAddress.addr)
            py_prefix_net = ip_network(
                f"{py_prefix_addr.compressed}/{fib_route.dest.prefixLength}",
                strict=False,
            )
            if py_prefix_net.network_address in self.prefix:
                LOG.info(
                    f"[OpenRChecker] {py_prefix_net.network_address} is in "
                    + f"{self.prefix.compressed}"
                )
                return True
        LOG.error("[OpenRChecker] No routes found in FIB")

        return False

    async def get_current_prefixes(self) -> Dict[IPNetwork, List[Any]]:
        """
        Get prefixes from Open/R to be advertized to BGP dynamically.
        Currently only CPE prefixes are retrieved from Open/R.
        """

        # If Auto prefix advertisement is disabled, only advertise
        # statically configured prefixes from node config.
        if not self.auto_prefix_adv:
            return {}

        params = kv_store_types.KeyDumpParams(Consts.PREFIX_DB_MARKER)
        params.originatorIds = []
        if self.zone_nodes:
            LOG.info(
                f"[OpenRChecker] Fetching CPE prefixes from zone nodes {self.zone_nodes}"
            )
        else:
            LOG.info("[OpenRChecker] Fetching CPE prefixes for all nodes")

        params.keyValHashes = None

        cpe_prefixes = await self.loop.run_in_executor(
            self.executor,
            get_openr_kvstore,
            params,
            network_types.PrefixType.CPE,
            self.zone_nodes,
        )

        advertise_prefixes: Dict[IPNetwork, List[Any]] = {}
        for prefix in cpe_prefixes:
            try:
                network_prefix = ip_network(prefix)
            except ValueError:
                LOG.error(f"{prefix} ignored - Invalid IP Network")
                continue
            # TODO - Add a PingChecker
            advertise_prefixes[network_prefix] = []

        return advertise_prefixes


def get_openr_kvstore(
    kwargs: Dict,
    prefix_type: network_types.PrefixType,
    nodes: List[str] = None,
    host: str = "localhost",
) -> List[str]:
    advertise_prefixes = []
    try:
        socket = TSocket.TSocket(host, Consts.CTRL_PORT)
        transport = TTransport.TBufferedTransport(socket)
        protocol = TBinaryProtocol.TBinaryProtocol(transport)

        with OpenrCtrl.Client(protocol) as orc:
            transport.open()
            resp = orc.getKvStoreKeyValsFiltered(kwargs)
            transport.close()
            prefix_maps = utils.collate_prefix_keys(resp.keyVals)

            # Node filter (nodes in same zone)
            if nodes:
                prefix_maps = {k: prefix_maps[k] for k in nodes if k in prefix_maps}

            for _, prefix_db in prefix_maps.items():
                for prefix_entry in prefix_db.prefixEntries:
                    if prefix_entry.type == prefix_type:
                        advertise_prefixes.append(
                            ipnetwork.sprint_prefix(prefix_entry.prefix)
                        )

    except TApplicationException as tae:
        LOG.error(f"openrctrl Thrift Error: {tae}")

    return advertise_prefixes


def get_openr_fib_db(host: str = "localhost") -> openr_types.RouteDatabase:
    try:
        socket = TSocket.TSocket(host, Consts.CTRL_PORT)
        transport = TTransport.TBufferedTransport(socket)
        protocol = TBinaryProtocol.TBinaryProtocol(transport)

        with OpenrCtrl.Client(protocol) as orc:
            transport.open()
            ret = orc.getRouteDb()
            transport.close()
            return ret
    except TApplicationException as tae:
        LOG.error(f"openrctrl Thrift Error: {tae}")

    return []
