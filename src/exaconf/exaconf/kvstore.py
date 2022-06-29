#!/usr/bin/env python3

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import logging
from ipaddress import IPv4Network, IPv6Network, ip_network
from typing import Optional, Union

from openr.KvStore import ttypes as kv_store_types  # type: ignore
from openr.OpenrCtrl import OpenrCtrl
from openr.utils.consts import Consts
from thrift.protocol import TBinaryProtocol
from thrift.transport import TSocket, TTransport
from thrift.transport.TTransport import TTransportException  # type: ignore


IPNetwork = Union[IPv4Network, IPv6Network]
LOG = logging.getLogger(__name__)


def get_e2e_network_prefix(
    *, hostname: str = "localhost", kv_network_prefix: str = "e2e-network-prefix"
) -> Optional[IPNetwork]:
    try:
        socket = TSocket.TSocket(hostname, Consts.CTRL_PORT)
        transport = TTransport.TBufferedTransport(socket)
        protocol = TBinaryProtocol.TBinaryProtocol(transport)

        with OpenrCtrl.Client(protocol) as client:
            keyDumpParams = kv_store_types.KeyDumpParams(kv_network_prefix)
            transport.open()
            kv = client.getKvStoreKeyValsFiltered(keyDumpParams)
            transport.close()
            if kv_network_prefix in kv.keyVals:
                prefix_str = kv.keyVals[kv_network_prefix].value.decode("utf-8")
                return ip_network(prefix_str.split(",", 1)[0])
    except TTransportException as ex:
        LOG.error(f"Cannot connect to {hostname} kvstore. Exception: {ex}")
    except (AttributeError, ValueError) as ve:
        LOG.error(f"{kv_network_prefix} from the kvstore is invalid: {ve}")

    return None
