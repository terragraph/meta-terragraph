SUMMARY = "wil6210 driver"
DESCRIPTION = "Wilocity 60g WiFi card wil6210 support"
SECTION = "net"
LICENSE = "ISC"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/ISC;md5=f3b90e78ea0cffb20bf5cca7947a896d"

require wigig-utils-oss.inc
DEPENDS = "kernel-module-direct-vpp"

SRC_URI += "file://0100-DVPP-add-direct-interface.patch \
            file://0101-DVPP-implement-packet-injection-DVPP-API.patch \
            file://0102-DVPP-support-for-multi-segment-packets.patch \
            file://0103-DVPP-do-not-map-unmap-buffers-on-cache-coher.patch \
           "

MODULES_MODULE_SYMVERS_LOCATION = "wil6210"

inherit module

S = "${WORKDIR}/git"

# Extra configuration for the module Makefile
CONFIG_WIL6210 = "m"
export CONFIG_WIL6210

CONFIG_CONFIG_WIL6210_ISR_COR = "y"
export CONFIG_CONFIG_WIL6210_ISR_COR

CONFIG_WIL6210_DEBUGFS = "y"
export CONFIG_WIL6210_DEBUGFS

# Use git to apply our changes
PATCHTOOL = "git"

EXTRA_OEMAKE += 'CFLAGS_MODULE="-I${STAGING_INCDIR}/kernel-module-direct-vpp -DDVPP -DCACHE_COHERENT" --directory=wil6210'

do_install_append () {
	install -Dm0644 ${S}/wil6210/slave.h ${D}${includedir}/${BPN}/wil6210/slave.h
}
