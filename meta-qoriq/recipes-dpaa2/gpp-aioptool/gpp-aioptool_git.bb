DESCRIPTION = "AIOP Tool is a userspace application for performing operations \
on an AIOP Tile using MC interfaces. This application enables the user to \
fetch status of tile, load a valid ELF file and run it on a tile and get and set \
time of day."
SECTION = "dpaa2"
LICENSE = "BSD"
LIC_FILES_CHKSUM = "file://LICENSE;md5=386a6287daa6504b7e7e5014ddfb3987"

SRC_URI = "git://source.codeaurora.org/external/qoriq/qoriq-components/gpp-aioptool;nobranch=1 \
"
SRCREV = "5160558bb6830c233d69b1e84d56c8bebac427c6"

SRC_URI += "file://0001-Do-not-include-deprecated-libio.h.patch"

S = "${WORKDIR}/git"

TARGET_CC_ARCH += "${LDFLAGS}"

# GCC10 now defaults to -fno-common and older packages fail to build.
TARGET_CFLAGS += "-fcommon"

EXTRA_OEMAKE = 'CC="${CC} ${CFLAGS}" LD="${CC}"'

do_install () {
    oe_runmake install DESTDIR=${D}
    chown -R root:root ${D}
}

COMPATIBLE_MACHINE = "(ls2088a|ls1088a)"
