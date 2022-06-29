#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

RESTART_DELAY=10

# shellcheck disable=SC1091
. tg.env

# load tg config environment variables
. /usr/sbin/config_get_env.sh

# Get TG_IF2IF. Assume RF if not present
TG_IF2IF=$(/usr/sbin/get_hw_info TG_IF2IF)
if [ -z "$TG_IF2IF" ] || [ "$TG_IF2IF" != "1" ]; then
  TG_IF2IF="0"
fi

_stop() {
  if [ ! -f /usr/bin/stop.sh ]; then
    echo "/usr/bin/stop.sh does not exist"
    exit 1
  fi
  /usr/bin/stop.sh
}

# clean up function when daemon is stopped
_term() {
  echo "Caught termination of $(cat /proc/$$/comm)"
  kill -TERM "$PROCESSID" 2>/dev/null
  _stop
}

_run() {
  if [ -z "$1" ]; then
    echo "specify the daemon to run"
    exit 1
  fi

  # load driver & firmware
  if [ ! -e /usr/bin/fb_load_dr_fw.sh ]; then
    echo "/usr/bin/fb_load_dr_fw.sh does not exist"
    exit 1
  fi
  # shellcheck source=/dev/null
  . /usr/bin/fb_load_dr_fw.sh
  trap _term EXIT
  fb_load_dr_fw "$FW_NUM_OF_LINKS" "$TG_IF2IF" "$OPENR_USE_FIB_NSS" \
                "$FB_DRIVER_VERBOSE" "$HMAC_VERBOSE" "$KMOD_VERBOSE" \
                "$BH_AMSDU_FRAME_FORMAT" "$DPDK_ENABLED" "$DVPP_ENABLED"
  if [ $? -ne 0 ]; then
    exit 1
  fi

  # HACK! update node info file with BB MACs if using DPDK
  if [ "${DPDK_ENABLED:-0}" -eq "1" ]; then
    if [ ! -e /usr/bin/fb_load_dpdk.sh ]; then
      echo "/usr/bin/fb_load_dpdk.sh does not exist"
      exit 1
    fi
    # shellcheck disable=SC1091
    . /usr/bin/fb_load_dpdk.sh
    fb_set_bb_macs "$FW_NUM_OF_LINKS"
  fi

  # Redistribute interrupts
  board=$(/usr/sbin/get_hw_info HW_BOARD_ID)
  if [ "${board}" = "NXP_LS1048A_JAGUAR" ]; then
    IRQS=$(cat /proc/interrupts | grep dhd | awk '{print $1}' | cut -f 1 -d: )

    affinity=1
    for u in $IRQS
    do
      echo $affinity > /proc/irq/$u/smp_affinity
      affinity=$((affinity << 1))
    done
  fi

  # Now start the daemon
  echo "Executing $1"
  # if an old incarnation still around kill it now
  procname=$(basename "$(echo "$1" | awk '{print $1}')")
  killall -s KILL "$procname"
  $1 &
  PROCESSID=$!
  wait $PROCESSID
}

_restart() {
  # This function is called to restart the firmware and backhaul
  # platform driver for just one PCIe interface.
  # The bus ID of the PCIe interface is input to this function.
  if [ -z "$1" ]; then
    echo "Specify PCIe bus ID of the device to restart"
    exit 1
  fi
  BUSID="${1}"
  # shellcheck source=/dev/null
  . /usr/bin/fb_load_dr_fw.sh
  fb_load_dr_fw "$FW_NUM_OF_LINKS" "$TG_IF2IF" "$OPENR_USE_FIB_NSS" \
                "$FB_DRIVER_VERBOSE" "$HMAC_VERBOSE" "$KMOD_VERBOSE" \
                "$BH_AMSDU_FRAME_FORMAT" "$DPDK_ENABLED" "$DVPP_ENABLED" \
                "$BUSID"
  if [ $? -ne 0 ]; then
    exit 1
  fi
}

# Return shared program flags between e2e_minion and driver_if_daemon
_get_minion_common_flags() {
  OUTPUT_FLAGS=""

  # timing parameters
  if [ ! -z "$PPS_TIMESTAMP_SOURCE" ]; then
    OUTPUT_FLAGS="$OUTPUT_FLAGS -pps_timestamp_source $PPS_TIMESTAMP_SOURCE"
  fi
  if [ ! -z "$PTP_TIMER_SOURCE" ]; then
    OUTPUT_FLAGS="$OUTPUT_FLAGS -ptp_timer_source $PTP_TIMER_SOURCE"
  fi
  if [ ! -z "$PTP_DEVICE" ]; then
    OUTPUT_FLAGS="$OUTPUT_FLAGS -ptp_device $PTP_DEVICE"
  fi
  if [ ! -z "$PTP_VSC_CTRL_SOCKET" ]; then
    OUTPUT_FLAGS="$OUTPUT_FLAGS -ptp_vsc_ctrl_socket $PTP_VSC_CTRL_SOCKET"
  fi
  if [ ! -z "$PTP_VSC_PORT" ]; then
    OUTPUT_FLAGS="$OUTPUT_FLAGS -ptp_vsc_port $PTP_VSC_PORT"
  fi
  if [ ! -z "$HTSF_SRC_MAC" ]; then
    OUTPUT_FLAGS="$OUTPUT_FLAGS -htsf_src_mac $HTSF_SRC_MAC"
  fi
  if [ ! -z "$HTSF_MAX_LOOP_SIZE" ]; then
    OUTPUT_FLAGS="$OUTPUT_FLAGS -htsf_max_loop_size $HTSF_MAX_LOOP_SIZE"
  fi

  # forward timestamps using gpsd?
  if [ "$GPSD_ENABLED" = "1" ]; then
    OUTPUT_FLAGS="$OUTPUT_FLAGS -enable_gpsd_client"
  fi
  if [ ! -z "$GPSD_GPS_MODULE" ]; then
    OUTPUT_FLAGS="$OUTPUT_FLAGS -gps_module $GPSD_GPS_MODULE"
  fi
  if [ "$GPSD_POSITION_HOLD_ENABLED" = "0" ]; then
    OUTPUT_FLAGS="$OUTPUT_FLAGS -enable_gps_position_hold=false"
  fi

  echo "$OUTPUT_FLAGS"
}
