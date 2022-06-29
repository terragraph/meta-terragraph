#!/bin/sh

##
# Write cal file(s) into baseband eeprom by dtb
#
#   * Encapsulated format for image frame
#     - Header (signature, payload len, tile mount type)
#       check only header data portion to check update availability
#     - Payload (dtb file)
#       dtb file which contains native board and/or json files
#
#   * Common shared interfaces from bb_shared_interface.sh
#
# Steps
#   1. create a header based on cal file type:
#       @header_v2.0: sig plen tm
#       - max 128 bytes
#       - @header_v2: marker
#       - sig: md5sum of manifest file containing md5sums of cal files
#       - plen: payload length (dtb file)
#       - tm: tile_mount - planar(1), angular(2)
#
#   # create a payload
#   2. zstd <cal files + manifest> => cal_files.tar.zst
#   3. md5sum cal_files.tar.zst for cal file integrity
#      set value for required fields on dts
#       - integrity_zst:    md5sum cal_files.tar.zst
#       - tile_mount:       PLANAR
#       - calfile format: BRD, BRD+JSON, JSON
#       - boardfile:        <content of cal_files.tar.zst>
#   4. generate bb.dts
#   5. generate bb.dtb
#       - dtc bb.dts -o bb.dtb
#
#   6. create an encapsulated frame by header + payload
#   7. flash into eeprom
#   8. check the correct flashing operation by reading RF eeprom again, optional
#

bb_ret_value=                       # return value

