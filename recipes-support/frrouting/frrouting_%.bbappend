# Enable zebra "FIB push" interface via Forwarding Plane Manager (FPM)
EXTRA_OECONF += " --enable-fpm"

FILESEXTRAPATHS_prepend := "${THISDIR}/frrouting:"

# Add custom service scripts

SRC_URI += "file://sv \
           "

do_install_append() {
  install -D -m 0755 ${WORKDIR}/sv/down ${D}${sysconfdir}/sv/bgpd/down
  install -D -m 0755 ${WORKDIR}/sv/bgpd.run ${D}${sysconfdir}/sv/bgpd/run
  install -D -m 0755 ${WORKDIR}/sv/bgpd.log ${D}${sysconfdir}/sv/bgpd/log/run

  install -D -m 0755 ${WORKDIR}/sv/down ${D}${sysconfdir}/sv/zebra/down
  install -D -m 0755 ${WORKDIR}/sv/zebra.run ${D}${sysconfdir}/sv/zebra/run
  install -D -m 0755 ${WORKDIR}/sv/zebra.log ${D}${sysconfdir}/sv/zebra/log/run
}

FILES_${PN} += " \
  ${sysconfdir}/sv/ \
  "
