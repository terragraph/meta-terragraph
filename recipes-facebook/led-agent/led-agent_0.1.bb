SUMMARY = "LED Agent"
DESCRIPTION = "LED Agent"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${WORKDIR}/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-leds"

SRC_URI = "file://src \
           file://sv \
           file://LICENSE \
           "

S = "${WORKDIR}/src"

inherit cmake tgdev

do_install_append() {
     install -D -m 755 ${WORKDIR}/sv/led-agent.run ${D}${sysconfdir}/sv/led-agent/run
     install -D -m 755 ${WORKDIR}/sv/led-agent.log ${D}${sysconfdir}/sv/led-agent/log/run
}

DEPENDS = "fbzmq"
