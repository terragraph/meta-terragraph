FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

RDEPENDS_${PN} += "${PN}-misc"

SRC_URI += "file://init \
            file://sshd_config \
            file://tg-CA.pub \
            "

do_install_append() {
  cp ${D}${sysconfdir}/ssh/sshd_config_readonly ${D}${sysconfdir}/ssh/sshd_config_oob_readonly
  sed -i 's:.*PidFile.*:PidFile /var/run/sshd_oob.pid:' ${D}${sysconfdir}/ssh/sshd_config_oob_readonly
  echo "ForceCommand /usr/sbin/use_default_netns.sh" >> ${D}${sysconfdir}/ssh/sshd_config_oob_readonly
  install -D ${WORKDIR}/tg-CA.pub ${D}${sysconfdir}/ssh/tg-CA.bak
  touch ${D}${sysconfdir}/ssh/authorized_keys
  chmod 600 ${D}${sysconfdir}/ssh/authorized_keys
}
