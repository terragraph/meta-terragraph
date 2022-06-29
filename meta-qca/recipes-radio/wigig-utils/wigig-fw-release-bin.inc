SUMMARY = "Pre-compiled TG firmware for wil6210"
DESCRIPTION = "Pre-compiled Terragraph firmware for Wilocity 60g WiFi card wil6210"
SECTION = "net"
LICENSE = "CLOSED"

PROVIDES = "virtual/wigig-fw-bin"
RPROVIDES_${PN} = "wigig-fw-bin"

do_install() {
    cd ${WORKDIR}/${WIGIG_RELEASE_DIR}
    install -d ${D}${base_libdir}/firmware

    for fw in TALYN*
    do
        install -d ${D}${base_libdir}/firmware/${fw}
        install -m 0644 ${fw}/*.fw  ${D}${base_libdir}/firmware/${fw}

        # The 2020/11/30 drop renamed wil6210.fw to wil6436.fw but inconsistently.
        # For now make a link if this case is detected.
        if [ ! -e "${D}${base_libdir}/firmware/${fw}/wil6210.fw" ]; then
            ln -s ${base_libdir}/firmware/${fw}/wil6436.fw ${D}${base_libdir}/firmware/${fw}/wil6210.fw
        fi

        # Install the board files if they are present.
        # These are sometimes missing from IF2IF because they are redundant.
        for brdfile in ${fw}/*.brd; do
            [ -f "$brdfile" ] || continue
            install -m 0644 "$brdfile" ${D}${base_libdir}/firmware/${fw}
        done
        # Install the debug strings if they are present.
        for strfile in ${fw}/*.bin; do
            [ -f "$strfile" ] || continue
            install -m 0644 "$strfile" ${D}${base_libdir}/firmware/${fw}
        done
    done

    # Set the default firmware and board file that will be used.
    ln -s ${base_libdir}/firmware/TALYN_M_B0/wil6210.fw ${D}${base_libdir}/firmware/wil6436.fw
    if [ ! -e ${D}${base_libdir}/firmware/TALYN_M_B0/${PUMA_BOARD_FILE_RF} ]; then
        echo "${PUMA_BOARD_FILE_RF} doesn't exist."
        exit 1
    fi
    cp ${D}${base_libdir}/firmware/TALYN_M_B0/${PUMA_BOARD_FILE_RF} ${D}${base_libdir}/firmware/TALYN_M_B0/wil6436.brd
    ln -s ${base_libdir}/firmware/TALYN_M_B0/wil6436.brd ${D}${base_libdir}/firmware/wil6436.brd
    # On Puma, install default RF + IF board files used in /usr/bin/ifrf_fw_swap.sh
    ln -s ./${PUMA_BOARD_FILE_RF} ${D}${base_libdir}/firmware/TALYN_M_B0/wil6436_puma.brd
    if [ -d ${D}${base_libdir}/firmware/TALYN_M_B0_IF2IF ]; then
        if [ ! -e ${D}${base_libdir}/firmware/TALYN_M_B0_IF2IF/${PUMA_BOARD_FILE_IF} ]; then
            echo "${PUMA_BOARD_FILE_IF} doesn't exist."
            exit 1
        fi
        ln -s ./${PUMA_BOARD_FILE_IF} ${D}${base_libdir}/firmware/TALYN_M_B0_IF2IF/wil6436_puma.brd
    fi

    # Copy the debug strings if they are present.
    cd ${D}${base_libdir}/firmware/TALYN_M_B0
    for strfile in ${D}${base_libdir}/firmware/TALYN_M_B0/*.bin; do
        [ -f "$strfile" ] || continue
        str=$(basename $strfile)
        ln -s ${base_libdir}/firmware/TALYN_M_B0/$str ${D}${base_libdir}/firmware/$str
    done
}

FILES_${PN} += "${base_libdir}/firmware"
