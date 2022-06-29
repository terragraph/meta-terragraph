#!/bin/sh

##
# Retrieve native cal file(s) from each baseband eeprom
#
# Expected DTS format from EEPROM on each baseband
#  * Encapsulated format for image frame
#     - Header (signature, payload len)
#       check only header data portion to check update availability
#     - Payload (dtb file)
#       dtb file including cal files (brd / brd+json / json)
#
#  * Payload: dtb file
#    - cal file format: zstd compressed tar archive of cal files (json cal files and/or  wil6436.brd)
#
#  * DTS attribute fields:
#    On RFBBs with custom_rev attribute set to "1.0":
#    - filename: wil6436.brd.gz
#    - integrity: md5sum value of compressed board file
#    - tile_mount: PLANAR, ANGULAR
#    - boardfile-format: BRD
#    - boardfile: <contents of wil6436.brd.gz>
#       > fdtget stores / retrives by decimal based binary data.
#         so it requires packing to recover it
#       > gunzip => wil6436.{brd}
#
#    On RFBBs with custom_rev attribute set to "2.0":
#    - integrity_zst: md5sum value of manifest file
#    - tile_mount: PLANAR, ANGULAR
#    - boardfile-format: BRD | BRD+JSON | JSON
#    - boardfile: <contents of cal_files.tar.zst>
#
#  * Target native cal file location
#    - /data/firmware/wil6210/<pci_bus>
#
#  * Common shared interfaces from bb_shared_interface.sh
#
# Steps
#  * Update cal files from each baseband eeprom
#  1. Read header data only from eeprom:
#     - compare signature with local brd/manifest file
#     - if signatures match, skip update procedure
#     - otherwise, read entire img frame (header + payload)
#     - Header format for v1.0
#        @header: sig=${signature} plen=${payload_len} tm=${tile_mount}
#         - max 64 bytes
#         - @header: marker
#         - sig: signature hash value to check update by md5sum brdfile
#         - plen: payload length (dtb file)
#         - tile_mount: planar(1), angular(2)
#
#     - Header format for v2.0
#        @header_v2: sig=${signature manifest.cal} plen=${payload_len} tm=${tile_mount}
#         - max 128 bytes
#         - @header_v2: marker
#         - sig: md5sum value of manifest file
#         - plen: payload length (dtb file)
#         - tile_mount: planar(1), angular(2)
#
#  2. Get payload, dtb from fct_check_update()
#  3. Retrieve dts attributes
#  4. Compute md5 checksum on downloaded cal file
#  5. Convert hex strings from extracted file contents into binary format data
#  6. Check integrity_checksum by comparing computed with stored md5 checksum
#  7. Decompress brd.gz/json.tar.zst, then *.{brd | brd+json | json}
#  8. Show baseband board info
#  9. Store json and/or brd files into '/data/firmware/wil6210/<pci_bus>/'
#

bb_ret_value=                       # return value

target_root="/data/firmware"        # target root
DEBUG=                              # debug flag

