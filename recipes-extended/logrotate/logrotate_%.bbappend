FILESEXTRAPATHS_prepend := "${THISDIR}/logrotate:"

SRC_URI += "file://examples__logrotate.cron.patch file://logrotate-cron.d"

do_install_append () {
  mkdir -p  ${D}/etc/cron.d
  install -m 0644 ${WORKDIR}/logrotate-cron.d ${D}${sysconfdir}/cron.d/logrotate
}

FILES_${PN} += "/etc/cron.d"
