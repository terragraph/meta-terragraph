#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

"""
Take TG Node Config and turn it into exabgp + aioexabgp announcer usable configs
"""

import argparse
import logging
import re
import string
import sys
from copy import copy
from ipaddress import IPv4Network, IPv6Network, ip_network
from json import JSONDecodeError, dump, load
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union

from .bgp import gen_router_id
from .kvstore import get_e2e_network_prefix


IPNetwork = Union[IPv4Network, IPv6Network]
LOG = logging.getLogger(__name__)
NODE_INFO_PATH = Path("/var/run/node_info")
ODICT = Optional[Dict]


BGP_NEIGHBOR_TEMPLATE = """\
neighbor $PEERADDRESS {
    inherit nt;
    peer-as $PEERAS;
    host-name $HOSTNAME;
    domain-name $DOMAINNAME;
}
"""


def _handle_debug(debug: bool) -> bool:
    """Turn on debugging if asked otherwise INFO default"""
    log_level = logging.DEBUG if debug else logging.INFO
    logging.basicConfig(
        format="[%(asctime)s] %(levelname)s: %(message)s (%(filename)s:%(lineno)d)",
        level=log_level,
    )
    return debug


def _load_json(json_file_path: Path) -> Dict:
    json = {}
    try:
        with json_file_path.open("r") as fp:
            json = load(fp)
    except (OSError, JSONDecodeError):
        LOG.exception(f"Unable to load/decode {json_file_path}")
    return json


def _valid_network_prefix(prefix: str) -> Optional[IPNetwork]:
    try:
        return ip_network(prefix)
    except ValueError:
        return None


def _write_json_config(json: Dict, json_file_path: Path) -> bool:
    try:
        with json_file_path.open("w") as fp:
            dump(json, fp, indent=2, sort_keys=True)
    except (OSError, ValueError):
        LOG.exception(f"Unable to dump JSON to {json_file_path}")
        return False
    return True


def load_node_config(e2e_config_path: Path) -> Tuple[ODICT, ODICT, ODICT, ODICT]:
    """Load node config from json and returns bgpParams, envParams,
    popParams and topologyInfo as a tuple."""

    e2e_config = _load_json(e2e_config_path)
    if not e2e_config_path:
        return (None, None, None)

    return (
        e2e_config.get("bgpParams"),
        e2e_config.get("envParams"),
        e2e_config.get("popParams"),
        e2e_config.get("topologyInfo"),
    )


def get_dp_mode(envParams: Dict) -> str:
    """Get DP mode from node config."""

    # Config missing - default to linux.
    if "DPDK_ENABLED" not in envParams:
        LOG.error("Missing 'envParams.DPDK_ENABLED' - Defaulting to linux")
        return "linux"
    elif envParams["DPDK_ENABLED"] == "0":
        return "linux"
    else:
        return "vpp"


def get_zone_nodes(topologyInfo: Dict) -> List[str]:
    """Get list of nodes from the same zone as the POP node,
    if Deterministic Prefix allocation (DPA) is used."""
    if not topologyInfo:
        return []
    zoneNodes = topologyInfo.get("zoneNodes")
    return zoneNodes.split(",") if zoneNodes else []


