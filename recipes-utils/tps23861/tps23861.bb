SUMMARY = "POE Daemon for Terragraph"
DESCRIPTION = "Daemon to manage POE ports"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${WORKDIR}/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://src \
           file://tps23861-daemon.sh \
           file://LICENSE \
          "

S = "${WORKDIR}/src"

DEPENDS = "json-c"

INITSCRIPT_NAME = "tps23861-daemon"
INITSCRIPT_PARAMS = "defaults 25"

inherit update-rc.d cmake tgdev

do_install_append () {
  install -D -m 0755 ${WORKDIR}/tps23861-daemon.sh ${D}${sysconfdir}/init.d/tps23861-daemon
}
