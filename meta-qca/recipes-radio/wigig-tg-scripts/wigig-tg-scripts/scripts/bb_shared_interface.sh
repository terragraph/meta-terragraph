#!/bin/sh

## Shared interface for native cal file support
#  - used by bb_store_brdfile.sh, bb_fetch_brdfile.sh
#

# Cal file format
CALFILE_FMT="BRD BRD+JSON JSON"
: "$CALFILE_FMT"

# op_mode
OP_MODE_NORMAL="1"              # normal operation by bb_fetch_brdfile.sh
OP_MODE_TEST="2"                # test operation by bb_fetch_brdfile.sh
OP_MODE_READ_BACK="3"           # read back operation by bb_store_brdfile.sh

# tile mount type
TILE_MOUNT_TYPE="PLANAR ANGULAR"

# header v2 Manifest filename
MANIFEST_FNAME="manifest.cal.sig"

## find path for each rfbb eeprom from sysfs DT ordered by wlan0-3 indices
#  return array of baseband i2c path
fct_get_eeprom_path () {

    eeprom_path=
    for i in $(seq 0 3) ; do
      eeprom_path=$(printf "%s %s" "$eeprom_path" "$( tr -d '\0' < "/sys/firmware/devicetree/base/chosen/wlan${i}_eeprom" )")
    done
    eeprom_path=$( echo "$eeprom_path" | sed 's/^\ //' )

    echo "$eeprom_path"         # return
}
#shellcheck disable=SC2034
EEPROM_PATH=$(fct_get_eeprom_path)

## find list of pci addresses ordered by wlan0-3
#  return array of pci addresses
fct_get_pci_path () {
    pci_path=
    for i in $(seq 0 3) ; do
        pci_path=$(printf "%s %s" "$pci_path" "$( tr -d '\0' < "/sys/firmware/devicetree/base/chosen/wlan${i}_pci" )")
    done
    pci_path=$( echo "$pci_path" | sed 's/^\ //' )

    echo "$pci_path"         # return
}
PCI_PATH=$(fct_get_pci_path)

## Check if pci address is in list of valid pci addresses
#  $1: pci_addr
#  return 1 if valid, 0 otherwise
is_pci_addr_valid () {
    pci_addr="$1"
    ret=0
    for i in $(seq 1 4) ; do
        entry=$(fct_array_ith "$PCI_PATH" "$i")
        if [ "$entry" = "$pci_addr" ] ; then
            ret=1
        fi
    done

    echo "$ret"     #return
}

## Get i-th array
#  $1: array to get i-th element
#  $2: index number in array (starting from 1)
#  return: string from array element
fct_array_ith () {
    array="$1"
    idx="$2"
    echo "$array" | cut -d ' ' -f "$idx"
}

print_wlan_ix_pci_mapping () {

    for ix in $(seq 0 3) ; do
        echo " wlan${ix} = pci $(fct_array_ith "$PCI_PATH" "$((ix+1))")"
    done
}

