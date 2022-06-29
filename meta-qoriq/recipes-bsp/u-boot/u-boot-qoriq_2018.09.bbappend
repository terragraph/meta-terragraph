UBOOT_SPL_BINARY ?= "u-boot-with-spl.bin"

# If SRCREV is being updated and if we also want to also upgrade uboot to
# this new rev please bump up TGPH_UBOOT_VERSION.
TGPH_UBOOT_VERSION = "1.0"

UBOOT_CONFIG = "ls1048atg ls1088ardb fb-ls1048a-puma fb-ls1048a-puma2 fb-ls1048a-mbh"

# Unix timestamp for 1/1/2020 00:00:00 (for reproducible builds)
EXTRA_OEMAKE += "SOURCE_DATE_EPOCH=1577836800"

do_add_tgph_uboot_version () {
    for config in ${UBOOT_MACHINE}; do
        echo CONFIG_IDENT_STRING=\"-Terragraph Uboot Version: ${TGPH_UBOOT_VERSION}\" >>  ${S}/configs/${config}
    done
}

do_patch_append () {
    bb.build.exec_func('do_add_tgph_uboot_version', d)
}

do_compile_append () {
    if [ "x${UBOOT_CONFIG}" != "x" ]
    then
        for config in ${UBOOT_MACHINE}; do
            i=`expr $i + 1`;
            for type  in ${UBOOT_CONFIG}; do
                j=`expr $j + 1`;
                if [ $j -eq $i ]
                then
		              if [ -e ${B}/${config}/${UBOOT_SPL_BINARY} ] ; then
			              cp ${B}/${config}/${UBOOT_SPL_BINARY}  ${B}/${config}/u-boot-${type}.${UBOOT_SUFFIX}
		              fi
                fi
            done
            unset  j
        done
        unset  i
    fi
}

do_install_append () {
	# Let us move u-boot to /usr/share/u-boot
	install -d ${D}${datadir}/u-boot
	/bin/mv  ${D}/boot/u-boot* ${D}${datadir}/u-boot/
	# remove spurious link u-boot.bin
	/bin/rm ${D}${datadir}/u-boot/u-boot.bin
}

FILES_${PN} = "${datadir}/u-boot"