main () {
    # input arguments
    serial_no=                      # RF board serial no
    json_folder="default"           # path to json cal files
    calfile_type=1                  # cal file type (default: raw)
    wlanix=
    pciaddr=                        # pci address of baseband card

    # dts attributes
    version=0x01
    custom_rev="2.0"
    hw_rev="2.0"                    # PUMA Proto-2
    format="header_payload(dtb)"
    tile_mount=1                    # tile mount type (planar)

    if [ "$#" -lt 1 ]; then
        fct_usage
    fi

    # load common interfaces
    # shellcheck disable=SC1091
    . bb_shared_interface.sh

    # handle option arguments
    while getopts "n:f:g:t:p:l:wrkmh" opt; do
        case $opt in
        n)
            serial_no="$OPTARG"
            ;;
        f)
            json_folder="$OPTARG"
            ;;
        g)
            calfile_type="$OPTARG"
            if [ "${calfile_type}" -gt 3 ] || [ "${calfile_type}" -lt 1 ] ; then
                echo "Invalid calfile type:${calfile_type}"
                exit 1
            fi
            ;;
        t)
            tile_mount=${OPTARG}
            ;;
        p)
            pciaddr=${OPTARG}
            ;;
        l)
            wlanix=${OPTARG}
            ;;
        w)
            write=1
            ;;
        r)
            read_back=1
            ;;
        k)
            keep_ws=1
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

    # working folder
    ws="$(mktemp -d)"           # workspace to save all output

    # Check for wlanix
    # If wlanix not given, then check for pci_addr
    if [ -z "$wlanix" ] && [ -z "$pciaddr" ] ; then
        echo "Must specify pci_address or wlan index"
        rm -rf "$ws"
        exit 1
    fi

    if [ -n "$pciaddr" ] ; then
        if [ "1" != "$(is_pci_addr_valid "$pciaddr")" ] ; then
            echo "PCI Address is NOT valid"
            rm -rf "$ws"
            exit 1
        fi
        # Find wlan index
        if echo "${PCI_PATH}" | grep -q "${pciaddr}" ; then
            wlanix=$(echo "${PCI_PATH}" |  tr ' ' '\n' | sed -n "/${pciaddr}/=")
            # Adjust for 0 based index
            wlanix=$((wlanix-1))
        else
            echo "Invalid PCI address:${pciaddr}"
            rm -rf "$ws"
            exit 1
        fi
    fi

    # standalone mode: support validation check only on read-back
    if [ $read_back ] && [ ! $write ] ; then
        fct_read_back "$wlanix" "$ws"

        if [ ! $keep_ws ] ; then
            echo "clean up ${ws}"
            rm -rf "$ws"
        fi

        exit 0
    fi

    if [ ! -d "$json_folder" ] ; then
        echo "Cannot open json cal file path, ${json_folder}"
        rm -rf "$ws"
        exit 1
    fi
    # Convert to absolute path
    json_folder=$(readlink -f "$json_folder")

    if [ -z "$serial_no" ] ; then
        echo "RF SERIAL_NO is required"
        rm -rf "$ws"
        exit 1
    fi


    # Validate tile_mount type
    if [ "${tile_mount}" -gt 2 ] || [ "${tile_mount}" -lt 1 ] ; then
        echo "Invalid tile_mount type:${tile_mount}"
        rm -rf "$ws"
        exit 1
    fi

    if [ "${wlanix}" -gt 3 ] || [ "${wlanix}" -lt 0 ] ; then
        echo "Invalid wlanix:${wlanix}"
        rm -rf "$ws"
        exit 1
    fi

    # tile mount type (PLANAR=1, ANGULAR=2)
    tile_mount_str=$(fct_array_ith "$TILE_MOUNT_TYPE" "$tile_mount")

    # cal file type (BRD=1, BRD+JSON=2, JSON=3)
    calfile_fmt_str=$(fct_array_ith "$CALFILE_FMT" "$calfile_type")
    if [ "${calfile_fmt_str}" = "BRD+JSON" ] ; then
        echo " ##################################################### "
        echo " ${calfile_fmt_str} is deprecated!"
        echo " ##################################################### "
    fi

    echo "Building dtb for BB wlan${wlanix}..."

    # create folder for cal files under workspace
    ws_calfiles_dir="${ws}/calfiles"
    mkdir -p "${ws_calfiles_dir}"
    archive_name="calfiles.tar"
    archive_zst_name="${archive_name}.zst"
    manifest_file="${ws_calfiles_dir}/${MANIFEST_FNAME}"

    # Check for board file
    num_brd_files=$(find "${json_folder}" -name "*brd" | wc -l)
    if [ "$calfile_fmt_str" != "JSON" ] && [ ! "$num_brd_files" -eq 1 ] ; then
        echo "BRD (*.brd) file not found / Multiple BRD files found"
        rm -rf "$ws"
        exit 1
    fi

    current_dir=$PWD
    cd "${ws_calfiles_dir}" || exit

    if [ "$calfile_fmt_str" != "JSON" ] ; then
        cp "${json_folder}"/*brd .
        #shellcheck disable=SC2035
        md5sum *.brd > "${manifest_file}"
    fi

    if  [ "$calfile_fmt_str" = "BRD+JSON" ] || [ "$calfile_fmt_str" = "JSON" ] ; then
        if [ "$tile_mount_str" = "PLANAR" ] ; then
            json_files=$(find "${json_folder}" -iname "*massive*json")
        else
            json_files=$(find "${json_folder}" -iname "*diversity*json")
        fi

        if [ -z "${json_files}" ] ; then
            echo "#########################################################"
            echo "  ERROR: NO json files found!"
            echo "#########################################################"
            rm -rf "$ws"
            exit 1
        fi

        num_json_files="$(echo "$json_files" | wc -l)"
        if [ ! "$num_json_files" -eq 8 ] ; then
            echo "#########################################################"
            echo "  Warning: Expected 8 json files; found ${num_json_files}"
            echo "#########################################################"
            sleep 1
        fi
        json_files="$(echo "$json_files" | tr '\n' ' ')"
        #shellcheck disable=SC2086
        cp $json_files "${ws_calfiles_dir}"
        #shellcheck disable=SC2046
        md5sum $(find . -iname "*json") >> "${manifest_file}"
    fi

    # Calculate hash value of manifest file
    sig_manifest=$(md5sum "${manifest_file}" | cut -d ' ' -f 1)

    # output
    dts_out="${ws}/wlan${wlanix}.dts"
    dtb_out="${ws}/wlan${wlanix}.dtb"


    # Create archive of cal files
    tar -cf "${ws_calfiles_dir}/${archive_name}" ./*

    # Compress with zst compression
    zstd -19 "${ws_calfiles_dir}/${archive_name}"
    integrity_zst=$(md5sum "${ws_calfiles_dir}/${archive_zst_name}" | cut -d ' ' -f 1)
    cd "$current_dir" || exit

    ## Generate wlan${wlanix}.dts
    fct_generate_dts > "$dts_out"
    ## Generate wlan${wlanix}.dtb
    dtc "$dts_out" -o "$dtb_out"

    if [ ! -f "$dtb_out" ]; then
        echo "fail to build dtb, $dtb_out"
        rm -rf "$ws"
        exit 1
    fi

    # get the length of bb.dtb
    payload_len=$(stat --printf="%s" "$dtb_out")

    # config
    printf '\n[CONFIG]\n'
    printf '  BB Serial no:      %s\n' "$serial_no"
    printf '  HW Rev:            %s\n' "$hw_rev"
    printf '  CAL file format:   %s\n' "$calfile_fmt_str"
    printf '  working dir:       %s\n\n' "$ws"

    # header info
    printf '[Header info]\n'
    printf '  Signature manifest:  %s\n' "$sig_manifest"
    printf '  Signature zst_arch:  %s\n' "$integrity_zst"
    printf '  Payload length:      %s\n' "$payload_len"
    printf '  Tile mount type:     %s\n\n' "$tile_mount_str"

    ## Merge header and payload
    #header="@header: sig=${signature_brd} plen=${payload_len} tm=${tile_mount}"
    ## Format of header changed in v2; signature of manifest file is stored in header now.
    #    Old header format will be accepted in bb_fetch_brdfile.sh to maintain backward
    #    compatibility.

    header="@header_v2: sig=${sig_manifest} plen=${payload_len} tm=${tile_mount}"
    echo "$header" > "$dtb_out.header"
    sed -i "1s/^/$header\\n/" "$dtb_out"

    ## Flash into eeprom
    if [ ! -f "${dtb_out}" ]; then
        echo ">> Fail to build ${dtb_out}"
    fi

    if [ $write ]; then
        i2cpath=$(fct_array_ith "$EEPROM_PATH" "$((wlanix+1))")

        # check the availability of installed BB card / eeprom
        if [ -z "$i2cpath" ] || [ ! -f "$i2cpath" ]; then
            echo "wlan${wlanix} eeprom does not exist, ${i2cpath}"
            rm -rf "$ws"
            exit 1
        fi
        echo "Checking for eeprom size.."
        tmpfile="$(mktemp)"
        dd if="$i2cpath" of="$tmpfile" bs=1 count=128 skip=65536 2> /dev/null
        size_tmp="$(stat --printf="%s" "${tmpfile}")"
        rm "$tmpfile"
        if [ "0" = "$size_tmp" ] ; then
            echo "64KB RFBB EEPROM detected!"
            if [ "$payload_len" -gt 65535 ] ; then
                echo "ERROR! Cal files(${payload_len} bytes) exceeds EEPROM size (64KB)"
                rm -rf "$ws"
                exit 1
            fi
        else
            echo "128KB RFBB EEPROM detected!"
        fi

        echo "${dtb_out} -> ${i2cpath}"
        echo "Flashing..."

        # flash into eeprom
        cat "$dtb_out" > "${i2cpath}"

        if [ ! $keep_ws ] ; then
            echo "clean up ${ws}"
            rm -rf "$ws"
        fi

        echo ">> Done wlan${wlanix} (pci: $(fct_array_ith "$PCI_PATH" "$((wlanix+1))"))..."

        ## 8. Check the correct flashing operation by reading RF eeprom again
        if [ $read_back ]; then
            fct_read_back "$wlanix" "$ws"
        fi
    fi
} # end-of-main

fct_usage () {
    echo "$0 <args>"
    echo "   -n <serial_no>              // Board serial number"
    echo "   -f <folder_path>            // Path to Folder with Cal files for this RFBB"
    echo "   -g <calfile_format>         // Cal file type            [${calfile_type}]"
    echo "       BRD(1), BRD+JSON(2), JSON(3)"
    echo "   -t <tile_mount>             // Antenna tile mount         [${tile_mount}]"
    echo "       PLANAR(1), ANGULAR(2)"
    echo "   -p                          // PCI address to program"
    echo "   -l                          // wlan index to program [0 - 3]"
    echo "   -w                          // Write to EEPROM of selected BB"
    echo "   -r                          // Read back from EEPROM to check flashing error"
    echo "   -k                          // Preserve working dir"
    echo "   -m                          // Print wlan index to pci address mapping"
    echo "   -h                          // Help"
    echo ""
    echo "e.g.,"
    echo "   $0 -n FNW19411000 -f /data/cal_files -p 0000:01:00.0 -w -g 2 -t 1"
    echo "   $0 -n FNW19411000 -f /data/cal_files -l 1 -w -g 2 -t 1"
    echo ""
    echo ""
    echo " Note: pci_address will take precedence over wlan index if both are specified"
    echo " Note: Folder specified in -f option must contain Cal files (1 brd and upto 8 json files)"
    echo "       for current sector only"

    exit 1
}

# Read data from RFBB eeprom to tmp_ws/read_back folder for verification
# $1: wlanix:  index of wlan (0 - 3)
# $2: wl:  temporary workspace
fct_read_back () {
    wlanix="$1"
    ws="$2"

    printf "\\n>> Start validation check by reading eeprom...\\n"
    printf "working dir: %s\\n\\n" "$ws"

    # destination to store cal file(s)
    target_path="/data/firmware/wil6210"     # local native cal file(s) path

    dest="${target_path}/$(fct_array_ith "$PCI_PATH" "$((wlanix+1))")"

    fct_check_update "$wlanix" "$dest" "$EEPROM_PATH" "$OP_MODE_READ_BACK" "$ws"
    if [ "$bb_ret_value" -eq "1" ] ; then
        echo ">> Passed   ( wlan${wlanix} - $(fct_array_ith "$PCI_PATH" "$((wlanix+1))") )"
    else
        echo ">> Failed: please try again to flash into wlan${wlanix} eeprom"
    fi
}

# Generate actual dts (device tree source) file for dtb
fct_generate_dts () {
cat << EOF;
/dts-v1/;

/ {
    version = <$version>;
    custom_rev = "$custom_rev";
    tile-mount = "$tile_mount_str";
    boardfile-format = "$calfile_fmt_str";
    integrity_zst = "$integrity_zst";
    boardfile = /incbin/("calfiles/$archive_zst_name");
    board {
        hw-rev = "$hw_rev";
        hw-sn = "$serial_no";
        format = "$format";
    };
};
EOF
}

main "$@"
