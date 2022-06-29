#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

tmp_event_cache="/tmp/events.json"
data_event_cache="/data/events.json"
tmp_event_cache_kafka="/tmp/kafka_events.json"
data_event_cache_kafka="/data/kafka_events.json"
if [ "$1" = "save" ]; then
	/bin/cp -fp "${tmp_event_cache}" "${data_event_cache}" 2>/dev/null
	/bin/cp -fp "${tmp_event_cache_kafka}" "${data_event_cache_kafka}" 2>/dev/null
elif [ "$1" = "load" ]; then
	/bin/mv -f "${data_event_cache}" "${tmp_event_cache}" 2>/dev/null
	/bin/mv -f "${data_event_cache_kafka}" "${tmp_event_cache_kafka}" 2>/dev/null
fi
