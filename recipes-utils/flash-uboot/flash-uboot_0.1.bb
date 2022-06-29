SUMMARY = "Uboot flashing script"
DESCRIPTION = "Utility to flash uboot image"
SECTION = "core"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://flash_uboot.sh \
	   file://flash_uboot_mv.sh \
	   file://flash_uboot_nxp.sh \
           file://LICENSE \
          "

S = "${WORKDIR}"

do_install_append() {
	mkdir -p ${D}${sbindir}/
	install -m 0755 ${WORKDIR}/flash_uboot.sh ${D}${sbindir}/flash_uboot
	install -m 0755 ${WORKDIR}/flash_uboot_mv.sh ${D}${sbindir}/flash_uboot_mv
	install -m 0755 ${WORKDIR}/flash_uboot_nxp.sh ${D}${sbindir}/flash_uboot_nxp
}

FILES_${PN} = "${sbindir}"
