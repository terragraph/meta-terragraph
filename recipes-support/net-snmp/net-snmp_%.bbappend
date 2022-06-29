FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI += "file://snmpd_env.default \
           "
do_install_append() {
  # install snmpd config env override
  install -D -m 644 ${WORKDIR}/snmpd_env.default ${D}${sysconfdir}/default/snmpd
}

FILES_${PN}-server-snmpd += " \
  ${sysconfdir}/default/snmpd \
  "

CONFFILES_${PN}-server-snmpd += " \
  ${sysconfdir}/default/snmpd \
  "

# snmp server doesn't need mib definitions
RDEPENDS_${PN}-server-snmpd_remove = "${PN}-mibs"
