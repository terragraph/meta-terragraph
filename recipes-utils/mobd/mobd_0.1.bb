SUMMARY = "Mobile App Server"
DESCRIPTION = "Mobile App Server"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${WORKDIR}/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://src \
           file://src/mobd \
           file://sv \
           file://LICENSE \
           "

S = "${WORKDIR}/src/mobd"

TARGET_CC_ARCH += "${LDFLAGS}"

do_install_append() {
     install -d ${D}${sbindir}
     #runsv
     install -D -m 0755 ${WORKDIR}/sv/mobd.run ${D}${sysconfdir}/sv/mobd/run
     install -D -m 0755 ${WORKDIR}/sv/mobd.log ${D}${sysconfdir}/sv/mobd/log/run
}

inherit cmake

DEPENDS = "folly \
           gflags \
           protobuf \
           mobile-common \
           "
