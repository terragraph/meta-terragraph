SUMMARY = "Header files for Netlink Driver Interface"
DESCRIPTION = "Terragraph wireless module Driver Interface header files"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://nl-driver-if-hdr \
          "

S = "${WORKDIR}/nl-driver-if-hdr"

inherit kernel-extra-headers

do_install() {
    install -d ${D}${includedir}/nl-driver-if
    cp -a * ${D}${includedir}/nl-driver-if/

    # For kernel modules using these headers
    kerneldir=${D}${LINUX_EXTRA_HEADER_BASE}
    install -d ${kerneldir}
    install -d ${kerneldir}/nl-driver-if
    cp -a * ${kerneldir}/nl-driver-if/
}
