SUMMARY = "Local Web UI"
DESCRIPTION = "Local Web UI"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${WORKDIR}/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://src \
           file://sv \
           file://LICENSE \
           "

S = "${WORKDIR}/src/webui"

inherit cmake tgdev

DEPENDS = "e2e-clients \
           e2e-common \
           e2e-if \
           fbzmq \
           stats-common \
           stats-if \
           uwebsockets \
           "

RDEPENDS_${PN} = "tg95"

do_install_append() {
     install -d ${D}${sbindir}
     #runsv
     install -D -m 0755 ${WORKDIR}/sv/webui.run ${D}${sysconfdir}/sv/webui/run
     install -D -m 0755 ${WORKDIR}/sv/webui.log ${D}${sysconfdir}/sv/webui/log/run
}
