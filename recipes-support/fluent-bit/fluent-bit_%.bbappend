FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

SRC_URI += "file://init \
            file://logrotate.fluentbit \
           "

do_install_append() {
  install -D -m 0755 ${WORKDIR}/init ${D}${sysconfdir}/init.d/fluent-bit
  install -D -m 0755 ${WORKDIR}/logrotate.fluentbit ${D}${sysconfdir}/logrotate.d/logrotate.fluentbit
  update-rc.d -r ${D} fluent-bit start 80 5 .
}