main () {
    # Target fw dir
    target_name="wil6436.brd"               # target brd file name
    target_path="${target_root}/wil6210"     # target native dir

    wlanix=5                        # wlan index of baseband card
    pciaddr=                        # pci address of baseband card

    # DT attribute field to retrieve each value by fdtget
    attr_fname="filename"
    attr_integrity="integrity"
    attr_tiletype="tile-mount"
    attr_sn="hw-sn"
    attr_hw_rev="hw-rev"
    attr_brdfile_fmt="boardfile-format"
    filename=

    # Iteration variables
    wlanix_arr=                         # selected array of wlan indices

    # load common interfaces
    # shellcheck disable=SC1091
    . bb_shared_interface.sh

    op_mode="$OP_MODE_NORMAL"           # operation mode: normal

    if [ "$#" -lt 1 ]; then
        fct_usage
    fi

    # Handle option arguments
    while getopts "l:p:ktvmh" opt; do
        case $opt in
        l)
            wlanix_arr="${OPTARG}"          # save as an array for multi-selection

            # sort selected bb array
            wlanix_arr="$(echo "$wlanix_arr" | tr ' ' '\n' | sort -n | tr '\n' ' ')"
            ;;
        p)
            pciaddr="${OPTARG}"
            ;;
        k)
            keep_ws=1
            ;;
        t)
            op_mode="2"                 # operation mode: test
            keep_ws=1                   # preserve working dir for test mode
            ;;
        v)
            DEBUG=1
            ;;
        m)
            print_wlan_ix_pci_mapping
            exit 0
            ;;
        h|\?)
            fct_usage
            ;;
        esac
    done

    if [ -n "$pciaddr" ] ; then
        if [ ! "1" = "$(is_pci_addr_valid "$pciaddr")" ] ; then
            echo "PCI Address is NOT valid"
            exit 1
        fi
        # Find wlan index
        if echo "${PCI_PATH}" | grep -q "${pciaddr}" ; then
            wlanix=$(echo "${PCI_PATH}" |  tr ' ' '\n' | sed -n "/${pciaddr}/=")
            # Adjust for 0 based index
            wlanix_arr=$((wlanix-1))
        else
            echo "Invalid PCI address:${pciaddr}"
            exit 1
        fi
    fi

    mkdir -p "${target_path}"

    # Set working dir
    workspace="$(mktemp -d)"                # tmp working dir
    cd "${workspace}" || exit

    # sysfs path for eeprom on each BB by DT
    if [ -z "${EEPROM_PATH}" ] ; then
        echo "Failed to read baseband EEPROM addresses from device tree"
        rm -rf "$workspace"
        exit 1
    fi

    if ! lspci -d 17cb:1201 | cut -d ' ' -f1 >/dev/null ; then
        echo "PCI bus interfaces are not available"
        rm -rf "$workspace"
        exit 1
    fi

    if [ $DEBUG ]; then echo "PCI BUS ID => ${PCI_PATH}"; fi

    # Configuration:
    printf "\\n[CONFIG]\\n"
    echo "  Baseband:       ${wlanix_arr}"
    echo "  working dir:    ${workspace}/"
    echo "  target dir:     ${target_path}/"

    printf "\\nImport native cal files into File System..."

    # Loop for wlanix array
    for wlanix in $wlanix_arr ; do
        # destination to store cal file(s)
        dest="${target_path}/$(fct_array_ith "$PCI_PATH" "$((wlanix+1))")"

        ## Start to build
        printf "\\n>> wlan%s\\n" "${wlanix}"

        ## Read header data only from eeprom:
        #     - check update availability by signature hash value
        #     - by comparing with value from local manifest/brd file
        fct_check_update "$wlanix" "$dest" "$EEPROM_PATH" "$op_mode" "$workspace"

        # Skip update procedure if return value is -1 or 0
        # -1 (header error), 0 (no update), and 1 (update)
        if [ "${bb_ret_value}" -ne "1" ] ; then
            continue                # continue to proceed next baseband
        fi

        ## Get payload, dtb from fct_check_update()
        dtb_out="wlan${wlanix}.dtb"
        if [ $DEBUG ]; then printf '\n dtb file: %s\n' "${dtb_out}"; fi

        ## Check custom_revision
        custom_rev=$(fdtget "${dtb_out}" / "custom_rev")
        cal_file_fmt=$(fdtget "${dtb_out}" / "boardfile-format")

        if [ "$custom_rev" = "1.0" ] ; then
            attr_fname="filename"
            attr_integrity="integrity"
        elif [ "$custom_rev" = "2.0" ] ; then
            attr_fname="filename_brd"
            attr_integrity="integrity_zst"
        else
            echo "Unsupported custom_rev"
            continue                # continue to proceed next baseband
        fi

        # Get tile mount type
        tile_mount_str=$(fdtget "${dtb_out}" / "${attr_tiletype}")
        if [ $DEBUG ]; then echo "tile mount type: ${tile_mount_str}"; fi

        # shellcheck disable=SC2154
        check_file_integrity "${attr_integrity}" "${attr_fname}" "${dtb_out}" "${ws_tmp}" "$custom_rev" "${wlanix}"
        if [ ! "$bb_ret_value" -eq "1" ] ; then
            continue
        fi

        if [ "$custom_rev" = "1.0" ] ; then
            # Use filename stored in eeprom and lose the trailing '.gz'
            brd_filename=$(basename "${filename}" | sed 's/.gz$//g')
            copy_brd_file_to_target
        elif [ "$custom_rev" = "2.0" ] ; then
            copy_cal_files_to_target "${cal_file_fmt}"
        else
            continue
        fi

        ## Show board attributes
        serial_no=$(fdtget "${dtb_out}" /board "$attr_sn")
        hw_rev=$(fdtget "${dtb_out}" /board "$attr_hw_rev")
        cal_file_fmt=$(fdtget "${dtb_out}" / "$attr_brdfile_fmt")

        printf '\n[Info]\n'
        printf '  BB Serial no:    %s\n' "$serial_no"
        printf '  HW Rev:          %s\n' "$hw_rev"
        printf '  CAL file type:   %s\n' "$cal_file_fmt"
        if [ "$custom_rev" = "1.0" ] ; then
            brdfile_name="$(echo "$brd_gz" | sed 's/.gz//g')"
            printf '  BRD file:        %s\n\n' "$brdfile_name"
        fi

        fct_store_cal_info > "${dest}/calfiles.info"
        echo "wlan${wlanix}:  $dest cal files retrieved. See calfiles.info for details"
    done    # end-of-loop

    if [ ! $keep_ws ] ; then
        echo "clean up ${workspace}"
        rm -rf "$workspace"
    fi

    # return back to current dir
    cd - >/dev/null || exit
    printf 'Done\n\n'
} # end-of-main

