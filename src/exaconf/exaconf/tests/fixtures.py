#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from ipaddress import ip_network
from typing import Any, NamedTuple

from openr.KvStore import ttypes as kv_store_types


EXPECTED_AIOEXABGP_CONF = {
    "conf_version": "0.0.4",
    "advertise": {
        "interval": 5.0,
        "next_hop": "69::3",
        "prefixes": {
            "1469:1280::/57": [
                {
                    "class": "OpenRChecker",
                    "kwargs": {"prefix": "1469:1280::/57", "timeout": 2},
                }
            ],
            "1469::/54": [
                {
                    "class": "OpenRChecker",
                    "kwargs": {"prefix": "1469::/54", "timeout": 2},
                }
            ],
            "2400:69::/48": [
                {
                    "class": "OpenRChecker",
                    "kwargs": {"prefix": "2400:69::/48", "timeout": 2},
                }
            ],
        },
        "withdraw_on_exit": True,
    },
    "learn": {
        "allow_default": True,
        "allow_non_default": False,
        "allow_ll_nexthop": False,
        "fibs": ["openr-prefixmgr", "vpp"],
        "filter_prefixes": [],
        "prefix_limit": 0,
    },
}

EXPECTED_EXABGP_CONF = """\
process all_peer_events {
    run /usr/bin/exabgp-to-openr -c /etc/aioexabgp.conf;
    encoder json;
}

template {
    neighbor nt {
        local-address 69::2;
        local-as 65070;
        router-id 79.226.30.112;
        hold-time 90;
        api all {
            processes [all_peer_events];
            neighbor-changes;
            receive {
                parsed;
                update;
            }
            send {
                parsed;
                update;
            }
        }
        capability {
            asn4 enable;
            route-refresh enable;
            graceful-restart 30;
        }
        family v6_only {
            ipv6 unicast;
        }
    }
}

# Example Peer Template - exaconf defines these per neighbor
# neighbor PEERADDRESS {
#    inherit nt;
#    peer-as PEERAS;
# }

neighbor 69::1 {
    inherit nt;
    peer-as 65069;
}
neighbor 69::2 {
    inherit nt;
    peer-as 65069;
}

"""

EXPECTED_EXABGP_MD5_CONF = """\
process all_peer_events {
    run /usr/bin/exabgp-to-openr -c /etc/aioexabgp.conf;
    encoder json;
}

template {
    neighbor nt {
        local-address 69::2;
        local-as 65070;
        router-id 79.226.30.112;
        hold-time 90;
        md5-password MD5PASS;
        api all {
            processes [all_peer_events];
            neighbor-changes;
            receive {
                parsed;
                update;
            }
            send {
                parsed;
                update;
            }
        }
        capability {
            asn4 enable;
            route-refresh enable;
            graceful-restart 30;
        }
        family v6_only {
            ipv6 unicast;
        }
    }
}

# Example Peer Template - exaconf defines these per neighbor
# neighbor PEERADDRESS {
#    inherit nt;
#    peer-as PEERAS;
# }

neighbor 69::1 {
    inherit nt;
    peer-as 65069;
}
neighbor 69::2 {
    inherit nt;
    peer-as 65069;
}

"""

EXPECTED_EXABGP_NEIGHBOR_CONF = """\
neighbor 69::1 {
    inherit nt;
    peer-as 65069;
}
neighbor 69::2 {
    inherit nt;
    peer-as 65069;
}
"""

EXPECTED_EXABGP_NEIGHBOR_HOSTNAME_CONF = """\
neighbor 69::1 {
    inherit nt;
    peer-as 65069;
    host-name router0;
    domain-name network.com;
}
neighbor 69::2 {
    inherit nt;
    peer-as 65069;
    host-name router1;
    domain-name network.com;
}
"""

FAKE_E2E_SITE_PREFIX = ip_network("2400:69::/48")


# NamedTuple to pretend to be a thriftVal
class ThriftVal(NamedTuple):
    value: bytes


FAKE_KV_RETURN = kv_store_types.Publication(
    keyVals={"e2e-network-prefix": ThriftVal(b"2400:69::/48,64")}
)
FAKE_KV_RETURN_EMPTY = kv_store_types.Publication(
    keyVals={"e2e-network-prefix": ThriftVal(b"")}
)


class FakeOpenRClient:
    def __init__(self, *args, **kwargs) -> None:
        pass

    def __enter__(self, *args, **kwargs) -> Any:
        return self

    def __exit__(self, *args, **kwargs) -> None:
        pass

    def getKvStoreKeyValsFiltered(self, *args, **kwargs) -> kv_store_types.Publication:
        return FAKE_KV_RETURN


class FakeOpenRClientEmpty(FakeOpenRClient):
    def getKvStoreKeyValsFiltered(self, *args, **kwargs) -> kv_store_types.Publication:
        return FAKE_KV_RETURN_EMPTY


FAKE_NODE_CONFIG = {
    "bgpParams": {
        "localAsn": 65070,
        "neighbors": {
            "0": {"asn": 65069, "ipv6": "69::1"},
            "1": {"asn": 65069, "ipv6": "69::2"},
        },
        "specificNetworkPrefixes": "1469::/54,1469:1280::/57",
    },
    "envParams": {"DPDK_ENABLED": "1"},
    "NotImportant": {},
    "popParams": {
        "GW_ADDR": "69::1",
        "POP_ADDR": "69::2",
        "POP_BGP_ROUTING": "1",
        "POP_IFACE": "TenGigabitEthernet0",
        "POP_STATIC_ROUTING": "0",
        "VPP_ADDR": "69::3",
    },
}
EXPECTED_LOADED_CONF = (
    FAKE_NODE_CONFIG["bgpParams"],
    FAKE_NODE_CONFIG["envParams"],
    FAKE_NODE_CONFIG["popParams"],
)

FAKE_NODE_INFO = """\
#### THIS FILE IS AUTO GENERATED. DO NOT EDIT  #####
NODE_ID="4a:57:dd:7d:17:01"
NUM_WLAN_MACS="4"
TG_IF2IF="0"
"""
EXPECTED_MAC_ADDRESS = "4a:57:dd:7d:17:01"
