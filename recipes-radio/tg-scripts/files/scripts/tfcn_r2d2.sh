#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# shellcheck disable=SC1117
usage () {
  echo "Usage: $0 [-STVhnrv] [-M mac] [-B bus] [-m mcs] [-f /path/to/node_config.json]"
  echo
  echo "Options:"
  printf "  -B\n\tsource bus id (for multi-radio boards)\n"
  printf "  -f\n\tuse a custom node config file (overrides other options)\n"
  printf "  -h\n\tdisplay the help message\n"
  printf "  -m\n\tMCS level\n"
  printf "  -M\n\tsource MAC address\n"
  printf "  -n\n\tload but do not initialize firmware\n"
  printf "  -r\n\tuse already running driver stack\n"
  printf "  -S\n\tenable wsec\n"
  printf "  -T\n\tuse tg2 CLI instead of r2d2\n"
  printf "  -v\n\tverbose info\n"
  printf "  -V\n\tverbose debug\n"
  exit 64
}

cli=r2d2
# For multi-radio DNs, mac is used for radio on BUS_0
# Make sure the link being established is using BUS_0
mac=02:08:02:00:01:92
verbose=0
noinit=0
noload=0
wsec_enable=0
while getopts M:B:STVhm:nrvf: name
do
  case $name in
  M) mac="$OPTARG";;
  B) bus="$OPTARG";;
  S) wsec_enable=1;;
  V) verbose=2;;
  h) usage;;
  m) mcs="$OPTARG";;
  n) noinit=1;;
  r) noload=1;;
  v) verbose=1;;
  f) custom_node_config="$OPTARG";;
  T) cli="tg2 fw";;
  ?) usage;;
  esac
done
shift $(($OPTIND - 1))

echo "Using ${mac} as MAC address"
export TG_TEST_WLAN_MACADDR="${mac}"

if [ -n "$bus" ]; then
  echo "Using ${bus} as bus id"
  export TG_TEST_WLAN_BUSID="${bus}"
fi

if [ "${noload}" != 1 ]; then
  # Get rid of all other instances and wait for them to finish shutting down
  killall driver_if_daemon
  sv stop /etc/sv/driver_if_daemon
  killall e2e_minion
  sv stop /etc/sv/e2e_minion
  > /var/log/kern.log
  sleep 2

  /usr/sbin/driver_if_start.sh -v${verbose} &> /tmp/driver_if.log &
  dpid=$!
  trap "kill $dpid; sleep 3; kill -9 $dpid; exit 1" HUP INT TERM QUIT
  sleep 5
  while [ -z "$(pidof e2e_minion)" ]; do
    echo "driver_if_daemon has not started, last log line is:"
    tail -n1 /tmp/driver_if.log
    sleep 5
  done
else
  dpid=$(pidof e2e_minion)
  if [ -z "${dpid}" ]; then
    echo "Driver interface daemon is not running?"
    exit 1
  fi
fi

#
# Create the config file (if needed)
#
if [ -n "${custom_node_config}" ]; then
  if [ ! -f "${custom_node_config}" ]; then
    echo "Custom node config file does not exist: ${custom_node_config}"
    exit 1
  fi
  echo "Using custom node config file: ${custom_node_config}"
  tmpcfg="${custom_node_config}"
else
  # create latest default config file
  tmpcfg=$(mktemp /tmp/fwcfg.XXXX)
  /usr/sbin/config_get_base "${tmpcfg}" >/dev/null

  # modify config file based on CLI inputs
  /usr/sbin/config_set -n "${tmpcfg}" \
    -i radioParamsBase.fwParams.wsecEnable "${wsec_enable}"
  if [ -n "${mcs}" ]; then
    /usr/sbin/config_set -n "${tmpcfg}" \
      -i radioParamsBase.fwParams.mcs "${mcs}" \
      -i linkParamsBase.fwParams.mcs "${mcs}"
  fi
fi

if [ "${noinit}" != 1 ]; then
  echo "Initializing the firmware"
  $cli --radio_mac "${mac}" node_init -f "${tmpcfg}"
  if [ "${verbose}" = 2 ]; then
    echo "setting debug log level"
    $cli --radio_mac "${mac}" fw_set_log_config -l debug
  elif [ "${verbose}" = 1 ]; then
    echo "setting info log level"
    $cli --radio_mac "${mac}" fw_set_log_config -l info
  fi
fi
echo "DriverIf daemon is running as pid $dpid"
if [ ! -n "${custom_node_config}" ]; then
  rm "${tmpcfg}"
fi
