SUMMARY = "Management Complex Utils"
LICENSE = "Freescale-EULA"
LIC_FILES_CHKSUM = "file://LICENSE;md5=386a6287daa6504b7e7e5014ddfb3987"

DEPENDS = "dtc-native"

inherit deploy

SRC_URI = "git://github.com/nxp-qoriq/mc-utils.git;protocol=https"
SRCREV = "8b3fe8cadba4a091c5ed49c25f2c33111c368426"

S = "${WORKDIR}/git"

SRC_URI += "file://dpl-eth.0x15-0x14.dts"
SRC_URI += "file://dpc.0x15-0x14.dts"
SRC_URI += "file://dpl-eth.0x19-0x14.dts"
SRC_URI += "file://dpc.0x19-0x14.dts"
SRC_URI += "file://dpl-eth.mbh.0x15-0x13.dts"
SRC_URI += "file://dpc.mbh.0x15-0x13.dts"
SRC_URI += "file://ls1088ardb-bump-number-of-dpmcp-objects.patch"


REGLEX_ls2088a = "ls2088a"
REGLEX_ls1088a = "ls1088a"

do_copy_extra_mc_utils () {
    dpl_dst=${S}/config/${REGLEX}/TG
    dpl_src=${WORKDIR}

    mkdir -p ${dpl_dst}
    /bin/cp ${dpl_src}/dpl* ${dpl_dst}
    /bin/cp ${dpl_src}/dpc* ${dpl_dst}

}

addtask copy_extra_mc_utils before do_compile after do_patch

do_compile () {
    cd config
    oe_runmake
}

do_install () {
    install -d ${D}/boot
    install -m 644 ${S}/config/${REGLEX}/RDB/*.dtb ${D}/boot
    install -m 644 ${S}/config/${REGLEX}/TG/*.dtb ${D}/boot
    install -m 644 ${S}/config/${REGLEX}/RDB/custom/*.dtb ${D}/boot
}

do_deploy () {
    install -d ${DEPLOYDIR}/mc-utils
    install -m 644 ${S}/config/${REGLEX}/RDB/*.dtb ${DEPLOYDIR}/mc-utils
    install -m 644 ${S}/config/${REGLEX}/TG/*.dtb ${DEPLOYDIR}/mc-utils
    install -m 644 ${S}/config/${REGLEX}/RDB/custom/*.dtb ${DEPLOYDIR}/mc-utils
}
addtask deploy before do_build after do_install

PACKAGES += "${PN}-image"
FILES_${PN}-image += "/boot"

COMPATIBLE_MACHINE = "(ls2080ardb|ls2088a|ls1088a)"
PACKAGE_ARCH = "${MACHINE_ARCH}"
