#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from subprocess import run

import click


@click.group()
def vpp():
    """ CLI with VPP helpers to remain compatibility with NSS """
    pass


@click.command(name="list", help="Show VPP FIB for respective Address Family")
@click.option("--ipv4", "-4", is_flag=True, show_default=True, help="Show IPv4 FIB")
@click.option("--timeout", "-t", default=10, show_default=True, help="Command timeout")
@click.pass_context
def list_routes(ctx: click.core.Context, ipv4: bool, timeout: float) -> None:
    cmd = ["/usr/bin/vppctl", "show", "ip6", "fib"]
    if ipv4:
        cmd[2] = "ip"
    ctx.exit(run(cmd, timeout=timeout).returncode)


vpp.add_command(list_routes)
