#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

while getopts v: name
do
	case ${name} in
	v)
		verbose=${OPTARG}
		;;
	\?)
		exit 1
		;;
	esac
done
shift $((OPTIND - 1))

# common file
FB_TG_LOAD_COMMON="/usr/sbin/fb_tg_load_common.sh"

if [ ! -f "$FB_TG_LOAD_COMMON" ]; then
  echo "$FB_TG_LOAD_COMMON does not exist" >&2
  exit 1
fi

# shellcheck source=/dev/null
. "$FB_TG_LOAD_COMMON"

# set the CLI desired FW verbosity level
if [ "${verbose}" != "" ]; then
  HMAC_VERBOSE="${verbose}"
fi
DRIVER_IF_DAEMON="/usr/sbin/driver_if_daemon -v ${HMAC_VERBOSE}"

# copy node info file (may be modified after baseband init)
E2E_NODE_INFO_FILE="/var/run/node_info"
TMP_E2E_NODE_INFO_FILE="/tmp/node_info"
cp "$E2E_NODE_INFO_FILE" "$TMP_E2E_NODE_INFO_FILE"
DRIVER_IF_DAEMON="$DRIVER_IF_DAEMON -node_info_file $TMP_E2E_NODE_INFO_FILE"

# add shared flags
DRIVER_IF_DAEMON="$DRIVER_IF_DAEMON $(_get_minion_common_flags)"

echo "Killing e2e_minion"
killall e2e_minion

echo "Unloading driver..."
_stop

echo "Starting driver-if: $DRIVER_IF_DAEMON"
_run "$DRIVER_IF_DAEMON"
