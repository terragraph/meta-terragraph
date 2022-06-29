#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import asyncio
import logging
from concurrent.futures import ThreadPoolExecutor
from ipaddress import IPv6Network, ip_network
from typing import Any, Dict, List, Optional, Set

from aioexabgp.announcer.fibs import Fib, FibOperation, IPAddress, IPNetwork
from openr.Network import ttypes as network_types
from openr.OpenrConfig import ttypes as openr_config_types
from openr.OpenrCtrl import OpenrCtrl
from openr.OpenrCtrl.ttypes import OpenrError
from openr.Platform import FibService, ttypes as platform_types
from openr.Types import ttypes as openr_types
from openr.utils import ipnetwork
from openr.utils.consts import Consts
from thrift.protocol import TBinaryProtocol
from thrift.Thrift import TApplicationException
from thrift.transport import TSocket, TTransport
from thrift.transport.TTransport import TTransportException


LOG = logging.getLogger(__name__)

# Client ID maps eventually a preference value. Open\R expects
# BGP routes with ClientID of 0 which maps to a preference of 20
# which is higher than Open\R route's preference of 10).
BGP_CLIENT_ID = 0

# IPv4 and IPv6 default route constants
DEFAULT_V4 = ip_network("0.0.0.0/0")
DEFAULT_V6 = ip_network("::/0")
DEFAULTS = (DEFAULT_V4, DEFAULT_V6)


