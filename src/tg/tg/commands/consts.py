#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# from E2EConsts

# --- Controller ---
IGNITION_APP_CTRL_ID = "ctrl-app-IGNITION_APP"
TOPOLOGY_APP_CTRL_ID = "ctrl-app-TOPOLOGY_APP"
STATUS_APP_CTRL_ID = "ctrl-app-STATUS_APP"
SCAN_APP_CTRL_ID = "ctrl-app-SCAN_APP"
UPGRADE_APP_CTRL_ID = "ctrl-app-UPGRADE_APP"
CONFIG_APP_CTRL_ID = "ctrl-app-CONFIG_APP"
SCHEDULER_APP_CTRL_ID = "ctrl-app-SCHEDULER_APP"
TRAFFIC_APP_CTRL_ID = "ctrl-app-TRAFFIC_APP"
BINARYSTAR_APP_CTRL_ID = "ctrl-app-BINARYSTAR_APP"

# --- Minion ---
UPGRADE_APP_MINION_ID = "minion-app-UPGRADE_APP"
DRIVER_APP_MINION_ID = "minion-app-DRIVER_APP"
STATUS_APP_MINION_ID = "minion-app-STATUS_APP"
CONFIG_APP_MINION_ID = "minion-app-CONFIG_APP"
IGNITION_APP_MINION_ID = "minion-app-IGNITION_APP"

# Cli specific consts
TG_CLI_APP_ID = "TG_CLI_APP"
MINION_APPS_SOCK_FORWARD_PREFIX = ":FWD:"

# -- Upgrade Consts --
UPGRADE_LINKUP_DAMPEN_INTERVAL = 20  # s
UPGRADE_HTTP_SERVER_PORT = 80
# We expect to find the image parameters near the beginning of the
# uprade binary - in the first few lines of the upgrade script.
UPGRADE_IMAGE_PARAM_MAX_POSITION = 1024
UPGRADE_IMAGE_LEGACY_PREAMBLE_BLOCK_SIZE = 16384

# from NMSConsts

# --- Aggregator ---
STATUS_APP_AGGR_ID = "aggr-app-STATUS_APP"
CONFIG_APP_AGGR_ID = "aggr-app-CONFIG_APP"


def byte_string_decode(msg_data):

    if isinstance(msg_data, bytes):
        msg_data = msg_data.decode("UTF-8")

    return msg_data
