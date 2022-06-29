SUMMARY = "DPAA2 Resource Manager Tool"
LICENSE = "BSD"
LIC_FILES_CHKSUM = "file://COPYING;md5=ec8d84e9cd4de287e290275d09db27f0"

RDEPENDS_${PN} += "bash"

SRC_URI = "git://source.codeaurora.org/external/qoriq/qoriq-components/restool.git;nobranch=1 \
    file://0001-restool-fix-build-error-with-gcc7.patch \
    file://0002-Use-environment-variable-to-disable-bus-rescans.patch \
"
# quilts .pc makes vendor makefile unhappy
PATCHTOOL = "git"

SRCREV = "263474672c9a16d2bbb2dfb80d3fb6ce01ad87fd"

S = "${WORKDIR}/git"

# GCC10 now defaults to -fno-common and older packages fail to build.
EXTRA_OEMAKE = 'CC="${CC}" EXTRA_CFLAGS="-Wno-missing-field-initializers -Wno-missing-braces -fcommon"'

do_install () {
    oe_runmake install DESTDIR=${D}
}

COMPATIBLE_MACHINE = "(ls2080ardb|ls2088ardb|ls1088ardb|ls1043ardb|ls1046ardb)"
PACKAGE_ARCH = "${MACHINE_ARCH}"
