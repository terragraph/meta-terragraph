SUMMARY = "Reset Configuration Word"
DESCRIPTION = "Reset Configuration Word - hardware boot-time parameters for the QorIQ targets"
LICENSE = "BSD"
LIC_FILES_CHKSUM = "file://LICENSE;md5=45a017ee5f4cfe64b1cddf2eb06cffc7"

DEPENDS += "tcl-native"

inherit deploy siteinfo

SRC_URI = "git://github.com/nxp-qoriq/rcw.git;nobranch=1;protocol=https"
SRCREV = "f1377876cc06a87ec8afa8a3412ca7c8455861f2"

S = "${WORKDIR}/git"

export PYTHON = "${USRBINPATH}/python2"

# Extra boards
SRC_URI += "file://board"
SRC_URI += "file://makefile.patch"

# Our idea of patching - copy whole board defintiions over
do_copy_board_defs () {
    for dir in ${WORKDIR}/board/* ; do
        cp -r ${dir} ${S}
    done
}

do_patch_append () {
    bb.build.exec_func('do_copy_board_defs', d)
}

# platforms we want to genarate rcw for
MLIST = "ls1012atg ls1048atg ls1088ardb ls1012ardb fb-ls1048a-puma fb-ls1048a-mbh"

do_install () {
    for BOARD in ${MLIST}; do
        oe_runmake BOARDS=${BOARD} DESTDIR=${D}/boot/rcw/ install
    done
}

do_deploy () {
    install -d ${DEPLOYDIR}/rcw
    cp -r ${D}/boot/rcw/* ${DEPLOYDIR}/rcw/
}
addtask deploy after do_install

PACKAGES += "${PN}-image"
FILES_${PN}-image += "/boot"

COMPATIBLE_MACHINE = "(qoriq)"
PACKAGE_ARCH = "${MACHINE_ARCH}"
