SUMMARY = "DPAA2 Management Complex Firmware"
LICENSE = "NXP-Binary-EULA"
LIC_FILES_CHKSUM = "file://NXP-Binary-EULA.txt;md5=2cb2fee5d3558ee2a81331ed121647ad"

SRC_URI = "https://github.com/NXP/qoriq-mc-binary/archive/refs/tags/mc_release_${PV}.tar.gz"
SRC_URI[md5sum] = "ae5d3a934abd0453460f13d24a001cb0"
SRC_URI[sha256sum] = "acd8ba24e1fa8141e8b11ce6c8aa6d2b163d3341d127df68d27dfc9e50d169cc"

S = "${WORKDIR}/qoriq-mc-binary-mc_release_${PV}"

inherit deploy

REGLEX_ls2088a = "ls2088a"
REGLEX_ls1088a = "ls1088a"

do_install () {
    install -d ${D}/boot
    install -m 755 ${S}/${REGLEX}/*.itb ${D}/boot
}

do_deploy () {
    install -d ${DEPLOYDIR}/mc_app
    install -m 755 ${S}/${REGLEX}/*.itb ${DEPLOYDIR}/mc_app
    # make a symlink to the latest binary
    rm -rf ${DEPLOYDIR}/mc_app/mc.itb
    for mc_binary in `ls ${DEPLOYDIR}/mc_app |sort`;do
        ln -sfT ${mc_binary} ${DEPLOYDIR}/mc_app/mc.itb
    done
}
addtask deploy before do_build after do_install

PACKAGES += "${PN}-image"
FILES_${PN}-image += "/boot"

INHIBIT_DEFAULT_DEPS = "1"
INHIBIT_PACKAGE_STRIP = "1"

COMPATIBLE_MACHINE = "(ls2080ardb|ls2088a|ls1088a)"
PACKAGE_ARCH = "${MACHINE_ARCH}"
