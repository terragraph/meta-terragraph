SUMMARY = "Terragraph ESP32 SLIP tunnel"
DESCRIPTION = "Terragraph ESP32 SLIP tunnel"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${META_TERRAGRAPH_DIR}/licenses/Meta-MIT;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://sv"

do_install() {
  # runsv for slip/slattach
  install -D -m 0755 ${WORKDIR}/sv/slip.run ${D}${sysconfdir}/sv/slip/run
  install -D -m 0755 ${WORKDIR}/sv/slip.log ${D}${sysconfdir}/sv/slip/log/run
  # runsv for slip_route
  install -D -m 0755 ${WORKDIR}/sv/slip_route.run ${D}${sysconfdir}/sv/slip_route/run
  install -D -m 0755 ${WORKDIR}/sv/slip_route.down ${D}${sysconfdir}/sv/slip_route/down
  install -D -m 0755 ${WORKDIR}/sv/slip_route.log ${D}${sysconfdir}/sv/slip_route/log/run
}
