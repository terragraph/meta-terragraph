DESCRIPTION = "PMD Driver for wil6436 WiGig chip"
LICENSE = "ISC"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/ISC;md5=f3b90e78ea0cffb20bf5cca7947a896d"

require ../wigig-utils-oss/wigig-utils-oss.inc

DEPENDS += "dpdk"
DEPENDS += "nl-driver-if-headers"
DEPENDS += "libnl"

SRC_URI += "file://0003-wil6210-dpdk-add-missing-mutex_lock-call-in-wil_slav.patch \
            file://0004-wil6210-dpdk-Upgrade-to-DPDK-20.11.2.patch \
            "

S = "${WORKDIR}/git"

inherit module kernel-extra-headers-export meson

MESON_SOURCEPATH = "${S}/dpdk"

do_compile() {
	meson_do_compile
	cd "${S}/dpdk/modules/dpdk-dhd"
	module_do_compile
}

do_install() {
	# Install PMD lib, external API header, and test binary
	meson_do_install

	# Install the kernel module
	cd ${S}/dpdk/modules/dpdk-dhd
	# Copy Module.symvers file to expected location so it works with devtool
	cp Module.symvers ${B}
	module_do_install
}

# The kernel module gets split into separate package,
#  make sure it can be found
PROVIDES += "kernel-module-dpdk-dhd"

# Package examples separately
PACKAGES += "${PN}-examples"
FILES_${PN}-examples = "${bindir}/wiltest"
