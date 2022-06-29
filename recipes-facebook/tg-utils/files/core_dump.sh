#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Script invoked to generate core dump

EXEC=$(echo "$1" | sed 's/!/\//g') # Executable path. Repace '!' with '/'
PID=$2  # process pid
TIME=$3 # time since epoch

# dump core to /var/volatile/cores with right name
EXEC_NAME=$(basename "${EXEC}") # get the executable name
mkdir -p /var/volatile/cores/"$EXEC_NAME"
UTIME=$(date -d @"$TIME" | tr ' ' '_') # convert to user readable time

if [ "${EXEC_NAME}" = "vpp" ]
then
  # vpp cores are very large, no need to store more than one
  CORE_FILE_NAME="/var/volatile/cores/${EXEC_NAME}/${EXEC_NAME}-core.gz"
else
  CORE_FILE_NAME="/var/volatile/cores/${EXEC_NAME}/${EXEC_NAME}-core.${PID}.${UTIME}.gz"
fi
/bin/gzip - > "${CORE_FILE_NAME}"

# Collect stack trace too
#
# If the /var/volatile/cores is getting full don't bother collecting stack trace.
# gdb sometimes seem to go into some kind of infinite loop.
# Being extra paranoid and use timeout so that gdb is terminated if running
# for more than 5 secs
perc_full=$(df /var/volatile/cores | sed '1d' | awk '{print $5}')
perc_full=${perc_full%\%} # get rid of the % at the end
if [ $perc_full -ge 90 ]; then
	exit 0
fi
temp_core=$(mktemp /tmp/abc-core.XXXXXX)
gunzip -c "${CORE_FILE_NAME}" > "$temp_core"
STACK_FILE_NAME="/var/volatile/cores/${EXEC_NAME}/${EXEC_NAME}-stack.${PID}.${UTIME}"
timeout 5 /usr/bin/gdb -n --batch --quiet -iex "set auto-load safe-path /" -ex "thread apply all bt full" -ex "quit" "${EXEC}" "${temp_core}" > "$STACK_FILE_NAME"
/bin/rm "${temp_core}"

# Copy the stack file contents to application's current log for upload via fluent-bit
APP_LOG_NAME="/var/log/${EXEC_NAME}/current"

datetime=$(date +"I%m%d %H:%M:%S.%3N")

if [ -f "${APP_LOG_NAME}" ] && [ "${EXEC_NAME}" != "vpp" ]
then
  echo "${datetime} ${EXEC_NAME} crashed. Dumping stack trace file ${STACK_FILE_NAME} contents with $(wc -l < "${STACK_FILE_NAME}") lines ..." >> "${APP_LOG_NAME}"
# fluent-bit tail plugin does not send logs in order, so we send the entire stack trace
# as a single multi-line log. For correct multi-line parsing, add whitespace markers
# to each line of the stack trace.
  sed -e 's/^/  /' "${STACK_FILE_NAME}" >> "${APP_LOG_NAME}"
fi
