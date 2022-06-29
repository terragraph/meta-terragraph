SUMMARY = "Terragraph monotomic touch utility"
DESCRIPTION = "Terragraph monotonic touch utility"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://src"

S = "${WORKDIR}/src"

TARGET_CC_ARCH += "${LDFLAGS}"

do_install() {
    install -d ${D}${sbindir}
    install -m 755 ${S}/monotonic-touch ${D}/${sbindir}/monotonic-touch
}
