#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from terragraph_thrift.NodeConfig import ttypes as configTypes


NODE_CONFIG_JSON_STR = """
{
  "firewallConfig": {
    "allowEstablished": false,
    "allowICMPv6": true,
    "allowLinkLocal": true,
    "allowLoopback": true,
    "defaultPolicy": "DROP",
    "tcpPorts": "22,179",
    "udpPorts": "123"
  }
}
"""

NODE_CONFIG_THRIFT = configTypes.NodeConfig()
NODE_CONFIG_THRIFT.readFromJson(NODE_CONFIG_JSON_STR)