def generate_aioexabgp_config(
    bgpParams: Dict,
    envParams: Dict,
    popParams: Dict,
    topologyInfo: Dict,
    aioexabgp_template: Path,
    output_path: Path,
) -> int:
    """Generate route specific ExaBGP config and store it. Some info is
    advertized in BGP messages like prefixes and nexthops."""

    default_checker_timeout = 2  # seconds
    dp_mode: str = get_dp_mode(envParams)
    # We need to program the VPP or Linux Fib + Redistribute into OpenR for other Nodes
    default_tg_fibs = ["openr-prefixmgr"]
    default_tg_fibs.append(dp_mode)
    LOG.info(f"FIBs are {default_tg_fibs}")

    if not aioexabgp_template.exists():
        LOG.error(f"{aioexabgp_template} aioexabgp template does not exist")
        return 20

    aioexabgp_conf_path = output_path / "aioexabgp.conf"
    aioexabgp_conf_staged_path = output_path / ".aioexabgp.conf"

    aioexabgp_json = _load_json(aioexabgp_template)
    if not aioexabgp_json:
        return 21

    # Get our OpenR set management prefix if it exists
    e2e_site_prefix = get_e2e_network_prefix()
    LOG.debug(f"Obtained e2e site prefix of {e2e_site_prefix} from KvStore")

    if (
        "specificNetworkPrefixes" not in bgpParams
        or not bgpParams["specificNetworkPrefixes"]
    ) and not e2e_site_prefix:
        LOG.error(
            "We do not have any e2e site prefixes to advertise. "
            + f"Generating {aioexabgp_conf_path} failed"
        )
        return 22

    # Get next hop based on vpp vs kernel mode
    next_hop = ""
    if dp_mode == "vpp":
        if "VPP_ADDR" not in popParams:
            LOG.error(
                "Missing 'popParams.VPP_ADDR', need VPP address to set as "
                + "next hop for BGP advertised routes"
            )
            return 23
        next_hop = popParams["VPP_ADDR"]
    # Linux kernel DP + FIB - use nic1/POP_ADDR
    else:
        next_hop = popParams["POP_ADDR"]

    advertise_prefixes: List[Union[IPNetwork, str]] = []
    if not e2e_site_prefix:
        LOG.debug("We do not have a e2e Site Prefix to advertise")
    else:
        advertise_prefixes.append(e2e_site_prefix)

    if "specificNetworkPrefixes" in bgpParams:
        advertise_prefixes.extend(bgpParams["specificNetworkPrefixes"].split(","))

    if "delegatedNetworkPrefixes" in bgpParams:
        advertise_prefixes.extend(bgpParams["delegatedNetworkPrefixes"].split(","))

    # Statically configured CPE prefixes
    if "cpeNetworkPrefix" in bgpParams:
        advertise_prefixes.append(bgpParams["cpeNetworkPrefix"])

    # Dynamic advertisement of CPE prefixes as well.
    auto_prefix_adv = bgpParams.get("cpePrefixesAutoAdvertisement", False)
    zone_nodes = get_zone_nodes(topologyInfo)

    # Set Prefixes to advertise and checkers to run on them
    # Today we check for a subnet of the summary routes in the OpenR RIB before we advertise
    prefixes = {}
    for network in advertise_prefixes:
        if isinstance(network, str) and not _valid_network_prefix(network):
            LOG.error(
                f"{network} is not a valid network prefix. Not adding to advertise prefixes"
            )
            return 24

        prefixes[str(network)] = [
            {
                "class": "OpenRChecker",
                "kwargs": {
                    "prefix": str(network),
                    "timeout": default_checker_timeout,
                    "auto_prefix_adv": auto_prefix_adv,
                    "zone_nodes": zone_nodes,
                },
            }
        ]
    if not prefixes:
        LOG.error("No advertise prefixes. BGP has nothing to announce")
        return 25
    aioexabgp_json["advertise"]["prefixes"] = prefixes
    aioexabgp_json["advertise"]["next_hop"] = next_hop

    # Lets default to withdraw routes on exit
    aioexabgp_json["advertise"]["withdraw_on_exit"] = True

    # Set FIBs to program learned BGP routes into
    aioexabgp_json["learn"]["fibs"] = default_tg_fibs
    aioexabgp_json["learn"]["allow_default"] = True
    aioexabgp_json["learn"]["allow_non_default"] = bgpParams.get(
        "allowNonDefaultRoutes", False
    )
    aioexabgp_json["learn"]["allow_ll_nexthop"] = False

    # Write out to temporary file then rename to prod location on success
    if _write_json_config(aioexabgp_json, aioexabgp_conf_staged_path):
        aioexabgp_conf_staged_path.rename(aioexabgp_conf_path)
    else:
        return 26

    LOG.info(f"Successfully generated aioexabgp config to {aioexabgp_conf_path}")
    return 0


