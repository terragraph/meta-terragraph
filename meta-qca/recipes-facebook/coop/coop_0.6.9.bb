SUMMARY = "Configuration Operation and Organization Process"
DESCRIPTION = "Tool to re-apply network settings on loopback prefix changes"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${META_TERRAGRAPH_DIR}/licenses/Meta-MIT;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = " \
  file://coop.log \
  file://coop.run \
  file://coop \
"

do_install_append() {
  install -D -m 0755 ${WORKDIR}/coop ${D}${bindir}/coop
  install -D -m 0755 ${WORKDIR}/coop.log ${D}${sysconfdir}/sv/coop/log/run
  install -D -m 0755 ${WORKDIR}/coop.run ${D}${sysconfdir}/sv/coop/run
}

RDEPENDS_${PN} += "vpp"
