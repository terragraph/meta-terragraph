#!/bin/sh
#
# The wil6210 driver searches for board files in /data/firmware/wil6210 first
# (see firmware_class.path), then falls back to /lib/firmware.
#
# We populate /data/firmware/wil6210 with different contents depending on
# whether we are operating in RF or IF2IF mode, or using golden vs. native
# board files (in RF mode only).
#

WIL_ROOT="$(cat /sys/module/firmware_class/parameters/path)"
NATIVE_ROOT="/data/firmware/native"
if [ -z "$WIL_ROOT" ]; then
    echo "Custom firmware search path was not found, exiting..."
    exit 1
fi
if [ -L "$WIL_ROOT" ]; then
    echo "Unlinking old firmware directory (migrating to new layout)"
    unlink "$WIL_ROOT"
    echo
fi
if ! mkdir -p "$WIL_ROOT"; then
    echo "Failed to create $WIL_ROOT"
    exit 1
fi

# load required variables
load_vars() {
    # load tg config environment variables
    # shellcheck disable=1091
    . /usr/sbin/config_get_env.sh
    echo "Found FW_USE_NATIVE_BOARDFILE = $FW_USE_NATIVE_BOARDFILE"

    # read TG_IF2IF field (assume RF if not present)
    TG_IF2IF=$(/usr/sbin/get_hw_info TG_IF2IF)
    if [ -z "$TG_IF2IF" ] || [ "$TG_IF2IF" != "1" ]; then
        TG_IF2IF="0"
    fi
    echo "Found TG_IF2IF = $TG_IF2IF"

    # read board ID
    hw_board_id=$(/usr/sbin/get_hw_info HW_BOARD_ID)
    echo "Found HW_BOARD_ID = '$hw_board_id'"
}

make_fw_brd_files() {
    # link RF/IF firmware files
    if [ "$TG_IF2IF" -eq 1 ]; then
        echo "Using IF2IF firmware"
        FWDIR="/lib/firmware/TALYN_M_B0_IF2IF"
    else
        echo "Using RF firmware"
        FWDIR="/lib/firmware/TALYN_M_B0"
    fi
    ln -sfv "${FWDIR}/wil6210.fw" "${WIL_ROOT}/wil6436.fw"

    # install native board files (if needed)
    native_brd_used=0
    if [ "$FW_USE_NATIVE_BOARDFILE" = "1" ] && [ "$TG_IF2IF" -eq 0 ]; then
        echo "Using native board files"
        if [ "$hw_board_id" != "NXP_LS1048A_PUMA" ]; then
            # only Puma is supported
            echo "ERROR: Only NXP_LS1048A_PUMA is supported (found '$hw_board_id')"
        else
            # update native board file for each baseband EEPROM
            if [ -d "$NATIVE_ROOT" ] && [ ! -L "$NATIVE_ROOT" ]; then
                # older versions of this script linked the other way around,
                # so delete this directory if it exists
                echo "Deleting old native directory (migrating to new layout)"
                rm -rfv "$NATIVE_ROOT"
            fi
            ln -sfv "$WIL_ROOT" "$NATIVE_ROOT"
            /usr/bin/bb_fetch_brdfile.sh -l '0 1 2 3'
            native_brd_used=1
        fi
    fi

    # install golden board files
    if [ $native_brd_used -eq 0 ]; then
        echo "Using golden board files"
        if [ -L "$NATIVE_ROOT" ]; then
            unlink "$NATIVE_ROOT"
        elif [ -d "$NATIVE_ROOT" ]; then
            rm -rfv "$NATIVE_ROOT"
        fi
    else
        echo "Installing golden board files (as fallback)"
    fi
    if [ "$hw_board_id" = "NXP_LS1048A_PUMA" ]; then
        brdfile="wil6436_puma.brd"
    else
        brdfile="wil6436.brd"
    fi
    ln -sfv "${FWDIR}/${brdfile}" "${WIL_ROOT}/wil6436.brd"

    # link strings files (if present)
    FW_STRINGS_NAME="fw_image_trace_string_load.bin"
    UCODE_STRINGS_NAME="ucode_image_trace_string_load.bin"
    if [ -f "${FWDIR}/${FW_STRINGS_NAME}" ]; then
        echo "Linking firmware strings file"
        ln -sfv "${FWDIR}/${FW_STRINGS_NAME}" "${WIL_ROOT}/${FW_STRINGS_NAME}"
    fi
    if [ -f "${FWDIR}/${UCODE_STRINGS_NAME}" ]; then
        echo "Linking ucode strings file"
        ln -sfv "${FWDIR}/${UCODE_STRINGS_NAME}" "${WIL_ROOT}/${UCODE_STRINGS_NAME}"
    fi
}

load_vars
echo
make_fw_brd_files
echo
echo "Finished."