def generate_exabgp_config(
    bgpParams: Dict, popParams: Dict, exabgp_template_path: Path, output_path: Path
) -> int:
    """Generate general ExaBGP config and store it."""
    if not exabgp_template_path.exists():
        LOG.error(f"ExaBGP template {exabgp_template_path} does not exist")
        return 30

    node_mac = get_mac_address()
    if not node_mac:
        return 31

    with exabgp_template_path.open("r") as etfp:
        exabgp_template = etfp.read()

    # RFC 4271 - hold time = 3 x keep alive. The RFC defaults
    # are 30 seconds for keep alive but 90 seconds is way too
    # long for TG network to re-route traffic. Reducing defaults
    # to 10 seconds. This is configurable via node config.
    exabgp_conf_data = {
        "HOLDTIME": bgpParams.get("keepalive", 10) * 3,
        "LOCAL_ADDRESS": popParams["POP_ADDR"],
        "LOCAL_ASN": bgpParams["localAsn"],
        "NEIGHBORS": generate_exabgp_config_neighbors(bgpParams["neighbors"]),
        "ROUTE_PROCESS_CMD": f"/usr/bin/exabgp-to-openr -c {output_path}/aioexabgp.conf",
        "ROUTERID": gen_router_id(bgpParams["localAsn"], node_mac),
    }

    # Work out if we have a md5-password to set
    if "md5Password" in bgpParams:
        exabgp_conf_data["MD5PASSWORD"] = bgpParams["md5Password"]
    else:
        exabgp_template = re.sub(r"\s*md5-password.*\n", "\n", exabgp_template, count=1)

    exabgp_template_content = string.Template(exabgp_template)
    exabgp_rendered_conf = exabgp_template_content.substitute(exabgp_conf_data)

    exabgp_conf_path = output_path / "exabgp.conf"
    exabgp_conf_staged_path = output_path / ".exabgp.conf"
    try:
        with exabgp_conf_staged_path.open("w") as ecsfp:
            ecsfp.write(exabgp_rendered_conf)
        exabgp_conf_staged_path.rename(exabgp_conf_path)
    except OSError:
        LOG.exception(f"Unable to write out {exabgp_conf_path}")
        return 32

    LOG.info(f"Successfully generated exabgp config to {exabgp_conf_path}")
    return 0


def generate_exabgp_config_neighbors(neighbors: Dict[str, Dict]) -> str:
    neighbors_conf = ""

    for neighbor in neighbors.values():
        bgp_neighbor_template = copy(BGP_NEIGHBOR_TEMPLATE)
        neighbor_template_data = {
            "PEERADDRESS": neighbor["ipv6"],
            "PEERAS": neighbor["asn"],
        }

        for optional_val, regex in (
            ("domainname", r"\s*domain-name.*\n"),
            ("hostname", r"\s*host-name.*\n"),
        ):
            if optional_val in neighbor:
                neighbor_template_data[optional_val.upper()] = neighbor[optional_val]
            else:
                bgp_neighbor_template = re.sub(
                    regex, "\n", bgp_neighbor_template, count=1
                )

        neighbor_template_content = string.Template(bgp_neighbor_template)
        neighbors_conf += neighbor_template_content.substitute(neighbor_template_data)

    return neighbors_conf


def generate_configs(args: argparse.Namespace, node_config_path: Path) -> int:
    return_value = 0
    output_path = Path(args.output_dir)
    if not output_path.exists():
        output_path.mkdir(parents=True)
        LOG.debug(f"mkdir {output_path} completed")

    bgpParams, envParams, popParams, topologyInfo = load_node_config(node_config_path)
    if not bgpParams or not popParams or not envParams:
        LOG.error("Invalid node config for a POP. Bad BGP, POP or env params")
        return 10

    return_value += generate_aioexabgp_config(
        bgpParams,
        envParams,
        popParams,
        topologyInfo,
        Path(args.aioexabgp_template),
        output_path,
    )
    return_value += generate_exabgp_config(
        bgpParams, popParams, Path(args.exabgp_template), output_path
    )

    return return_value


def get_mac_address(node_info_path: Path = NODE_INFO_PATH) -> str:
    if not node_info_path.exists():
        LOG.error(f"{node_info_path} does not exist. Can't get Node MAC address")
        return ""

    with node_info_path.open("r") as nifp:
        node_info = nifp.read()

    for line in node_info.splitlines():
        if "NODE_ID=" in line:
            return line.split('"')[1].lower()

    return ""


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate ExaBGP configs for Terragraph Devices",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "-a",
        "--aioexabgp-template",
        default="/etc/exabgp/aioexabgp.template",
        help="Location of aioexabgp template",
    )
    parser.add_argument(
        "-d", "--debug", action="store_true", help="Verbose debug output"
    )
    parser.add_argument(
        "-e",
        "--exabgp-template",
        default="/etc/exabgp/exabgp.conf.template",
        help="Location of exabgp template",
    )
    parser.add_argument(
        "-n",
        "--node-config",
        default="/data/cfg/node_config.json",
        help="Location of node config",
    )
    parser.add_argument(
        "-o",
        "--output-dir",
        default="/data/etc/exabgp",
        help="Path to save generated config files",
    )
    args = parser.parse_args()
    _handle_debug(args.debug)

    node_config_path = Path(args.node_config)
    if not node_config_path.exists():
        LOG.error(f"No node config exists @ {node_config_path}")
        return 1

    return generate_configs(args, node_config_path)  # pragma: no cover


if __name__ == "__main__":
    sys.exit(main())  # pragma: no cover