## Read header info and decide to update cal files based on signature
#
#  - For read back mode, run validation test to check the correct flashing
#    operation by reading RF eeprom
#
#  $1: wlanix: wlan index [0-3]
#  $2: destination path to store cal files
#  $3: BB_PATH (i2c eeprom address list ordered by wlanix)
#  $4: operation mode (1: normal, 2: test, 3: read_back)
#  $5: working dir
#  return: -1 (header error), 0 (no update), 1 (update or validation success)
fct_check_update () {
    #bb_no="$1"
    wlanix="$1"
    brd_current="$2/wil6436.brd"
    dest_folder="$2"
    manifest_file="$2/${MANIFEST_FNAME}"
    EEPROM_PATH="$3"
    op_mode="$4"
    ws_tmp="$5"
    need_cal_file_update="0"

    eeprom=$(fct_array_ith "${EEPROM_PATH}" "$((wlanix+1))")
    hdr_version=0

    # check the availability of installed BB and eeprom
    if [ -z "$eeprom" ] || [ ! -f "$eeprom" ]; then
        echo "wlan${wlanix} eeprom does not exist, $eeprom"
        bb_ret_value=-1
        return
    fi

    # Fixed maximum header length of 128 bytes
    BLK_SIZE=128
    HEADER_LEN=$BLK_SIZE
    HEADER_MARKER="@header:"
    HEADER_MARKER_v2="@header_v2:"

    if [ "$op_mode" = "$OP_MODE_READ_BACK" ] ; then
        ws_tmp="$5/read-back"
    fi

    mkdir -p "$ws_tmp"
    dtb_out="${ws_tmp}/wlan${wlanix}.dtb"
    bb_header="${ws_tmp}/wlan${wlanix}.header"
    bb_dump="${ws_tmp}/wlan${wlanix}.dump"

    # extract header only
    error="$(dd if="$eeprom" of="$bb_header" bs="$HEADER_LEN" count=1 2>&1)"

    if grep -q "$HEADER_MARKER" "$bb_header" ; then
      hdr_version="1"
    elif grep -q "$HEADER_MARKER_v2" "$bb_header" ; then
      hdr_version="2"
    else
        echo "Error: header is not valid"
        bb_ret_value=-1
        return
    fi

    # signature (md5) of brd file (v1) or manifest file (v2)
    sig_brd_update=$(strings "$bb_header" | cut -d ' ' -f2 | cut -d '=' -f2)
    sig_manifest=${sig_brd_update}

    # payload length
    plen=$(strings "$bb_header" | cut -d ' ' -f3 | cut -d '=' -f2)

    # tile_mount type
    tile_mount=$(strings "$bb_header" | cut -d ' ' -f4 | cut -d '=' -f2)
    tile_mount_str=$(fct_array_ith "$TILE_MOUNT_TYPE" "$tile_mount")

    printf '[Header info]\n'
    if [ "1" = "$hdr_version" ] ; then
        printf '  signature (brd):   %s\n' "$sig_brd_update"
    elif [ "2" = "$hdr_version" ] ; then
        printf '  signature (mnfst):  %s\n' "$sig_manifest"
    fi
    
    printf '  payload length :   %s\n' "$plen"
    printf '  tile mount type:   %s\n\n' "$tile_mount_str"

    if [ "1" = "$hdr_version" ] ; then
        if [ ! -f "$brd_current" ] ; then
            need_cal_file_update="1"
        elif [ "$op_mode" = "$OP_MODE_NORMAL" ] ; then # Do not check for test mode
            # current brdfile
            sig_brd_current=$(md5sum "$brd_current" | cut -d' ' -f1)

            # compare signature
            if [ ! "$sig_brd_update" = "$sig_brd_current" ]; then
                echo "Update required: brd signature mismatch"
                need_cal_file_update="1"
            fi
        fi
    elif [ "2" = "$hdr_version" ] ; then
        if [ ! -f "$manifest_file" ] ; then
            need_cal_file_update="1"
        elif [ "$op_mode" = "$OP_MODE_NORMAL" ] ; then
            verify_json_files_checksums "$manifest_file" "$sig_manifest" "${ws_tmp}" "${dest_folder}"
        fi
    fi

    if [ "0" = "$need_cal_file_update" ] && [ "$op_mode" = "$OP_MODE_NORMAL" ] ; then
        echo "No update required"
        bb_ret_value=0
        return
    fi

    echo "Update..."

    # calculate block count
    blk_cnt=$(( (plen + BLK_SIZE - 1) / BLK_SIZE + 1))

    # dump whole img
    error="$(dd if="$eeprom" of="${bb_dump}" bs="$BLK_SIZE" count="$blk_cnt" 2>&1)"
    : "$error"

    # trim out header and payload only (dtb file)
    tail -n +2 "$bb_dump" > "$dtb_out"

    if [ "$op_mode" = "$OP_MODE_NORMAL" ] || [ "$op_mode" = "$OP_MODE_TEST" ] ; then
        bb_ret_value=1
        return
    elif [ "$op_mode" = "$OP_MODE_READ_BACK" ] ; then
        # inspect further on cal file on dtb by integrity checksum test

        #Check if dtb v1 or v2. Get the appropriate attributes
        attr_custom_rev="custom_rev"
        custom_rev=$(fdtget "${dtb_out}" / "${attr_custom_rev}")
        if [ "$custom_rev" = "1.0" ] ; then
            attr_integrity_brd="integrity"
            attr_fname_brd="filename"

            check_file_integrity "${attr_integrity_brd}" "${attr_fname_brd}" "${dtb_out}" "${ws_tmp}" "${custom_rev}" "${wlanix}"
            if [ ! "$bb_ret_value" -eq "1" ] ; then
                return
            fi
        elif [ "$custom_rev" = "2.0" ] ; then
            attr_integrity_json="integrity_zst"
            attr_fname_json="filename_json"
            check_file_integrity "${attr_integrity_json}" "${attr_fname_json}" "${dtb_out}" "${ws_tmp}" "${custom_rev}" "${wlanix}"
        else
            echo "Error: custom_rev not found in dtb. Corrupted dtb?"
            bb_ret_value=-1
            return;
        fi

    fi
    : "$bb_ret_value"
}

