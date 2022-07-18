#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import time

import click
from terragraph_thrift.Controller import ttypes as ctrlTypes
from terragraph_thrift.Event import ttypes as eventTypes
from tg.commands import base


levels = eventTypes.EventLevel._NAMES_TO_VALUES
categories = eventTypes.EventCategory._NAMES_TO_VALUES
event_ids = eventTypes.EventId._NAMES_TO_VALUES
level_options = "Choose from: {}".format(str(levels.keys()))
category_options = "Choose from: {}".format(str(categories.keys()))
event_id_options = "Choose from: {}".format(str(event_ids.keys()))


class EventCli(object):
    def __init__(self):
        self.event.add_command(self._add, name="add")

    @click.group()
    def event():
        """Send an event to the stats agent"""
        pass

    @click.command()
    @click.option(
        "--category",
        "-c",
        type=str,
        required=True,
        help="Category of event.\n{}".format(category_options),
    )
    @click.option(
        "--id",
        "-i",
        type=str,
        required=True,
        help="Event ID.\n{}".format(event_id_options),
    )
    @click.option(
        "--level",
        "-l",
        type=str,
        default="INFO",
        help="Severity of event.\n{}".format(level_options),
    )
    @click.option(
        "--reason",
        "-r",
        required=True,
        type=str,
        help="Explanation of the event in plaintext",
    )
    @click.option(
        "--details",
        "-d",
        type=str,
        help="JSON formatted string containing additional details",
    )
    @click.option(
        "--source",
        "-s",
        type=str,
        default="CLI",
        help="Event source (ex. process or file name)",
    )
    @click.option("--entity", "-e", type=str, help="Event entity (optional)")
    @click.option("--node-id", "-n", type=str, help="Node ID (MAC, optional)")
    @click.option("--node-name", type=str, help="Node name")
    @click.option("--topology", "-t", type=str, help="Topology name")
    @click.pass_obj
    def _add(
        cli_opts,
        category,
        id,
        level,
        reason,
        details,
        source,
        entity,
        node_id,
        node_name,
        topology,
    ):
        """Add an event"""
        EventCmd(cli_opts).add(
            category,
            id,
            level,
            reason,
            details,
            source,
            entity,
            node_id,
            node_name,
            topology,
        )


class EventCmd(base.BaseCmd):
    def __init__(self, cli_opts):
        base.BaseCmd.__init__(self, cli_opts)
        self.socket = self._connect_to_router(self._agent_host, self._agent_port)

    def add(
        self,
        category,
        id,
        level,
        reason,
        details,
        source,
        entity,
        node_id,
        node_name,
        topology,
    ):
        # create Event
        event = eventTypes.Event()
        event.source = source
        try:
            event.category = categories[category.upper()]
            event.eventId = event_ids[id.upper()]
            event.level = levels[level.upper()]
        except Exception as ex:
            self._my_exit(False, "Invalid category, id, or level argument: %s" % ex)
        event.reason = reason
        event.details = details
        event.timestamp = int(time.time())
        event.entity = entity
        event.nodeId = node_id
        event.nodeName = node_name
        event.topologyName = topology

        # send message
        data = base.serialize(
            ctrlTypes.Message(ctrlTypes.MessageType.EVENT, base.serialize(event))
        )
        try:
            self.socket.send(data)
            self.socket.close()
        except Exception as ex:
            self._my_exit(False, "Failed to send event: %s" % ex)
