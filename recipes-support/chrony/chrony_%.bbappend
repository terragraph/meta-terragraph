FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

# Set up local chrony configuration.
# Run time_set on OOB once during startup.

SRC_URI += "file://logrotate.chrony \
            file://chrony.conf.default \
            file://chronyd \
            file://time_set.init \
           "

do_install_append() {
  install -D ${WORKDIR}/chrony.conf.default ${D}${sysconfdir}/chrony.conf.default
  install -D -m 0755 ${WORKDIR}/logrotate.chrony ${D}${sysconfdir}/logrotate.d/logrotate.chrony
  install -D -m 0755 ${WORKDIR}/time_set.init ${D}${sysconfdir}/init.d/time_set
}

FILES_${PN} += " \
  ${sysconfdir}/chrony.conf.default \
  ${sysconfdir}/logrotate.d/logrotate.chrony \
  "

# Enable command line editing support and make sure we build with IPv6 support.
PACKAGECONFIG += "readline ipv6"

PACKAGES =+ "${PN}-timeset"
RDEPENDS_${PN} =+ "${PN}-timeset"
FILES_${PN}-timeset =+ " \
  ${sysconfdir}/init.d/time_set \
  "

# Run after oob_netns
INITSCRIPT_PACKAGES = "${PN} ${PN}-timeset"
INITSCRIPT_NAME_${PN}-timeset = "time_set"
INITSCRIPT_PARAMS_${PN}-timeset = "start 50 5 ."
