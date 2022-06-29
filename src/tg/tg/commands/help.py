#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import click


@click.command()
@click.pass_context
def help(ctx):
    """ Show help for all commands """
    _print_help_recursive(ctx.parent, ctx.parent.command, [ctx.parent.info_name])


def _print_help_recursive(ctx, cmd, args):
    # Print header with args
    print("-" * 80)
    print(" " + " ".join(args))
    print("-" * 80)

    # Print help text
    formatter = ctx.make_formatter()
    with formatter.section("Description"):
        if cmd.help is not None:
            formatter.write_text(cmd.help)
    cmd.format_options(ctx, formatter)
    cmd.format_epilog(ctx, formatter)
    print(formatter.getvalue().rstrip("\n"))
    print()

    # Recurse on all subcommands
    if type(cmd) is click.core.Group:
        for subcommand in cmd.list_commands(ctx):
            _print_help_recursive(
                ctx, cmd.get_command(ctx, subcommand), args + [subcommand]
            )