class OpenrPrefixMgr(Fib):
    """Inform prefix manager to advertise / withdraw (redistribute) BGP
    learnt prefixes into OpenR"""

    def __init__(self, config: Dict, timeout: float = 2.0) -> None:
        super().__init__(config, timeout)
        self.FIB_NAME = "OpenrPrefixMgr"

        self.loop = asyncio.get_event_loop()
        self.threads = config["learn"].get("threads", 4)
        self.executor = ThreadPoolExecutor(max_workers=self.threads)
        self.allow_non_default = config["learn"].get("allow_non_default", False)
        self.bgp_learnt_prefixes: Dict[IPNetwork, Set[IPAddress]] = {}

    async def add_route(self, prefix: IPNetwork, next_hop: IPAddress) -> bool:
        if not await super().add_route(prefix, next_hop):
            return False

        if not self.allow_non_default and prefix not in DEFAULTS:
            LOG.info(
                f"[{self.FIB_NAME}] Ignoring non-default {prefix.compressed} route"
            )
            return False

        advertise_args = {
            "prefixes": [
                openr_types.PrefixEntry(
                    forwardingType=openr_config_types.PrefixForwardingType.IP,
                    prefix=ipnetwork.ip_str_to_prefix(prefix.exploded),
                    type=network_types.PrefixType.BREEZE,
                )
            ]
        }
        LOG.info(
            f"[{self.FIB_NAME}] Advertising learned {prefix.compressed} prefix into Open/R"
        )
        try:
            await self.loop.run_in_executor(
                self.executor,
                openr_prefixmgr_client,
                "advertisePrefixes",
                advertise_args,
            )
        except OpenrError as oe:
            LOG.error(
                f"Unable to advertise {advertise_args['prefixes']} prefixes: {oe}"
            )
            return False

        # if added, remember the prefix+nexthop
        if prefix not in self.bgp_learnt_prefixes:
            self.bgp_learnt_prefixes[prefix] = {next_hop}
        else:

            self.bgp_learnt_prefixes[prefix].add(next_hop)
        return True

    async def check_for_route(self, prefix: IPNetwork, next_hop: IPAddress) -> bool:
        LOG.info(
            f"[{self.FIB_NAME}] Fetching all {network_types.PrefixType.BREEZE} routes"
            + f"to look for {prefix.compressed}"
        )
        wanted_route = openr_types.PrefixEntry(
            forwardingType=openr_config_types.PrefixForwardingType.IP,
            prefix=ipnetwork.ip_str_to_prefix(prefix.exploded),
            type=network_types.PrefixType.BREEZE,
        )
        routes = await self.loop.run_in_executor(
            self.executor,
            openr_prefixmgr_client,
            "getPrefixesByType",
            {"prefixType": network_types.PrefixType.BREEZE},
        )
        if not routes:
            LOG.error(
                f"{self.FIB_NAME} No {network_types.PrefixType.BREEZE} routes returned"
            )
            if next_hop in self.bgp_learnt_prefixes[prefix]:
                self.bgp_learnt_prefixes[prefix].remove(next_hop)
            return False

        if wanted_route in routes:
            if next_hop not in self.bgp_learnt_prefixes[prefix]:
                # local memory is out-of-sync, fix it
                if prefix not in self.bgp_learnt_prefixes:
                    self.bgp_learnt_prefixes[prefix] = {next_hop}
                else:
                    self.bgp_learnt_prefixes[prefix].add(next_hop)
        else:
            # no entry for prefix in open/r
            if next_hop in self.bgp_learnt_prefixes[prefix]:
                self.bgp_learnt_prefixes[prefix].remove(next_hop)
            return False

        return True

    async def del_route(self, prefix: IPNetwork, next_hop: IPAddress) -> bool:

        if next_hop not in self.bgp_learnt_prefixes[prefix]:
            LOG.info(
                f"[OpenrPrefixMgr] del_routes did not find {prefix} via {next_hop}"
            )
            return False

        if len(self.bgp_learnt_prefixes[prefix]) > 1:
            # we have another path in BGP table, so keep it in open/r
            self.bgp_learnt_prefixes[prefix].remove(next_hop)
            LOG.info(
                f"[OpenrPrefixMgr] del_routes virtually deleting {prefix} via {next_hop}"
            )
            return True

        advertise_args = {
            "prefixes": [
                openr_types.PrefixEntry(
                    forwardingType=openr_config_types.PrefixForwardingType.IP,
                    prefix=ipnetwork.ip_str_to_prefix(prefix.exploded),
                    type=network_types.PrefixType.BREEZE,
                )
            ]
        }
        LOG.info(f"[{self.FIB_NAME}] Withdrawing {prefix.compressed} from OpenR")
        try:
            await self.loop.run_in_executor(
                self.executor,
                openr_prefixmgr_client,
                "withdrawPrefixes",
                advertise_args,
            )
        except OpenrError as oe:
            LOG.error(f"Unable to withdraw {advertise_args['prefixes']} prefixes: {oe}")
            return False

        self.bgp_learnt_prefixes[prefix].remove(next_hop)
        LOG.info(f"[OpenrPrefixMgr] remove type BREEZE prefix {prefix} from open/r")
        return True

    async def del_all_routes(self, next_hop: IPAddress) -> bool:
        removed = 0
        for prefix in self.bgp_learnt_prefixes:
            if next_hop in self.bgp_learnt_prefixes[prefix]:
                # prefix_type is next-hop btw
                LOG.debug(
                    f"[OpenrPrefixMgr] del_all_route deleting {prefix} with next_hop",
                    f" {next_hop} with del_route()",
                )
                if await self.del_route(prefix, next_hop):
                    removed += 1
        if removed == 0:
            # no routes removed
            LOG.debug("[OpenrPrefixMgr] del_all_routes found no matching routes")
            return False
        LOG.info(f"[OpenrPrefixMgr] del_all_routes removed {removed} prefixes")
        return True


