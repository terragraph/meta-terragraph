#!/bin/sh

# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# shellcheck disable=SC1091
. tg.env

# Vendor-specific functions
# shellcheck disable=SC1091
. fb_load_bh_drv.sh

# QEMU-specific functions
# shellcheck disable=SC1091
. qemu_utils.sh

# load driver & fw, and bring up interfaces
# shellcheck disable=SC2154
fb_load_dr_fw() {
    NUM_OF_LINKS=${1}
    FW_USE_IF2IF=${2}
    FB_DRIVER_NSS=${3}
    FB_DRIVER_VERBOSE=${4}
    HMAC_VERBOSE=${5}
    KMOD_VERBOSE=${6}
    BH_AMSDU_FRAME_FORMAT=${7}
    DPDK_ENABLED="${8}"
    DVPP_ENABLED="${9}"
    FW_BUSID="${10}"

    TG_LOAD_WITH_DVPP=0

    # initialize the wireless driver
    if is_in_qemu; then
      # load hwsim if running in QEMU
      TG_MOD_NAME="terragraph_hwsim"
      TG_MOD_FILE="terragraph-hwsim.ko"
    elif [ "${DPDK_ENABLED:-0}" -ne "1" ]; then
      # call vendor to load and initialize its driver
      if [ "${DVPP_ENABLED:-0}" = "1" ]; then
          TG_LOAD_WITH_DVPP=1
          fb_load_bh_driver "${FW_USE_IF2IF}" "${FW_BUSID}" "module_has_dvpp" || return 1
      else
          fb_load_bh_driver "${FW_USE_IF2IF}" "${FW_BUSID}" || return 1
      fi
    else

      # initialize the dpdk driver
      if [ ! -e /usr/bin/fb_load_dpdk.sh ]; then
        echo "/usr/bin/fb_load_dpdk.sh does not exist"
        exit 1
      fi
      # shellcheck disable=SC1091
      . /usr/bin/fb_load_dpdk.sh
      fb_load_dpdk "${FW_BUSID}"

      # hack for the rest of this script...
      TG_MOD_NAME="$DPDK_TG_MOD_NAME"
      TG_MOD_FILE="$DPDK_TG_MOD_FILE"
    fi

    # set driver verbosity
    fb_set_bh_verbose "${HMAC_VERBOSE}" "${KMOD_VERBOSE}" "${FW_BUSID}"

    # Calculate TG driver log mask, by translating legacy zero to
    # mean 'display all errors'. If non-zero value if specified,
    # just trust the user and pass it along
    if [ "${FB_DRIVER_VERBOSE}" = "0" ]
    then
        FB_DRIVER_VERBOSE=0x10001
    fi
    if [ -z "${FW_BUSID}" ]; then
        # BUSID not specified. This is the initial bring up.
        # Load the backhaul platform driver.
        insmod "${DRV_PATH}/${TG_MOD_FILE}" \
            dbg_mask="${FB_DRIVER_VERBOSE}" \
            tgd_auto_up="Y" \
            tgd_def_mtu="${WL_MOD_MTU}" \
            tgd_enable_nss="${FB_DRIVER_NSS}" \
            tgd_num_of_virt_links="${NUM_OF_LINKS}" \
            tgd_bh_amsdu_frame_format="${BH_AMSDU_FRAME_FORMAT}" \
            module_has_dvpp=${TG_LOAD_WITH_DVPP}

        # check that terragraph driver did indeed load
        if [ ! -d "/sys/module/${TG_MOD_NAME}" ]
        then
            echo "Error! Terragraph module failed to load"
            return 1
        fi

        # Load MBH-specific drivers
        hw_board_id=$(/usr/sbin/get_hw_info HW_BOARD_ID)
        if [ "$hw_board_id" = "NXP_LS1048A_MBH" ]; then
            if [ "${PPS_TIMESTAMP_SOURCE}" = "GPS" ] ; then
                PPS_SOURCE_GPS=1
            else
                PPS_SOURCE_GPS=0
            fi
            insmod "${DRV_PATH}/zl3079x.ko" pps_source_gps=${PPS_SOURCE_GPS}
        fi

        # Wait for terra devices.
        # If at least one device has initialized, we will wait up to X seconds
        # before declaring other devices "dead" and proceeding anyway.
        DEV_TIMEOUT_S=20
        if [ "${DPDK_ENABLED:-0}" -ne "1" ]; then
            fb_wait_for_dev_ready "$DEV_TIMEOUT_S"
        else
            fb_dpdk_wait_for_dev_ready "$DEV_TIMEOUT_S"
        fi

        # generate wsec config files (wpa_supplicant, hostapd)
        for ifpath in /sys/class/net/terra* ""; do
            if [ -d "$ifpath" ]; then
                ifname=$(basename "$ifpath")
                terra_mac=$(ip -o link show "$ifname" | awk '{print $(NF-2)}')
                /usr/sbin/export_security_config conf "$ifname" "$terra_mac"
            fi
        done
        /usr/sbin/export_security_config wpa_psk_file

        if is_in_qemu; then
            qemu_set_bb_macs
            qemu_bring_virtual_eth_up
        fi
    fi
}
