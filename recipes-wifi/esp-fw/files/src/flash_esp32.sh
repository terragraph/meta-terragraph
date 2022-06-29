#!/bin/bash

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

PUMA_BOARD="NXP_LS1048A_PUMA"
MBH_BOARD="NXP_LS1048A_MBH"
FDT_FILE="/sys/bus/nvmem/devices/1-00540/nvmem"
DEFAULT_DIR=/usr/share/esp32
PORT=/dev/ttyS2
BAUD=115200

# Restart slip
slip_restart() {
  if [ "${slip_run}" != "1" ]; then
    echo "slip was down, no need to restart it"
  else
    echo "Restarting slip"
    sleep 1 # Delay after ESP32 reset
    if sv start slip >/dev/null 2>/dev/null; then
      echo "slip restarted"
    else
      echo "Failed to restart slip"
      exit 1
    fi
  fi
}

gpio() {
  pin="$1"
  value="$2"
  echo "$pin" > /sys/class/gpio/export
  echo out > "/sys/class/gpio/gpio${pin}/direction"
  echo "${value}" > "/sys/class/gpio/gpio${pin}/value"
  echo "${pin}" > /sys/class/gpio/unexport
}

esp32reset() {
  if [ "$1" = "${NORMAL_EXECUTION_MODE}" ]; then
    echo "Booting the ESP32 into normal execution mode"
  else
    echo "Booting the ESP32 into serial bootloader mode"
  fi
  gpio "${BOOT_MODE_SELECT_GPIO}" "$1"
  sleep 0.5
  gpio "${RESET_GPIO}" 1
}

usage() {
  echo "Usage: $0 [-h] [-abdefgpr] [-c cfg]"
  echo "       -a     flash the application"
  echo "       -b     flash the bootloader"
  echo "       -c cfg flash the configuration"
  echo "       -d     flash the partition table"
  echo "       -e     same as -a -b -d"
  echo "       -f     do not confirm flash operations"
  echo "       -g     get configuration and do nothing else"
  echo "       -p     path for all images except configuration"
  echo "              default: ${DEFAULT_DIR}"
  echo "       -r     reset the ESP32 and do nothing else"
  exit 64
}

DIR="${DEFAULT_DIR}"

while getopts ":abc:defgp:r" opt; do
  case $opt in
  a)
    flash_app=1
    ;;
  b)
    flash_boot=1
    ;;
  c)
    config_file="${OPTARG}"
    ;;
  d)
    flash_part=1
    ;;
  e)
    flash_app=1
    flash_boot=1
    flash_part=1
    ;;
  f)
    force=1
    ;;
  g)
    get_config=1
    ;;
  p)
    DIR="${OPTARG}"
    ;;
  r)
    reset=1
    ;;
  \?)
    usage
    ;;
  :)
    echo "-$OPTARG requires an argument!" >&2
    usage
    ;;
  esac
done

declare -a image
declare -a addr

if [ -n "${flash_app}" ]; then
  image+=("${DIR}/wifi_softAP.bin")
  addr+=(0x10000)
fi

if [ -n "${flash_boot}" ]; then
  image+=("${DIR}/bootloader.bin")
  addr+=(0x1000)
fi

if [ -n "${config_file}" ]; then
  image+=("${config_file}")
  addr+=(0x9000)
fi

if [ -n "${flash_part}" ]; then
  image+=("${DIR}/partitions_singleapp.bin")
  addr+=(0x8000)
fi

# Sanity check the command line
if [ "${#image[@]}" = "0" ] && [ -z "${reset}" ] && [ -z "${get_config}" ]; then
  echo "Nothing to do!"
  usage
  exit 1
fi

# Sanity check the board type
board=$(fdtget ${FDT_FILE} "/board/hw-ids" "hw-board")
if [ "${board}" != "${PUMA_BOARD}" ] && [ "${board}" != "${MBH_BOARD}" ]; then
  echo "Unknown board: \"${board}\""
  exit 1
