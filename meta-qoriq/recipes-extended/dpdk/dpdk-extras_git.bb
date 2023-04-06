DESCRIPTION = "Data Plane Development Kit Extended utilities"
HOMEPAGE = "http://dpdk.org"
LICENSE = "BSD"
LIC_FILES_CHKSUM = "file://COPYING;md5=4ac6ab4e4bcf4c79cb9257f3c61934f6"

RDEPENDS_${PN} = "bash"

SRC_URI = "git://github.com/nxp-qoriq/dpdk-extras.git;protocol=https;nobranch=1"
SRCREV = "d17f3e065474a41e314ff73b9035e5b3dc021a28"

S = "${WORKDIR}/git"

DPAA_VER ?= "dpaa2"

do_install() {
    install -d ${D}/${bindir}/dpdk-extras

    for file_suffix in xml sh; do
        if [ "`ls ${S}/${DPAA_VER}/*.${file_suffix}`" != "" ]; then
            install -m 755 ${S}/${DPAA_VER}/*.${file_suffix} ${D}/${bindir}/dpdk-extras
        fi
    done
}

PACKAGE_ARCH = "${MACHINE_ARCH}"
COMPATIBLE_MACHINE = "(ls2080ardb|ls2084ardb|ls2088a|ls1043a|ls1046a|ls1088a)"
