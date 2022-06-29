#!/bin/bash

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# shellcheck source=/dev/null
. /etc/monit_config.sh

now="$(monotonic-touch -t)"

# shellcheck disable=SC2154
if [ ! -e "${gps_good_received_file}" ] && [ -e "${gps_timeout_tmp_file}" ]; then
	# gps has been continuously bad since startup _and_ the gps timeout from
	# the previous shutdown is available.
	#
	# Consume the remaining gps time at the last shutdown.
	startup_gps_timeout_sec="$(cat "$gps_timeout_tmp_file" 2>/dev/null)"
	gps_timeout_left="$((startup_gps_timeout_sec - now))"
else
	# gps has been healthy least once since startup, or the remaining
	# gps timeout at the previous shutdown is not available.
	#
	# Consume the default/fixed gps timeout.
	gps_good_time="$(stat -c %Y "${gps_good_file}" 2>/dev/null)"
	if [ -z "${gps_good_time}" ]; then
		gps_good_time=0
	fi
	gps_timeout_left="$((gps_timeout_sec + gps_good_time - now))"
fi
if [ "${gps_timeout_left}" -lt 0 ]; then
	gps_timeout_left=0
fi

# shellcheck disable=SC2154
echo -n "${gps_timeout_left}" > "${gps_timeout_data_file}" 2>/dev/null
