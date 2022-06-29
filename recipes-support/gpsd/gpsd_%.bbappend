FILESEXTRAPATHS_prepend := "${THISDIR}/gpsd:"

# Add custom gpsd sv scripts

SRC_URI += "file://sv \
           "
do_install_append() {
  install -D -m 0755 ${WORKDIR}/sv/gpsd.run ${D}${sysconfdir}/sv/gpsd/run
  install -D -m 0755 ${WORKDIR}/sv/gpsd.log ${D}${sysconfdir}/sv/gpsd/log/run
}

# Disable SysVinit scripts since we are using runit
INITSCRIPT_PARAMS = "stop 20 0 1 6 ."

FILES_${PN} += " \
  ${sysconfdir}/sv/gpsd/run \
  ${sysconfdir}/sv/gpsd/log/run \
  "