copy_brd_file_to_target () {

    ## Decompress *.gz, then *.brd
    if [ $DEBUG ]; then printf '\n gunzip\n'; fi

    # shellcheck disable=SC2154
    brd_gz="$filename"
    brd_out="wlan${wlanix}.brd"
    target_name="${brd_filename}"

    if [ $DEBUG ]; then echo "brd_gz=${brd_gz}, brd_out=${brd_out}"; fi

    gunzip -cf "${brd_gz}" > "${brd_out}"
    if [ $DEBUG ]; then echo "cal_file: ${brd_out}"; fi


    ## Copy both cal and info files into file system
    # test mode: store into tmp dir instead of target path
    if [ "$op_mode" = "$OP_MODE_TEST" ] ; then
        dest="${workspace}/native/$(fct_array_ith "$PCI_PATH" "$((wlanix+1))")"
        rm -rf "$dest"
        echo ">> Test mode: ON"
        echo "   output dir: ${workspace}/native/"
    fi

    mkdir -p "$dest"
    cp "${brd_out}" "${dest}/${target_name}"
    ln -sf "${target_name}" "${dest}/wil6436.brd"
    if [ $DEBUG ]; then
        printf '\n cp both %s and calfiles.info into %s\n\n' "$brd_out" "$dest/"
    fi
}

## Copy files read from RFBB EEPROM to file system
# $1: cal_file_fmt (BRD / BRD+JSON / JSON)
copy_cal_files_to_target () {

    ## Decompress zst then untar
    if [ $DEBUG ]; then printf '\n Decompress zst followed by untar\n'; fi

    # shellcheck disable=SC2154
    calfiles_zst="$filename"
    calfiles_tar="cal_files_wlan${wlanix}.tar"
    dir_name="cal_files_wlan${wlanix}"

    if [ $DEBUG ]; then echo "calfiles_zst=${calfiles_zst}"; fi

    #Create subdir for json files
    mkdir "${dir_name}"

    zstd -cfd "${calfiles_zst}" > "${dir_name}/${calfiles_tar}"
    if [ $DEBUG ]; then echo "json_tar_file: ${dir_name}/${calfiles_tar}"; fi

    tar -C "${dir_name}" -xf "${dir_name}/${calfiles_tar}"

    ## Copy both cal and info files into file system
    # test mode: store into tmp dir instead of target path
    if [ "$op_mode" = "$OP_MODE_TEST" ] ; then
        dest="${workspace}/native/$(fct_array_ith "$PCI_PATH" "$((wlanix+1))")"
        rm -rf "$dest"
        echo ">> Test mode: ON"
        echo "   output dir: ${workspace}/native/"
    fi

    mkdir -p "$dest"
    cp "${dir_name}"/"${MANIFEST_FNAME}" "${dest}"
    if echo "${cal_file_fmt}" | grep -q "JSON" ; then
        cp "${dir_name}"/*json "${dest}"
    fi
    if echo "${cal_file_fmt}" | grep -q "BRD" ; then
        #shellcheck disable=SC2038
        brd_fname=$(find "${dir_name}" -iname "*.brd" -printf "%f")
        cp "${dir_name}"/"${brd_fname}" "${dest}"
        if [ "$brd_fname" != "wil6436.brd" ] ; then
            ln -sf "${brd_fname}" "${dest}"/wil6436.brd
        fi
    fi

    if [ $DEBUG ]; then
        printf '\n cp both %s and calfiles.info into %s\n\n' "$brd_out" "$dest/"
    fi
}

## Help
fct_usage () {
    echo "$0 <args>"
    echo "   -p '<pci_address of BB>'   // PCI address of RFBB to fetch cal files from"
    echo "   -l '<wlan index of BB>'    // Select wlan index no (0 - 3)"
    echo "   -k                         // Preserve working dir"
    echo "   -t                         // Test mode, save only into working dir"
    echo "   -v                         // Enable debug msg"
    echo "   -m                         // Print wlan index to pci address mapping"
    echo "   -h                         // Help"
    echo ""
    echo "e.g.,"
    echo "   $0 -l 1                    // Update wlan1 cal file(s) only"
    echo "   $0 -l '0 1 2 3'            // Update all BB cal files"
    echo "   $0 -p 0002:03:00.0         // Update pci address 2.3 (wlan2) BB cal files"
    echo ""
    exit 1
}

fct_store_cal_info () {
    printf "# Native cal file info on RF Board\\n\\n"
    printf "BB_SERIAL_NO=\"%s\"\\n" "$serial_no"
    printf "HW_REV=\"%s\"\\n" "$hw_rev"
    printf "CAL_FILE_FORMAT=\"%s\"\\n" "$cal_file_fmt"
    if [ "$custom_rev" = "1.0" ] ; then
        printf "CAL_FILE_NAME=\"%s\"\\n" "$brdfile_name"
    fi
    printf "TILE_MOUNT_TYPE=\"%s\"\\n" "$tile_mount_str"
    printf "RF_BASEBAND=\"%s\"\\n" "wlan$wlanix"
    printf "PCI_BUS_Id=\"%s\"\\n" "$(fct_array_ith "$PCI_PATH" "$((wlanix+1))")"
    printf "UPDATE_DATE=\"%s\"\\n" "$(date +'%T %m-%d-%Y')"
}

main "$@"
