#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging


LOG = logging.getLogger(__name__)


def gen_router_id(asn: int, mac: str) -> str:
    try:
        router_id = [0, 0, 0, 0]
        mac_int = int(f"0x{mac.replace(':', '')}", 16)

        # Assign router_id per
        # https://tools.ietf.org/html/draft-dupont-durand-idr-ipv6-bgp-routerid-01
        # except we are forcing the first bit to 0
        router_id[0] = (((asn & 0xF000) >> 12) | 0x45) & 0x7FFF
        router_id[1] = (0xFF0 & asn) >> 4
        router_id[2] = (0xF & asn) | ((0xF & mac_int) << 4)
        router_id[3] = (0xFF0 & mac_int) >> 4
    except Exception:
        LOG.exception(f"Error getting router_id from ASN and MAC {mac}")
        return ""

    return ".".join(map(str, router_id))