class PlatformBaseFib(Fib):
    """Base Platform FIB for programming BGP routes."""

    def __init__(
        self,
        config: Dict,
        fibName: str,
        fibService: FibService,
        clientId: int = BGP_CLIENT_ID,
        timeout: float = 2.0,
    ) -> None:
        super().__init__(config, timeout)
        self.clientId = clientId
        self.FIB_NAME = fibName
        self.fibService = fibService
        self.loop = asyncio.get_event_loop()
        self.threads = config["learn"].get("threads", 4)
        self.executor = ThreadPoolExecutor(max_workers=self.threads)
        self.allow_non_default = config["learn"].get("allow_non_default", False)
        self.bgp_learnt_prefixes: Dict[IPNetwork, Set[IPAddress]] = {}

    def track_learnt_routes(
        self,
        next_hop: IPAddress,
        fib_operation: FibOperation,
        prefix: Optional[IPNetwork] = None,
    ) -> None:
        """Keep track of learnt BGP prefixes from all active BGP peers as a
        map of prefix to next-hops."""

        LOG.debug(
            f"[{self.FIB_NAME}][track_learnt_routes] Updating learnt BGP prefixes"
        )
        if fib_operation == FibOperation.ADD_ROUTE:
            if not next_hop:
                LOG.error(
                    f"[{self.FIB_NAME}] Received a learnt route with no nexthop: {prefix}"
                )
            else:
                if prefix not in self.bgp_learnt_prefixes:
                    self.bgp_learnt_prefixes[prefix] = {next_hop}
                else:
                    self.bgp_learnt_prefixes[prefix].add(next_hop)

                LOG.info(
                    f"[{self.FIB_NAME}][track_learnt_routes] Added {prefix} from {next_hop}"
                )

        elif fib_operation == FibOperation.REMOVE_ROUTE:
            if not next_hop:
                LOG.error(
                    f"[{self.FIB_NAME}][track_learnt_routes] {next_hop} next hop not found"
                    + f" in current {prefix} prefix. Not deleted."
                )
            elif prefix not in self.bgp_learnt_prefixes:
                LOG.error(
                    f"[track_learnt_routes] {prefix} not found in BGP Learnt "
                    + "Prefixes - Not deleted"
                )
            else:
                LOG.info(
                    f"[{self.FIB_NAME}][track_learnt_routes] Removing {prefix} from {next_hop}"
                )
                self.bgp_learnt_prefixes[prefix].remove(next_hop)

        elif fib_operation == FibOperation.REMOVE_ALL_ROUTES:
            for prefix, cur_next_hops in self.bgp_learnt_prefixes.items():
                if next_hop in cur_next_hops:
                    cur_next_hops.remove(next_hop)

                LOG.info(
                    f"[{self.FIB_NAME}][track_learnt_routes] Removing route {prefix}"
                    + f" from {next_hop}"
                )

        LOG.info(
            f"[{self.FIB_NAME}] New learnt prefixes are {self.bgp_learnt_prefixes}"
        )

    def get_current_next_hops(self, prefix: IPNetwork) -> Set[IPAddress]:
        return self.bgp_learnt_prefixes.get(prefix, set())

    async def _get_all_fib_routes(self) -> List[network_types.UnicastRoute]:
        current_unicast_routes: List[network_types.UnicastRoute] = []
        try:
            current_unicast_routes = await self.loop.run_in_executor(
                self.executor,
                fib_client_run,
                self.fibService,
                "getRouteTableByClient",
                {"clientId": self.clientId},
            )
        except platform_types.PlatformError as pe:
            LOG.error(f"Unable to retrieve all FIB routes: {pe}")

        LOG.debug(f"[{self.FIB_NAME}] Current Unicast Routes {current_unicast_routes}")
        return current_unicast_routes

    async def add_route(self, prefix: IPNetwork, next_hop: IPAddress) -> bool:
        if not await super().add_route(prefix, next_hop):
            return False

        if not self.allow_non_default and prefix not in DEFAULTS:
            LOG.info(
                f"[{self.FIB_NAME}] Ignoring non-default {prefix.compressed} route"
            )
            return False

        self.track_learnt_routes(
            next_hop=next_hop,
            fib_operation=FibOperation.ADD_ROUTE,
            prefix=prefix,
        )

        # Get all active next-hops for this prefix and program to FIB simultaneously
        # to take advantage of multiple ECMP routes.
        current_next_hops: Set[IPAddress] = self.get_current_next_hops(prefix)

        add_args = {
            "clientId": self.clientId,
            "route": network_types.UnicastRoute(
                dest=ipnetwork.ip_str_to_prefix(prefix.exploded),
                nextHops=[
                    network_types.NextHopThrift(
                        address=ipnetwork.ip_str_to_addr(next_hop.exploded)
                    )
                    for next_hop in current_next_hops
                ],
            ),
        }

        LOG.info(
            f"[{self.FIB_NAME}] Adding {prefix.compressed} prefix via {current_next_hops}"
        )
        try:
            await self.loop.run_in_executor(
                self.executor,
                fib_client_run,
                self.fibService,
                "addUnicastRoute",
                add_args,
            )
        except platform_types.PlatformError as pe:
            LOG.error(
                f"Unable to add route for {prefix.compressed} via {next_hop.compressed}: {pe}"
            )
            return False

        return True

    async def check_for_route(self, prefix: IPNetwork, next_hop: IPAddress) -> bool:
        wanted_prefix = network_types.UnicastRoute(
            dest=ipnetwork.ip_str_to_prefix(prefix.exploded),
            nextHops=[
                network_types.NextHopThrift(
                    address=ipnetwork.ip_str_to_addr(next_hop.exploded)
                )
            ],
        )
        LOG.info(
            f"[{self.FIB_NAME}] Looking for {prefix.compressed} via {next_hop.compressed}"
        )
        current_unicast_routes = await self._get_all_fib_routes()
        if not current_unicast_routes:
            LOG.error(f"[{self.FIB_NAME}] returns no FIB Routes")
            return False

        for unicast_route in current_unicast_routes:
            if unicast_route == wanted_prefix:
                return True

        return False

    async def del_all_routes(self, next_hop: IPAddress) -> bool:
        LOG.error(
            f"[{self.FIB_NAME}] Attempting to delete all routes via {next_hop.compressed}"
        )
        current_unicast_routes = await self._get_all_fib_routes()
        if not current_unicast_routes:
            LOG.error(f"[{self.FIB_NAME}] returns no FIB Routes")
            return False

        prefixes_to_del: List[network_types.IpPrefix] = []
        next_hop_addr = ipnetwork.ip_str_to_addr(next_hop.exploded)

        for fib_route in current_unicast_routes:
            for fib_next_hop in fib_route.nextHops:
                if next_hop_addr.addr == fib_next_hop.address.addr:
                    prefixes_to_del.append(fib_route.dest)

        if not prefixes_to_del:
            LOG.info(f"[{self.FIB_NAME}] del_all_routes found no routes to delete")
            return False

        # Delete the tracked routes from this peer.
        self.track_learnt_routes(
            next_hop=next_hop,
            fib_operation=FibOperation.REMOVE_ALL_ROUTES,
        )
        LOG.debug(
            f"[{self.FIB_NAME}] del_all_routes found routes to delete {[prefixes_to_del]}"
        )

        for fib_prefix in prefixes_to_del:

            prefix: IPNetwork = IPv6Network(ipnetwork.sprint_prefix(fib_prefix))
            # Use add route to maintain other existing next-hops and delete
            # this peer's next-hop.
            current_next_hops: Set[IPAddress] = self.get_current_next_hops(prefix)

            LOG.info(
                f"[{self.FIB_NAME}] Deleting {prefix} via {next_hop.compressed}"
                + f" with {len(current_next_hops)} existing next hops."
            )

            add_args = {
                "clientId": self.clientId,
                "route": network_types.UnicastRoute(
                    dest=ipnetwork.ip_str_to_prefix(prefix.exploded),
                    nextHops=[
                        network_types.NextHopThrift(
                            address=ipnetwork.ip_str_to_addr(next_hop.exploded)
                        )
                        for next_hop in current_next_hops
                    ],
                ),
            }

            try:
                await self.loop.run_in_executor(
                    self.executor,
                    fib_client_run,
                    self.fibService,
                    "addUnicastRoute",
                    add_args,
                )
            except platform_types.PlatformError as pe:
                LOG.error(
                    "Delete all routes: Unable to add existing route for "
                    + f"{prefix.compressed} via {next_hop.compressed}: {pe}"
                )
                return False

        return True

    async def del_route(self, prefix: IPNetwork, next_hop: IPAddress) -> bool:

        self.track_learnt_routes(
            next_hop=next_hop,
            fib_operation=FibOperation.REMOVE_ROUTE,
            prefix=prefix,
        )
        # Use add route to maintain other existing next-hops and delete
        # this peer's next-hop.
        current_next_hops: Set[IPAddress] = self.get_current_next_hops(prefix)

        LOG.info(
            f"[{self.FIB_NAME}] Deleting {prefix} via {next_hop.compressed}"
            + f" with {len(current_next_hops)} existing next hops."
        )

        add_args = {
            "clientId": self.clientId,
            "route": network_types.UnicastRoute(
                dest=ipnetwork.ip_str_to_prefix(prefix.exploded),
                nextHops=[
                    network_types.NextHopThrift(
                        address=ipnetwork.ip_str_to_addr(next_hop.exploded)
                    )
                    for next_hop in current_next_hops
                ],
            ),
        }

        try:
            await self.loop.run_in_executor(
                self.executor,
                fib_client_run,
                self.fibService,
                "addUnicastRoute",
                add_args,
            )
        except platform_types.PlatformError as pe:
            LOG.error(
                "Delete route: Unable to add existing route for "
                + f"{prefix.compressed} via {next_hop.compressed}: {pe}"
            )
            return False

        return True


