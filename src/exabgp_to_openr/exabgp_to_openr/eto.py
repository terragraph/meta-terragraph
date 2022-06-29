#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import asyncio
import logging
import signal
import sys
from json import JSONDecodeError, load
from pathlib import Path
from typing import Dict, Union

import aioexabgp.announcer.fibs
import aioexabgp.announcer.healthcheck
import click
from aioexabgp.announcer import Announcer
from aioexabgp.announcer.fibs import Fib
from aioexabgp.announcer.healthcheck import gen_advertise_prefixes
from exabgp_to_openr.fibs import LinuxFib, OpenrPrefixMgr, VppFib
from exabgp_to_openr.healthcheck import OpenRChecker
from exaconf import get_zone_nodes, load_node_config


LOG = logging.getLogger(__name__)


def get_fib(fib_name: str, config: Dict) -> Fib:
    if fib_name.lower() == "vpp":
        return VppFib(config)
    elif fib_name.lower() == "openr-prefixmgr":
        return OpenrPrefixMgr(config)
    elif fib_name.lower() == "linux":
        return LinuxFib(config)

    raise NotImplementedError(f"{fib_name} is an invalid config option")


aioexabgp.announcer.fibs.get_fib = get_fib


def get_health_checker(
    checker_name: str, kwargs: Dict
) -> aioexabgp.announcer.healthcheck.HealthChecker:
    if checker_name.lower() == "openrchecker":
        return OpenRChecker(**kwargs)

    raise NotImplementedError(f"{checker_name.lower()} is not a valid option")


aioexabgp.announcer.healthcheck.get_health_checker = get_health_checker


def _handle_debug(
    ctx: click.core.Context,
    param: Union[click.core.Option, click.core.Parameter],
    debug: Union[bool, int, str],
) -> Union[bool, int, str]:
    """Turn on debugging if asked otherwise INFO default"""
    log_level = logging.DEBUG if debug else logging.INFO
    logging.basicConfig(
        format="[%(asctime)s] %(levelname)s: %(message)s (%(filename)s:%(lineno)d)",
        level=log_level,
    )
    return debug


def _load_json_config(config: str) -> Dict:
    """Generate an Announce config - We have one by default"""
    json_conf: Dict = {}
    try:
        with open(config, "r") as cfp:
            return load(cfp)
    except JSONDecodeError:
        LOG.error(f"Invalid JSON in {config}")

    return json_conf


def _signal_handler(sig_obj: signal.Signals, event: asyncio.Event) -> None:
    LOG.info(f"[signal_handler] Signaling event {event} due to {sig_obj} signal")
    event.set()


async def async_main(config: str, node_config: Path, debug: bool, dry_run: bool) -> int:
    config_json = _load_json_config(config)
    if not config_json:
        return 1
    advertise_prefixes = gen_advertise_prefixes(config_json)

    node_config_path = Path(node_config)
    if not node_config_path.exists():
        LOG.error(f"No node config exists @ {node_config_path}")
        return 2

    bgpParams, _, _, topologyInfo = load_node_config(node_config_path)
    # Run a default OpenRChecker to check for CPE prefixes to be advertised.
    openr_checker = OpenRChecker(
        prefix="::/0",
        timeout=2,
        auto_prefix_adv=bgpParams.get("cpePrefixesAutoAdvertisement", False),
        zone_nodes=get_zone_nodes(topologyInfo),
    )

    announcer = Announcer(
        config_json, advertise_prefixes, prefix_checker=openr_checker, dry_run=dry_run
    )
    if dry_run:
        LOG.info("!! DRY RUN MODE for eto !!")

    loop = asyncio.get_event_loop()
    coordinator_task = asyncio.create_task(announcer.coordinator())
    event = asyncio.Event()
    for s in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(s, _signal_handler, s, event)

    await asyncio.wait(
        [coordinator_task, event.wait()], return_when=asyncio.FIRST_COMPLETED
    )
    if event.is_set() and not coordinator_task.done():
        coordinator_task.cancel()
        await asyncio.sleep(0)  # weird, but its in the docs
        await coordinator_task  # will raise cancel exception
    return coordinator_task.result()


@click.command(context_settings={"help_option_names": ["-h", "--help"]})
@click.option(
    "-c",
    "--config",
    type=click.Path(exists=True, readable=True),
    default="/etc/aioexabgp.json",
    show_default=True,
    help="Path for aioexabgp JSON config",
)
@click.option(
    "-n",
    "--node-config",
    type=click.Path(exists=True, readable=True),
    default="/data/cfg/node_config.json",
    show_default=True,
    help="Location of node config",
)
@click.option(
    "--debug",
    is_flag=True,
    callback=_handle_debug,
    show_default=True,
    help="Turn on debug logging",
)
@click.option(
    "--dry-run",
    is_flag=True,
    show_default=True,
    help="Only log what actions would be taken and don't perform any operations",
)
@click.pass_context
def main(ctx: click.core.Context, **kwargs) -> None:
    LOG.debug(f"Starting {sys.argv[0]}")
    try:
        ctx.exit(asyncio.run(async_main(**kwargs)))
    except asyncio.CancelledError:
        LOG.info("Shutting down due to CancelledError")


if __name__ == "__main__":
    main()
