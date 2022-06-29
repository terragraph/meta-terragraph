#!/bin/sh
#
# Collects firmware and microcode logs when using the wil6210 kernel driver.
#
# Runs wil_fw_trace for each device and stores logs in /var/log/wil_fw_trace/.
# Log verbosity can set as WARN, ERROR, INFO, or VERBOSE; defaults to INFO.
#
# To collect firmware/microcode logs when using dpdk and the PMD, set
# the node_config.json param FW_LOGGING_ENABLED to enable the PMD to collect
# logs when running vpp. Or you can manually run wil_fw_trace with the `-d`
# option specified.

finish() {
  echo "Exiting: killing any wil_fw_trace child processes"
  jobs -p | while IFS='' read -r pid
  do
    kill "$pid"
  done
}

trap "exit" INT TERM
trap finish EXIT

VERBOSITY=$1
if [ -z "$VERBOSITY" ]; then
  VERBOSITY="INFO"
fi

# set firmware search path for strings files
FW_PATH=$(cat /sys/module/firmware_class/parameters/path)
if [ -z "$FW_PATH" ]; then
  FW_PATH="/lib/firmware"
fi

# make directory to store logs
if [ ! -d "/var/log/wil_fw_trace" ]; then
  mkdir -p /var/log/wil_fw_trace
fi

# start wil_fw_trace to collect fw and ucode logs for each interface that uses
# wil6210 kernel driver
DEVICE_ADDRESSES=$(lspci -d 17cb:1201 | cut -d " " -f1)
for pci in $DEVICE_ADDRESSES; do
  if ! ifname=$(ls /sys/bus/pci/drivers/wil6210/"$pci"/net); then
    echo "wil6210 driver not in use for $pci"
    continue
  fi

  wil_fw_trace -d "$pci" -s "$FW_PATH"/fw_image_trace_string_load.bin -v "$VERBOSITY" > /var/log/wil_fw_trace/fw_trace_"$pci".log &
  echo "Started fw log collection for interface $ifname on pci bus $pci: /var/log/wil_fw_trace/fw_trace_$pci.log"

  wil_fw_trace -d "$pci" -s "$FW_PATH"/ucode_image_trace_string_load.bin -u -v "$VERBOSITY" > /var/log/wil_fw_trace/ucode_trace_"$pci".log &
  echo "Started ucode log collection for interface $ifname on pci bus $pci: /var/log/wil_fw_trace/ucode_trace_$pci.log"
done

wait