## Verify whether json and brd cal files are current
#  Step 1: compare checksum of manifest.cal.sig in eeprom header
#          with checksum of manifest.cal.sig in corresponding
#          native cal folder.
#  Step 2: check md5sum of brd and json files in native cal folder
#          with md5sum contained in manifest.cal.sig.
#
#  Parameters:
#     $1 : manifest_cal_sig_fname. Filename containing md5sums
#     $2 : sig_manifest. md5sum of manifest.cal.sig in header
#     $3 : ws_tmp. Temporary workspace
#     $4 : dest_folder.
verify_json_files_checksums () {
    manifest_file="$1"
    sig_manifest_hdr="$2"
    ws_tmp="$3"
    dest_folder="$4"

    # current json files signature
    #shellcheck disable=SC2002

    current_manifest_md5sum=$(md5sum "$manifest_file" | cut -d ' ' -f1)

    if [ ! "$current_manifest_md5sum" = "$sig_manifest_hdr" ] ; then
        echo "Update required: manifest.cal.sig file signature mismatch"
        need_cal_file_update="1"
        return
    fi

    current_dir="$PWD"
    cd "$dest_folder" || exit

    if ! md5sum -c --quiet "${manifest_file}" ; then
        echo "Update required: json files signature mismatch"
        need_cal_file_update="1"
        cd "$current_dir" || exit
        return
    fi

    cd "$current_dir" || exit
}


## Verify integrity checksum: match md5sum of stored archive with value
##  stored in the respective 'integrity_xxx' field to rule out data corruption
#   Params:
#           $1: attr_integrity - integrity property name in dtb
#           $2: attr_fname     - filename property name in dtb
#           $3: dtb_out        - dtb filename
#           $4: ws_tmp         - working folder path
#           $5: custom_rev     - custom rev [1.0 / 2.0]
check_file_integrity () {
    attr_integrity="$1"
    attr_fname="$2"
    dtb_out="$3"
    ws_tmp="$4"
    custom_rev="$5"
    wlanix="$6"

    # Get integrity hash value
    integrity_checksum=$(fdtget "${dtb_out}" / "${attr_integrity}")

    # Get cal file and save into *.gz.extracted
    # - use fdtdump instead of fdtget to avoid decimal to ascii decoding
    if [ "1.0" = "${custom_rev}" ] ; then
        filename="${ws_tmp}/$(fdtget "${dtb_out}" / "${attr_fname}")"
        fname_extracted="${filename}.extracted"
    else
        filename="${ws_tmp}/cal_files_wlan${wlanix}.tar.zst"
        fname_extracted="${filename}.extracted"
    fi

    fdtdump "${dtb_out}" 2>&1 | grep "boardfile =" | cut -d '=' -f2 \
            | tr -d ' [];<>' | sed 's/0x//g' > "${fname_extracted}"

    integrity_data="${integrity_checksum}  ${filename}"

    if [ -f "${fname_extracted}" ]; then
        fct_packing "${fname_extracted}" "${filename}"
    else
        echo "Error: not available, ${fname_extracted}"
        bb_ret_value=-1
        return
    fi

    # Check integrity_checksum by md5
    if ! echo "${integrity_data}" | md5sum -c --quiet ; then
        echo "Error: mismatch between stored and computed checksum for wlan${wlanix} ($filename)"
        bb_ret_value=-1
    else
        bb_ret_value=1
    fi
    return
}

## Convert hex strings from extracted file contents into binary format data
fct_packing () {
    lua -e "
        inFile = io.open('$1', 'rb')
        s = inFile:read('*a')
        inFile:close()
        bin = s:gsub('%s+', ''):gsub('..', function(c)
            return string.char(tonumber(c, 16))
        end)
        outFile = io.open('$2', 'w')
        outFile:write(bin)
        outFile:close()
    "
}
