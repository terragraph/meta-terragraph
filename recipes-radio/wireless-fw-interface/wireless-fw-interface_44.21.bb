SUMMARY = "Header files for Firmware Interface"
DESCRIPTION = "Terragraph Firmware Interface header files. Interface with e2e etc."
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"


SRC_URI = "file://M44.21/fb-fw-if"

S = "${WORKDIR}/M44.21/fb-fw-if"

inherit cmake kernel-extra-headers

DEPENDS = "nl-driver-if-headers"

ALLOW_EMPTY_${PN} = "1"

do_install_append() {
    cd ${S}
    kerneldir=${D}${LINUX_EXTRA_HEADER_BASE}
    install -D -d ${kerneldir}/fb-fw-if
    install -m 644 fb_tg_fw_param_pack.h ${kerneldir}/fb-fw-if
    install -m 644 fb_tg_fw_pt_if.h ${kerneldir}/fb-fw-if
}
