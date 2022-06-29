#!/bin/bash

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

namespace="default"
min_password_len=8 # enforced by esp-idf
part_size="0x6000"
part_file="esp32config.bin"
csv_file="esp32config.csv"
part_gen_py="nvs_partition_gen.py"
part_gen="$(command -v "${part_gen_py}")"
flash_exe="flash_esp32"
flash_cmd="${flash_exe} -c ${part_file}"

usage() {
  echo "Usage: $0 -fu -m <mac> -p <password> -s <ssid>"
  echo "       -f            flash the ESP32 with the generated config"
  echo "       -m <mac>      mac address, format: xx:xx:xx:xx:xx:xx"
  echo "                     bit 0 of the first octet must be 0"
  echo "                     default: ESP32 factory mac address"
  echo "       -p <password> wifi password, default: facebook"
  echo "                     must be at least 8 characters"
  echo "       -s <ssid>     ssid to advertise"
  echo "                     default: factory mac address"
  echo "       -u            enable logging to uart (/dev/ttyS2)"
  echo "                     default: disable uart logs"
  echo "       -w            enable slip (default: disabled)"
  echo "       -b            slip baud rate (default: 115200)"
  echo "                     options: 115200, 230400, 460800, 576000"
  exit 64
}

if [ -z "${part_gen}" ]; then
  echo "Can't find \"${part_gen_py}\" required to generate the config-partition image"
  exit 1
fi

if command -v python3 > /dev/null; then
  part_gen="python3 ${part_gen}"
fi

slip_baud="115200"

while getopts ":fm:p:s:uwb:" opt; do
  case $opt in
  f)
    flash=1
    if [ -z "$(command -v "${flash_exe}")" ]; then
      echo "Can't find \"${flash_exe}\" required to flash the config"
      exit 1
    fi
    ;;
  m)
    mac="${OPTARG}"
    if grep -Ev "^[0-9a-f][02468ace]:([0-9a-f]{2}:){4}[0-9a-f]{2}$" <<< "${mac}" > /dev/null; then
      echo "Invalid mac address \"${mac}\""
      exit 1
    fi
    ;;
  p)
    password="${OPTARG}"
    if [ ${#password} -lt ${min_password_len} ]; then
      echo "Password is too short. Minimum length ${min_password_len}"
      exit 1
    fi
    ;;
  s)
    ssid="${OPTARG}"
    ;;
  u)
    uartlog=1
    ;;
  w)
    slip=1
    ;;
  b)
    slip_baud="${OPTARG}"
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

#
# Create intermediate CSV file
# https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/storage/nvs_partition_gen.html#csv-file-format
#
echo
echo "Creating CSV file ${csv_file} with the following config"

echo "key,type,encoding,value" > "${csv_file}"
echo "${namespace},namespace,," >> "${csv_file}"

if [ -n "${mac}" ]; then
  echo "  mac address: ${mac}"
  echo "mac,data,string,${mac}" >> "${csv_file}"
else
  echo "  mac: not set, use ESP32 factory default"
fi

if [ -n "${password}" ]; then
  echo "  password: ${password}"
  echo "password,data,string,${password}" >> "${csv_file}"
else
  echo "  password: not set, use the ESP32 f/w default (facebook)"
fi

if [ -n "${ssid}" ]; then
  echo "  ssid: ${ssid}"
  echo "ssid,data,string,${ssid}" >> "${csv_file}"
else
  echo "  ssid: not set, use the ESP32 f/w default (factory mac address)"
fi

if [ -n "${uartlog}" ]; then
  echo "  uart logs: enabled"
  echo "log2uart,data,i8,1" >> "${csv_file}"
else
  echo "  uart logs: disabled"
  echo "log2uart,data,i8,0" >> "${csv_file}"
fi

if [ -n "${slip}" ]; then
  echo "  slip: enabled"
  echo "slip,data,i8,1" >> "${csv_file}"
else
  echo "  slip: disabled"
  echo "slip,data,i8,0" >> "${csv_file}"
fi

echo "  slip_baud: ${slip_baud}"
echo "slip_baud,data,i32,${slip_baud}" >> "${csv_file}"

echo "Created ${csv_file}"

#
# Create the binary config-partition image
# https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/storage/nvs_flash.html#nvs-partition-generator-utility
#
echo
echo "Creating binary config-partition image \"${part_file}\" from \"${csv_file}\""
if ${part_gen} --size "${part_size}" --input "${csv_file}" --output "${part_file}"; then
  echo "${part_file} created"
else
  echo "Failed to create ${part_file}"
  echo "The failed command was:"
  echo "  ${part_gen} --size ${part_size} --input ${csv_file} --output ${part_file}"
  unset flash
fi

#
# Confirm the ESP32 flash
#
if [ -n "${flash}" ] && [ -e "${part_file}" ]; then
  echo -n "Flash the ESP32 with the new config? "
  read -r answer
  if [ "${answer}" != "y" ]; then
    echo "Skipping ESP32 flash."
    unset flash
  fi
fi

#
# Flash the ESP32 with the config partition
#
if [ -n "${flash}" ] && [ -e "${part_file}" ]; then
  echo
  echo "Flashing the ESP32 with the new config"
  if ${flash_cmd}; then
    flashok=1
  else
    echo "Failed to flash the ESP32 with the new config"
  fi
fi

if [ -z "${flashok}" ]; then
  echo
  echo "The following command flashes the config image on the ESP32"
  echo "  ${flash_cmd}"
fi

echo
echo "Done!"
