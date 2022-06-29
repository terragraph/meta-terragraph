#!/bin/bash

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# shellcheck source=/dev/null
. /etc/monit_config.sh # for reboot reason indicators

dir=/data/log
hist="${dir}"/reboot_history # active reboot history file
hist2="${hist}".2 # previous reboot history file
maxLines=100

mkdir -p "${dir}"
touch "${hist}"
numLines=$(wc -l "${hist}" | cut -d " " -f 1 2>/dev/null)
if [ "${numLines}" -ge "${maxLines}" ];
then
	mv -f "${hist}" "${hist2}"
	if [ $? -ne 0 ];
	then
		exit 1 # Failed to rotate full reboot history file
	fi
fi

 # shutting down
if [ "$1" = "down" ];
then
	echo "down $(date +%s 2>/dev/null) $(date -R 2>/dev/null)" >> "${hist}"
	exit 0 # done with shutdown log
fi

# starting up
echo -n "up $(date +%s 2>/dev/null) $(date -R 2>/dev/null)" >> "${hist}"

# shellcheck disable=SC2154
if [ -e "${prev_reboot_dirty_indicator}" ];
then
	echo -n " dirty" >> "${hist}"
fi

# shellcheck disable=SC2154
if [ -e "${pop_reboot_indicator}" ];
then
	rm -f "${pop_reboot_indicator}"
	echo -n " pop_unreachable" >> "${hist}"
fi

# shellcheck disable=SC2154
if [ -e "${config_fallback_reboot_reason_file}" ];
then
	reason="config-$(cat "${config_fallback_reboot_reason_file}" 2>/dev/null)"
	rm -f "${config_fallback_reboot_reason_file}"
	echo -n " ${reason}" >> "${hist}"
fi

# shellcheck disable=SC2154
if [ -e "${fsys_reboot_reason_file}" ];
then
	reason="fsys-$(cat "${fsys_reboot_reason_file}" 2>/dev/null)"
	rm -f "${fsys_reboot_reason_file}"
	echo -n " ${reason}" >> "${hist}"
fi

# shellcheck disable=SC2154
if [ -e "${testcode_reboot_indicator}" ];
then
	rm -f "${testcode_reboot_indicator}"
	echo -n " testcode-timed-out" >> "${hist}"
fi

# shellcheck disable=SC2154
if [ -e "${e2e_minion_restart_failed_reboot_indicator}" ];
then
	reason="$(cat "${e2e_minion_restart_failed_reboot_indicator}" 2>/dev/null)"
	rm -f "${e2e_minion_restart_failed_reboot_indicator}"
	echo -n " e2e_minion_restart_failed_${reason}" >> "${hist}"
fi

# shellcheck disable=SC2154
if [ -e "${link_monit_reboot_indicator}" ];
then
	rm -f "${link_monit_reboot_indicator}"
	echo -n " link_monit" >> "${hist}"
fi

# TODO: Append other surprise startup tags here
echo >> "${hist}"
