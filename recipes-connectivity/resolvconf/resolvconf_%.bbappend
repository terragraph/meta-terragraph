FILESEXTRAPATHS_prepend := "${THISDIR}/files:"

DEPENDS = " \
  update-rc.d-native \
"

SRC_URI += " \
    file://resolv_conf.base \
    file://init \
    "
do_install_append() {
  install -D -m 0755 ${WORKDIR}/init ${D}${sysconfdir}/init.d/resolvconf
  ln -f -s /var/run/etc/resolv.conf.d/head ${D}${sysconfdir}/resolvconf/resolv.conf.d/head
  install -D ${WORKDIR}/resolv_conf.base ${D}${sysconfdir}/resolvconf/resolv.conf.d/base
  update-rc.d -r ${D} resolvconf start 08 5 .
}

FILES_${PN} += " \
  /var/run/etc/resolv.conf.d/head \
  "
