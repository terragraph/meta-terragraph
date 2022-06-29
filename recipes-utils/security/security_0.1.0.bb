SUMMARY = "Security"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${META_TERRAGRAPH_DIR}/licenses/Meta-MIT;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://src"

S = "${WORKDIR}/src"

RDEPENDS_${PN} = "iw hostapd wpa-supplicant rng-tools"

do_install_append() {
  install -D -m 0644 -d ${D}${sysconfdir}/
  ln -sf /var/run/wpa_supplicant/ ${D}${sysconfdir}/wpa_supplicant
  ln -sf /var/run/hostapd/ ${D}${sysconfdir}/hostapd
  cp ${WORKDIR}/src/config/wpa_supplicant.template ${D}${sysconfdir}/
  cp ${WORKDIR}/src/config/hostapd.template ${D}${sysconfdir}/
  cp ${WORKDIR}/src/config/wpa_supplicant-8021x.template ${D}${sysconfdir}/
  cp ${WORKDIR}/src/config/hostapd-8021x.template ${D}${sysconfdir}/
}