fi

# Select the GPIO's
board_rev=$(fdtget ${FDT_FILE} "/board/hw-ids" "hw-rev")

if [ "${board}" = "${MBH_BOARD}" ] || [ "${board_rev}" = "2.0" ] || [ "${board_rev}" = "2.1" ] || [ "${board_rev}" = "2.2" ]; then
  RESET_GPIO=471 # WIFI_RST_N, GPIO2_23
  BOOT_MODE_SELECT_GPIO=480 # WIFI_PROG_EN_N, GPIO1_00
elif [ "${board_rev}" = "1.0" ]; then
  RESET_GPIO=458 # RST_PHY_N, GPIO2_10
  BOOT_MODE_SELECT_GPIO=469 # WIFI_PROG_EN_N, GPIO2_21
else
  echo "Unknown board-rev: \"${board_rev}\""
  exit 1
fi

if [ "${board}" = "${MBH_BOARD}" ] || [ "${board_rev}" = "2.2" ]; then
  SERIAL_BOOTLOADER_MODE=0
  NORMAL_EXECUTION_MODE=1
else
  SERIAL_BOOTLOADER_MODE=1
  NORMAL_EXECUTION_MODE=0
fi

# Stop slip (which also uses the ESP32 console serial port)
echo "Stop slip if necessary"
slip_run="$(sv status slip 2>/dev/null | grep -c  '^run:')"
if [ "${slip_run}" != "1" ]; then
  echo "slip is not running"
elif sv stop slip >/dev/null 2>/dev/null; then
  echo "slip stopped"
else
  echo "Failed to stop slip"
  exit 1
fi

# Reset only
if [ -n "${reset}" ]; then
  echo "Resetting the ESP32"
  esp32reset "${NORMAL_EXECUTION_MODE}"
  slip_restart
  echo "Done"
  exit 0
fi

# Get configuration (for now, only default ssid)
if [ -n "${get_config}" ]; then
  echo "Setting serial port (${PORT}) to ${BAUD} baud."
  stty -F ${PORT} ${BAUD}
  esp32reset "${SERIAL_BOOTLOADER_MODE}"
  sleep 5
  echo "Getting configuration"
  otpmac="$(python3 /usr/bin/esptool.py --chip esp32 --port "${PORT}" --baud "${BAUD}" read_mac | grep -m2 'MAC:' | tail -n 1 | cut -d' ' -f 2)"
  echo "Default SSID: PM-${otpmac}"
  sleep 5
  esp32reset "${NORMAL_EXECUTION_MODE}"
  slip_restart
  echo "Done"
  exit 0
fi

# Pick the parts to program
i=0
echo "Preparing to flash the following parts:"
while [ $i -lt ${#image[@]} ]; do
  echo "${addr[i]} ${image[i]}"
  if [ ! -e "${image[i]}" ]; then
    echo "Can't find ${image[i]}"
    echo "Nothing flashed"
    exit 1
  fi
  items="${items} ${addr[i]} ${image[i]}"
  i=$((i+1))
done

# Confirm flashing
if [ -z ${force} ]; then
  echo -n "Are you sure? "
  read -r answer
  if [ "${answer}" != "y" ]; then
    slip_restart
    echo "Nothing flashed"
    exit 0
  fi
fi

echo "Setting serial port (${PORT}) to ${BAUD} baud."
stty -F ${PORT} ${BAUD}

esp32reset "${SERIAL_BOOTLOADER_MODE}"
sleep 5

echo "Flashing"
# shellcheck disable=SC2086
python3 /usr/bin/esptool.py --chip esp32 --port "${PORT}" --baud "${BAUD}" \
  --before no_reset --after no_reset write_flash -z --flash_mode dio \
  --flash_freq 40m --flash_size detect ${items}
sleep 5

esp32reset "${NORMAL_EXECUTION_MODE}"

slip_restart

echo "Done"
