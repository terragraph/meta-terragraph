SUMMARY = "Tools for wil6210"
DESCRIPTION = "Utilities for Wilocity 60g WiFi card wil6210"
SECTION = "net"
LICENSE = "ISC"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/ISC;md5=f3b90e78ea0cffb20bf5cca7947a896d"

require wigig-utils-oss.inc

S = "${WORKDIR}/git"

TOOL_BINARIES = "wil_fw_trace wil_mem wil_memdump"

# The Makefile.3pp from vendor is very badly written
do_compile() {
    oe_runmake CC="${CC} ${CFLAGS} ${LDFLAGS}" \
        SYSROOT_CPP=${STAGING_DIR_TARGET} \
        -C wil6210/tools -f Makefile.3pp \
        ${TOOL_BINARIES}
}

do_install() {
    install -d ${D}${bindir}
    for tool in ${TOOL_BINARIES}
    do
        install -m 0755 wil6210/tools/${tool} ${D}${bindir}
    done
}
