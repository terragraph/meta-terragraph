FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI += "file://rsyslog.conf file://rsyslog.logrotate"
VALGRIND = ""

# Create optional directory for dynamic config files
SRC_URI += "file://volatiles.99_rsyslog"
do_install_append () {
    install -m 0755 -d ${D}${sysconfdir}/default ${D}${sysconfdir}/default/volatiles
    install -m 0644 ${WORKDIR}/volatiles.99_rsyslog  ${D}${sysconfdir}/default/volatiles/99_rsyslog
}