class VppFib(PlatformBaseFib):
    """VPP FIB for programming all BGP learnt routes in VPP mode."""

    def __init__(
        self, config: Dict, clientId: int = BGP_CLIENT_ID, timeout: float = 2.0
    ) -> None:
        super().__init__(
            config, "VppFib", FibService, clientId=clientId, timeout=timeout
        )


class LinuxFib(PlatformBaseFib):
    """Linux Kernel FIB for programming all BGP learnt routes in kernel mode."""

    def __init__(
        self, config: Dict, clientId: int = BGP_CLIENT_ID, timeout: float = 2.0
    ) -> None:
        super().__init__(
            config, "LinuxFib", FibService, clientId=clientId, timeout=timeout
        )


def fib_client_run(
    service: FibService,
    method_name: str,
    kwargs: Dict,
    host: str = "localhost",
    port: int = Consts.FIB_AGENT_PORT,
    timeout: float = 5,
) -> Any:
    """Function to allow wrapping of blocking thrift API calls into threads"""
    try:
        socket = TSocket.TSocket(host, port)
        socket.setTimeout(int(timeout * 1000))
        transport = TTransport.TBufferedTransport(socket)
        protocol = TBinaryProtocol.TBinaryProtocol(transport)

        with service.Client(protocol) as fib_client:
            fib_call = getattr(fib_client, method_name)
            if not fib_call:
                raise platform_types.PlatformError(f"{method_name} does not exist!")
            LOG.debug(f"[fib_client_run] Calling {method_name} with {kwargs} args")
            transport.open()
            ret = fib_call(**kwargs)
            transport.close()
            return ret

    except (TApplicationException, TTransportException) as te:
        LOG.exception(f"fib_client_run Thrift Error: {te}")

    return None


def openr_prefixmgr_client(
    method_name: str, kwargs: Dict, host: str = "localhost"
) -> Any:
    """Function to allow wrapping of blocking thrift API calls into threads"""
    try:
        socket = TSocket.TSocket(host, Consts.CTRL_PORT)
        transport = TTransport.TBufferedTransport(socket)
        protocol = TBinaryProtocol.TBinaryProtocol(transport)

        with OpenrCtrl.Client(protocol) as or_client:
            openr_call = getattr(or_client, method_name)
            if not openr_call:
                raise OpenrError(f"{method_name} does not exist!")
            LOG.debug(
                f"[openr_prefixmgr_client] Calling {method_name} with {kwargs} args"
            )
            transport.open()
            ret = openr_call(**kwargs)
            transport.close()
            return ret

    except (TApplicationException, TTransportException) as te:
        LOG.exception(f"openrctrl Thrift Error: {te} ({type(te)})")

    return None
