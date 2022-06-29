SUMMARY = "Facebook Terragraph VPP implementation"
DESCRIPTION = "Facebook Terragraph VPP implementation"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${WORKDIR}/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "\
  file://scripts \
  file://etc \
  file://bench \
  file://LICENSE \
"

inherit systemd useradd update-rc.d

INITSCRIPT_NAME = "tg-vpp"
INITSCRIPT_PARAMS = "start 61 S ."

# Add a vpp group for our code to use to access the API
USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM_${PN} = "-g 69 vpp"

do_install_append () {
  install -d ${D}${sysconfdir}/vpp
  install -d ${D}${bindir}/bench

  # Create a hugepages directory for mounting hugetlbfs
  install -d ${D}/mnt/hugepages

  install -m 0755 ${WORKDIR}/scripts/* ${D}${bindir}
  install -m 0755 ${WORKDIR}/etc/vpp/* ${D}${sysconfdir}/vpp
  install -m 0755 ${WORKDIR}/bench/* ${D}${bindir}/bench

  if ${@bb.utils.contains('DISTRO_FEATURES','systemd','true','false',d)}; then
    install -m 0644 ${WORKDIR}/etc/systemd/vpp.service ${D}${systemd_unitdir}/system
    install -m 0644 ${WORKDIR}/etc/systemd/hugetlbfs.mount ${D}${systemd_unitdir}/system
  else
    install -D -m 0755 ${WORKDIR}/etc/sv/vpp/vpp.run ${D}${sysconfdir}/sv/vpp/run
    install -D -m 0755 ${WORKDIR}/etc/sv/vpp/vpp.finish ${D}${sysconfdir}/sv/vpp/finish
    install -D -m 0755 ${WORKDIR}/etc/sv/vpp/vpp.log ${D}${sysconfdir}/sv/vpp/log/run
    # uncomment to disable vpp on boot
    #install -D -m 0755 ${WORKDIR}/etc/sv/vpp/vpp.down ${D}${sysconfdir}/sv/vpp/down
  fi

  # Install service startup file
  install -D -m 0755 ${WORKDIR}/etc/init.d/tg-vpp.init \
    ${D}${sysconfdir}/init.d/tg-vpp

  install -D -m 0755 ${WORKDIR}/etc/logrotate/logrotate.vpp ${D}${sysconfdir}/logrotate.d/logrotate.vpp
}

RDEPENDS_${PN} = "\
  vpp \
  dpdk-extras \
  "

FILES_${PN} += "/mnt/hugepages"

SYSTEMD_SERVICE_${PN} = "vpp.service"
SYSTEMD_AUTO_ENABLE = "disable"
